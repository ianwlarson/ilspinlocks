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

__attribute__((always_inline))
static inline void
sev(void) {
    asm volatile("sev");
}
__attribute__((always_inline))
static inline void
wfe(void) {
    asm volatile("wfe");
}
#else
__attribute__((always_inline))
static inline void
backoff(void) {
    ;
}
#endif

// Add sev/wfe inlines

#if defined(__arm__) || defined(__aarch64__)
__attribute__((always_inline))
static inline void
sev(void) {
    asm volatile("sev");
}
__attribute__((always_inline))
static inline void
wfe(void) {
    asm volatile("wfe");
}
#else
__attribute__((always_inline))
static inline void
sev(void) {
}
__attribute__((always_inline))
static inline void
wfe(void) {
    backoff();
}
#endif
