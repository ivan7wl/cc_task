// SPDX-License-Identifier: Apache-2.0

#include "cc_task.h"
#include "cc_swap.h"

#if CC_CFG_TASK_STACKFUL

//--------------------------------------------------------------------------------------------------------
// stackful task layout: [cc_task_t][ctx (pad)][control block (16B aligned)][stack (start and size 16B aligned) ...]
// the task base must be 8B aligned
// ctx size is padded to make sure control block is 16B aligned

// the task base is required to be 8B aligned by the stackful layout
#define CC_TSTACK_ALIGN_BASE    8

// the control block, the stack start and its bottom 16 bytes are 16B aligned
#define CC_TSTACK_ALIGN         16

// the control block
typedef struct _cc_tstack_t
{
    void        *saved;         // saved context to swap
    cc_tconv_func fn;           // the original body invoke
    cc_size      size;          // stack bytes, multiple of 16
    cc_bool      running;       // reject re-entry while this stack is live

    union
    {
        cc_u64 canary[0] CC_ALIGN(CC_TSTACK_ALIGN); // the 16 bytes at the stack bottom
        cc_u8  stack[0];                            // the stack starts here
    };
} cc_tstack_t;

CC_STATIC_ASSERT(sizeof(cc_tstack_t) == 4 * CC_PTR_SIZE, "cc_tstack_t: 32 bytes on LP64, 16 on ILP32");
CC_STATIC_ASSERT(CC_OFFSETOF(cc_tstack_t, stack) == sizeof(cc_tstack_t), "cc_tstack_t: stack");
CC_STATIC_ASSERT(CC_OFFSETOF(cc_tstack_t, canary) == sizeof(cc_tstack_t), "cc_tstack_t: canary");
CC_STATIC_ASSERT(CC_TSTACK_ALIGN_BASE >= CC_ALIGNOF(cc_task_t), "cc_tstack_t: base alignment");

//--------------------------------------------------------------------------------------------------------

// the pad before the control block is kept in ctx_size, 15 bytes cover the worst case
#define CC_TSTACK_PAD           (CC_TSTACK_ALIGN - 1)
#define CC_TSTACK_CANARY        0xDEADBEEFAA556677ull

// the control block of t right after the ctx area, the pad is already inside ctx_size
#define CC_TSTACK(t)            ((cc_tstack_t *)((cc_u8 *)(t) + CC_OFFSETOF(cc_task_t, ctx) + (t)->ctx_size))

//--------------------------------------------------------------------------------------------------------

static CC_NORETURN void _cc_tstack_bug(const char *what)
{
    CC_PORT_PANIC(what);
    __builtin_unreachable();
}

static cc_tstack_t *_cc_tstack_of(cc_task_t *t)
{
    if (CC_UNLIKELY(!cc_tstack_is_stackful(t)))
    {
        _cc_tstack_bug("cc_tstack: the current task is not a stackful task");
    }
    return CC_TSTACK(t);
}

// the suspended context must stay inside its own stack
static void _cc_tstack_check_sp(cc_tstack_t *ts, void *sp)
{
    CC_UNUSED(ts, sp);
    CC_PORT_ASSERT((cc_uintptr)sp >= (cc_uintptr)ts->stack &&
                   (cc_uintptr)sp < (cc_uintptr)(ts->stack + ts->size));
}

//--------------------------------------------------------------------------------------------------------

// the first frame on the task stack, call the body then swap back
static CC_NORETURN void _cc_tstack_entry(void *p)
{
    cc_task_t   *t  = (cc_task_t *)p;
    cc_tstack_t *ts = CC_TSTACK(t);
    cc_tr_e    code = ts->fn(t, t, t->ctx);

    // the body returned end; an abort is cc_task_abort() plus cc_tr_end
    CC_PORT_ASSERT(code == cc_tr_end || t->lc == CC_LC_DONE);

    // the body returned, leave the task stack and end it in the wrapper
    ts->running = CC_FALSE;
    CC_PORT_SET_CURRENT_TASK(t); // trans can return to an earlier task's wrapper
    cc_swap(&ts->saved, ts->saved, (void *)(cc_uintptr)code);
    CC_PORT_PANIC("cc_tstack: finished task resumed");
    __builtin_unreachable();
}

