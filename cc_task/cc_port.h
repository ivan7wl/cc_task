// SPDX-License-Identifier: Apache-2.0

#ifndef CC_PORT_H
#define CC_PORT_H

// port layer: everything the library needs from the platform
//
// defaults use the C runtime (malloc/free when enabled, compiler builtins) and
// trap on an assert failure. override the CC_PORT_* macros with -D, or provide a strong
// cc_port_panic() that prints and resets, to port the library elsewhere.

// optional project port header: build with -DCC_PORT_INCLUDE='"my_port.h"' and it
// is pulled in before every default, so it may declare functions and define the
// CC_PORT_* macros of this header
#include "cc_types.h"

#ifdef CC_PORT_INCLUDE
#include CC_PORT_INCLUDE
#endif

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------------------------------------------
// memory

#if CC_CFG_USE_MALLOC
#ifndef CC_PORT_MALLOC
void *malloc(cc_size);
#define CC_PORT_MALLOC(n)           malloc(n)
#endif

#ifndef CC_PORT_FREE
void free(void *);
#define CC_PORT_FREE(p)             free(p)
#endif
#endif // CC_CFG_USE_MALLOC

#ifndef CC_PORT_MEMCPY
#define CC_PORT_MEMCPY(d, s, n)     __builtin_memcpy((d), (s), (n))
#endif

#ifndef CC_PORT_MEMSET
#define CC_PORT_MEMSET(d, c, n)     __builtin_memset((d), (c), (n))
#endif

//--------------------------------------------------------------------------------------------------------
// current task and transient end value, one slot per thread

// storage class of the task slot, a single threaded port can define it empty
#ifndef CC_PORT_THREAD_LOCAL
#ifdef __cplusplus
#define CC_PORT_THREAD_LOCAL        thread_local
#else
#define CC_PORT_THREAD_LOCAL        _Thread_local
#endif
#endif

// task slot of this thread; a port can replace this single accessor to relocate
// both current and ret (a single threaded port can also define CC_PORT_THREAD_LOCAL empty)
#ifndef CC_PORT_TASK_SLOT
CC_INLINE cc_port_task_slot_t *cc_port_task_slot(void)
{
    extern CC_PORT_THREAD_LOCAL cc_port_task_slot_t cc_port_task_slot_default; // in cc_port.c
    return &cc_port_task_slot_default;
}
#define CC_PORT_TASK_SLOT()         cc_port_task_slot()
#endif

// current task of this thread, NULL before the first one and after a root task ends
#ifndef CC_PORT_CURRENT_TASK
CC_INLINE cc_task_t *cc_port_current_task(void)
{
    return CC_PORT_TASK_SLOT()->current;
}
#define CC_PORT_CURRENT_TASK()      cc_port_current_task()
#endif

// set the current task of this thread
// return: the previous current task
#ifndef CC_PORT_SET_CURRENT_TASK
CC_INLINE cc_task_t *cc_port_set_current_task(cc_task_t *t)
{
    cc_port_task_slot_t *slot = CC_PORT_TASK_SLOT();
    cc_task_t *prev = slot->current;
    slot->current = t;
    return prev;
}
#define CC_PORT_SET_CURRENT_TASK(t) cc_port_set_current_task(t)
#endif

// transient task result of this thread
#ifndef CC_PORT_TASK_RET
CC_INLINE cc_uintptr cc_port_task_ret(void)
{
    return CC_PORT_TASK_SLOT()->ret;
}
#define CC_PORT_TASK_RET()          cc_port_task_ret()
#endif

#ifndef CC_PORT_SET_TASK_RET
CC_INLINE void cc_port_set_task_ret(cc_uintptr ret)
{
    CC_PORT_TASK_SLOT()->ret = ret;
}
#define CC_PORT_SET_TASK_RET(ret)   cc_port_set_task_ret(ret)
#endif

//--------------------------------------------------------------------------------------------------------
// panic / assert

// assert failure: the default panic traps, a strong cc_port_panic() in the project can
// print the location, dump state and reset the board; a port may also define
// CC_PORT_PANIC(e) itself; the macro supplies the call-site file and line
#ifndef CC_PORT_PANIC
void cc_port_panic(const char *file, int line, const char *expr);
#define CC_PORT_PANIC(e)            cc_port_panic(__FILE__, __LINE__, (e))
#endif

//--------------------------------------------------------------------------------------------------------
// assert, compiled out with -DCC_NDEBUG

#ifndef CC_PORT_ASSERT
#if defined(CC_NDEBUG)
#define CC_PORT_ASSERT(c)           ((void)0)
#else
#define CC_PORT_ASSERT(c)           do                         \
                                    {                          \
                                        if (CC_UNLIKELY(!(c))) \
                                        {                      \
                                            CC_PORT_PANIC(#c); \
                                        }                      \
                                    } while (0)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // CC_PORT_H
