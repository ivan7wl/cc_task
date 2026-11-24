#include "cc_primitive.h"
#include "cc_task.h"

// a conventional (no block literal) body driven by the shared primitives: the
// caller calls it exactly like a task body, the primitives do not care

static int s_steps;

static cc_tr_e body(cc_task_t *t, void *_, void *ctx)
{
    cc_uintptr rc = 0x33;

    CC_BEGIN(t);

    s_steps++;
    CC_YIELD(t);

    s_steps++;
    if (ctx == CC_NULL)
    {
        rc = 0xBAD;
    }

    CC_END(t, rc);
}

int main(void)
{
    static cc_task_t t;
    int              ctx = 1;

    t.lc    = 0;
    t.next  = CC_NULL;
    t.flags = cc_tf_retain;

    if (body(&t, CC_NULL, &ctx) != cc_tr_yield || s_steps != 1)
    {
        return 1;
    }

    if (body(&t, CC_NULL, &ctx) != cc_tr_end || s_steps != 2 || cc_task_ret() != 0x33)
    {
        return 1;
    }

    if (cc_task_ended(&t) != CC_TRUE)
    {
        return 1;
    }

    if (body(&t, CC_NULL, &ctx) != cc_tr_end || s_steps != 2) // done: the tail must not run again
    {
        return 1;
    }

    return 0;
}
