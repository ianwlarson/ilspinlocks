#include "mcs.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>

typedef struct {
    int num_threads;
    int num_iterations;
    volatile int value;
    pthread_barrier_t barrier;
    mcs_t lock;
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

    pthread_barrier_wait(&st->barrier);

    mcs_t *mydat = malloc(sizeof(*mydat));

    for (int i = 0; i < st->num_iterations; ++i) {
        int const t = xorshift32(&rng_state) & 0xff;
        for (volatile int j = 0; j < t; ++j) {
        }
        mcs_acquire(&st->lock, mydat);
        ++st->value;
        --st->value;
        ++st->value;
        --st->value;
        ++st->value;
        --st->value;
        ++st->value;
        --st->value;
        ++st->value;
        --st->value;
        mcs_release(&st->lock, mydat);
    }

    return NULL;
}

int
main(int argc, char **argv)
{
    test_state *const st = malloc(sizeof(*st));
    if (st == NULL) {
        printf("Failed to alloc test state\n");
        abort();
    }

    st->num_threads = 1;
    if (argc > 1) {
        st->num_threads = (int)strtol(argv[1], NULL, 10);
    }
    st->num_iterations = 1000;
    if (argc > 2) {
        st->num_iterations = (int)strtol(argv[2], NULL, 10);
    }
    //printf("starting test with %d threads, %d iterations\n", st->num_threads, st->num_iterations);

    pthread_barrier_init(&st->barrier, NULL, st->num_threads + 1);

    st->lock = (mcs_t) {
        .m_next = NULL,
        .m_locked = 0
    };

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

    for (;;) {

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
        uint64_t time_diff = (end.tv_sec - start.tv_sec);
        time_diff *= NSEC_PER_SECOND;
        time_diff += (end.tv_nsec - start.tv_nsec);
        //printf("nanosecond difference is %lu\n", time_diff);
        printf("%"PRIu64"\n", time_diff);

        printf("Incremented value is %d\n", st->value);
        //printf("Expected value is %d\n", st->num_threads * st->num_iterations);
    }

    return 0;
}

