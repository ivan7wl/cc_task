// SPDX-License-Identifier: Apache-2.0

#include "cc_task.h"
#include "cc_list.h"

#include <stdarg.h>

typedef enum _cc_rnf_e
{
    cc_rnf_dynamic = CC_BIT(0),
} cc_rnf_e;

typedef enum _cc_run_state_e
{
    cc_run_free = 0,
    cc_run_ready,
    cc_run_running,
    cc_run_wait,
} cc_run_state_e;

typedef struct _cc_run_node_t cc_run_node_t;
typedef cc_list_head_t(_cc_run_node_t) cc_run_list_t;

struct _cc_run_node_t
{
    cc_run_list_t   link;
    cc_task_t      *task;
    cc_tconv_func   original_fn;
    cc_task_t      *runner;
    cc_u8           flags; // cc_rnf_e
    cc_u8           state; // cc_run_state_e
};

typedef struct _cc_runner_ctx_t
{
    cc_run_list_t ready;
    cc_run_list_t free;
    cc_u32        alive;
    cc_u16        capacity;
    cc_run_node_t nodes[];
} cc_runner_ctx_t;

CC_STATIC_ASSERT(CC_OFFSETOF(cc_runner_ctx_t, nodes) <= 0xFFFFu, "cc_runner: ctx_size");

//--------------------------------------------------------------------------------------------------------

cc_size cc_runner_size(int n)
{
    if (CC_UNLIKELY(n <= 0))
    {
        return 0;
    }

    cc_size count = (cc_size)n;
    cc_size fixed = CC_OFFSETOF(cc_runner_ctx_t, nodes);
    if (CC_UNLIKELY(count > (0xFFFFu - fixed) / sizeof(cc_run_node_t)))
    {
        return 0; // runner.ctx_size is a cc_u16
    }
    return CC_TASK_SIZE(fixed + count * sizeof(cc_run_node_t));
}

//--------------------------------------------------------------------------------------------------------

static cc_tr_e _cc_runner_body(cc_task_t *runner, void *_, void *ctx);

static cc_runner_ctx_t *_cc_runner_ctx(cc_task_t *runner)
{
    return CC_CTX(runner, cc_runner_ctx_t);
}

static void _cc_ready_push(cc_task_t *runner, cc_run_node_t *node)
{
    node->state = cc_run_ready;
    cc_list_add_tail(&_cc_runner_ctx(runner)->ready, node, link);
}

static cc_run_node_t *_cc_list_pop(cc_run_list_t *head)
{
    cc_run_node_t *node = cc_list_first(head);
    cc_list_del(node, link);
    return node;
}

//--------------------------------------------------------------------------------------------------------

static cc_runner_ctx_t *_cc_runner_pre_init(void *mem, cc_size size, int n)
{
    cc_size need = cc_runner_size(n);
    if (need == 0 || size < need || !CC_IS_ALIGNED(mem, CC_ALIGNOF(cc_task_t)))
    {
        return CC_NULL;
    }

    return CC_CTX((cc_task_t *)mem, cc_runner_ctx_t);
}

static cc_task_t *_cc_runner_post_init(void *mem, int n, cc_u32 flags)
{
    cc_task_t *runner = (cc_task_t *)mem;
    cc_runner_ctx_t *ctx = CC_CTX(runner, cc_runner_ctx_t);
    cc_task_t *current = cc_task_current();
    cc_task_t *self = CC_NULL;

    // The inline array already holds the candidate task pointers. Validate all of
    // them before modifying tasks or publishing the runner.
    for (int i = 0; i < n; ++i)
    {
        cc_task_t *task = ctx->nodes[i].task;
        if (task->fn == CC_NULL || task->ext != CC_NULL ||
            cc_task_ended(task) || (task->next != CC_NULL && task != current))
        {
            return CC_NULL;
        }
        if (task == current)
        {
            if (self != CC_NULL)
            {
                return CC_NULL;
            }
            self = task;
        }
        for (int j = 0; j < i; ++j)
        {
            if (ctx->nodes[j].task == task)
            {
                return CC_NULL;
            }
        }
    }

    cc_task_t *up = self != CC_NULL ? self->next : CC_NULL;
    runner->next = up;
    runner->ctx_size = (cc_u16)(CC_OFFSETOF(cc_runner_ctx_t, nodes) +
                                (cc_size)n * sizeof(cc_run_node_t));
    runner->flags = flags | cc_tf_runner;
    runner->fn = (void (*)(void))_cc_runner_body;
    runner->ext = CC_NULL;
    CC_LC_INIT(runner->lc);

    cc_list_init(&ctx->ready, link);
    cc_list_init(&ctx->free, link);
    ctx->alive = (cc_u32)n;
    ctx->capacity = (cc_u16)n;

    for (int i = 0; i < n; ++i)
    {
        cc_run_node_t *node = &ctx->nodes[i];
        node->original_fn = CC_NULL;
        node->runner = runner;
        node->flags = 0;
        _cc_ready_push(runner, node);
    }
    if (self != CC_NULL)
    {
        self->next = CC_NULL;
    }
    return runner;
}

