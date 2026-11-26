#include "cc_task.h"

#include <stdio.h>

#if CC_CFG_TASK_STACKFUL

// stackful task checks: create / init / exec, suspend at depth, wait + abort, transfer to
// a stackful and to a stackless task, exit from depth, the stack bottom mark, one block

static int s_fail;
static int s_basic_step;
static int s_abort_step;
static int s_peer_step;
static int s_src_step;
static int s_exec_step;

#define CHECK(c)    do { if (!(c)) { s_fail++; printf("  FAIL: %s\n", #c); } } while (0)

typedef struct
{
    int step;
    int rc;
} ctx_t;

#define CTX(t)      CC_CTX((t), ctx_t)

// stackful tests need plain peers even when the public stackless conv API is off
static cc_task_t *plain_new(cc_tconv_func func, CC_NULLABLE const void *ctx, cc_size ctx_size)
{
#if CC_CFG_TASK_CONV
    return cc_tconv_new(func, ctx, ctx_size);
#else
    if (ctx_size > 0xFFFFu)
    {
        return CC_NULL;
    }

    cc_task_t *t = (cc_task_t *)CC_PORT_MALLOC(CC_TASK_SIZE(ctx_size));
    if (t == CC_NULL)
    {
        return CC_NULL;
    }

    t->next     = CC_NULL;
    t->ctx_size = (cc_u16)ctx_size;
    t->flags    = cc_tf_none;
    t->fn       = (void (*)(void))func;
    t->ext      = CC_NULL;
    CC_LC_INIT(t->lc);
    if (ctx_size != 0)
    {
        if (ctx != CC_NULL)
        {
            CC_PORT_MEMCPY(t->ctx, ctx, ctx_size);
        }
        else CC_PORT_MEMSET(t->ctx, 0, ctx_size);
    }
    return t;
#endif
}

// the control block sits right after the ctx area, the pad is part of ctx_size
static cc_size cb_off(const cc_task_t *t)
{
    return CC_OFFSETOF(cc_task_t, ctx) + t->ctx_size;
}

static cc_bool cb_aligned(const cc_task_t *t)
{
    return (((cc_uintptr)t + cb_off(t)) & 15) == 0 ? CC_TRUE : CC_FALSE;
}

//--------------------------------------------------------------------------------------------------------
// a body that suspends inside a nested call

static cc_bool yield_in_helper(int *mark)
{
    (*mark)++;
    if (!cc_tstack_yield())
    {
        return CC_FALSE;
    }
    return CC_TRUE;
}

static cc_tr_e body_basic(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c    = (ctx_t *)ctx;
    int    mark = 0;

    CC_UNUSED(_, t);

    c->step++;
    if (!yield_in_helper(&mark))                // suspends two frames down
    {
        return cc_tr_end;
    }
    if (mark != 1)                              // the helper's frame survived
    {
        return cc_tr_end;
    }

    c->step++;
    cc_tstack_eh(cc_tstack_wait(), eh);         // plain wait, no owner side state
    c->step++;
    cc_tstack_eh(cc_tstack_yield_until(mark == 1), eh);
    if (mark != 1)
    {
        return cc_tr_end;
    }

    s_basic_step = c->step;
    cc_tstack_exit((cc_uintptr)c->rc);

eh:
    s_basic_step = -1;
    return cc_tr_end;
}

static void test_basic(void)
{
    ctx_t      initial = { 0x10, 0x1234 };
    cc_task_t *t       = cc_tstack_conv_new(body_basic, &initial, sizeof(initial), 4096);

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(((cc_uintptr)t & 7) == 0);                            // malloc is 8 byte aligned
    CHECK(cb_aligned(t) == CC_TRUE);                            // the control block is 16
    CHECK(t->ctx_size >= sizeof(ctx_t));                        // ctx capacity, pad included
    CHECK(t->ctx_size <= sizeof(ctx_t) + 15);
    CHECK(cc_tstack_is_stackful(t) == CC_TRUE);
    CHECK(cc_tstack_canary_hit(t) == CC_FALSE);
    CHECK(CTX(t)->step == 0x10);                                // ctx copied
    CHECK(cc_task_ended(t) == CC_FALSE);

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(CTX(t)->step == 0x11);
    CHECK(cc_task_resume(&t) == cc_tr_wait);
    CHECK(CTX(t)->step == 0x12);
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_basic_step == 0x13);
    CHECK(cc_task_ret() == 0x1234);
}

