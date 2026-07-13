#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <stdatomic.h>

/* ---------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------- */
#define RING_BUFFER_CAPACITY   4096          /* number of message slots      */
#define MAX_MSG_LEN            8192          /* max bytes per JSON message   */
#define WS_HOST                "jetstream1.us-east.bsky.network"
#define WS_PATH                "/subscribe?wantedCollections=app.bsky.feed.post"
#define WS_PORT                443
#define LOG_FILE_PATH          "metrics_log.txt"

#define RECONNECT_BACKOFF_MIN_MS   500       /* initial retry delay          */
#define RECONNECT_BACKOFF_MAX_MS   30000     /* cap on exponential backoff   */

/* If the WebSocket has been continuously unreachable for this many
 * seconds, assume the Wi-Fi link itself is stuck (not just the remote
 * server) and force-reset the interface (see network_reset.c). */
#define WIFI_IFACE                 "wlan0"
#define WIFI_RESET_THRESHOLD_SEC   45
/* Minimum time between two consecutive interface resets, so a long
 * outage doesn't repeatedly toggle the interface every retry cycle. *//* Minimum time between two consecutive interface resets, so a long
 * outage doesn't repeatedly toggle the interface every retry cycle. */
#define WIFI_RESET_COOLDOWN_SEC    90
/* Floor for the *shortened* wait used after a reset that ran but did
 * NOT verifiably restore connectivity (see producer.c) - keeps retries
 * from happening so fast they hammer the interface, while staying well
 * below the full WIFI_RESET_COOLDOWN_SEC so a stuck link doesn't sit
 * dead for the whole normal cooldown every cycle. */
#define WIFI_RESET_RETRY_MIN_SEC   20
/* Watchdog: some libwebsockets failure paths (DNS resolves but no
 * route to any resolved address, certain netlink races right after
 * an interface bounce) destroy the connection attempt WITHOUT ever
 * invoking LWS_CALLBACK_CLIENT_CONNECTION_ERROR. If that happens, the
 * connection state gets stuck at CONNECTING forever and the retry
 * loop never fires. This must be longer than lws's own internal SSL/
 * connect timeout (observed ~30s) so it never fights a legitimate
 * in-progress handshake. */
#define CONNECT_ATTEMPT_TIMEOUT_SEC 45

/* ---------------------------------------------------------------------
 * Bounded circular queue of raw JSON frames (Producer -> Consumer)
 * ------------------------------------------------------------------- */
typedef struct {
    char   data[MAX_MSG_LEN];
    size_t len;
} ring_slot_t;

typedef struct {
    ring_slot_t     *slots;
    size_t           capacity;
    size_t           head;      /* next slot to write */
    size_t           tail;      /* next slot to read  */
    size_t           count;     /* occupied slots, right now */
    size_t           peak_count;/* max `count` seen since the last reset */
    unsigned long    dropped;   /* frames dropped because buffer was full */
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;
    pthread_cond_t   not_full;
} ring_buffer_t;

int    ring_buffer_init(ring_buffer_t *rb, size_t capacity);
void   ring_buffer_destroy(ring_buffer_t *rb);

/* Producer side: never blocks the network thread indefinitely.
 * If the buffer is full, the oldest frame is dropped to make room
 * (bounded latency is more important than completeness for a live feed). */
int    ring_buffer_push(ring_buffer_t *rb, const char *data, size_t len);

/* Consumer side: blocks on not_empty until data is available or shutdown. */
int    ring_buffer_pop(ring_buffer_t *rb, char *out, size_t out_cap, size_t *out_len);

/* Snapshot CURRENT occupancy as a percentage (0-100). Thread-safe.
 * Useful for a live "how full right now" read, but a single
 * once-a-second sample of this will almost always read ~0 if the
 * consumer drains messages much faster than they arrive - see
 * ring_buffer_peak_occupancy_pct_and_reset() below for the metric
 * that actually shows burst behaviour. */
double ring_buffer_occupancy_pct(ring_buffer_t *rb);

/* Returns the HIGHEST occupancy percentage seen since the last call
 * to this function, then resets that high-water mark to the current
 * occupancy (mirroring how counters_snapshot_and_reset works for the
 * message counters). This is what the logger should use once a
 * second: it captures bursts that filled the buffer and drained
 * again between two log lines, which an instantaneous sample would
 * almost always miss entirely. */
double ring_buffer_peak_occupancy_pct_and_reset(ring_buffer_t *rb);

/* ---------------------------------------------------------------------
 * Message-type counters (Consumer -> Logger), protected by one mutex
 * ------------------------------------------------------------------- */
typedef struct {
    unsigned long commit_count;
    unsigned long identity_count;
    unsigned long account_count;
    unsigned long info_count;
    pthread_mutex_t mutex;
} counters_t;

void counters_init(counters_t *c);
void counters_increment(counters_t *c, const char *kind);
/* Atomically read-and-reset all four counters (used once per second) */
void counters_snapshot_and_reset(counters_t *c,
                                  unsigned long *commit,
                                  unsigned long *identity,
                                  unsigned long *account,
                                  unsigned long *info);

/* ---------------------------------------------------------------------
 * Connection status (Producer -> Logger)
 *
 * `state` reflects the instantaneous link state.
 * `had_disconnect` is a *sticky* flag: the producer sets it the moment
 * the socket drops, and the logger clears it once per second after
 * reading it. This lets the logger know whether the link was down at
 * ANY point during the interval just elapsed, not merely at the exact
 * instant clock_gettime() fired - this is what lets us tell "0 because
 * nothing happened" apart from "0 because we were offline".
 * ------------------------------------------------------------------- */
typedef enum {
    CONN_DISCONNECTED = 0,
    CONN_CONNECTING   = 1,
    CONN_CONNECTED    = 2
} conn_state_t;

typedef struct {
    atomic_int state;           /* conn_state_t */
    atomic_int had_disconnect;  /* sticky 0/1   */
} connection_status_t;

void conn_status_init(connection_status_t *cs);
void conn_status_set(connection_status_t *cs, conn_state_t s);
/* Read + clear the sticky "was disconnected at some point" flag */
int  conn_status_consume_disconnect_flag(connection_status_t *cs);

/* ---------------------------------------------------------------------
 * Global shared state visible to all three threads
 * ------------------------------------------------------------------- */
typedef struct {
    ring_buffer_t         ring;
    counters_t            counters;
    connection_status_t   conn;
    volatile sig_atomic_t running; /* 0 => all threads should exit */
} system_state_t;

extern system_state_t g_state;

#endif /* COMMON_H */
