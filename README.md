
Naïve Spinlock
==============

We often refer to a naïve spinlock for comparison purposes.

A naïve spinlock needs to do
1. Acquire using a Compare-and-Swap
2. Release using a store

It is unfair (order of arrival has no effect) and it does not scale (as the
number of spinning cores increases there is an eventual performance collapse).

Ticket Lock
===========

Basic ticket spinlock.

It's 2 counters.
To lock it, the waiter takes a number and then waits until the other number
matches its number.
To release the lock, increment the `now_serving` value.

It probably scales linearly with the number of threads
attempting to acquire it, and also probably has a point at which there is a
performance collapse.

MCS Lock
========

Annotated version of the MCS Spinlock using C11 standard atomics.

I've found that there's an approximate 12% performance hit compared to a naïve
spinlock in the uncontested case.

In the uncontested acquire, the a swap is used, and in the release case a
compare-and-swap is used, compared to the CAS + Store in the naïve version so
the performance hit makes sense.

Adding in mild contention can cause a moderate performance degredation because
the false-uncontended release case is expensive. Once the number of threads
contending for the lock in a dead spin goes over 3, the MCS lock performance
reaches a steady state where adding more contenders does not negatively affect
lock performance.


Graunke and Thakkar's Array-Based Queue Lock
============================================

Annotated version of the GTA Spinlock using C11 standard atomics.

This lock is fantastic. It's scalable and fair.

It has the following disadvantages:

1. The lock has a larger constant cost.
2. You must know how many contenders for the lock there will be and
   pre-allocate the lock.
3. Each contender for the lock must have a fixed ID.

In any circumstance where these aren't a major concern, this lock is fantastic.
It's incredibly simple and efficient.

NOTE
====

Usages of wfe and sev assume that you're running either on bare metal or with
interrupts disabled. If you're running on an operating system (don't do this)
you want to use the version where wfe is replaced by a pause/yield.


Also, scalable locks are scalable in that they don't suffer a large performance
penalty when there is high contention. That being said, a system with high
contention of resources by definition doesn't scale. Spinlocks should only be
used under very rare circumstances.