//--------------------------------------------------------------------------------------------------------
// wait + abort: the resume returns CC_FALSE and the task cleans up by itself

static cc_tr_e body_wait_abort(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_, t);

    c->step = 1;
    cc_tstack_eh(cc_tstack_wait(), eh);
    c->step = 3;
    return cc_tr_end;

eh:
    c->step = 2;                                // cooperative cleanup runs with the abort
    s_abort_step = c->step;
    return cc_tr_end;
}

static void test_abort(void)
{
    cc_task_t *t = cc_tstack_conv_new(body_wait_abort, CC_NULL, sizeof(ctx_t), 2048);

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_wait);
    CHECK(CTX(t)->step == 1);

    cc_task_abort(t);
    CHECK(cc_task_aborted(t) == CC_TRUE);
    CHECK(s_abort_step == 0);               // no cleanup before the owner resumes it

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_abort_step == 2);
}

static void test_abort_before_start(void)
{
    cc_task_t *t = cc_tstack_conv_new(body_wait_abort, CC_NULL, sizeof(ctx_t), 2048);
    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    s_abort_step = 0;
    cc_task_abort(t);
    CHECK(cc_task_resume(&t) == cc_tr_end); // owner-side end must not trigger the CC_END guard
    CHECK(t == CC_NULL);
    CHECK(s_abort_step == 0);                 // the body never entered
}

//--------------------------------------------------------------------------------------------------------
// a stackful body aborts itself with cc_task_abort(), then returns cc_tr_end

static int s_self_abort_step;

static cc_tr_e body_self_abort(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_);

    c->step = 1;
    c->step = 2;                                // cleanup stays on the original task stack
    s_self_abort_step = c->step;
    cc_task_abort(t);
    return cc_tr_end;
}

static void test_abort_inside(void)
{
    static CC_ALIGN(16) cc_u8 mem[2048];
    cc_task_t *kept = cc_tstack_conv_init(mem, sizeof(mem), body_self_abort, CC_NULL, sizeof(ctx_t));
    cc_task_t *t    = kept;

    CHECK(kept != CC_NULL);
    if (kept == CC_NULL)
    {
        return;
    }

    s_self_abort_step = 0;
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_self_abort_step == 2);
    CHECK(cc_task_ended(kept) == CC_TRUE);
    CHECK(cc_task_aborted(kept) == CC_TRUE);
}

//--------------------------------------------------------------------------------------------------------
// a stackful child that returns cc_tr_end does not abort its parent;
// cc_task_abort() before that return does

static int        s_abort_parent_step;
static cc_task_t *s_abort_child;

static cc_tr_e body_end_only(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(t, _, ctx);
    return cc_tr_end;
}

static cc_tr_e body_mark_abort(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, ctx);
    cc_task_abort(t);
    return cc_tr_end;
}

static cc_tr_e body_abort_parent(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_BEGIN(t);
    CC_AWAIT(t, s_abort_child);
    c->step = 1;
    s_abort_parent_step = 1;
    CC_CATCH(t):
    c->step = 2;
    s_abort_parent_step = 2;
    CC_END(t, 0x92);
}

static void test_self_abort_parent(void)
{
    ctx_t      initial = { 0, 0 };
    cc_task_t *parent;
    cc_task_t *cur;

    s_abort_child = cc_tstack_conv_new(body_end_only, CC_NULL, 0, 2048);
    parent        = plain_new(body_abort_parent, &initial, sizeof(initial));
    CHECK(s_abort_child != CC_NULL && parent != CC_NULL);
    if (s_abort_child == CC_NULL || parent == CC_NULL)
    {
        return;
    }

    s_abort_parent_step = 0;
    cur                 = parent;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL);
    CHECK(s_abort_parent_step == 1);            // plain cc_tr_end is not an abort

    s_abort_child = cc_tstack_conv_new(body_mark_abort, CC_NULL, 0, 2048);
    parent        = plain_new(body_abort_parent, &initial, sizeof(initial));
    CHECK(s_abort_child != CC_NULL && parent != CC_NULL);
    if (s_abort_child == CC_NULL || parent == CC_NULL)
    {
        return;
    }

    s_abort_parent_step = 0;
    cur                 = parent;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL);
    CHECK(s_abort_parent_step == 2);            // the flag, not the return code, aborts the parent
    CHECK(cc_task_ret() == 0x92);
}

