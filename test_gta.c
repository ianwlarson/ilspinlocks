#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <sys/mman.h>

#include "gta.h"

#define NSEC_PER_SECOND UINT64_C(1000000000)

static gta_t *g_lock;

static gta_t *
allocate_gta(size_t const n_lockers)
{
    size_t const alloc_size = (n_lockers + 1) * 64;
    gta_t *p_lock = malloc(alloc_size);
    memset(p_lock, 0, alloc_size);
    p_lock->slots = (void *)((unsigned char *)p_lock + 64);
    p_lock->m_allocsz = alloc_size;

    // Start the lock unlocked!
    p_lock->m_tail = (uintptr_t)&p_lock->slots[0].v | 0x1;

    return p_lock;
}

typedef struct {
    int num_threads;
    int num_iterations;
    volatile int value;
    pthread_barrier_t barrier;
} test_state;

typedef struct {
    test_state *state;
    int threadnum;
} pthread_arg;

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

    unsigned const my_num = parg->threadnum;

    gta_t *const l_lock = g_lock;

    pthread_barrier_wait(&st->barrier);

    for (int i = 0; i < st->num_iterations; ++i) {
        //int const t = xorshift32(&rng_state) & 0xff;
        //for (volatile int j = 0; j < t; ++j) {
        //}
        gta_acquire(l_lock, my_num);
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
        gta_release(l_lock, my_num);
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

    g_lock = allocate_gta(st->num_threads);

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
        printf("timer per iteration: %f\n", 1.0*time_diff / (st->num_threads * st->num_iterations));

        printf("Incremented value is %d\n", st->value);
        //printf("Expected value is %d\n", st->num_threads * st->num_iterations);
    }

    return 0;
}

