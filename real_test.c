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
    long num_threads;
    long num_iterations;
    pthread_barrier_t barrier;
    pthread_mutex_t m_pmtx;
    uint64_t earliest_cc;
    uint64_t latest_cc;
    atomic_uint interrupt_barrier;
    uint64_t collision_prevention;
} test_state;

typedef struct {
    test_state *state;
    int corenum;
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

    unsigned runmask = 1 << parg->corenum;
    status = ThreadCtl(_NTO_TCTL_RUNMASK_GET_AND_SET, &runmask);

    // This may fail if you try to use more threads than there are cores
    assert(status == 0);

    // Ensure we can disable interrupts
    status = ThreadCtl(_NTO_TCTL_IO_PRIV, 0);
    assert(status == 0);

    uint64_t const collide_prot = st->collision_prevention;

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
        int const t = xorshift32(&rng_state) & collide_prot;
        for (volatile int j = 0; j < t; ++j) {
        }
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

static uint64_t
get_overhead(test_state *const st)
{
    int status = 0;

    int volatile*const l_value = g_value;

    unsigned rng_state = (time(NULL) ^ getpid()) * gettid();
    for (int i = 0; i < 1000; ++i) {
        (void)xorshift32(&rng_state);
    }

    uint64_t const collide_prot = st->collision_prevention;

    // Ensure we can disable interrupts
    status = ThreadCtl(_NTO_TCTL_IO_PRIV, 0);
    assert(status == 0);

    InterruptDisable();
    uint64_t const cc1 = ClockCycles();
    for (int i = 0; i < st->num_iterations; ++i) {
        int const t = xorshift32(&rng_state) & collide_prot;
        for (volatile int j = 0; j < t; ++j) {
        }
        *l_value += 1;
        *l_value -= 1;
        *l_value += 1;
        *l_value -= 1;
        *l_value += 1;
        *l_value -= 1;
    }
    uint64_t const cc2 = ClockCycles();
    InterruptEnable();

    return cc2 - cc1;
}

int
main(int argc, char **argv)
{
    uint64_t const cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    double const cpusec = 1.0 * cps / 1000000;

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
    st->num_iterations = 10000;

    bool default_cores[1] = { true };
    bool *cores = default_cores;

    int c;
    while ((c = getopt(argc, argv, "c:d:")) != -1) {
        switch (c) {
        case 'c': {
            st->num_threads = 0;
            long ncores = strlen(optarg);
            cores = malloc(sizeof(*cores) * ncores);
            for (int i = 0; i < ncores; ++i) {
                cores[i] = (optarg[i] != '0');
                if (cores[i]) {
                    ++st->num_threads;
                }
            }
            break;
        }
        case 'p':
            long c = strtol(optarg, NULL, 10);
            st->collision_prevention = (1<<c) - 1;
            printf("Using collision prevention %"PRIx64"\n", st->collision_prevention);
        case 'n':
            st->num_iterations = strtol(optarg, NULL, 10);
            printf("Using %d iterations\n", st->num_iterations);
            break;
        case '?':
            break;
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }
    if (optind < argc) {
        printf ("non-option ARGV-elements: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        printf ("\n");
    }

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

    int core_idx = 0;
    for (int i = 0; i < st->num_threads; ++i) {
        pargs[i].state = st;

        // Find a core that we want to turn on
        while (!cores[core_idx]) {
            ++core_idx;
        }

        pargs[i].corenum = core_idx;
        printf("Putting a thread on core %d\n", core_idx);
        ++core_idx;
    }

    uint64_t const overhead = get_overhead(st);
    double const cycles_per_iteration  = 1.0 * overhead / st->num_iterations;

    printf("Overhead of %f cycles, (loop time %f usec)\n", cycles_per_iteration, cycles_per_iteration / cpusec);

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
        double const usec_diff = cc_diff / cpusec;

        printf("Time difference was %"PRIu64" clock cycles or %f usec\n", cc_diff, usec_diff);
        uint64_t const num_crit = st->num_threads * st->num_iterations;
        printf("The spinlock was acquired & released a total of %"PRIu64" times\n", num_crit);

        printf("The average critical cycle time was %f usec\n", usec_diff / num_crit);
        sleep(1);
    }

    return 0;
}

