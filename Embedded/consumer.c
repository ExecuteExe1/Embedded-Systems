#define _GNU_SOURCE
#include "common.h"
#include "consumer.h"

#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>

/* Consumer thread entry point.*/
/* Blocks on the ring buffer's condition variable until the producer signals new data, pops one frame at a time, parses only the "kind" field, and bumps the matching global counter under its mutex.
 */
/* IMPORTANT (performance / real-time constraint): no printf(), no file  I/O happens in this thread. All logging is the Logger thread's job.
 */
void *consumer_thread_main(void *arg)
{
    (void)arg;
    char buf[MAX_MSG_LEN];
    size_t len;

    while (g_state.running) {
        if (ring_buffer_pop(&g_state.ring, buf, sizeof(buf), &len) != 0) {
            break; /* shutdown requested and buffer drained */
        }

        cJSON *root = cJSON_ParseWithLength(buf, len);
        if (!root) {
            /* Malformed / partial JSON: count as "info" so it is not silently lost from the record. */
            counters_increment(&g_state.counters, "info");
            continue;
        }

        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
        if (cJSON_IsString(kind) && kind->valuestring) {
            counters_increment(&g_state.counters, kind->valuestring);
        } else {
            counters_increment(&g_state.counters, "info");
        }

        cJSON_Delete(root);
    }
    return NULL;
}