//--------------------------------------------------------------------------------------------------------

static cc_task_t *_cc_runner_vinit(void *mem, cc_size size, int n, cc_u32 flags, va_list ap)
{
    cc_runner_ctx_t *ctx = _cc_runner_pre_init(mem, size, n);
    if (ctx == CC_NULL)
    {
        return CC_NULL;
    }
    for (int i = 0; i < n; ++i)
    {
        ctx->nodes[i].task = va_arg(ap, cc_task_t *);
    }
    return _cc_runner_post_init(mem, n, flags);
}

cc_task_t *cc_runner_init_n(void *mem, cc_size size, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    cc_task_t *runner = _cc_runner_vinit(mem, size, n, cc_tf_retain, ap);
    va_end(ap);
    return runner;
}

//--------------------------------------------------------------------------------------------------------

static cc_task_t *_cc_runner_ainit(void *mem, cc_size size, int n, cc_u32 flags,
                                   cc_task_t *const tasks[])
{
    cc_runner_ctx_t *ctx = _cc_runner_pre_init(mem, size, n);
    if (ctx == CC_NULL)
    {
        return CC_NULL;
    }
    for (int i = 0; i < n; ++i)
    {
        ctx->nodes[i].task = tasks[i];
    }
    return _cc_runner_post_init(mem, n, flags);
}

cc_task_t *cc_runner_init_a(void *mem, cc_size size, int n, cc_task_t *const tasks[])
{
    return _cc_runner_ainit(mem, size, n, cc_tf_retain, tasks);
}

//--------------------------------------------------------------------------------------------------------

#if CC_CFG_USE_MALLOC

cc_task_t *cc_runner_new_n(int n, ...)
{
    cc_size size = cc_runner_size(n);
    if (size == 0)
    {
        return CC_NULL;
    }

    void *mem = CC_PORT_MALLOC(size);
    if (mem == CC_NULL)
    {
        return CC_NULL;
    }

    va_list ap;
    va_start(ap, n);
    cc_task_t *runner = _cc_runner_vinit(mem, size, n, cc_tf_none, ap);
    va_end(ap);
    if (runner == CC_NULL)
    {
        CC_PORT_FREE(mem);
    }
    return runner;
}

cc_task_t *cc_runner_new_a(int n, cc_task_t *const tasks[])
{
    cc_size size = cc_runner_size(n);
    if (size == 0)
    {
        return CC_NULL;
    }
    void *mem = CC_PORT_MALLOC(size);
    if (mem == CC_NULL)
    {
        return CC_NULL;
    }

    cc_task_t *runner = _cc_runner_ainit(mem, size, n, cc_tf_none, tasks);
    if (runner == CC_NULL)
    {
        CC_PORT_FREE(mem);
    }
    return runner;
}
#endif // CC_CFG_USE_MALLOC

#if CC_CFG_USE_MALLOC
cc_bool cc_runner_add(cc_task_t *runner, cc_task_t *task)
{
    if (!(runner->flags & cc_tf_runner) || runner == task ||
        cc_task_ended(runner) || task->ext != CC_NULL || task->next != CC_NULL ||
        task == cc_task_current() || cc_task_ended(task))
    {
        return CC_FALSE;
    }

    cc_runner_ctx_t *ctx = _cc_runner_ctx(runner);
    if (ctx->alive == 0 || ctx->alive == (cc_u32)-1)
    {
        return CC_FALSE;
    }

    // READY tasks are deliberately unhooked, so ext alone cannot prove that a
    // task is not already queued. WAIT/RUNNING duplicates are caught above.
    cc_run_node_t *it;
    cc_list_foreach(&ctx->ready, it, link)
    {
        if (it->task == task)
        {
            return CC_FALSE;
        }
    }

    cc_run_node_t *node;
    cc_u8 flags = 0;
    if (!cc_list_is_empty(&ctx->free, link))
    {
        node = _cc_list_pop(&ctx->free);
    }
    else
    {
        node = (cc_run_node_t *)CC_PORT_MALLOC(sizeof(*node));
        if (node == CC_NULL)
        {
            return CC_FALSE;
        }
        flags = cc_rnf_dynamic;
    }

    node->task = task;
    node->original_fn = CC_NULL;
    node->runner = runner;
    node->flags = flags;
    _cc_ready_push(runner, node);
    ctx->alive++;
    return CC_TRUE;
}
#endif // CC_CFG_USE_MALLOC

