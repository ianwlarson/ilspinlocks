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

#define NSEC_PER_SECOND UINT64_C(1000000000)

static inline void
gta_acquire(gta_t *const p_lock, unsigned const my_id)
{
    uintptr_t const my_whis = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed);
    // There is a direct data dependency between the read of our slot and the
    // write to m_tail so we know that it'll be ordered correctly.
    uintptr_t const myset = (uintptr_t)&p_lock->slots[my_id].v | my_whis;
    uintptr_t const ahead = atomic_exchange_explicit(&p_lock->m_tail, myset, memory_order_relaxed);

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
    uintptr_t const lockval = atomic_load_explicit(&p_lock->slots[my_id].v, memory_order_relaxed) & 0x1;
    atomic_store_explicit(&p_lock->slots[my_id].v, lockval ^ 0x1, memory_order_release);
    sev();
}
