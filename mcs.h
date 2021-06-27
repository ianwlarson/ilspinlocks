#pragma once

#include <stddef.h>
#include <stdatomic.h>

#if defined(__x86_64__) || defined(__x86__)
__attribute__((always_inline))
static inline void
backoff(void) {
    asm volatile("pause");
}
#elif defined(__arm__) || defined(__aarch64__)
__attribute__((always_inline))
static inline void
backoff(void) {
    asm volatile("yield");
}
#else
__attribute__((always_inline))
static inline void
backoff(void) {
    ;
}
#endif

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
    atomic_store_explicit(&p_node->m_next, NULL, memory_order_relaxed);
    atomic_store_explicit(&p_node->m_locked, 1, memory_order_relaxed);
    // p_lock is the actual lock and its m_next field points to the tail of the
    // queue for the lock

    // Place our context at the tail of the queue and get the previous tail.
    // Use seq_cst so that the writes to p_node above aren't ordered after
    // putting our node in the queue, and prevents future writes from being
    // ordered before we have the lock in the NULL case.
    mcs_t *const prev_tail = atomic_exchange_explicit(&p_lock->m_next, p_node, memory_order_seq_cst);
    if (prev_tail != NULL) {
        // Link our node in so the node ahead of us can unlock us
        atomic_store_explicit(&prev_tail->m_next, p_node, memory_order_relaxed);

        for (;;) {
            backoff();
            long const locked = atomic_load_explicit(&p_node->m_locked, memory_order_relaxed);
            if (!locked) {
                // Ensure modifications following the acquire don't get
                // reordered before we have the lock.
                atomic_thread_fence(memory_order_acquire);
                break;
            }
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
    mcs_t *l_node = atomic_load_explicit(&p_node->m_next, memory_order_relaxed);
    if (l_node == NULL) {
        // If we cannot see a waiter, try to atomically release the spinlock
        l_node = p_node;
        if (atomic_compare_exchange_strong_explicit(&p_lock->m_next, &l_node, NULL, memory_order_release, memory_order_relaxed)) {
            return;
        }

        // If we fail to atomically release the spinlock, we need to spin until
        // the new waiter has installed itself in our m_next ptr. This should
        // be on the order of 2 instructions.
        do {
            backoff();
            l_node = atomic_load_explicit(&p_node->m_next, memory_order_relaxed);
        } while (l_node == NULL);
    }

    // Release the lock to the next waiter
    atomic_store_explicit(&l_node->m_locked, 0, memory_order_release);
}

