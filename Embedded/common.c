#define _GNU_SOURCE
#include "common.h"
#include <stdlib.h>
#include <string.h>

system_state_t g_state; /*creates the one global shared object*/

/* Ring buffer */
int ring_buffer_init(ring_buffer_t *rb, size_t capacity)
{
    memset(rb, 0, sizeof(*rb)); /*Clear structure*/
    rb->slots = calloc(capacity, sizeof(ring_slot_t)); /*Allocate storage*/ 
    if (!rb->slots) return -1; /*if Ram allocation fails,return error */
    rb->capacity = capacity;  /*store capacity*/
    rb->head = rb->tail = rb->count = rb->dropped = 0; /*Initializes indexes*/
    pthread_mutex_init(&rb->mutex, NULL); /*Initializes Mutex,aka creates the lock*/
    pthread_cond_init(&rb->not_empty, NULL);  /*These 2 conditions allow the thread to wake up or sleep*/
    pthread_cond_init(&rb->not_full, NULL);
    return 0;
}

void ring_buffer_destroy(ring_buffer_t *rb)
{
    pthread_mutex_destroy(&rb->mutex); /*Destroy synchronization objects*/
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);
    free(rb->slots); /*returns memory to the OS*/
    rb->slots = NULL; /*Avoids dagling pointers*/
}

int ring_buffer_push(ring_buffer_t *rb, const char *data, size_t len) /*Producer inserts message*/
{
    if (len >= MAX_MSG_LEN) len = MAX_MSG_LEN - 1; /* truncate defensively */

    pthread_mutex_lock(&rb->mutex); /*Protect buffer by locking it*/

    if (rb->count == rb->capacity) {
        /* Buffer full: drop the OLDEST frame rather than block the
         * network thread. Keeping the producer non-blocking is what
         * lets it return immediately to lws_service() so no packets
         * are missed at the socket layer. */
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        rb->dropped++;
    }

    ring_slot_t *slot = &rb->slots[rb->head]; /*Insert new message and find new empty slot*/
    memcpy(slot->data, data, len); /*Copies JSON*/
    slot->data[len] = '\0'; /*Makes it a valid C String*/
    slot->len = len; /*store size*/
    rb->head = (rb->head + 1) % rb->capacity; /*Move head,circular movement*/
    rb->count++;

    pthread_cond_signal(&rb->not_empty); /*Wake consumer*/
    pthread_mutex_unlock(&rb->mutex);  /*Unlocke the buffer for other threads to access*/
    return 0;
}

/*Consumer Removes message*/
int ring_buffer_pop(ring_buffer_t *rb, char *out, size_t out_cap, size_t *out_len)
{
    pthread_mutex_lock(&rb->mutex);/*Lock the buffer*/
    while (rb->count == 0 && g_state.running) { /*Wait while empty*/
        pthread_cond_wait(&rb->not_empty, &rb->mutex); /*sleep if empty*/
    }
    if (rb->count == 0 && !g_state.running) { /*if shutting down*/
        pthread_mutex_unlock(&rb->mutex); /*Unlock buffer*/
        return -1; /* shutting down */ 
    }

    ring_slot_t *slot = &rb->slots[rb->tail]; /*consumer always removes from tail*/
    size_t n = slot->len < out_cap - 1 ? slot->len : out_cap - 1; 
    memcpy(out, slot->data, n); /*Copy data*/
    out[n] = '\0';
    *out_len = n;

    rb->tail = (rb->tail + 1) % rb->capacity; /*Moce tail*/ 
    rb->count--; /*Decrease count*/

    pthread_cond_signal(&rb->not_full); /*Notify Producer,there is now space!*/
    pthread_mutex_unlock(&rb->mutex); /*Unlock the buffer*/
    return 0;
}

double ring_buffer_occupancy_pct(ring_buffer_t *rb) /*Current occupancy*/
{
    pthread_mutex_lock(&rb->mutex); 
    double pct = (100.0 * (double)rb->count) / (double)rb->capacity;
    pthread_mutex_unlock(&rb->mutex);
    return pct;
}

/*Counters */
void counters_init(counters_t *c)
{
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->mutex, NULL);
}
/*increment counters*/
void counters_increment(counters_t *c, const char *kind)
{
    pthread_mutex_lock(&c->mutex);
    if (strcmp(kind, "commit") == 0)        c->commit_count++;
    else if (strcmp(kind, "identity") == 0) c->identity_count++;
    else if (strcmp(kind, "account") == 0)  c->account_count++;
    else                                     c->info_count++; /* unknown/info/error */
    pthread_mutex_unlock(&c->mutex);
}
 /*Used by logger every second,gives values to the variables from the counters*/
void counters_snapshot_and_reset(counters_t *c,                                  unsigned long *commit,
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

/*Connection status*/
void conn_status_init(connection_status_t *cs)
{  /*if connected atomic write,thread-sade*/
    atomic_init(&cs->state, CONN_DISCONNECTED);
    atomic_init(&cs->had_disconnect, 1); /* start "unknown/offline" until first connect */
}
/*Change connection state*/
void conn_status_set(connection_status_t *cs, conn_state_t s)
{      /*if disconnected,set had_connect=1 this remembers the event*/
    atomic_store(&cs->state, s);
    if (s == CONN_DISCONNECTED) {
        atomic_store(&cs->had_disconnect, 1);
    }
}
 /*Cosnume disconnect event*/
int conn_status_consume_disconnect_flag(connection_status_t *cs)
{
    return atomic_exchange(&cs->had_disconnect, 0); /*Read OLD value ANDset it to zero atomically*/
}
