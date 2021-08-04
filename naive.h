#pragma once

#include <stddef.h>
#include <stdatomic.h>

#include "backoff.h"

__attribute__((always_inline))
static inline void
acquire(atomic_uint *const lock)
{
    for (;;) {
        unsigned v = 0;
#if defined(__arm__) || defined(__aarch64__)
        // Need to use strong if also using wfe, otherwise a core could
        // spuriously fail to lock an unlocked lock and then spin waiting for a
        // sev that will never happen.
        _Bool const acquired = atomic_compare_exchange_strong_explicit(lock, &v, 1, memory_order_acquire, memory_order_relaxed);
        if (acquired) {
            break;
        }

        wfe();
#else
        _Bool const acquired = atomic_compare_exchange_weak_explicit(lock, &v, 1, memory_order_acquire, memory_order_relaxed);
        if (acquired) {
            break;
        }

        backoff();
#endif
    }
}

__attribute__((always_inline))
static inline void
release(atomic_uint *const lock)
{
    atomic_store_explicit(lock, 0, memory_order_release);
#if defined(__arm__) || defined(__aarch64__)
    sev();
#endif
}

__attribute__((always_inline))
static inline _Bool
try_acquire(atomic_uint *const lock)
{
    unsigned v = 0;
    return atomic_compare_exchange_weak_explicit(lock, &v, 1, memory_order_acquire, memory_order_relaxed);
}