//--------------------------------------------------------------------------------------------------------
// exit from an arbitrary depth

static void exit_from(int depth)
{
    if (depth != 0)
    {
        exit_from(depth - 1);
    }
    cc_tstack_exit(0x77);
}

static cc_tr_e body_exit(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(_, t, ctx);

    exit_from(3);
    return cc_tr_end;                         // never reached
}

static void test_exit(void)
{
    cc_task_t *t = cc_tstack_conv_new(body_exit, CC_NULL, sizeof(ctx_t), 2048);

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(cc_task_ret() == 0x77);
}

//--------------------------------------------------------------------------------------------------------
// transfer: a stackless peer through our owner-side wrapper and a direct stackful target

static cc_task_t *s_peer;
static cc_task_t *s_target;

static cc_tr_e peer_body(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_BEGIN(t);
    c->step = 1;
    s_peer_step = c->step;
    CC_YIELD(t);
    c->step = 2;
    s_peer_step = c->step;
    CC_END(t, 0x33);
}

static cc_tr_e target_body(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_, t);
    c->step++;
    cc_tstack_eh(cc_tstack_yield(), eh);
    c->step++;
    return cc_tr_end;

eh:
    return cc_tr_end;
}

static cc_tr_e body_trans(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_, t);

    c->step = 1;
    cc_tstack_eh(cc_tstack_trans(s_peer), eh);  // wrapper runs the stackless peer
    c->step = 2;                                // the owner resumed us
    cc_tstack_eh(cc_tstack_trans(s_peer), eh);  // the peer ends, we keep running
    c->step = 3;
    cc_tstack_eh(cc_tstack_trans(s_target), eh); // the stackful target yields to the owner
    c->step = 4;                                // the owner resumed us again
    s_src_step = c->step;
    return cc_tr_end;

eh:
    s_src_step = -1;
    return cc_tr_end;
}

static void test_trans(void)
{
    cc_task_t *src = cc_tstack_conv_new(body_trans, CC_NULL, sizeof(ctx_t), 4096);
    cc_task_t *cur;

    s_peer   = plain_new(peer_body, CC_NULL, sizeof(ctx_t));
    s_target = cc_tstack_conv_new(target_body, CC_NULL, sizeof(ctx_t), 4096);

    CHECK(src != CC_NULL && s_peer != CC_NULL && s_target != CC_NULL);
    if (src == CC_NULL || s_peer == CC_NULL || s_target == CC_NULL)
    {
        return;
    }

    cur = src;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);     // the stackless peer stopped
    CHECK(cur == s_peer);                           // owner tracks the peer, not the source
    CHECK(CTX(s_peer)->step == 1);

    cur = src;                                      // resume the source independently
    CHECK(cc_task_resume(&cur) == cc_tr_end);       // it transfers to the peer, which ends
    CHECK(cur == CC_NULL);
    CHECK(s_peer_step == 2);
    CHECK(cc_task_ret() == 0x33);

    cur = src;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);     // now the stackful target stops
    CHECK(cur == s_target);

    CHECK(cc_task_resume(&cur) == cc_tr_end);       // the target ended and was released
    CHECK(cur == CC_NULL);

    cur = src;                                      // the source is parked at its transfer
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL);
    CHECK(s_src_step == 4);
}

