#include "mcs.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>

typedef struct {
    int num_threads;
    int num_iterations;
    int value;
    int test_type;
    pthread_barrier_t barrier;
    mcs_t test_lock1;
    pthread_mutex_t test_lock2;
} test_state;

typedef struct {
    test_state *state;
    int threadnum;
} pthread_arg;

#define NSEC_PER_SECOND 1000000000u
#define NSEC_PER_MILLISECOND 1000000u

static inline void
msleep(unsigned int const milliseconds)
{
    struct timespec t = {
        .tv_sec = 0,
        .tv_nsec = NSEC_PER_MILLISECOND * milliseconds,
    };
    while (t.tv_nsec >= NSEC_PER_SECOND) {
        t.tv_sec += 1;
        t.tv_nsec -= NSEC_PER_SECOND;
    }

    nanosleep(&t, NULL);
}

static inline unsigned
xorshift32(unsigned *const rng_state)
{
    unsigned x = *rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng_state = x;
    return *rng_state;
}

static void *
pthread_routine(void *const arg)
{
    pthread_arg *const parg = arg;
    test_state *const st = parg->state;
    unsigned rng_state = time(NULL);
    for (int i = 0; i < 1000; ++i) {
        (void)xorshift32(&rng_state);
    }

    mcs_t *const mynode = malloc(sizeof(*mynode));
    if (mynode == NULL) {
        fprintf(stderr, "worker thread failed to allocate mcs_node\n");
        abort();
    }

    pthread_barrier_wait(&st->barrier);

    for (int i = 0; i < st->num_iterations; ++i) {
        if (st->test_type == 2) {
            pthread_mutex_lock(&st->test_lock2);
        } else {
            mcs_acquire(&st->test_lock1, mynode);
        }
        st->value += 1;
        if (st->test_type == 2) {
            pthread_mutex_unlock(&st->test_lock2);
        } else {
            mcs_release(&st->test_lock1, mynode);
        }
    }

    return NULL;
}

int
main(int argc, char **argv)
{
    assert(argc == 4);

    test_state *const st = malloc(sizeof(*st));
    if (st == NULL) {
        printf("Failed to alloc test state\n");
        abort();
    }

    st->num_threads = (int)strtol(argv[1], NULL, 10);
    st->test_type = (int)strtol(argv[2], NULL, 10);
    st->num_iterations = (int)strtol(argv[3], NULL, 10);
    printf("starting test with %d threads, test type %d, %d iterations\n", st->num_threads, st->test_type, st->num_iterations);

    pthread_barrier_init(&st->barrier, NULL, st->num_threads + 1);
    if (st->test_type == 2) {
        pthread_mutex_init(&st->test_lock2, NULL);
    }

    pthread_t *threads = malloc(st->num_threads * sizeof(pthread_t));
    if (threads == NULL) {
        fprintf(stderr, "Failed to allocate threads\n");
        abort();
    }
    pthread_arg *pargs = malloc(st->num_threads * sizeof(*pargs));
    if (pargs == NULL) {
        fprintf(stderr, "Failed to allocate args\n");
        abort();
    }
    for (int i = 0; i < st->num_threads; ++i) {
        pargs[i].state = st;
        pargs[i].threadnum = i;
    }

    for (int i = 0; i < st->num_threads; ++i) {
        pthread_create(&threads[i], NULL, pthread_routine, (void *)&pargs[i]);
    }

    pthread_barrier_wait(&st->barrier);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < st->num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    unsigned long time_diff = (end.tv_sec - start.tv_sec);
    time_diff *= NSEC_PER_SECOND;
    time_diff += (end.tv_nsec - start.tv_nsec);
    printf("nanosecond difference is %lu\n", time_diff);

    printf("Incremented value is %d\n", st->value);
    printf("Expected value is %d\n", st->num_threads * st->num_iterations);

    return 0;
}