// the task's fn, runs on the owner stack
static cc_tr_e _cc_tstack_wrapper(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);

    cc_tstack_t *ts = CC_TSTACK(t);
    if (CC_UNLIKELY(ts->running)) // resuming a task that is already on the cpu
    {
        _cc_tstack_bug("cc_tstack: resume a task that is already running");
    }

    if (CC_UNLIKELY(t->lc == CC_LC_DONE)) // ended: same answer as a stackless task
    {
        CC_PORT_SET_CURRENT_TASK(CC_NULL);
        return cc_tr_end;
    }

    void *sp = ts->saved; // my own resume point, NULL before the first run
    if (CC_UNLIKELY(t->lc == CC_LC_ABORT && sp == CC_NULL))
    {
        CC_PORT_SET_CURRENT_TASK(CC_NULL); // owner stack: cc_task_end() is allowed here
        return cc_task_end(t);             // aborted before it ever ran, nothing to unwind
    }

    ts->running = CC_TRUE;
    if (CC_LIKELY(sp != CC_NULL))
    {
        _cc_tstack_check_sp(ts, sp);
    }

    cc_tr_e code = (cc_tr_e)(cc_uintptr)(CC_UNLIKELY(sp == CC_NULL)
                   ? cc_swap_start(&ts->saved, ts->stack + ts->size, ts->size, _cc_tstack_entry, t)
                   : cc_swap(&ts->saved, sp, (void *)(cc_uintptr)cc_tr_yield));

    if (CC_UNLIKELY(code == cc_tr_trans)) // a target that must run on the owner stack
    {
        cc_task_t *next = cc_task_current(); // trans put the target in the slot
        CC_MUSTTAIL return cc_task_call(next, next, next->ctx);
    }

    if (code == cc_tr_end) // done, the current task may be a target
    {
        cc_task_t *done = cc_task_current();

        // running is false and execution is back on the owner stack
        CC_PORT_SET_CURRENT_TASK(CC_NULL);
        return cc_task_end(done);
    }

    return code;
}

//--------------------------------------------------------------------------------------------------------

// whole size with stack on worst pad case
// return: the block size needed, 0 for a zero stack, overflow, or ctx not fitting into cc_u16
static cc_size _cc_tstack_max_size(cc_size ctx_size, cc_size stack_size)
{
    if (CC_UNLIKELY(ctx_size > 0xFFFFu - CC_TSTACK_PAD || stack_size == 0 ||
                    stack_size > (cc_size)-1 - (CC_TSTACK_ALIGN - 1)))
    {
        return 0;
    }

    cc_size fixed = CC_OFFSETOF(cc_task_t, ctx) + ctx_size + CC_TSTACK_PAD + sizeof(cc_tstack_t);
    cc_size stack = CC_ALIGN_UP(stack_size, CC_TSTACK_ALIGN);
    if (CC_UNLIKELY(stack > (cc_size)-1 - fixed))
    {
        return 0;
    }
    return fixed + stack;
}

static void _cc_tstack_mark(cc_tstack_t *ts)
{
    ts->canary[0] = CC_TSTACK_CANARY;
    ts->canary[1] = CC_TSTACK_CANARY;
}

// reset the per run state, the stack geometry is left as it is
static void _cc_tstack_reset(cc_tstack_t *ts, cc_tconv_func func)
{
    ts->saved   = CC_NULL;
    ts->running = CC_FALSE;
    ts->fn      = func;
    _cc_tstack_mark(ts);
}

// settle the stack, the control block sits at the 16 byte aligned address ts
// the stack runs up to end
static void _cc_tstack_cb_init(cc_tstack_t *ts, cc_uintptr end, cc_tconv_func func)
{
    ts->size = (cc_size)(end - (cc_uintptr)ts->stack);
    _cc_tstack_reset(ts, func);
}

