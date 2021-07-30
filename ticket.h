#pragma once

#include <stdbool.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>

#include "backoff.h"

typedef struct simple_ticket_spinlock tick_t;

struct simple_ticket_spinlock {
    union {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        struct {
            atomic_uint next_ticket;
            atomic_uint now_serving;
        };
#else
        struct {
            atomic_uint now_serving;
            atomic_uint next_ticket;
        };
#endif
        atomic_uint_fast64_t total;
    };
};

static inline void
ticket_acq(tick_t *const p_lock)
{
    unsigned const my_ticket = atomic_fetch_add_explicit(&p_lock->next_ticket, 1, memory_order_relaxed);

    for (;;) {
        unsigned const now_serving = atomic_load_explicit(&p_lock->now_serving, memory_order_acquire);
        unsigned const diff = my_ticket - now_serving;

        if (diff == 0) {
            break;
        } else {
            for (unsigned i = 0; i < diff; ++i) {
                backoff();
            }
#if defined(__arm__) || defined(__aarch64__)
            wfe();
#endif
        }
    }
}

static inline void
ticket_rel(tick_t *const p_lock)
{
    unsigned const next_val = atomic_load_explicit(&p_lock->now_serving, memory_order_relaxed) + 1;
    atomic_store_explicit(&p_lock->now_serving, next_val, memory_order_release);
#if defined(__arm__) || defined(__aarch64__)
    sev();
#endif
}

static inline bool
ticket_tryacq(tick_t *const p_lock)
{
    unsigned const next_ticket = atomic_load_explicit(&p_lock->next_ticket, memory_order_relaxed);

    // Assume that the lock is unlocked (the next_ticket and now_serving match)
    uint64_t const l_exp = ((uint64_t)next_ticket << 32) | (uint64_t)next_ticket;

    // try to write in 1. p_lock is a union so that this will do:
    // next_ticket = 1
    // now_serving = 0
    // atomically.
    return atomic_compare_exchange_weak_explicit(&p_lock->total, &l_exp, 1, memory_order_acquire, memory_order_relaxed);
}
