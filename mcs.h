#pragma once

#include <stddef.h>
#include <stdalign.h>
#include <stdatomic.h>

/*
 * Common memory ordering explanations
 *
 * Lock is Already Owned Case
 * ==========================
 *
 * When a new thread comes to lock it does 3 things:
 * (assume that the lock was already owned)
 *
 * node->m_next = NULL;
 * node->m_locked = 1;
 * old_tail = swap(&lock->m_next, node);
 * if (old_tail != NULL) {
 *     old_tail->m_next = node;
 *     ...
 *
 * and when another thread wants to release the lock it does:
 *
 * if (node->m_next) {
 *      node->m_next->m_locked = 0;
 * }
 *
 * The acquire <-> release synchronization point is clearly on
 * node/old_tail->m_next.
 *
 * We want to avoid the situation where the release thread writes `m_locked=0`
 * BEFORE the acquire thread writes `m_locked=1` (otherwise the lock will stay
 * permanently locked).
 *
 * This means that the thread trying to acquire the lock needs to write to
 * old_tail->m_next with `memory_order_release` and threads trying to release
 * the lock must always read their `m_next` field with `memory_order_acquire`.
 *
 * Two Threads Acquiring Case
 * ==========================
 *
 * Let's look back at the previous case
 *
 * 1. node->m_next = NULL;
 * 2. node->m_locked = 1;
 * 3. old_tail = swap(&lock->m_next, node);
 *    if (old_tail != NULL) {
 * 4.     old_tail->m_next = node;
 *        ...
 *
 * Let's examine the case of two threads acquiring the lock simultaneously
 *
 * A1 - A stores to own m_next
 * B1 - B stores to own m_next
 * A2 - A stores to own m_locked
 * B2 - B stores to own m_locked
 * A3 - A has acquired the lock, storing itself in lock->m_next
 * B3 - B is the new_tail, old_tail is A
 * B4 - B writes to A->m_next
 *
 * We want to ensure that the write in B4 happens _AFTER_ the write in A1.
 * To accomplish this, A3 needs to be `memory_order_release` AND
 * `memory_order_acquire`
 *
 * Releasing & Spinning on a Lock
 * ==============================
 *
 * When releasing a lock we want to write
 * next_node->m_locked = 0 with release semantics and the waiter should read it
 * with acquire semantics to ensure it sees all of our writes.
 *
 * Release then Acquire
 * ====================
 *
 * In the uncontested release then uncontested acquire case, the memory
 * location of synchronization is p_lock->m_next.
 *
 */

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
    alignas(64) mcs_t *_Atomic m_next;
    long _Atomic m_locked;
};

/**
 * Acquire a mcs lock.
 *
 * @param p_lock The actual lock.
 * @param p_node Contributed node.
 */
static inline void
mcs_acquire(mcs_t *const p_lock, mcs_t *const p_node)
{
    atomic_store_explicit(&p_node->m_next, NULL, memory_order_relaxed);
    atomic_store_explicit(&p_node->m_locked, 1, memory_order_relaxed);

    // Place our context at the tail of the queue and get the previous tail.
    // Explanation of ordering above (two acquire case)
    mcs_t *const prev_tail = atomic_exchange_explicit(&p_lock->m_next, p_node, memory_order_acq_rel);
    if (prev_tail != NULL) {

        // Link our node in so the node ahead of us can unlock us.
        // Explanation of order above (lock already owned case)
        atomic_store_explicit(&prev_tail->m_next, p_node, memory_order_release);

        for (;;) {
            // TODO Is it better to do memory_order_acquire with no thread
            // fence?
            long const locked = atomic_load_explicit(&p_node->m_locked, memory_order_relaxed);
            if (!locked) {
                // Ensure modifications following the acquire don't get
                // reordered before we have the lock.
                atomic_thread_fence(memory_order_acquire);
                break;
            }
            backoff();
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
    // (lock already owned case explanation)
    mcs_t *l_node = atomic_load_explicit(&p_node->m_next, memory_order_acquire);
    if (l_node == NULL) {
        // If we cannot see a waiter, try to atomically release the spinlock.
        // Must use strong instead of weak because a spurious failure could
        // leave us waiting for another waiter that hasn't arrived.
        l_node = p_node;
        // 1. release (don't reorder writes after we release the lock), an
        // acquiring thread does a swap on p_lock->next with acquire so this
        // will work.
        // 2. relaxed - we don't have any writes we need another thread to see
        if (atomic_compare_exchange_strong_explicit(&p_lock->m_next, &l_node, NULL, memory_order_release, memory_order_relaxed)) {
            return;
        }

        // If we fail to atomically release the spinlock, we need to spin until
        // the new waiter has installed itself in our m_next ptr.
        for (;;) {

            // TODO Is it better to do the load with acquire and have no fence?
            // (lock already owned case explanation)
            l_node = atomic_load_explicit(&p_node->m_next, memory_order_relaxed);
            if (l_node != NULL) {
                atomic_thread_fence(memory_order_acquire);
                break;
            }
            backoff();
        };
    }

    // Release the lock to the next waiter
    // (release-acquire case)
    atomic_store_explicit(&l_node->m_locked, 0, memory_order_release);
}

/**
 * Release a mcs lock, variant 2.
 *
 * @param p_lock
 * @param p_node
 *
 */
static inline void
mcs_release2(mcs_t *const p_lock, mcs_t *const p_node)
{
    // (lock already owned case)
    mcs_t *l_node = atomic_load_explicit(&p_node->m_next, memory_order_acquire);
    if (l_node == NULL) {
        // It seems like there was no waiter, try to release the lock.
        // 1. release - don't reorder writes after we release the lock.
        mcs_t *const old_tail = atomic_exchange_explicit(&p_lock->m_next, NULL, memory_order_release);
        if (old_tail == p_node) {
            // I was really the tail.
            return;
        }

        // Someone else was actually the tail of the lock (they hadn't
        // installed themselves to our m_next when we checked)
        //
        // In this case we need to take the node after us and add it to the
        // queue. (e.g. we are trying to acquire the lock on behalf of someone
        // else)

        // Place the old_tail back at the end of the queue.
        // - relaxed, we don't have any other memory accesses we need to be
        // ordered
        mcs_t *const usurper = atomic_exchange_explicit(&p_lock->m_next, old_tail, memory_order_relaxed);

        for (;;) {
            // TODO Is it better to do the load with acquire and have no fence?
            // Wait for the node after us to install itself in our m_next.
            // (lock already owned case)
            l_node = atomic_load_explicit(&p_node->m_next, memory_order_relaxed);
            if (l_node != NULL) {
                atomic_thread_fence(memory_order_acquire);
                break;
            }
            backoff();
        }

        if (usurper != NULL) {
            // writes to l_node are visible to use because of our release ->
            // acquire link, but we need to ensure that usurper also sees them
            // so we need to do another release -> acquire ordering.

            // One or more threads added themselves to the queue, store l_node
            // as its m_next
            atomic_store_explicit(&usurper->m_next, l_node, memory_order_release);
        } else {

            // Nobody got in, we can release the next node.
            atomic_store_explicit(&l_node->m_locked, 0, memory_order_release);
        }

    } else {
        atomic_store_explicit(&l_node->m_locked, 0, memory_order_release);
    }
}

