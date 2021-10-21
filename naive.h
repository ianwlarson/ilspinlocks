#pragma once

#include <stddef.h>
#include <stdatomic.h>

#include "backoff.h"

__attribute__((always_inline))
static inline void
acquire(atomic_uint *const lock)
{
    for (;;) {
        unsigned const v = atomic_fetch_or_explicit(lock, 1, memory_order_acquire);
        if (v == 0) {
            break;
        }
        backoff();
    }
}

__attribute__((always_inline))
static inline void
release(atomic_uint *const lock)
{
    atomic_store_explicit(lock, 0, memory_order_release);
}

/**
 *
 * Try to acquire a naive spinlock.
 *
 */
__attribute__((always_inline))
static inline _Bool
try_acquire(atomic_uint *const lock)
{
    unsigned const v = atomic_fetch_or_explicit(lock, 1, memory_order_acquire);
    return v == 0;
}
