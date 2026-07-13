#define _GNU_SOURCE
#include "common.h"
#include <stdlib.h>
#include <string.h>

system_state_t g_state;

/* ---------------------------------------------------------------------
 * Ring buffer
 * ------------------------------------------------------------------- */
int ring_buffer_init(ring_buffer_t *rb, size_t capacity)
{
    memset(rb, 0, sizeof(*rb));
    rb->slots = calloc(capacity, sizeof(ring_slot_t));
    if (!rb->slots) return -1;
    rb->capacity = capacity;
    rb->head = rb->tail = rb->count = rb->dropped = 0;
    pthread_mutex_init(&rb->mutex, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
    pthread_cond_init(&rb->not_full, NULL);
    return 0;
}

void ring_buffer_destroy(ring_buffer_t *rb)
{
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);
    free(rb->slots);
    rb->slots = NULL;
}

int ring_buffer_push(ring_buffer_t *rb, const char *data, size_t len)
{
    if (len >= MAX_MSG_LEN) len = MAX_MSG_LEN - 1; /* truncate defensively */

    pthread_mutex_lock(&rb->mutex);

    if (rb->count == rb->capacity) {
        /* Buffer full: drop the OLDEST frame rather than block the
         * network thread. Keeping the producer non-blocking is what
         * lets it return immediately to lws_service() so no packets
         * are missed at the socket layer. */
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        rb->dropped++;
    }

    ring_slot_t *slot = &rb->slots[rb->head];
    memcpy(slot->data, data, len);
    slot->data[len] = '\0';
    slot->len = len;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;

    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->mutex);
    return 0;
}

int ring_buffer_pop(ring_buffer_t *rb, char *out, size_t out_cap, size_t *out_len)
{
    pthread_mutex_lock(&rb->mutex);
    while (rb->count == 0 && g_state.running) {
        pthread_cond_wait(&rb->not_empty, &rb->mutex);
    }
    if (rb->count == 0 && !g_state.running) {
        pthread_mutex_unlock(&rb->mutex);
        return -1; /* shutting down */
    }

    ring_slot_t *slot = &rb->slots[rb->tail];
    size_t n = slot->len < out_cap - 1 ? slot->len : out_cap - 1;
    memcpy(out, slot->data, n);
    out[n] = '\0';
    *out_len = n;

    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;

    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
    return 0;
}

double ring_buffer_occupancy_pct(ring_buffer_t *rb)
{
    pthread_mutex_lock(&rb->mutex);
    double pct = (100.0 * (double)rb->count) / (double)rb->capacity;
    pthread_mutex_unlock(&rb->mutex);
    return pct;
}

/* ---------------------------------------------------------------------
 * Counters
 * ------------------------------------------------------------------- */
void counters_init(counters_t *c)
{
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->mutex, NULL);
}

void counters_increment(counters_t *c, const char *kind)
{
    pthread_mutex_lock(&c->mutex);
    if (strcmp(kind, "commit") == 0)        c->commit_count++;
    else if (strcmp(kind, "identity") == 0) c->identity_count++;
    else if (strcmp(kind, "account") == 0)  c->account_count++;
    else                                     c->info_count++; /* unknown/info/error */
    pthread_mutex_unlock(&c->mutex);
}

void counters_snapshot_and_reset(counters_t *c,
                                  unsigned long *commit,
                                  unsigned long *identity,
                                  unsigned long *account,
                                  unsigned long *info)
{
    pthread_mutex_lock(&c->mutex);
    *commit   = c->commit_count;
    *identity = c->identity_count;
    *account  = c->account_count;
    *info     = c->info_count;
    c->commit_count = c->identity_count = c->account_count = c->info_count = 0;
    pthread_mutex_unlock(&c->mutex);
}

/* ---------------------------------------------------------------------
 * Connection status
 * ------------------------------------------------------------------- */
void conn_status_init(connection_status_t *cs)
{
    atomic_init(&cs->state, CONN_DISCONNECTED);
    atomic_init(&cs->had_disconnect, 1); /* start "unknown/offline" until first connect */
}

void conn_status_set(connection_status_t *cs, conn_state_t s)
{
    atomic_store(&cs->state, s);
    if (s == CONN_DISCONNECTED) {
        atomic_store(&cs->had_disconnect, 1);
    }
}

int conn_status_consume_disconnect_flag(connection_status_t *cs)
{
    return atomic_exchange(&cs->had_disconnect, 0);
}
