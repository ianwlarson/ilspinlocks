#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "backoff.h"

typedef struct {
    alignas(64) atomic_uintptr_t v;
} gs_t;
typedef struct gta_lock gta_t;
struct gta_lock {
    alignas(64) atomic_uintptr_t m_tail;
    gs_t *slots;
};

#define NSEC_PER_SECOND UINT64_C(1000000000)

static inline void
gta_acquire(gta_t *const p_lock, unsigned const my_id)
{
    uintptr_t const my_whis = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed);
    uintptr_t const myset = (uintptr_t)&p_lock->slots[my_id].v | my_whis;
    uintptr_t const ahead = atomic_exchange_explicit(&p_lock->m_tail, myset, memory_order_acq_rel);

    atomic_uintptr_t *const who_is_ahead_of_me = (void *)(ahead & ~(uintptr_t)0x1);
    uintptr_t const other_whis = ahead & 0x1;

    for (;;) {
        uintptr_t const v = atomic_load_explicit(who_is_ahead_of_me, memory_order_relaxed);
        if (other_whis != v) {
            atomic_thread_fence(memory_order_acquire);
            break;
        }
        wfe();
    }
}

static inline void
gta_release(gta_t *const p_lock, unsigned const my_id)
{
    uintptr_t const nv = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed) ^ (uintptr_t)1;
    atomic_store_explicit(&p_lock->slots[my_id].v, nv, memory_order_release);
    sev();
}
