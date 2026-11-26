// SPDX-License-Identifier: Apache-2.0

#include "cc_task.h"
#include "cc_port.h"

//--------------------------------------------------------------------------------------------------------
// internals

#if CC_CFG_TASK_BLOCK

// weak defaults for environments without a Blocks runtime; apps may override
unsigned char CC_WEAK _NSConcreteStackBlock[0];
unsigned char CC_WEAK _NSConcreteGlobalBlock[0];

// captured bytes of a block literal, (cc_size)-1 when it does not fit a cc_u16
static cc_size _cc_ctx_bytes(cc_task_func blk)
{
    const cc_blk_t *src = (const cc_blk_t *)(void *)blk;
    if (CC_UNLIKELY(src->flags & (1 << 25))) // BLOCK_HAS_COPY_DISPOSE
    {
        return (cc_size)-1;
    }

    cc_size n = (cc_size)(src->desc->size - sizeof(cc_blk_t));
    return CC_UNLIKELY(n > 0xFFFFu) ? (cc_size)-1 : n;
}

static cc_task_t *_cc_task_init(cc_task_t *t, cc_size n, cc_task_func blk, cc_tf_e flags)
{
    t->next     = CC_NULL;
    t->ctx_size = (cc_u16)n;
    t->flags    = flags;

    cc_blk_t *src = (cc_blk_t *)(void *)blk;
    t->fn    = src->invoke; // the invoke slot, read by the block call
    t->ext   = CC_NULL;
    CC_LC_INIT(t->lc);

    if (n != 0) // verbatim, padding included, keeps the capture offsets
    {
        CC_PORT_MEMCPY(t->ctx, src->ctx, n);
    }
    return t;
}

#endif // CC_CFG_TASK_BLOCK

cc_tr_e cc_task_end(cc_task_t *t)
{
#if CC_CFG_TASK_STACKFUL
    // stackful task should use return cc_tr_end instead of call this, but here just in case
    if (CC_UNLIKELY(cc_tstack_is_stackful(t) && cc_task_current() == t))
    {
        cc_tstack_exit(cc_task_ret());
    }
#endif

    cc_task_t *up = t->next;

    t->lc   = CC_LC_DONE;
    t->next = CC_NULL; // resuming the ended task again must not reach the parent

    if (up == CC_NULL)
    {
        CC_PORT_SET_CURRENT_TASK(CC_NULL);
        cc_task_free(t);
        return cc_tr_end;
    }

    if (CC_UNLIKELY(t->flags & cc_tf_abort))
    {
        up->flags |= cc_tf_abort;
        up->lc     = CC_LC_ABORT;
    }

    cc_task_free(t);
    return cc_task_call(up, up, up->ctx); // best-effort tail call
}

//--------------------------------------------------------------------------------------------------------
// public api

#if CC_CFG_TASK_BLOCK

cc_size cc_task_size(cc_task_func blk)
{
    cc_size n = _cc_ctx_bytes(blk);
    return CC_UNLIKELY(n == (cc_size)-1) ? 0 : CC_TASK_SIZE(n);
}

#if CC_CFG_USE_MALLOC
cc_task_t *cc_task_new(cc_task_func blk)
{
    cc_size n = _cc_ctx_bytes(blk);
    if (CC_UNLIKELY(n == (cc_size)-1))
    {
        return CC_NULL;
    }

    cc_task_t *t = (cc_task_t *)CC_PORT_MALLOC(CC_TASK_SIZE(n));
    if (CC_UNLIKELY(t == CC_NULL))
    {
        return CC_NULL;
    }

    return _cc_task_init(t, n, blk, cc_tf_none);
}
#endif // CC_CFG_USE_MALLOC

cc_task_t *cc_task_init(void *mem, cc_size size, cc_task_func blk)
{
    cc_size n = _cc_ctx_bytes(blk);
    if (CC_UNLIKELY(n == (cc_size)-1 || size < CC_TASK_SIZE(n)))
    {
        return CC_NULL;
    }

    return _cc_task_init((cc_task_t *)mem, n, blk, cc_tf_retain);
}

cc_tr_e cc_task_exec(cc_task_t *t, cc_task_func blk)
{
    cc_size n = _cc_ctx_bytes(blk);
    if (CC_UNLIKELY(cc_tstack_is_stackful(t) || n == (cc_size)-1 || n > t->ctx_size))
    {
        return cc_tr_error; // stackful, or ctx does not fit: leave the task untouched
    }

    cc_blk_t *src = (cc_blk_t *)(void *)blk;
    t->fn     = src->invoke;
    t->flags &= cc_tf_retain; // preserve ownership and parent link, clear the old abort
    CC_LC_INIT(t->lc);

    if (n != 0)
    {
        CC_PORT_MEMCPY(t->ctx, src->ctx, n);
    }

    return cc_task_call(t, t, t->ctx);
}

#endif // CC_CFG_TASK_BLOCK

void cc_task_abort(cc_task_t *t)
{
    if (CC_UNLIKELY(t->lc == CC_LC_DONE)) // ended: cleanup already ran or must not run
    {
        return;
    }

    // only mark it here, the next resume enters the cleanup path
    t->flags |= cc_tf_abort;
    t->lc     = CC_LC_ABORT;
}
