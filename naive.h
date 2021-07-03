#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "backoff.h"

static inline void
acquire(atomic_uint *const lock)
{
    unsigned v = 0;
    for (;;) {
        bool const acquired = atomic_compare_exchange_weak_explicit(lock, &v, 1, memory_order_acquire, memory_order_relaxed);
        if (acquired) break;
        wfe();
    }
}

static inline void
release(atomic_uint *const lock)
{
    atomic_store_explicit(lock, 0, memory_order_release);
    sev();
}

