#pragma once

#include <stdatomic.h>

typedef struct mcs_spinlock_node mcs_t;

struct mcs_spinlock_node {
    _Atomic (mcs_t *) m_next; 
    _Atomic long m_locked;
};


static inline void
mcs_acquire(mcs_t *const p_base, mcs_t *const p_context)
{
    p_context->m_next = NULL;

    // Place our context at the tail of the queue
    // Use seq_cst instead of acquire because our context's m_next must be NULL
    mcs_t *const predecessor = atomic_exchange_explicit(&p_base->m_next, p_context, memory_order_seq_cst);
    if (predecessor != NULL) {
        p_context->m_locked = 1;
        // Use the predecessor's m_next as an implicit lock to ensure it doesn't clear
        // m_locked until will set it
        atomic_store_explicit(&predecessor->m_next, p_context, memory_order_release);

        for (;;) {
            long const locked = atomic_load_explicit(&p_context->m_locked, memory_order_acquire);
            if (!locked) {
                break;
            }
            atomic_thread_fence(memory_order_seq_cst);
        }
    }
}

static inline void
mcs_release(mcs_t *const p_base, mcs_t *const p_context)
{
    if (p_context->m_next == NULL) {
        // If we cannot see a next in our waiter, try to atomically release the spinlock
        mcs_t *l_node = p_context;
        if (atomic_compare_exchange_strong_explicit(&p_base->m_next, &l_node, NULL, memory_order_release, memory_order_relaxed)) {
            return;
        }

        // If we fail to atomically release the spinlock, we need to wait until the new
        // waiter has installed itself in our m_next ptr
        while (atomic_load_explicit(&p_context->m_next, memory_order_relaxed) == NULL) {
            atomic_thread_fence(memory_order_seq_cst);
        }
    }

    // Release the lock to the next waiter
    atomic_store_explicit(&p_context->m_next->m_locked, 0, memory_order_release);
}