static void test_trans_abort(void)
{
    cc_task_t *src;
    cc_task_t *cur;

    s_peer = plain_new(peer_body, CC_NULL, sizeof(ctx_t));
    src    = cc_tstack_conv_new(body_trans, CC_NULL, sizeof(ctx_t), 4096);
    CHECK(src != CC_NULL && s_peer != CC_NULL);
    if (src == CC_NULL || s_peer == CC_NULL)
    {
        return;
    }

    s_src_step = 0;
    cur        = src;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);     // source is parked at trans to the peer
    CHECK(cur == s_peer);

    cc_task_abort(src);
    CHECK(s_src_step == 0);                         // marking does not resume the source

    cur = src;
    CHECK(cc_task_resume(&cur) == cc_tr_end);     // the owner drives the actual abort
    CHECK(cur == CC_NULL);
    CHECK(s_src_step == -1);                        // cc_tstack_eh() entered the cleanup label

    cc_task_free(s_peer);                           // the peer is still alive and parked
}

//--------------------------------------------------------------------------------------------------------
// transfer chains: the wrapper may belong to an earlier task in the chain

// next is the transfer target, step points outside the task so it survives auto-free
typedef struct
{
    cc_task_t *next;
    int       *step;
} chain_ctx_t;

static cc_tr_e body_chain(cc_task_t *t, void *_, void *ctx)
{
    chain_ctx_t *c = (chain_ctx_t *)ctx;

    CC_UNUSED(_, t);

    (*c->step)++;
    cc_tstack_eh(cc_tstack_trans(c->next), eh);
    (*c->step)++;
    return cc_tr_end;

eh:
    *c->step = -1;
    return cc_tr_end;
}

static cc_task_t *chain_new(cc_task_t *next, int *step)
{
    chain_ctx_t c;

    c.next = next;
    c.step = step;
    return cc_tstack_conv_new(body_chain, &c, sizeof(c), 4096);
}

static cc_tr_e body_return(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_, t);
    c->step++;
    return cc_tr_end;
}

static void test_resume_ended(void)
{
    static CC_ALIGN(16) cc_u8 mem[2048];
    ctx_t      initial = { 4, 0 };
    cc_task_t *kept    = cc_tstack_conv_init(mem, sizeof(mem), body_return, &initial, sizeof(initial));
    cc_task_t *cur     = kept;

    CHECK(kept != CC_NULL);
    if (kept == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && cc_task_current() == CC_NULL);
    CHECK(cc_task_ended(kept) == CC_TRUE && CTX(kept)->step == 5);

    // retained storage remains inspectable, but must not stay registered as current
    cur = kept;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && cc_task_current() == CC_NULL);
    CHECK(CTX(kept)->step == 5);                    // the finished body did not run again
}

static void test_trans_abort_before_start(void)
{
    static CC_ALIGN(16) cc_u8 mem[2048];
    ctx_t      initial     = { 7, 0 };
    int        source_step = 0;
    cc_task_t *target      = cc_tstack_conv_init(mem, sizeof(mem), body_return, &initial, sizeof(initial));
    cc_task_t *source      = chain_new(target, &source_step);
    cc_task_t *cur;

    CHECK(target != CC_NULL && source != CC_NULL);
    if (target == CC_NULL || source == CC_NULL)
    {
        return;
    }

    cc_task_abort(target);
    cur = source;
    CHECK(cc_task_resume(&cur) == cc_tr_end);       // the target wrapper ends it on the owner stack
    CHECK(cur == CC_NULL && cc_task_current() == CC_NULL);
    CHECK(cc_task_ended(target) == CC_TRUE && cc_task_aborted(target) == CC_TRUE);
    CHECK(target->next == CC_NULL && CTX(target)->step == 7); // the target body never ran
    CHECK(source_step == 1);                        // the source stays parked at its transfer

    cur = source;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && source_step == 2);      // the source can still continue and finish
}

static int s_end_macro_step;

static cc_tr_e body_end_macro(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_BEGIN(t);
    c->step++;
    s_end_macro_step = c->step;
    CC_END(t, c->rc);
}

static void test_end_macro(void)
{
    ctx_t initial = { 7, 0x68 };
    cc_task_t *t = cc_tstack_conv_new(body_end_macro, &initial, sizeof(initial), 2048);

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);                    // released by the wrapper, not on the task stack
    CHECK(s_end_macro_step == 8);
    CHECK(cc_task_ret() == 0x68);           // CC_END's value survives the swap
}

