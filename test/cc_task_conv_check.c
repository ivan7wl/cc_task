#include "cc_task.h"

// a conventional (function pointer) task body driven by the same primitives:
// no block literal, no capture, the ctx is the one given to cc_tconv_new()

static int s_fail;

#define CHECK(c)            do { if (!(c)) { s_fail++; } } while (0)

typedef struct
{
    int step;
    int rc;
} conv_ctx;

static conv_ctx s_observed;

#define CTX(t)              CC_CTX((t), conv_ctx)

//--------------------------------------------------------------------------------------------------------
// yield / end through a plain function

static cc_tr_e body(cc_task_t *t, void *_, void *ctx)
{
    conv_ctx *c = (conv_ctx *)ctx;

    CC_BEGIN(t);

    c->step++;
    CHECK(cc_task_current() == t);
    CHECK(c == CTX(t));
    CC_YIELD(t);

    c->step++;
    s_observed = *c;
    CC_END(t, c->rc);
}

//--------------------------------------------------------------------------------------------------------
// malloc'd task

static void test_conv(void)
{
    conv_ctx initial = { 3, 0x11 };
    cc_task_t *t = cc_tconv_new(body, &initial, sizeof(initial));

    CHECK(t != CC_NULL);
    CHECK(t->ctx_size == sizeof(conv_ctx));
    CHECK(CTX(t)->step == 3);       // copied from initial
    CHECK(CTX(t)->rc == 0x11);
    CHECK(cc_task_ended(t) == CC_FALSE);

    initial.step = 99;
    CHECK(CTX(t)->step == 3);       // task owns an independent copy

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(CTX(t)->step == 4);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_observed.step == 5);
    CHECK(cc_task_ret() == 0x11);
}

//--------------------------------------------------------------------------------------------------------
// caller provided storage, retained

static CC_ALIGN(16) cc_u8 s_mem[64];

static void test_conv_init(void)
{
    cc_size    need = CC_TASK_SIZE(sizeof(conv_ctx));
    cc_task_t *t    = cc_tconv_init(s_mem, sizeof(s_mem), body, CC_NULL, sizeof(conv_ctx));

    CHECK(t != CC_NULL);
    CHECK(cc_tconv_init(s_mem, need - 1, body, CC_NULL, sizeof(conv_ctx)) == CC_NULL);
    CHECK(CTX(t)->step == 0);       // NULL ctx keeps the zero-fill behavior

    CTX(t)->rc = 0x21;

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);
    CHECK(cc_task_ret() == 0x21);
    CHECK(cc_task_ended(t) == CC_TRUE); // automatic release kept the caller storage
    CHECK(s_mem[0] == 0);           // still readable
}

//--------------------------------------------------------------------------------------------------------
// a conventional task awaiting another one

static cc_task_t *g_child;

static cc_tr_e child_body(cc_task_t *t, void *_, void *ctx)
{
    conv_ctx *c = (conv_ctx *)ctx;

    CC_BEGIN(t);
    c->step = 0x55;
    CC_END(t, 0x56);
}

static cc_tr_e parent_body(cc_task_t *t, void *_, void *ctx)
{
    conv_ctx *c = (conv_ctx *)ctx;

    CC_BEGIN(t);
    CC_AWAIT(t, g_child, c->rc);
    s_observed = *c;
    CC_END(t, 0x57);
}

static void test_conv_await(void)
{
    cc_task_t *p;

    g_child = cc_tconv_new(child_body, CC_NULL, sizeof(conv_ctx));
    p       = cc_tconv_new(parent_body, CC_NULL, sizeof(conv_ctx));

    CHECK(g_child != CC_NULL);
    CHECK(p != CC_NULL);

    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);
    CHECK(s_observed.rc == 0x56);   // the child end value was copied before release
    CHECK(cc_task_ret() == 0x57);
}

//--------------------------------------------------------------------------------------------------------
// exec: replace the function, preserve the existing ctx

static cc_tr_e exec_body(cc_task_t *t, void *_, void *ctx)
{
    conv_ctx *c = (conv_ctx *)ctx;

    CC_BEGIN(t);
    c->step += 10;
    CC_END(t, c->rc + 1);
}

static void test_conv_exec(void)
{
    static CC_ALIGN(16) cc_u8 mem[CC_TASK_SIZE(sizeof(conv_ctx))];
    conv_ctx initial = { 2, 0x41 };
    conv_ctx replacement = { 20, 0x51 };
    struct { cc_u8 data[sizeof(conv_ctx) + 1]; } too_big = {{ 0 }};
    cc_task_t *t = cc_tconv_init(mem, sizeof(mem), body, &initial, sizeof(initial));

    CHECK(t != CC_NULL);
    t->flags |= cc_tf_abort;                       // exec starts a fresh body, not the old abort
    CHECK(cc_tconv_exec(t, exec_body, &replacement, sizeof(replacement)) == cc_tr_end);
    CHECK(cc_task_aborted(t) == CC_FALSE);
    CHECK(CTX(t)->step == 30);
    CHECK(cc_task_ret() == 0x52);
    CHECK(t->ctx_size == sizeof(conv_ctx));

    CHECK(cc_tconv_exec(t, body, CC_NULL, 0) == cc_tr_yield); // size 0 preserves ctx
    CHECK(CTX(t)->step == 31);
    CHECK(cc_task_ended(t) == CC_FALSE);

    CHECK(cc_tconv_exec(t, body, &too_big, sizeof(too_big)) == cc_tr_error);
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end); // rejected exec left the body and ctx intact
    CHECK(CTX(t)->step == 32);
    CHECK(cc_task_ret() == 0x51);

    CHECK(cc_tconv_exec(t, body, CC_NULL, sizeof(conv_ctx)) == cc_tr_yield);
    CHECK(CTX(t)->step == 1);                     // NULL with nonzero size zeroes ctx
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);
    CHECK(CTX(t)->step == 2);
    CHECK(cc_task_ret() == 0);
    CHECK(cc_task_ended(t) == CC_TRUE);
}

//--------------------------------------------------------------------------------------------------------

int main(void)
{
    test_conv();
    test_conv_init();
    test_conv_await();
    test_conv_exec();

    return s_fail ? 1 : 0;
}
