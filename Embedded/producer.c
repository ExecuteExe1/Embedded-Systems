#define _GNU_SOURCE
#include "common.h"
#include "producer.h"
#include "network_reset.h"
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* Per-connection buffer used to reassemble fragmented WS messages
 * (Jetstream frames can arrive split across several LWS_CALLBACK_CLIENT_RECEIVE
 * calls; lws tells us via lws_is_final_fragment()). */
typedef struct {
    char   *accum;
    size_t  accum_len;
    size_t  accum_cap;
} per_session_data_t;

static struct lws_context *g_ctx = NULL;
static volatile sig_atomic_t g_want_exit = 0;

static void append_fragment(per_session_data_t *psd, const void *in, size_t len)
{
    if (psd->accum_len + len + 1 > psd->accum_cap) {
        size_t newcap = (psd->accum_cap == 0 ? 4096 : psd->accum_cap * 2);
        while (newcap < psd->accum_len + len + 1) newcap *= 2;
        if (newcap > MAX_MSG_LEN * 4) newcap = MAX_MSG_LEN * 4; /* hard cap */
        char *tmp = realloc(psd->accum, newcap);
        if (!tmp) return; /* drop on OOM, extremely unlikely on a Pi */
        psd->accum = tmp;
        psd->accum_cap = newcap;
    }
    size_t room = psd->accum_cap - 1 - psd->accum_len;
    size_t n = (len < room) ? len : room;
    memcpy(psd->accum + psd->accum_len, in, n);
    psd->accum_len += n;
}

static int callback_jetstream(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len)
{
    per_session_data_t *psd = (per_session_data_t *)user;
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_notice("Jetstream: connection established\n");
        conn_status_set(&g_state.conn, CONN_CONNECTED);
        if (psd) { psd->accum_len = 0; }
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (psd) {
            append_fragment(psd, in, len);
            if (lws_is_final_fragment(wsi)) {
                ring_buffer_push(&g_state.ring, psd->accum, psd->accum_len);
                psd->accum_len = 0;
            }
        }
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        lwsl_warn("Jetstream: connection error: %s\n",
                  in ? (char *)in : "(no info)");
        conn_status_set(&g_state.conn, CONN_DISCONNECTED);
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        lwsl_warn("Jetstream: connection closed\n");
        conn_status_set(&g_state.conn, CONN_DISCONNECTED);
        break;
    case LWS_CALLBACK_WSI_DESTROY:
        if (psd && psd->accum) { free(psd->accum); psd->accum = NULL; }
        break;
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        .name                  = "jetstream-protocol",
        .callback              = callback_jetstream,
        .per_session_data_size = sizeof(per_session_data_t),
        .rx_buffer_size        = 0,
    },
    { 0 } /* terminator: zero-init all fields, robust across lws versions */
};

static struct lws *attempt_connect(struct lws_context *ctx)
{
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context        = ctx;
    ccinfo.address        = WS_HOST;
    ccinfo.port           = WS_PORT;
    ccinfo.path           = WS_PATH;
    ccinfo.host           = ccinfo.address;
    ccinfo.origin         = ccinfo.address;
    ccinfo.protocol       = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL;

    conn_status_set(&g_state.conn, CONN_CONNECTING);
    lwsl_notice("Jetstream: attempting connection to wss://%s%s\n", WS_HOST, WS_PATH);
    return lws_client_connect_via_info(&ccinfo);
}

/*
 * Producer thread entry point.
 *
 * Runs the libwebsockets event loop (fully event-driven / non-blocking on
 * the network side) and owns the reconnection state machine: whenever the
 * connection drops, it backs off exponentially (capped) and retries until
 * either it reconnects or the program is told to shut down.
 *
 * If WebSocket-level retries alone don't recover within
 * WIFI_RESET_THRESHOLD_SEC, it escalates to an OS-level Wi-Fi interface
 * reset (see network_reset.c). That reset now verifies for itself
 * whether it actually restored connectivity (a real IPv4 address), not
 * just whether `ip link set up` exited 0 - a toggled-but-still-broken
 * link would otherwise silently leave us dead until the next cooldown
 * expired, which is what caused the multi-hour gaps seen in earlier runs.
 */