static void test_size_limits(void)
{
    // Invalid sizes must be rejected before allocation or touching the task memory.
    CHECK(cc_tstack_conv_new(body_return, CC_NULL, 0, 0) == CC_NULL);
    CHECK(cc_tstack_conv_new(body_return, CC_NULL, 0, (cc_size)-1) == CC_NULL);
    CHECK(cc_tstack_conv_new(body_return, CC_NULL, 0, (cc_size)-15) == CC_NULL); // total-size overflow
    CHECK(cc_tstack_conv_new(body_return, CC_NULL, (cc_size)-1, 16) == CC_NULL);
}

static void test_trans_chain(void)
{
    cc_task_t *a;
    cc_task_t *b;
    cc_task_t *cur;
    int        a_step;
    int        b_step;

    // A -> B -> stackless peer: B returns to A's wrapper with cc_tr_trans
    a_step = b_step = 0;
    s_peer = plain_new(peer_body, CC_NULL, sizeof(ctx_t));
    b      = chain_new(s_peer, &b_step);
    a      = chain_new(b, &a_step);
    CHECK(a != CC_NULL && b != CC_NULL && s_peer != CC_NULL);
    if (a == CC_NULL || b == CC_NULL || s_peer == CC_NULL)
    {
        return;
    }

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cur == s_peer);
    CHECK(a_step == 1 && b_step == 1 && s_peer_step == 1);
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && cc_task_ret() == 0x33);

    cur = b;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && b_step == 2);
    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 2);

    // A -> B -> C, all stackful: C yields and ends through A's wrapper
    a_step  = b_step = 0;
    s_target = cc_tstack_conv_new(target_body, CC_NULL, sizeof(ctx_t), 4096);
    b        = chain_new(s_target, &b_step);
    a        = chain_new(b, &a_step);
    CHECK(a != CC_NULL && b != CC_NULL && s_target != CC_NULL);
    if (a == CC_NULL || b == CC_NULL || s_target == CC_NULL)
    {
        return;
    }

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cur == s_target && a_step == 1 && b_step == 1);
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL);

    cur = b;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && b_step == 2);
    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 2);

    // the transfer target exits from depth, then the source is resumed separately
    a_step = 0;
    b      = cc_tstack_conv_new(body_exit, CC_NULL, 0, 4096);
    a      = chain_new(b, &a_step);
    CHECK(a != CC_NULL && b != CC_NULL);
    if (a == CC_NULL || b == CC_NULL)
    {
        return;
    }

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 1 && cc_task_ret() == 0x77);
    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 2);
}

static void test_trans_cycle(void)
{
    cc_task_t *a;
    cc_task_t *b;
    cc_task_t *cur;
    int        a_step = 0;
    int        b_step = 0;

    // A -> B -> A: A ends first, B remains parked at its own transfer
    b = chain_new(CC_NULL, &b_step);
    a = chain_new(b, &a_step);
    CHECK(a != CC_NULL && b != CC_NULL);
    if (a == CC_NULL || b == CC_NULL)
    {
        return;
    }
    CC_CTX(b, chain_ctx_t)->next = a;

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 2 && b_step == 1);

    cur = b;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && b_step == 2);
}

static void test_trans_retain(void)
{
    static CC_ALIGN(16) cc_u8 ma[4096];
    static CC_ALIGN(16) cc_u8 mb[4096];
    chain_ctx_t ac;
    ctx_t       bc = { 0, 0 };
    cc_task_t  *a;
    cc_task_t  *b;
    cc_task_t  *cur;
    int         a_step = 0;

    // B ends through A's wrapper; retained B must not stay marked as running
    b       = cc_tstack_conv_init(mb, sizeof(mb), body_return, &bc, sizeof(bc));
    ac.next = b;
    ac.step = &a_step;
    a       = cc_tstack_conv_init(ma, sizeof(ma), body_chain, &ac, sizeof(ac));
    CHECK(a != CC_NULL && b != CC_NULL);
    if (a == CC_NULL || b == CC_NULL)
    {
        return;
    }

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 1);
    CHECK(cc_task_ended(b) == CC_TRUE && CTX(b)->step == 1);

    bc = *CTX(b);
    CHECK(cc_tstack_conv_exec(b, body_return, &bc, sizeof(bc)) == cc_tr_end);
    CHECK(cc_task_ended(b) == CC_TRUE && CTX(b)->step == 2);

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end);
    CHECK(cur == CC_NULL && a_step == 2);
}

