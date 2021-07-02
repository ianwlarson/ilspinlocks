#pragma once

//
// Graunke and Thakkar's Array-Based Queue Lock
//
// This lock trades upfront allocation size for speed and simplicity. An
// excellent tradeoff if I do say so myself!
//
// The way it works is that each thread has a unique ID and uses that to index
// into the slots of the lock.
//
// When a thread wants to acquire the lock, it reads the value in it's slot and combines
// the least significant bit of the slot with the address of the slot.
//
// It will then do an atomic swap with the value in m_tail which describes who
// currently has the lock and what value it will write into it's slot when it
// is done. We then wait for it to write that value into the slot.
//

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
    size_t m_allocsz;
};

static inline void
gta_acquire(gta_t *const p_lock, unsigned const my_id)
{
    uintptr_t const my_cond = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed);
    // There is a direct data dependency between the read of our slot and the
    // write to m_tail so we know that it'll be ordered correctly.
    uintptr_t const my_set = (uintptr_t)&p_lock->slots[my_id].v | (my_cond & 0x1);
    uintptr_t const ahead = atomic_exchange_explicit(&p_lock->m_tail, my_set, memory_order_relaxed);

    atomic_uintptr_t *const ahead_ptr = (void *)(ahead & ~(uintptr_t)0x1);
    uintptr_t const ahead_cond = ahead & 0x1;

    for (;;) {
        uintptr_t const new_cond = atomic_load_explicit(ahead_ptr, memory_order_acquire);
        if (ahead_cond != new_cond) {
            // The owner has released the lock to us
            break;
        }
        wfe();
    }
}

static inline void
gta_release(gta_t *const p_lock, unsigned const my_id)
{
    uintptr_t const my_cond = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed) & 0x1;
    atomic_store_explicit(&p_lock->slots[my_id].v, my_cond ^ 0x1, memory_order_release);
    sev();
}

static inline void
gta_reset(gta_t *const p_lock)
{
    p_lock->slots[0].v = 0;
    p_lock->m_tail = (uintptr_t)&p_lock->slots[0].v | 0x1;
}
