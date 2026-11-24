// SPDX-License-Identifier: Apache-2.0

#ifndef CC_TASK_CONV_H
#define CC_TASK_CONV_H

// conventional task: the body is a plain cc_tconv_func function instead of a block literal

#include "cc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------------------------------------------
// public api

#if CC_CFG_TASK_CONV

#if CC_CFG_USE_MALLOC
/** Return a new auto-free task, or NULL on allocation failure/ctx_size > 65535;
 *  copy ctx or zero-fill it when ctx is NULL. */
cc_task_t *cc_tconv_new(cc_tconv_func func, CC_NULLABLE const void *ctx, cc_size ctx_size);
#endif

/** Return mem as a retained task, or NULL if ctx_size > 65535 or size < CC_TASK_SIZE(ctx_size). */
cc_task_t *cc_tconv_init(void *mem, cc_size size, cc_tconv_func func,
                         CC_NULLABLE const void *ctx, cc_size ctx_size);

/** Replace func and run. ctx_size 0 keeps ctx; otherwise copy ctx or zero-fill if NULL.
 *  Return the new body's result, or cc_tr_error without changing t if t is stackful
 *  or ctx_size > t->ctx_size. */
cc_tr_e cc_tconv_exec(cc_task_t *t, cc_tconv_func func,
                      CC_NULLABLE const void *ctx, cc_size ctx_size);

#endif // CC_CFG_TASK_CONV

#ifdef __cplusplus
}
#endif

#endif // CC_TASK_CONV_H
