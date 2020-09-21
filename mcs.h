#pragma once

#include <stdatomic.h>

typedef struct mcs_spinlock_node mcs_t;

struct mcs_spinlock_node {
    mcs_t *_Atomic m_next __attribute__((aligned(256)));
    long _Atomic m_locked;
};

/**
 * Acquire a mcs lock
 *
 * @param p_lock Actual lock
 * @param p_node Memory to spin on
 */
static inline void
mcs_acquire(mcs_t *const p_lock, mcs_t *const p_node)
{
    p_node->m_next = NULL;
    p_node->m_locked = 1;
    // p_lock is the actual lock and its m_next field points to the tail of the
    // queue for the lock

    // Place our context at the tail of the queue and get the previous tail.
    // Use seq_cst so that the writes to p_node aren't moved past here in the
    // prev_tail == NULL case.
    mcs_t *const prev_tail = atomic_exchange_explicit(&p_lock->m_next, p_node, memory_order_seq_cst);
    if (prev_tail != NULL) {
        // Use the prev_tail's m_next as an implicit lock to ensure that
        // writing to p_node occurs before writing to prev_tail->m_next
        atomic_store_explicit(&prev_tail->m_next, p_node, memory_order_release);

        for (;;) {
            long const locked = atomic_load_explicit(&p_node->m_locked, memory_order_acquire);
            if (!locked) {
                break;
            }
            atomic_thread_fence(memory_order_seq_cst);
        }
    }
}

/**
 * Release a mcs lock
 *
 * @param p_lock Actual lock
 * @param p_node Memory that was being spun on
 */
static inline void
mcs_release(mcs_t *const p_lock, mcs_t *const p_node)
{
    if (p_node->m_next == NULL) {
        // If we cannot see a waiter, try to atomically release the spinlock
        mcs_t *l_node = p_node;
        if (atomic_compare_exchange_strong_explicit(&p_lock->m_next, &l_node, NULL, memory_order_release, memory_order_relaxed)) {
            return;
        }

        // If we fail to atomically release the spinlock, we need to spin until
        // the new waiter has installed itself in our m_next ptr. This should
        // be on the order of 2 instructions.
        while (atomic_load_explicit(&p_node->m_next, memory_order_relaxed) == NULL) {
            atomic_thread_fence(memory_order_seq_cst);
        }
    }

    // Release the lock to the next waiter
    atomic_store_explicit(&p_node->m_next->m_locked, 0, memory_order_release);
}