// copy or zero the ctx bytes
static void _cc_tstack_ctx_fill(cc_task_t *t, CC_NULLABLE const void *ctx, cc_size n)
{
    if (n != 0)
    {
        if (ctx != CC_NULL)
        {
            CC_PORT_MEMCPY(t->ctx, ctx, n);
        }
        else CC_PORT_MEMSET(t->ctx, 0, n);
    }
}

// init cc_task_t and ctx
static void _cc_tstack_task_init(cc_task_t *t, CC_NULLABLE const void *ctx, cc_size ctx_size,
                                 cc_tf_e flags)
{
    cc_uintptr p   = (cc_uintptr)t + CC_OFFSETOF(cc_task_t, ctx) + ctx_size;
    cc_size    pad = (cc_size)(CC_ALIGN_UP(p, CC_TSTACK_ALIGN) - p); // align the control block

    t->next     = CC_NULL;
    t->ctx_size = (cc_u16)(ctx_size + pad);
    t->flags    = flags;
    t->fn       = (void (*)(void))_cc_tstack_wrapper;
    t->ext      = CC_NULL;
    CC_LC_INIT(t->lc);

    _cc_tstack_ctx_fill(t, ctx, ctx_size);
}

// init task, ctx and control block
static cc_task_t *_cc_tstack_init(cc_task_t *t, cc_size size, cc_tconv_func func,
                                  CC_NULLABLE const void *ctx, cc_size ctx_size, cc_tf_e flags)
{
    _cc_tstack_task_init(t, ctx, ctx_size, flags);

    cc_uintptr end = CC_ALIGN_DOWN((cc_uintptr)t + size, CC_TSTACK_ALIGN); // aligned stack top
    _cc_tstack_cb_init(CC_TSTACK(t), end, func);
    return t;
}

static cc_tr_e _cc_tstack_exec(cc_task_t *t, cc_tconv_func func, CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    cc_tstack_t *ts = CC_TSTACK(t);
    if (CC_UNLIKELY(ts->running)) // re-entry
    {
        return cc_tr_error;
    }

    _cc_tstack_reset(ts, func); // the stack top stays at ts->stack + ts->size

    t->flags &= cc_tf_retain; // preserve ownership, clear the old abort
    CC_LC_INIT(t->lc);
    _cc_tstack_ctx_fill(t, ctx, ctx_size);

    return cc_task_call(t, t, t->ctx);
}

//--------------------------------------------------------------------------------------------------------

cc_bool cc_tstack_is_stackful(const cc_task_t *t)
{
    return (t->fn == (void (*)(void))_cc_tstack_wrapper) ? CC_TRUE : CC_FALSE;
}

//--------------------------------------------------------------------------------------------------------

#if CC_CFG_TASK_BLOCK

cc_size cc_tstack_size(cc_size stack_size, cc_task_func blk)
{
    cc_size sz = cc_task_size(blk); // 0 for not fit
    return CC_UNLIKELY(sz == 0) ? 0 : _cc_tstack_max_size(sz - CC_OFFSETOF(cc_task_t, ctx), stack_size);
}

#if CC_CFG_USE_MALLOC
cc_task_t *cc_tstack_new(cc_size stack_size, cc_task_func blk)
{
    cc_size task = cc_task_size(blk); // task size with ctx
    if (CC_UNLIKELY(task == 0)) // blk not fit
    {
        return CC_NULL;
    }

    cc_size n    = task - CC_OFFSETOF(cc_task_t, ctx);  // ctx size, captured bytes
    cc_size size = _cc_tstack_max_size(n, stack_size);
    if (CC_UNLIKELY(size == 0)) // whole size not fit
    {
        return CC_NULL;
    }

    cc_task_t *t = (cc_task_t *)CC_PORT_MALLOC(size);
    if (CC_UNLIKELY(t == CC_NULL))
    {
        return CC_NULL;
    }

    const cc_blk_t *src = (const cc_blk_t *)(const void *)blk;
    return _cc_tstack_init(t, size, (cc_tconv_func)src->invoke, src->ctx, n, cc_tf_none);
}
#endif // CC_CFG_USE_MALLOC

