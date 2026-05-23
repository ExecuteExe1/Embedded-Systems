#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#define QUEUESIZE 20
#define PRODUCERS 25
#define CONSUMERS 4500
#define WORK_ITEMS 100000

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

void *sinWork(void *arg)
{
    double *angles = (double *)arg;

    for(int i=0;i<10;i++)
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

void *producer(void *arg)
{
    for(long i=0;i<WORK_ITEMS;i++)
    {
        workFunction item;

        double *angles = malloc(10 * sizeof(double));

        for(int j=0;j<10;j++)
        {
            angles[j] = (double)(rand()%360);
        }

        item.work = sinWork;
        item.arg = angles;

        gettimeofday(&item.enqueue_time, NULL);

        pthread_mutex_lock(&fifo.mut);

        while(fifo.full)
        {
            pthread_cond_wait(&fifo.notFull, &fifo.mut);
        }

        queueAdd(&fifo, item);

        pthread_mutex_unlock(&fifo.mut);

        pthread_cond_signal(&fifo.notEmpty);
    }

    return NULL;
}

void *consumer(void *arg)
{
    while(1)
    {
        workFunction item;

        pthread_mutex_lock(&fifo.mut);

        while(fifo.empty)
        {
            pthread_cond_wait(&fifo.notEmpty, &fifo.mut);
        }

        queueDel(&fifo, &item);

        struct timeval now;

        gettimeofday(&now, NULL);

        pthread_mutex_unlock(&fifo.mut);

        pthread_cond_signal(&fifo.notFull);

        double wait_time =
            (now.tv_sec - item.enqueue_time.tv_sec) * 1000000.0 +
            (now.tv_usec - item.enqueue_time.tv_usec);

        pthread_mutex_lock(&stats_mutex);

        stats.total_wait += wait_time;
        stats.total_jobs++;

        pthread_mutex_unlock(&stats_mutex);

        item.work(item.arg);
    }

    return NULL;
}

int main()
{
    pthread_t prod[PRODUCERS];
    pthread_t cons[CONSUMERS];

    queueInit(&fifo);

    pthread_mutex_init(&stats_mutex, NULL);

    stats.total_wait = 0;
    stats.total_jobs = 0;

    for(int i=0;i<CONSUMERS;i++)
    {
        pthread_create(&cons[i], NULL, consumer, NULL);
    }

    for(int i=0;i<PRODUCERS;i++)
    {
        pthread_create(&prod[i], NULL, producer, NULL);
    }

    for(int i=0;i<PRODUCERS;i++)
    {
        pthread_join(prod[i], NULL);
    }

    sleep(2);

    printf("Total jobs: %ld\n", stats.total_jobs);
    printf("Average waiting time: %.2f usec\n",
           stats.total_wait / stats.total_jobs);

    return 0;
}