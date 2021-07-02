
Naïve Spinlock
==============

We often refer to a naïve spinlock for comparison purposes.

A naïve spinlock needs to do
1. Acquire using a Compare-and-Swap
2. Release using a store

It is unfair (order of arrival has no effect) and it does not scale (as the
number of spinning cores increases there is an eventual performance collapse).

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
