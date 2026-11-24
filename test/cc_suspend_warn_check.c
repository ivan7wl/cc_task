#include "cc_task.h"

// stack locals must not be used across a suspend point. with -Wall and
// -Wconditional-uninitialized this file is expected to fail:
//   yield / wait / trans / await, and a struct passed by value: -Wuninitialized
//   yield_until / yield_while:    -Wconditional-uninitialized
// a constant condition makes the use after yield_until / yield_while
// unreachable, so the condition here is a real variable
// clang does not diagnose a member or array-element read after the suspend
// point (s.a, a[i]); only a scalar, or the whole struct passed by value

static int g_ready;
static int sink;

typedef struct
{
    int a;
    int b;
} pair_t;

static void use(int v)
{
    sink = v;
}

static void use_pair(pair_t p)
{
    sink = p.a + p.b;
}

static cc_tr_e bad_yield(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    CC_BEGIN(t);
    int n_yield = 10;
    CC_YIELD(t);
    use(n_yield);
    CC_END(t);
}

static cc_tr_e bad_wait(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    CC_BEGIN(t);
    int n_wait = 10;
    CC_WAIT(t);
    use(n_wait);
    CC_END(t);
}

static cc_tr_e bad_until(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    CC_BEGIN(t);
    int n_until = 10;
    CC_YIELD_UNTIL(t, g_ready);
    use(n_until);
    CC_END(t);
}

static cc_tr_e bad_while(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    CC_BEGIN(t);
    int n_while = 10;
    CC_YIELD_WHILE(t, g_ready);
    use(n_while);
    CC_END(t);
}

static cc_tr_e bad_trans(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_);
    CC_BEGIN(t);
    int n_trans = 10;
    CC_TRANS(t, (cc_task_t *)ctx);
    use(n_trans);
    CC_END(t);
}

static cc_tr_e bad_await(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_);
    CC_BEGIN(t);
    int n_await = 10;
    CC_AWAIT(t, (cc_task_t *)ctx);
    use(n_await);
    CC_END(t);
}

static cc_tr_e bad_struct(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    CC_BEGIN(t);
    pair_t n_struct = { 1, 2 };
    CC_YIELD(t);
    use_pair(n_struct);
    CC_END(t);
}

#if CC_CFG_TASK_BLOCK
static cc_task_func bad_block(void)
{
    return CC_TASK(t)
    {
        CC_BEGIN(t);
        int n_block = 10;
        CC_YIELD(t);
        use(n_block);
        CC_END(t);
    };
}
#endif

int main(void)
{
    cc_tconv_func fns[] = {
        bad_yield, bad_wait, bad_until, bad_while, bad_trans, bad_await, bad_struct,
    };

    CC_UNUSED(fns);
#if CC_CFG_TASK_BLOCK
    CC_UNUSED(bad_block());
#endif
    return sink;
}
