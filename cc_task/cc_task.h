// SPDX-License-Identifier: Apache-2.0

#ifndef CC_TASK_H
#define CC_TASK_H

// cc_task version 0.1.0
// https://github.com/ivan7wl/cc_task
//
// cc_task: symmetric coroutines
// cc_task.h: tasks + closure (block) bodies
// cc_task_conv.h: conventional function pointer bodies
// cc_task_stackful.h: stackful variants

#include "cc_primitive.h"
#include "cc_port.h"
#include "cc_task_conv.h"
#include "cc_task_stackful.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------------------------------------------
// closure (block) task body

#if CC_CFG_TASK_BLOCK

/** Make a block task body: cc_task_new(CC_TASK(t) { ... }). */
#define CC_TASK(t)                  ^cc_tr_e (cc_task_t *t, void *_)

/** Write access to a read only captured variable in block, e.g. for (c = 0; c != 10; $(c)++) */
#define $(var)                      (*(__typeof__(&(var)))(&(var)))

#endif // CC_CFG_TASK_BLOCK

//--------------------------------------------------------------------------------------------------------
// internal api

// enter t and return its body's cc_tr_e result
CC_INLINE cc_tr_e cc_task_call(cc_task_t *t, void *_, void *ctx)
{
    CC_PORT_SET_CURRENT_TASK(t);
    CC_MUSTTAIL return ((cc_tconv_func)t->fn)(t, _, ctx);
}

// return the current task on this thread, or NULL
CC_NULLABLE CC_INLINE cc_task_t *cc_task_current(void)
{
    return CC_PORT_CURRENT_TASK();
}

// free a non cc_tf_retain task; malloc-free builds only have retained tasks
CC_INLINE void cc_task_free(cc_task_t *t)
{
#if CC_CFG_USE_MALLOC
    if (!(t->flags & cc_tf_retain))
    {
        CC_PORT_FREE(t);
    }
#else
    CC_UNUSED(t);
#endif
}

// finish/free t, resume its parent, and return the resulting cc_tr_e; end value via cc_task_ret()
cc_tr_e cc_task_end(cc_task_t *t);

//--------------------------------------------------------------------------------------------------------
// public api
// see also cc_task_conv.h and cc_task_stackful.h

#if CC_CFG_TASK_BLOCK

/** Return bytes required for blk, or 0 for unsupported/oversized captures. */
cc_size cc_task_size(cc_task_func blk);

#if CC_CFG_USE_MALLOC
/** Return a new auto-free task, or NULL on failure; __block variables and blocks cannot be captured. */
cc_task_t *cc_task_new(cc_task_func blk);
#endif

/** Return mem as a retained task, or NULL if captures are unsupported or size < cc_task_size(blk). */
cc_task_t *cc_task_init(void *mem, cc_size size, cc_task_func blk);

/** Replace and run blk. Return the new body's result, or cc_tr_error without changing t
 *  if t is stackful or captures are unsupported/exceed capacity. */
cc_tr_e cc_task_exec(cc_task_t *t, cc_task_func blk);

#endif // CC_CFG_TASK_BLOCK

/** Run *t and return its cc_tr_e result; transfer/await may update *t; root end sets *t = NULL. */
CC_INLINE cc_tr_e cc_task_resume(CC_INOUT cc_task_t **t)
{
    cc_tr_e ret = cc_task_call(*t, *t, (*t)->ctx);
    *t = cc_task_current();
    return ret;
}

/** Mark suspended t for abort; resume it to run cleanup. No-op if already ended. */
void cc_task_abort(cc_task_t *t);

//--------------------------------------------------------------------------------------------------------
// task object

// size of a task object with n captured / user ctx bytes
#define CC_TASK_SIZE(n)             (CC_OFFSETOF(cc_task_t, ctx) + (cc_size)(n))

// typed access to the ctx area
// ctx alignment: 8 bytes for lp64, 4 bytes for ilp32
#define CC_CTX(t, type)             ((type *)(t)->ctx)

/** Return the last task result on this thread; save it before another task ends. */
CC_INLINE cc_uintptr cc_task_ret(void)
{
    return CC_PORT_TASK_RET();
}

/** Return CC_TRUE if t ended; do not pass an auto-freed task. */
CC_INLINE cc_bool cc_task_ended(cc_task_t *t)
{
    return (t->lc == CC_LC_DONE) ? CC_TRUE : CC_FALSE;
}

/** Return CC_TRUE if abort was requested for t. */
CC_INLINE cc_bool cc_task_aborted(cc_task_t *t)
{
    return (t->flags & cc_tf_abort) ? CC_TRUE : CC_FALSE;
}

#ifdef __cplusplus
}
#endif

#endif // CC_TASK_H
