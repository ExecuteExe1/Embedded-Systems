#define _GNU_SOURCE
#include "common.h"
#include "producer.h"
#include "consumer.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

static void handle_signal(int sig)
{
    (void)sig;
    g_state.running = 0;
    /* Wake the consumer if it is blocked waiting on the ring buffer,
     * so it can observe running==0 and exit cleanly. */
    pthread_mutex_lock(&g_state.ring.mutex);
    pthread_cond_broadcast(&g_state.ring.not_empty);
    pthread_mutex_unlock(&g_state.ring.mutex);
}

int main(void)
{
    if (ring_buffer_init(&g_state.ring, RING_BUFFER_CAPACITY) != 0) {
        fprintf(stderr, "main: failed to allocate ring buffer\n");
        return EXIT_FAILURE;
    }
    counters_init(&g_state.counters);
    conn_status_init(&g_state.conn);
    g_state.running = 1;

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_t producer_tid, consumer_tid, logger_tid;

    if (pthread_create(&producer_tid, NULL, producer_thread_main, NULL) != 0) {
        fprintf(stderr, "main: failed to start producer thread\n");
        return EXIT_FAILURE;
    }
    if (pthread_create(&consumer_tid, NULL, consumer_thread_main, NULL) != 0) {
        fprintf(stderr, "main: failed to start consumer thread\n");
        g_state.running = 0;
        pthread_join(producer_tid, NULL);
        return EXIT_FAILURE;
    }
    if (pthread_create(&logger_tid, NULL, logger_thread_main, NULL) != 0) {
        fprintf(stderr, "main: failed to start logger thread\n");
        g_state.running = 0;
        pthread_join(producer_tid, NULL);
        pthread_join(consumer_tid, NULL);
        return EXIT_FAILURE;
    }

    printf("jetstream_rt running. Logging to %s. Press Ctrl+C to stop.\n", LOG_FILE_PATH);

    pthread_join(producer_tid, NULL);
    pthread_join(consumer_tid, NULL);
    pthread_join(logger_tid, NULL);

    ring_buffer_destroy(&g_state.ring);
    pthread_mutex_destroy(&g_state.counters.mutex);

    printf("jetstream_rt: clean shutdown, dropped %lu frames total.\n", g_state.ring.dropped);
    return EXIT_SUCCESS;
}
