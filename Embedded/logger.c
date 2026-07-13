#define _GNU_SOURCE
#include "common.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>

/* ---------------------------------------------------------------------
 * /proc/stat based CPU usage (first "cpu" aggregate line only)
 * ------------------------------------------------------------------- */
typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_jiffies_t;

static int read_cpu_jiffies(cpu_jiffies_t *out)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char label[16];
    int n = fscanf(f, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                    label,
                    &out->user, &out->nice, &out->system, &out->idle,
                    &out->iowait, &out->irq, &out->softirq, &out->steal);
    fclose(f);
    return (n == 9) ? 0 : -1;
}

/* Returns CPU busy percentage over the delta between two readings.
 * On the very first call (no valid previous reading), returns -1.0
 * to signal "not yet available" and the caller should print NaN. */
static double cpu_pct_from_delta(const cpu_jiffies_t *prev, const cpu_jiffies_t *cur)
{
    unsigned long long prev_idle  = prev->idle + prev->iowait;
    unsigned long long cur_idle   = cur->idle  + cur->iowait;

    unsigned long long prev_total = prev->user + prev->nice + prev->system +
                                     prev->idle + prev->iowait + prev->irq +
                                     prev->softirq + prev->steal;
    unsigned long long cur_total  = cur->user + cur->nice + cur->system +
                                     cur->idle + cur->iowait + cur->irq +
                                     cur->softirq + cur->steal;

    if (cur_total <= prev_total) return 0.0;

    double delta_total = (double)(cur_total - prev_total);
    double delta_idle   = (double)(cur_idle  - prev_idle);
    double busy_pct = 100.0 * (delta_total - delta_idle) / delta_total;
    if (busy_pct < 0.0) busy_pct = 0.0;
    if (busy_pct > 100.0) busy_pct = 100.0;
    return busy_pct;
}

/* Advance a timespec by whole seconds, keeping it an exact grid so
 * repeated additions never accumulate rounding error (drift). */
static void ts_add_seconds(struct timespec *ts, long seconds)
{
    ts->tv_sec += seconds;
}

/*
 * Logger / Monitor thread entry point.
 *
 * Fires exactly once per second, aligned to the wall-clock second
 * boundary, using clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, ...).
 * Using an ABSOLUTE target that is incremented by a fixed 1s each
 * iteration (rather than repeatedly sleeping "1 second from now")
 * is what prevents clock drift over a 24h run: any scheduling delay
 * in one iteration does not push subsequent wakeups later, because
 * the target times are computed independently of when we actually woke.
 */
void *logger_thread_main(void *arg)
{
    (void)arg;

    FILE *log = fopen(LOG_FILE_PATH, "a");
    if (!log) {
        fprintf(stderr, "logger: could not open %s for append\n", LOG_FILE_PATH);
        return NULL;
    }
    setvbuf(log, NULL, _IOLBF, 0); /* line-buffered: survives a crash mid-run */

    cpu_jiffies_t prev_cpu, cur_cpu;
    int have_prev_cpu = (read_cpu_jiffies(&prev_cpu) == 0);

    struct timespec target;
    clock_gettime(CLOCK_REALTIME, &target);
    target.tv_sec += 1;      /* align to the NEXT whole-second boundary */
    target.tv_nsec = 0;

    while (g_state.running) {
        int rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &target, NULL);
        if (rc != 0 && rc != EINTR) {
            /* Unexpected error: fall back to relative sleep to avoid a
             * busy loop, but keep the absolute schedule for next time. */
        }
        if (!g_state.running) break;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        /* --- Snapshot + reset message counters (atomic under mutex) --- */
        unsigned long commit, identity, account, info;
        counters_snapshot_and_reset(&g_state.counters, &commit, &identity, &account, &info);

        /* --- Was the link down at any point during this interval? --- */
        int was_disconnected = conn_status_consume_disconnect_flag(&g_state.conn);
        conn_state_t cur_state = (conn_state_t)atomic_load(&g_state.conn.state);
        int disconnected_now   = (cur_state != CONN_CONNECTED);
        int mark_nan = was_disconnected || disconnected_now;

        /* --- Buffer occupancy (always meaningful, even if offline) --- */
        double occ_pct = ring_buffer_occupancy_pct(&g_state.ring);

        /* --- CPU usage from /proc/stat delta --- */
        double cpu_pct = -1.0;
        if (read_cpu_jiffies(&cur_cpu) == 0) {
            if (have_prev_cpu) {
                cpu_pct = cpu_pct_from_delta(&prev_cpu, &cur_cpu);
            }
            prev_cpu = cur_cpu;
            have_prev_cpu = 1;
        }

        /* --- Write the CSV line ---
         * Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,
         * Info_Count,Buffer_Occupancy_Pct,CPU_Pct
         *
         * Message counts become "NaN" (not 0) whenever the link was down
         * at any point in the window just measured, so a silent second
         * caused by an idle network can never be confused, during
         * post-processing, with a silent second caused by being offline.
         */
        char commit_f[24], identity_f[24], account_f[24], info_f[24], cpu_f[24];

        if (mark_nan) {
            snprintf(commit_f,   sizeof commit_f,   "NaN");
            snprintf(identity_f, sizeof identity_f, "NaN");
            snprintf(account_f,  sizeof account_f,  "NaN");
            snprintf(info_f,     sizeof info_f,     "NaN");
        } else {
            snprintf(commit_f,   sizeof commit_f,   "%lu", commit);
            snprintf(identity_f, sizeof identity_f, "%lu", identity);
            snprintf(account_f,  sizeof account_f,  "%lu", account);
            snprintf(info_f,     sizeof info_f,     "%lu", info);
        }

        if (cpu_pct >= 0.0) {
            snprintf(cpu_f, sizeof cpu_f, "%.3f", cpu_pct);
        } else {
            snprintf(cpu_f, sizeof cpu_f, "NaN");
        }

        fprintf(log, "%ld,%ld,%s,%s,%s,%s,%.3f,%s\n",
                (long)now.tv_sec, (long)now.tv_nsec,
                commit_f, identity_f, account_f, info_f,
                occ_pct, cpu_f);
        fflush(log);

        ts_add_seconds(&target, 1);
    }

    fclose(log);
    return NULL;
}
