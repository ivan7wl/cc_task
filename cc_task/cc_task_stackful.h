// SPDX-License-Identifier: Apache-2.0

#ifndef CC_TASK_STACKFUL_H
#define CC_TASK_STACKFUL_H

// stackful coroutines in closure (block) and conventional function variants

#include "cc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CC_CFG_TASK_STACKFUL

//--------------------------------------------------------------------------------------------------------
// queries

/** CC_TRUE if live t is stackful */
cc_bool cc_tstack_is_stackful(const cc_task_t *t);

//--------------------------------------------------------------------------------------------------------
// public api

#if CC_CFG_TASK_BLOCK

/** Return bytes needed for a task built from blk and stack size, or 0 on invalid capture/size. */
cc_size cc_tstack_size(cc_size stack_size, cc_task_func blk);

#if CC_CFG_USE_MALLOC
/** Return a new auto-free stackful task, or NULL on failure. */
cc_task_t *cc_tstack_new(cc_size stack_size, cc_task_func blk);
#endif

/** Initialize with 8-byte-aligned mem and size from cc_tstack_size().
 *  Return mem as a retained task, or NULL on invalid storage/captures. */
cc_task_t *cc_tstack_init(void *mem, cc_size size, cc_task_func blk);

/** Replace and run a suspended task. Return the new body's result, or cc_tr_error
 *  without changing t if running, not stackful, or captures are invalid/too large. */
cc_tr_e cc_tstack_exec(cc_task_t *t, cc_task_func blk);

#endif // CC_CFG_TASK_BLOCK

#if CC_CFG_USE_MALLOC
/** Return a new auto-free stackful task, or NULL on failure; copy ctx or zero-fill if NULL. */
cc_task_t *cc_tstack_conv_new(cc_tconv_func func, CC_NULLABLE const void *ctx,
                              cc_size ctx_size, cc_size stack_size);
#endif

/** Initialize with 8-byte-aligned mem; size covers task, ctx and stack.
 *  Return mem as a retained task, or NULL on invalid alignment/size. */
cc_task_t *cc_tstack_conv_init(void *mem, cc_size size, cc_tconv_func func,
                               CC_NULLABLE const void *ctx, cc_size ctx_size);

/** Replace and run a suspended task. ctx_size 0 keeps ctx; otherwise copy ctx or zero-fill if NULL.
 *  Return the new body's result, or cc_tr_error without changing t if not stackful,
 *  running, or ctx_size > t->ctx_size. */
cc_tr_e cc_tstack_conv_exec(cc_task_t *t, cc_tconv_func func,
                            CC_NULLABLE const void *ctx, cc_size ctx_size);

//--------------------------------------------------------------------------------------------------------
// stackful task control primitives

/** Suspend with cc_tr_yield; return CC_TRUE on normal resume. On CC_FALSE,
 *  clean up and return cc_tr_end without suspending again. */
cc_bool cc_tstack_yield(void);
/** Same as cc_tstack_yield(), but the owner receives cc_tr_wait. */
cc_bool cc_tstack_wait(void);

/** Transfer to next. Return CC_TRUE on normal resume; on CC_FALSE, clean up and
 *  return cc_tr_end without suspending again. */
cc_bool cc_tstack_trans(cc_task_t *next);

/** End with result ret from any depth; skipped frames are not unwound. */
CC_NORETURN void cc_tstack_exit(cc_uintptr ret);

/** Yield until cond; return CC_TRUE when met, CC_FALSE on abort. */
#define cc_tstack_yield_until(cond)     ({                                              \
                                            cc_bool ok_ = CC_TRUE;                      \
                                            while (ok_ && !(cond))                      \
                                            {                                           \
                                                ok_ = cc_tstack_yield();                \
                                            }                                           \
                                            ok_;                                        \
                                        })

/** Yield while cond; return CC_TRUE when cond becomes false, CC_FALSE on abort. */
#define cc_tstack_yield_while(cond)     cc_tstack_yield_until(!(cond))

/** Evaluate op once; goto label on CC_FALSE. */
#define cc_tstack_eh(op, label)         do if (CC_UNLIKELY(!(op))) { goto label; } while (0)

//--------------------------------------------------------------------------------------------------------
// debug

/** CC_TRUE if the bottom 16-byte stack canary was overwritten. */
cc_bool cc_tstack_canary_hit(cc_task_t *t);

#else // CC_CFG_TASK_STACKFUL

// no stackful task in this build, the query still exists and never says yes
CC_INLINE cc_bool cc_tstack_is_stackful(const cc_task_t *t)
{
    CC_UNUSED(t);
    return CC_FALSE;
}

#endif // CC_CFG_TASK_STACKFUL

#ifdef __cplusplus
}
#endif

#endif // CC_TASK_STACKFUL_H
