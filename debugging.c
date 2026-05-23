#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#define QUEUESIZE 5
#define PRODUCERS 2
#define CONSUMERS 3
#define WORK_ITEMS 10

typedef struct {
    void * (*work)(void *);
    void *arg;
    struct timeval enqueue_time;
} workFunction;

typedef struct {
    workFunction buf[QUEUESIZE];
    int head;
    int tail;
    int full;
    int empty;

    pthread_mutex_t mut;
    pthread_cond_t notFull;
    pthread_cond_t notEmpty;

} queue;

typedef struct {
    double total_wait;
    long total_jobs;
} statistics;

queue fifo;
statistics stats;

pthread_mutex_t stats_mutex;
pthread_mutex_t print_mutex;

void *sinWork(void *arg)
{
    double *angles = (double *)arg;

    for(int i = 0; i < 10; i++)
    {
        sin(angles[i]);
    }

    free(arg);

    return NULL;
}

void queueInit(queue *q)
{
    q->head = 0;
    q->tail = 0;
    q->full = 0;
    q->empty = 1;

    pthread_mutex_init(&q->mut, NULL);
    pthread_cond_init(&q->notFull, NULL);
    pthread_cond_init(&q->notEmpty, NULL);
}

void queueAdd(queue *q, workFunction item)
{
    q->buf[q->tail] = item;

    q->tail = (q->tail + 1) % QUEUESIZE;

    if(q->tail == q->head)
        q->full = 1;

    q->empty = 0;
}

void queueDel(queue *q, workFunction *item)
{
    *item = q->buf[q->head];

    q->head = (q->head + 1) % QUEUESIZE;

    if(q->head == q->tail)
        q->empty = 1;

    q->full = 0;
}

int queueCount(queue *q)
{
    if(q->full)
        return QUEUESIZE;

    if(q->tail >= q->head)
        return q->tail - q->head;

    return QUEUESIZE - q->head + q->tail;
}

void *producer(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    for(long i = 0; i < WORK_ITEMS; i++)
    {
        workFunction item;

        double *angles = malloc(10 * sizeof(double));

        for(int j = 0; j < 10; j++)
        {
            angles[j] = (double)(rand() % 360);
        }

        item.work = sinWork;
        item.arg = angles;

        gettimeofday(&item.enqueue_time, NULL);

        pthread_mutex_lock(&print_mutex);
        printf("[PRODUCER %d] Trying to add item %ld\n", id, i);
        pthread_mutex_unlock(&print_mutex);

        pthread_mutex_lock(&fifo.mut);

        while(fifo.full)
        {
            pthread_mutex_lock(&print_mutex);
            printf("[PRODUCER %d] Queue FULL -> waiting...\n", id);
            pthread_mutex_unlock(&print_mutex);

            pthread_cond_wait(&fifo.notFull, &fifo.mut);
        }

        queueAdd(&fifo, item);

        pthread_mutex_lock(&print_mutex);
        printf("[PRODUCER %d] Added item %ld | head=%d tail=%d count=%d\n",
               id,
               i,
               fifo.head,
               fifo.tail,
               queueCount(&fifo));
        pthread_mutex_unlock(&print_mutex);

        pthread_mutex_unlock(&fifo.mut);

        pthread_cond_signal(&fifo.notEmpty);

        usleep(100000);
    }

    pthread_mutex_lock(&print_mutex);
    printf("[PRODUCER %d] Finished producing.\n", id);
    pthread_mutex_unlock(&print_mutex);

    return NULL;
}

void *consumer(void *arg)
{
    int id = *((int *)arg);
    free(arg);

    while(1)
    {
        workFunction item;

        pthread_mutex_lock(&fifo.mut);

        while(fifo.empty)
        {
            pthread_mutex_lock(&print_mutex);
            printf("[CONSUMER %d] Queue EMPTY -> waiting...\n", id);
            pthread_mutex_unlock(&print_mutex);

            pthread_cond_wait(&fifo.notEmpty, &fifo.mut);
        }

        queueDel(&fifo, &item);

        struct timeval now;
        gettimeofday(&now, NULL);

        pthread_mutex_lock(&print_mutex);
        printf("[CONSUMER %d] Removed item | head=%d tail=%d count=%d\n",
               id,
               fifo.head,
               fifo.tail,
               queueCount(&fifo));
        pthread_mutex_unlock(&print_mutex);

        pthread_mutex_unlock(&fifo.mut);

        pthread_cond_signal(&fifo.notFull);

        double wait_time =
            (now.tv_sec - item.enqueue_time.tv_sec) * 1000000.0 +
            (now.tv_usec - item.enqueue_time.tv_usec);

        pthread_mutex_lock(&stats_mutex);

        stats.total_wait += wait_time;
        stats.total_jobs++;

        pthread_mutex_unlock(&stats_mutex);

        pthread_mutex_lock(&print_mutex);
        printf("[CONSUMER %d] Job waited %.2f usec\n",
               id,
               wait_time);

        printf("[CONSUMER %d] Executing work...\n", id);
        pthread_mutex_unlock(&print_mutex);

        item.work(item.arg);

        usleep(150000);
    }

    return NULL;
}

int main()
{
    pthread_t prod[PRODUCERS];
    pthread_t cons[CONSUMERS];

    queueInit(&fifo);

    pthread_mutex_init(&stats_mutex, NULL);
    pthread_mutex_init(&print_mutex, NULL);

    stats.total_wait = 0;
    stats.total_jobs = 0;

    for(int i = 0; i < CONSUMERS; i++)
    {
        int *id = malloc(sizeof(int));
        *id = i;

        pthread_create(&cons[i], NULL, consumer, id);
    }

    for(int i = 0; i < PRODUCERS; i++)
    {
        int *id = malloc(sizeof(int));
        *id = i;

        pthread_create(&prod[i], NULL, producer, id);
    }

    for(int i = 0; i < PRODUCERS; i++)
    {
        pthread_join(prod[i], NULL);
    }

    sleep(5);

    printf("\n========== FINAL STATS ==========\n");
    printf("Total jobs: %ld\n", stats.total_jobs);

    if(stats.total_jobs > 0)
    {
        printf("Average waiting time: %.2f usec\n",
               stats.total_wait / stats.total_jobs);
    }

    return 0;
}