static void _cc_retire_node(cc_run_node_t *node)
{
    cc_task_t *runner = node->runner;
    node->task = CC_NULL;
    node->original_fn = CC_NULL;
    if (node->flags & cc_rnf_dynamic)
    {
#if CC_CFG_USE_MALLOC
        CC_PORT_FREE(node);
#else
        CC_UNUSED(node);
#endif
    }
    else
    {
        node->state = cc_run_free;
        cc_list_add(&_cc_runner_ctx(runner)->free, node, link);
    }
}

static CC_NORETURN void _cc_runner_bug(const char *what)
{
    CC_PORT_PANIC(what);
    __builtin_unreachable();
}

static cc_tr_e _cc_resume_hook(cc_task_t *task, void *_, void *ctx);

static cc_tr_e _cc_runner_result(cc_task_t *runner, cc_run_node_t *node, cc_tr_e ret)
{
    cc_runner_ctx_t *ctx = _cc_runner_ctx(runner);
    cc_task_t *current = cc_task_current();

    if (ret == cc_tr_error)
    {
        if (CC_UNLIKELY(current == CC_NULL || current->ext != CC_NULL))
        {
            _cc_runner_bug("cc_runner: invalid error continuation");
        }
        node->task = current;
        _cc_ready_push(runner, node);
        CC_PORT_SET_CURRENT_TASK(runner);
        return ret;
    }

    if (current == CC_NULL)
    {
        if (CC_UNLIKELY(ret != cc_tr_end || ctx->alive == 0))
        {
            _cc_runner_bug("cc_runner: invalid end state");
        }
        ctx->alive--;
        _cc_retire_node(node);
        if (ctx->alive == 0)
        {
            return cc_task_end(runner);
        }
        CC_PORT_SET_CURRENT_TASK(runner);
        return cc_list_is_empty(&ctx->ready, link) ? cc_tr_wait : cc_tr_yield;
    }

    if (CC_UNLIKELY(ret == cc_tr_end || ret == cc_tr_trans || current == runner ||
                    current->ext != CC_NULL))
    {
        _cc_runner_bug("cc_runner: invalid continuation");
    }

    node->task = current;
    if (ret == cc_tr_yield)
    {
        _cc_ready_push(runner, node);
    }
    else if (ret == cc_tr_wait)
    {
        node->original_fn = (cc_tconv_func)current->fn;
        node->state = cc_run_wait;
        current->fn = (void (*)(void))_cc_resume_hook;
        current->ext = node;
    }
    else
    {
        _cc_runner_bug("cc_runner: invalid task result");
    }

    CC_PORT_SET_CURRENT_TASK(runner);
    return cc_list_is_empty(&ctx->ready, link) ? cc_tr_wait : cc_tr_yield;
}

static cc_tr_e _cc_resume_hook(cc_task_t *task, void *_, void *ctx_mem)
{
    CC_UNUSED(_, ctx_mem);
    cc_run_node_t *node = (cc_run_node_t *)task->ext;
    if (CC_UNLIKELY(node->task != task || node->state != cc_run_wait))
    {
        _cc_runner_bug("cc_runner: invalid resume hook");
    }

    cc_task_t *runner = node->runner;
    cc_tconv_func fn = node->original_fn;
    task->fn = (void (*)(void))fn;
    task->ext = CC_NULL;
    node->state = cc_run_running;

    cc_tr_e ret = cc_task_call(task, task, task->ctx);
    return _cc_runner_result(runner, node, ret);
}

static cc_tr_e _cc_runner_body(cc_task_t *runner, void *_, void *ctx_mem)
{
    CC_UNUSED(_);
    cc_runner_ctx_t *ctx = (cc_runner_ctx_t *)ctx_mem;
    if (ctx->alive == 0)
    {
        return cc_task_end(runner);
    }
    if (cc_list_is_empty(&ctx->ready, link))
    {
        CC_PORT_SET_CURRENT_TASK(runner);
        return cc_tr_wait;
    }

    cc_run_node_t *node = _cc_list_pop(&ctx->ready);
    cc_task_t *task = node->task;
    if (CC_UNLIKELY(task->ext != CC_NULL))
    {
        _cc_runner_bug("cc_runner: invalid ready node");
    }
    node->state = cc_run_running;
    cc_tr_e ret = cc_task_call(task, task, task->ctx);
    return _cc_runner_result(runner, node, ret);
}