cc_task_t *cc_tstack_init(void *mem, cc_size size, cc_task_func blk)
{
    cc_size task = cc_task_size(blk); // task size with ctx
    if (CC_UNLIKELY(task == 0)) // blk not fit
    {
        return CC_NULL;
    }

    cc_size n    = task - CC_OFFSETOF(cc_task_t, ctx);
    cc_size need = _cc_tstack_max_size(n, 16); // min size of the task
    if (CC_UNLIKELY(need == 0 || size < need || !CC_IS_ALIGNED(mem, CC_TSTACK_ALIGN_BASE))) // invalid storage
    {
        return CC_NULL;
    }

    const cc_blk_t *src = (const cc_blk_t *)(const void *)blk;
    return _cc_tstack_init((cc_task_t *)mem, size, (cc_tconv_func)src->invoke, src->ctx, n,
                           cc_tf_retain);
}

cc_tr_e cc_tstack_exec(cc_task_t *t, cc_task_func blk)
{
    cc_size task = cc_task_size(blk);
    if (CC_UNLIKELY(!cc_tstack_is_stackful(t) || task == 0))
    {
        return cc_tr_error;
    }

    cc_size n = task - CC_OFFSETOF(cc_task_t, ctx); // new ctx size
    if (CC_UNLIKELY(n > t->ctx_size))               // ctx not fit
    {
        return cc_tr_error;
    }

    const cc_blk_t *src = (const cc_blk_t *)(const void *)blk;
    return _cc_tstack_exec(t, (cc_tconv_func)src->invoke, src->ctx, n);
}

#endif // CC_CFG_TASK_BLOCK

//--------------------------------------------------------------------------------------------------------

#if CC_CFG_USE_MALLOC
cc_task_t *cc_tstack_conv_new(cc_tconv_func func, CC_NULLABLE const void *ctx,
                              cc_size ctx_size, cc_size stack_size)
{
    cc_size    size = _cc_tstack_max_size(ctx_size, stack_size);
    cc_task_t *t    = CC_UNLIKELY(size == 0) ? CC_NULL : (cc_task_t *)CC_PORT_MALLOC(size);
    if (CC_UNLIKELY(t == CC_NULL))
    {
        return CC_NULL;
    }

    return _cc_tstack_init(t, size, func, ctx, ctx_size, cc_tf_none);
}
#endif // CC_CFG_USE_MALLOC

cc_task_t *cc_tstack_conv_init(void *mem, cc_size size, cc_tconv_func func,
                               CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    cc_size need = _cc_tstack_max_size(ctx_size, 16); // min size of the task
    if (CC_UNLIKELY(need == 0 || size < need || !CC_IS_ALIGNED(mem, CC_TSTACK_ALIGN_BASE))) // invalid storage
    {
        return CC_NULL;
    }

    return _cc_tstack_init((cc_task_t *)mem, size, func, ctx, ctx_size, cc_tf_retain);
}

cc_tr_e cc_tstack_conv_exec(cc_task_t *t, cc_tconv_func func,
                            CC_NULLABLE const void *ctx, cc_size ctx_size)
{
    if (CC_UNLIKELY(!cc_tstack_is_stackful(t) || ctx_size > t->ctx_size))
    {
        return cc_tr_error;
    }

    return _cc_tstack_exec(t, func, ctx, ctx_size);
}

//--------------------------------------------------------------------------------------------------------

// suspend t with code
// return: CC_TRUE when resumed, CC_FALSE when aborted meanwhile
static cc_bool _cc_tstack_suspend(cc_task_t *t, cc_tr_e code)
{
    cc_tstack_t *ts = _cc_tstack_of(t);
    CC_PORT_ASSERT(ts->running);

    ts->running = CC_FALSE;
    cc_swap(&ts->saved, ts->saved, (void *)(cc_uintptr)code); // swap to saved (wrapper)

    // resume swap back; abort was marked on t before this resume
    ts->running = CC_TRUE;
    return CC_UNLIKELY(t->flags & cc_tf_abort) ? CC_FALSE : CC_TRUE;
}