//--------------------------------------------------------------------------------------------------------
// exec: replace the body of a stopped task, keep the ctx capacity and the stack

static cc_tr_e body_exec(cc_task_t *t, void *_, void *ctx)
{
    ctx_t *c = (ctx_t *)ctx;

    CC_UNUSED(_, t);

    c->step += 10;
    if (!cc_tstack_yield())
    {
        return cc_tr_end;
    }
    c->step += 10;
    s_exec_step = c->step;
    cc_tstack_exit((cc_uintptr)c->step);
}

static void test_exec(void)
{
    ctx_t       initial = { 1, 0 };
    ctx_t       repl    = { 5, 0 };
    struct { cc_u8 big[sizeof(ctx_t) + 16]; } too_big = {{ 0 }}; // more than ctx_size, pad included
    cc_task_t  *t;
    cc_task_t  *plain;

    t = cc_tstack_conv_new(body_basic, &initial, sizeof(initial), 2048);
    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(CTX(t)->step == 2);

    CHECK(cc_tstack_conv_exec(t, body_exec, &too_big, sizeof(too_big)) == cc_tr_error);
    CHECK(CTX(t)->step == 2);                       // rejected exec leaves the task untouched

    CHECK(cc_tstack_conv_exec(t, body_exec, &repl, sizeof(repl)) == cc_tr_yield);
    CHECK(CTX(t)->step == 15);                      // 5 + 10, ctx replaced
    CHECK(cc_tstack_canary_hit(t) == CC_FALSE);     // the mark was rewritten

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_exec_step == 25);
    CHECK(cc_task_ret() == 25);

    plain = plain_new(body_exec, CC_NULL, sizeof(ctx_t));       // a plain task is rejected
    CHECK(plain != CC_NULL);
    CHECK(cc_tstack_is_stackful(plain) == CC_FALSE);            // and it says so
    CHECK(cc_tstack_conv_exec(plain, body_exec, CC_NULL, 0) == cc_tr_error);
    cc_task_free(plain);                         // still live: manual discard is allowed
}

//--------------------------------------------------------------------------------------------------------
// one caller provided block: the caller passes a total size, the stack is what is left

#define TSTACK_BLOCK    4096

static CC_ALIGN(16) cc_u8 s_mem[TSTACK_BLOCK];

static void test_init(void)
{
    ctx_t      initial = { 2, 0x22 };
    cc_task_t *t;

    CHECK(cc_tstack_conv_init(s_mem + 4, TSTACK_BLOCK - 4, body_basic, CC_NULL,
                              sizeof(ctx_t)) == CC_NULL);       // not 8 byte aligned
    CHECK(cc_tstack_conv_init(s_mem, 64, body_basic, CC_NULL,
                              sizeof(ctx_t)) == CC_NULL);       // no room for a stack

    t = cc_tstack_conv_init(s_mem, TSTACK_BLOCK, body_basic, &initial, sizeof(initial));
    CHECK(t == (cc_task_t *)(void *)s_mem);
    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK((t->flags & cc_tf_retain) != 0);           // the block stays with the caller
    CHECK(CTX(t)->step == 2);

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_resume(&t) == cc_tr_wait);
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(cc_task_ret() == 0x22);
    CHECK(cc_task_ended((cc_task_t *)(void *)s_mem) == CC_TRUE); // retained storage remains ours

    // 8 byte aligned, not 16: the control block is aligned inside the ctx area
    t = cc_tstack_conv_init(s_mem + 8, TSTACK_BLOCK - 8, body_basic, &initial, sizeof(initial));
    CHECK(t == (cc_task_t *)(void *)(s_mem + 8));
    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(((cc_uintptr)t & 15) != 0);               // the block itself is not 16 aligned
    CHECK(cb_aligned(t) == CC_TRUE);                // but the control block is
    CHECK(cc_tstack_canary_hit(t) == CC_FALSE);

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_resume(&t) == cc_tr_wait);
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(cc_task_ret() == 0x22);
}

#if CC_CFG_TASK_BLOCK

//--------------------------------------------------------------------------------------------------------
// block bodies: the captured copy is the task ctx, it survives suspends and an exec

