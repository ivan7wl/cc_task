// SPDX-License-Identifier: Apache-2.0

#include "cc_task_conv.h"
#include "cc_port.h"
#include "cc_task.h"

#if CC_CFG_TASK_CONV

//--------------------------------------------------------------------------------------------------------
// internal api

// fill task header, copy or zero the ctx
static cc_task_t *_cc_tconv_init(cc_task_t *t, cc_tconv_func func, CC_NULLABLE const void *ctx,
                                 cc_size n, cc_tf_e flags)
{
    t->next     = CC_NULL;
    t->ctx_size = (cc_u16)n;
    t->flags    = flags;
    t->fn       = (void (*)(void))func;  // the invoke slot, read by the block call
    t->_desc    = CC_NULL;
    CC_LC_INIT(t->lc);

    if (n != 0)
    {
        if (ctx != CC_NULL)
        {
            CC_PORT_MEMCPY(t->ctx, ctx, n);
        }
        else CC_PORT_MEMSET(t->ctx, 0, n);
    }

    return t;
}

//--------------------------------------------------------------------------------------------------------
// public api

#if CC_CFG_USE_MALLOC
cc_task_t *cc_tconv_new(cc_tconv_func func, CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    if (CC_UNLIKELY(ctx_size > 0xFFFFu)) // ctx_size is a cc_u16
    {
        return CC_NULL;
    }

    cc_task_t *t = (cc_task_t *)CC_PORT_MALLOC(CC_TASK_SIZE(ctx_size));
    if (CC_UNLIKELY(t == CC_NULL))
    {
        return CC_NULL;
    }

    return _cc_tconv_init(t, func, ctx, ctx_size, cc_tf_none);
}
#endif // CC_CFG_USE_MALLOC

cc_task_t *cc_tconv_init(void *mem, cc_size size, cc_tconv_func func,
                         CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    if (CC_UNLIKELY(ctx_size > 0xFFFFu || size < CC_TASK_SIZE(ctx_size)))
    {
        return CC_NULL;
    }

    return _cc_tconv_init((cc_task_t *)mem, func, ctx, ctx_size, cc_tf_retain);
}

cc_tr_e cc_tconv_exec(cc_task_t *t, cc_tconv_func func,
                      CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    if (CC_UNLIKELY(cc_tstack_is_stackful(t) || ctx_size > t->ctx_size))
    {
        return cc_tr_error; // stackful, or ctx does not fit: leave the task untouched
    }

    t->fn     = (void (*)(void))func;
    t->flags &= cc_tf_retain; // preserve ownership and parent link, clear the old abort
    CC_LC_INIT(t->lc);

    if (ctx_size != 0)
    {
        if (ctx != CC_NULL)
        {
            CC_PORT_MEMCPY(t->ctx, ctx, ctx_size);
        }
        else CC_PORT_MEMSET(t->ctx, 0, ctx_size);
    }

    return cc_task_call(t, t, t->ctx);
}

#endif // CC_CFG_TASK_CONV