cc_bool cc_tstack_yield(void)
{
    return _cc_tstack_suspend(cc_task_current(), cc_tr_yield);
}

cc_bool cc_tstack_wait(void)
{
    return _cc_tstack_suspend(cc_task_current(), cc_tr_wait);
}

// hand a target to our owner-side wrapper, this task stays parked here
static cc_bool _cc_tstack_trans_owner(cc_task_t *t, cc_tstack_t *from, cc_task_t *next)
{
    from->running = CC_FALSE;
    CC_PORT_SET_CURRENT_TASK(next);
    cc_swap(&from->saved, from->saved, (void *)(cc_uintptr)cc_tr_trans);

    from->running = CC_TRUE; // the owner resumed this task again
    return CC_UNLIKELY(t->flags & cc_tf_abort) ? CC_FALSE : CC_TRUE;
}

// hand control to next, this task parked until resumed
cc_bool cc_tstack_trans(cc_task_t *next)
{
    cc_task_t   *t    = cc_task_current(); // read before the slot points at next
    cc_tstack_t *from = _cc_tstack_of(t);

    if (!cc_tstack_is_stackful(next))
    {
        return _cc_tstack_trans_owner(t, from, next);
    }

    cc_tstack_t *to = CC_TSTACK(next);
    if (CC_UNLIKELY(to->running)) // transfer to itself / to a task on the cpu
    {
        _cc_tstack_bug("cc_tstack_trans: the target is running");
    }

    if (CC_UNLIKELY(next->lc == CC_LC_DONE)) // a finished task has no live stack to jump into
    {
        _cc_tstack_bug("cc_tstack_trans: the target has finished");
    }

    void *sp = to->saved; // the target's resume point, read before overwriting
    if (CC_UNLIKELY(next->lc == CC_LC_ABORT && sp == CC_NULL))
    {
        // no target stack exists yet, so its wrapper must end it on the owner stack
        return _cc_tstack_trans_owner(t, from, next);
    }

    to->saved     = from->saved; // hand the owner context over, not the old task
    to->running   = CC_TRUE;
    from->running = CC_FALSE;
    CC_PORT_SET_CURRENT_TASK(next);

    if (CC_LIKELY(sp != CC_NULL))
    {
        _cc_tstack_check_sp(to, sp);
        cc_swap(&from->saved, sp, (void *)(cc_uintptr)cc_tr_yield);
    }
    else cc_swap_start(&from->saved, to->stack + to->size, to->size, _cc_tstack_entry, next);

    from->running = CC_TRUE; // the owner resumed this task again or trans to this task
    return CC_UNLIKELY(t->flags & cc_tf_abort) ? CC_FALSE : CC_TRUE;
}

// end the task from any depth
CC_NORETURN void cc_tstack_exit(cc_uintptr ret)
{
    cc_task_t   *t  = cc_task_current();
    cc_tstack_t *ts = _cc_tstack_of(t);

    CC_PORT_SET_TASK_RET(ret);
    ts->running = CC_FALSE;
    cc_swap(&ts->saved, ts->saved, (void *)(cc_uintptr)cc_tr_end); // swap to saved (wrapper)

    // ended task maybe freed
    CC_PORT_PANIC("cc_tstack_exit returned");
    __builtin_unreachable();
}

//--------------------------------------------------------------------------------------------------------

cc_bool cc_tstack_canary_hit(cc_task_t *t)
{
    cc_tstack_t *ts = _cc_tstack_of(t);
    return CC_LIKELY(ts->canary[0] == CC_TSTACK_CANARY && ts->canary[1] == CC_TSTACK_CANARY)
           ? CC_FALSE : CC_TRUE;
}

#endif // CC_CFG_TASK_STACKFUL

