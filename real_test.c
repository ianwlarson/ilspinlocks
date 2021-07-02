#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <sys/syspage.h>
#include <sys/neutrino.h>

#include "mcs.h"

static volatile int *g_value;
static mcs_t *g_lock;

typedef struct {
    int num_threads;
    int num_iterations;
    pthread_barrier_t barrier;
    pthread_mutex_t m_pmtx;
    uint64_t earliest_cc;
    uint64_t latest_cc;
    atomic_uint interrupt_barrier;
} test_state;

typedef struct {
    test_state *state;
    int threadnum;
} pthread_arg;

#define NSEC_PER_SECOND 1000000000u
#define NSEC_PER_MILLISECOND 1000000u

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
    int status = 0;
    pthread_arg *const parg = arg;
    test_state *const st = parg->state;
    unsigned rng_state = (time(NULL) ^ getpid()) * gettid();
    for (int i = 0; i < 1000; ++i) {
        (void)xorshift32(&rng_state);
    }

    mcs_t my_node;

    unsigned runmask = 1 << parg->threadnum;
    status = ThreadCtl(_NTO_TCTL_RUNMASK_GET_AND_SET, &runmask);

    // This may fail if you try to use more threads than there are cores
    assert(status == 0);

    // Ensure we can disable interrupts
    status = ThreadCtl(_NTO_TCTL_IO_PRIV, 0);
    assert(status == 0);

    pthread_barrier_wait(&st->barrier);

    mcs_t *const l_lock = g_lock;
    int volatile*const l_value = g_value;

    // When using spinlocks, interrupts aren't recommended
    InterruptDisable();

    // Spin until all threads are running on their cores
    st->interrupt_barrier++;
    while (st->interrupt_barrier < st->num_threads) {
        backoff();
    }

    uint64_t const cc1 = ClockCycles();
    for (int i = 0; i < st->num_iterations; ++i) {
        //int const t = xorshift32(&rng_state) & 0xff;
        //for (volatile int j = 0; j < t; ++j) {
        //}
        mcs_acquire(l_lock, &my_node);
        // Alternate adding and subtracting. If two threads get into the
        // critical section simultaneously it should be obvious.
        *l_value += 1;
        *l_value -= 1;
        *l_value += 1;
        *l_value -= 1;
        *l_value += 1;
        *l_value -= 1;
        mcs_release(l_lock, &my_node);
    }
    uint64_t const cc2 = ClockCycles();
    InterruptEnable();

    pthread_mutex_lock(&st->m_pmtx);
    if (cc1 < st->earliest_cc) {
        st->earliest_cc = cc1;
    }
    if (cc2 > st->latest_cc) {
        st->latest_cc = cc2;
    }
    pthread_mutex_unlock(&st->m_pmtx);

    return NULL;
}

int
main(int argc, char **argv)
{
    int status = 0;
    test_state *const st = malloc(sizeof(*st));
    if (st == NULL) {
        printf("Failed to alloc test state\n");
        abort();
    }

    unsigned char *nowhere = mmap(0, 0x2000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(nowhere != MAP_FAILED);

    // Put g_value in its own special location to prevent any false sharing
    g_value = (void *)(nowhere + 0x1000);
    *g_value = 0;

    // Put g_lock at its own special location to prevent any false sharing
    g_lock = (void *)(nowhere + 0x400);
    *g_lock = (mcs_t) {
        .m_next = NULL,
        .m_locked = 0
    };


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

    status = pthread_mutex_init(&st->m_pmtx, NULL);
    assert(status == 0);

    printf("sizeof(mcs_t) = %zu\n", sizeof(mcs_t));

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

    uint64_t const cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    for (;;) {

        for (int i = 0; i < st->num_threads; ++i) {
            pthread_create(&threads[i], NULL, pthread_routine, (void *)&pargs[i]);
        }

        st->interrupt_barrier = 0;
        st->earliest_cc = UINT64_MAX;
        st->latest_cc = 0;

        pthread_barrier_wait(&st->barrier);

        for (int i = 0; i < st->num_threads; ++i) {
            pthread_join(threads[i], NULL);
        }

        assert(*g_value == 0);

        uint64_t const cc_diff = st->latest_cc - st->earliest_cc;
        double const sec_diff = 1.0 * cc_diff / cps;

        printf("Time difference was %"PRIu64" clock cycles or %f seconds\n", cc_diff, sec_diff);
        uint64_t const num_crit = st->num_threads * st->num_iterations;
        printf("The spinlock was acquired & released a total of %"PRIu64" times\n", num_crit);

        double const nsec_diff = sec_diff * 1000000000.0;
        printf("The average critical cycle time was %f nanoseconds\n", nsec_diff / num_crit);
    }

    return 0;
}

