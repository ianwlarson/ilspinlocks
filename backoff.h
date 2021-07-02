#pragma once

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