static int s_blk;

// cc_tstack_t is 32 bytes on LP64 and 16 on ILP32 (asserted in cc_task_stackful.c), the stack
// and its bottom canary start right after it
#define CB_SIZE     (4 * CC_PTR_SIZE)

static void test_block(void)
{
    int          seed = 7;
    cc_task_func blk  = CC_TASK(t) {
        CC_UNUSED(t, _);
        s_blk = seed;
        if (!cc_tstack_yield())
        {
            return cc_tr_end;
        }
        s_blk += seed;
        return cc_tr_end;
    };
    cc_size     head = cc_tstack_size(16, blk);     // minimum task, including the 16 byte canary
    cc_task_t  *t;

    CHECK(cc_tstack_size(0, blk) == 0);
    CHECK(cc_tstack_size((cc_size)-1, blk) == 0);
    CHECK(cc_tstack_size((cc_size)-15, blk) == 0);  // total-size overflow
    CHECK(cc_tstack_new(0, blk) == CC_NULL);
    CHECK(head != 0);
    CHECK(head > cc_task_size(blk));                // a cc_task_t plus the control block
    CHECK(head - cc_task_size(blk) < 64);           // 15 pad plus 32 block plus rounding
    CHECK(cc_tstack_size(4096, blk) == head + 4096 - 16);
    CHECK(cc_tstack_size(17, blk) == head + 16);    // stack rounded up to 16

    t = cc_tstack_new(4096, blk);

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_tstack_canary_hit(t) == CC_FALSE);
    CHECK(cb_aligned(t) == CC_TRUE);                // the control block is 16 byte aligned
    CHECK(t->ctx_size >= cc_task_size(blk) - CC_OFFSETOF(cc_task_t, ctx));  // capture + pad
    CHECK(t->ctx_size <= cc_task_size(blk) - CC_OFFSETOF(cc_task_t, ctx) + 15);
    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(s_blk == 7);

    ((cc_u8 *)t)[cb_off(t) + CB_SIZE] = 0x5A;       // a deep stack eats the mark
    CHECK(cc_tstack_canary_hit(t) == CC_TRUE);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_blk == 14);                             // the captured copy survived

    s_blk = 0;
    t = cc_tstack_new(1024, CC_TASK(t) {
        CC_UNUSED(t, _);
        s_blk += 1;
        if (!cc_tstack_yield())
        {
            return cc_tr_end;
        }
        return cc_tr_end;
    });

    CHECK(t != CC_NULL);
    if (t == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_tstack_exec(t, CC_TASK(t) {
        CC_UNUSED(t, _);
        s_blk += 10;
        return cc_tr_end;
    }) == cc_tr_end);
    CHECK(s_blk == 11);
}

#endif // CC_CFG_TASK_BLOCK

//--------------------------------------------------------------------------------------------------------

int main(void)
{
    test_basic();
    test_abort();
    test_abort_before_start();
    test_abort_inside();
    test_self_abort_parent();
    test_exit();
    test_trans();
    test_trans_abort();
    test_trans_abort_before_start();
    test_trans_chain();
    test_trans_cycle();
    test_trans_retain();
    test_resume_ended();
    test_end_macro();
    test_size_limits();
    test_exec();
    test_init();
#if CC_CFG_TASK_BLOCK
    test_block();
#endif

    if (s_fail != 0)
    {
        printf("cc_tstack: FAILED (%d checks)\n", s_fail);
        return 1;
    }

    printf("cc_tstack: suspend at depth / wait / abort / trans / exit / exec / mark / block ok\n");
    return 0;
}

#else

int main(void)
{
    cc_task_t task;

    CC_PORT_MEMSET(&task, 0, sizeof(task));

    // the query exists in this build too, it just never reports a stackful task
    if (cc_tstack_is_stackful(&task) != CC_FALSE)
    {
        printf("cc_tstack: FAILED, a build without stackful tasks reported one\n");
        return 1;
    }

    printf("cc_tstack: CC_CFG_TASK_STACKFUL=0, only cc_tstack_is_stackful is declared (always false)\n");
    return 0;
}

#endif // CC_CFG_TASK_STACKFUL
