// SPDX-License-Identifier: Apache-2.0

#ifndef CC_HELPER_H
#define CC_HELPER_H

// compile time and compiler helpers, no include at all

//--------------------------------------------------------------------------------------------------------
// compile time

#ifdef __cplusplus
#define CC_STATIC_ASSERT(c, m)      static_assert((c), m)
#else
#define CC_STATIC_ASSERT(c, m)      _Static_assert((c), m)
#endif

#define CC_OFFSETOF(t, m)           __builtin_offsetof(t, m)
#define CC_ARRAY_SIZE(a)            ((__SIZE_TYPE__)sizeof(a) / (__SIZE_TYPE__)sizeof((a)[0]))
#define CC_BIT(n)                   (1u << (n))

// alignment helpers; align must be non-zero (typically CC_ALIGNOF(type))
#define CC_ALIGNOF(t)               __alignof__(t)
#define CC_IS_ALIGNED(p, align)     (((__UINTPTR_TYPE__)(p) % (__UINTPTR_TYPE__)(align)) == 0)

// round n down / up to a multiple of a, a should be a power of two
#define CC_ALIGN_DOWN(n, a)         ((n) & ~((a) - 1))
#define CC_ALIGN_UP(n, a)           (((n) + ((a) - 1)) & ~((a) - 1))

//--------------------------------------------------------------------------------------------------------
// compiler

#define CC_ALIGN(n)                 __attribute__((aligned(n)))
#define CC_NORETURN                 __attribute__((noreturn))
#define CC_NOINLINE                 __attribute__((noinline))
#define CC_NAKED                    __attribute__((naked))
#define CC_WEAK                     __attribute__((weak))
#define CC_MAYBE_UNUSED             __attribute__((unused))
#ifndef __has_attribute
#define __has_attribute(x)          0
#endif
#if __has_attribute(musttail)
#define CC_MUSTTAIL                 __attribute__((musttail))
#else
#define CC_MUSTTAIL
#endif
#define CC_INLINE                   static inline
#define CC_NULLABLE                 /* marks a pointer parameter that may be NULL */
#define CC_INOUT                    /* marks a pointer parameter the callee writes too */
#define CC_NULL                     ((void *)0)
#define CC_LIKELY(x)                __builtin_expect(!!(x), 1)
#define CC_UNLIKELY(x)              __builtin_expect(!!(x), 0)

// suppress the unused warning of every argument, the arguments are read, so
// pass plain variables or expressions without side effects
#define CC_UNUSED(x, ...)           (void)((void)(x), ##__VA_ARGS__)

#endif // CC_HELPER_H