void *producer_thread_main(void *arg)
{
    (void)arg;
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid       = -1;
    info.uid       = -1;
    info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    g_ctx = lws_create_context(&info);
    if (!g_ctx) {
        fprintf(stderr, "producer: failed to create lws context\n");
        return NULL;
    }

    unsigned int backoff_ms = RECONNECT_BACKOFF_MIN_MS;

    /* Tracks when the CURRENT connection attempt started, so the
     * watchdog below can detect an attempt that libwebsockets silently
     * abandoned without ever calling our callback (see comment below). */
    struct timespec attempt_start;
    clock_gettime(CLOCK_MONOTONIC, &attempt_start);

    struct lws *wsi = attempt_connect(g_ctx);
    if (!wsi) {
        /* Some failures (e.g. no usable route immediately available)
         * make lws_client_connect_via_info() fail synchronously,
         * returning NULL without ever invoking our callback. Catch
         * that directly instead of waiting on a callback that will
         * never come. */
        lwsl_warn("Jetstream: connect call failed synchronously (returned NULL)\n");
        conn_status_set(&g_state.conn, CONN_DISCONNECTED);
    }

    /* Tracks how long we've been continuously disconnected, and when we
     * last force-reset the Wi-Fi interface, so we only intervene at the
     * OS/link level after the WebSocket-level retries have clearly had
     * enough time to work on their own, and not more often than the
     * cooldown allows. CLOCK_MONOTONIC is used since it can't jump
     * backwards/forwards from NTP adjustments during a long outage. */
    struct timespec disconnect_since;
    int have_disconnect_since = 0;
    struct timespec last_reset_time;
    int have_last_reset = 0;

    while (g_state.running) {
        /* lws_service returns roughly every `timeout_ms`, or sooner if
         * there is network activity to process. This keeps the producer
         * responsive to new frames while still letting us poll the
         * shutdown flag and connection state regularly. */
        lws_service(g_ctx, 100);

        conn_state_t st = (conn_state_t)atomic_load(&g_state.conn.state);

        /* Watchdog: certain lws failure paths (DNS resolves but no
         * route to any resolved address, netlink races right after an
         * interface bounce) destroy the connection attempt WITHOUT
         * ever invoking LWS_CALLBACK_CLIENT_CONNECTION_ERROR. When
         * that happens, `state` gets stuck at CONNECTING forever and
         * the retry branch below never fires again - this is exactly
         * what caused a real 18h+ run to never recover. If we've been
         * "connecting" longer than any legitimate handshake could
         * take, force it back to DISCONNECTED ourselves so the retry
         * logic runs regardless of whether lws ever tells us it failed. */
        if (st == CONN_CONNECTING) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double connecting_secs = (double)(now.tv_sec - attempt_start.tv_sec) +
                                      (double)(now.tv_nsec - attempt_start.tv_nsec) / 1e9;
            if (connecting_secs > CONNECT_ATTEMPT_TIMEOUT_SEC) {
                lwsl_warn("Jetstream: connection attempt stalled %.1fs with no "
                          "callback from lws; forcing a retry\n", connecting_secs);
                conn_status_set(&g_state.conn, CONN_DISCONNECTED);
                st = CONN_DISCONNECTED;
            }
        }

        if (st == CONN_DISCONNECTED && g_state.running) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (!have_disconnect_since) {
                disconnect_since = now;
                have_disconnect_since = 1;
            }
            double down_secs = (double)(now.tv_sec - disconnect_since.tv_sec) +
                                (double)(now.tv_nsec - disconnect_since.tv_nsec) / 1e9;
            double since_last_reset_secs = have_last_reset
                ? (double)(now.tv_sec - last_reset_time.tv_sec) +
                  (double)(now.tv_nsec - last_reset_time.tv_nsec) / 1e9
                : (double)(WIFI_RESET_COOLDOWN_SEC + 1); /* never reset yet -> cooldown already "elapsed" */

            if (down_secs >= WIFI_RESET_THRESHOLD_SEC &&
                since_last_reset_secs >= WIFI_RESET_COOLDOWN_SEC) {
                /* WebSocket-level retries alone haven't recovered the
                 * link in a long time - assume the Wi-Fi association
                 * itself is stuck and force a reset at the OS level.
                 * wifi_interface_reset() now blocks until it has
                 * verified an actual IPv4 address (or given up), so
                 * its return value tells us whether this genuinely
                 * fixed things rather than just having "run". */
                int reset_ok = (wifi_interface_reset(WIFI_IFACE) == 0);
                last_reset_time = now;
                have_last_reset = 1;

                if (reset_ok) {
                    /* Confirmed the link is actually back - clear the
                     * outage clock so a future drop starts its own
                     * fresh threshold instead of inheriting this one. */
                    lwsl_notice("Jetstream: Wi-Fi reset verified connectivity restored\n");
                    have_disconnect_since = 0;
                } else {
                    /* Toggle ran but did not restore a usable address.
                     * Previously this fell straight through to a full
                     * WIFI_RESET_COOLDOWN_SEC wait before trying again,
                     * which is what produced multi-hour dead windows
                     * when a single toggle wasn't enough. Shrink the
                     * effective wait before the next attempt instead of
                     * silently accepting the full cooldown. */
                    lwsl_warn("Jetstream: Wi-Fi reset did NOT restore connectivity; "
                              "will retry reset sooner than the normal cooldown\n");
                    double shortened = WIFI_RESET_COOLDOWN_SEC / 4.0;
                    if (shortened < WIFI_RESET_RETRY_MIN_SEC)
                        shortened = WIFI_RESET_RETRY_MIN_SEC;
                    /* Pretend the last reset happened further in the
                     * past than it did, so since_last_reset_secs clears
                     * the cooldown gate after `shortened` seconds. */
                    last_reset_time.tv_sec -= (time_t)(WIFI_RESET_COOLDOWN_SEC - shortened);
                }
            }

            /* Back off, then retry. Exponential with a cap, so a long
             * outage doesn't spin the network stack needlessly, but a
             * short blip still recovers within a second or two. */
            struct timespec ts;
            ts.tv_sec  = backoff_ms / 1000;
            ts.tv_nsec = (long)(backoff_ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);

            if (!g_state.running) break;

            clock_gettime(CLOCK_MONOTONIC, &attempt_start);
            wsi = attempt_connect(g_ctx);
            if (!wsi) {
                lwsl_warn("Jetstream: connect call failed synchronously (returned NULL)\n");
                conn_status_set(&g_state.conn, CONN_DISCONNECTED);
            }
            backoff_ms *= 2;
            if (backoff_ms > RECONNECT_BACKOFF_MAX_MS) backoff_ms = RECONNECT_BACKOFF_MAX_MS;

        } else if (st == CONN_CONNECTED) {
            backoff_ms = RECONNECT_BACKOFF_MIN_MS; /* reset backoff once healthy */
            have_disconnect_since = 0;              /* clear the outage timer */
        }
    }

    lws_context_destroy(g_ctx);
    return NULL;
}

void producer_request_stop(void)
{
    g_want_exit = 1;
    g_state.running = 0;   /* or whatever the real shutdown flag is */
}
