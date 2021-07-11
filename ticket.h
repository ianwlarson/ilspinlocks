#pragma once

#include <stdatomic.h>
#include <stddef.h>

#include "backoff.h"

typedef struct simple_ticket_spinlock tick_t;

struct simple_ticket_spinlock {
    atomic_uint next_ticket;
    atomic_uint now_serving;
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
#if defined(__arm__) || defined(__aarch64__)
            wfe();
#else
            for (unsigned i = 0; i < diff; ++i) {
                backoff();
            }
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
