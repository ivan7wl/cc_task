#include "cc_task.h"

#include <stdio.h>

// test-only no-op copy/dispose helpers for the rejected byref capture
CC_WEAK void _Block_object_assign(void *dst, const void *src, const int flags)
{
    (void)dst;
    (void)src;
    (void)flags;
}

CC_WEAK void _Block_object_dispose(const void *src, const int flags)
{
    (void)src;
    (void)flags;
}

static int s_fail;

#define CHECK(c)            do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

// captured payload, one struct so the ctx layout is obvious
typedef struct
{
    int step;
    int rc;
} probe_ctx;

// heap tasks are consumed at end; copy observable ctx state out before CC_END
static probe_ctx s_probe;

//--------------------------------------------------------------------------------------------------------
// size of a capture free task

static void test_size(void)
{
    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t);
    };

    CHECK(cc_task_size(blk) == sizeof(cc_task_t));
}

//--------------------------------------------------------------------------------------------------------
// yield / end / re-entry

static cc_task_t *new_yield(void)
{
    probe_ctx c = { 0, 0x11 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);

        $(c).step++;
        CC_YIELD(t);

        $(c).step++;
        CC_YIELD(t);

        $(c).step++;
        if (c.step != 3)
        {
            $(c).rc = 0xBAD;
        }

        s_probe = c;
        CC_END(t, c.rc);
    });
}

static void test_yield(void)
{
    cc_task_t *t = new_yield();

    CHECK(t != NULL);
    CHECK(t->ctx_size == sizeof(probe_ctx));
    CHECK(cc_task_ended(t) == CC_FALSE);
    CHECK(cc_task_current() == CC_NULL);        // nothing ran on this thread yet

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(CC_CTX(t, probe_ctx)->step == 1);
    CHECK(cc_task_current() == t);              // the task that just ran

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(CC_CTX(t, probe_ctx)->step == 2);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);                         // the heap task was consumed
    CHECK(cc_task_current() == CC_NULL);
    CHECK(s_probe.step == 3);
    CHECK(cc_task_ret() == 0x11);
}

//--------------------------------------------------------------------------------------------------------
// wait, the owner resumes the parked task

static cc_task_t *new_wait(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_WAIT(t);
        $(c).step = 1;
        s_probe = c;
        CC_END(t, 0x31);
    });
}

static void test_wait(void)
{
    cc_task_t *t = new_wait();

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_wait);
    CHECK(CC_CTX(t, probe_ctx)->step == 0);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(cc_task_ret() == 0x31);
}

//--------------------------------------------------------------------------------------------------------
// $(var), writes the captured copy in the task ctx

static void test_dollar(void)
{
    int v = 0;

    cc_task_t *t = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        $(v) = 7;
        CC_END(t, v);
    });

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(cc_task_ret() == 7);
}

//--------------------------------------------------------------------------------------------------------
// byref/block captures need runtime copy helpers and cannot be copied verbatim

static void test_copy_dispose(void)
{
    __block int shared = 0x42;
    static CC_ALIGN(16) cc_u8 mem[64];
    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, shared);
    };

    CHECK(cc_task_size(blk) == 0);

    cc_task_t *t = cc_task_new(blk);
    CHECK(t == NULL);
    if (t != NULL)
    {
        cc_task_free(t);
    }

    CHECK(cc_task_init(mem, sizeof(mem), blk) == NULL);
}

//--------------------------------------------------------------------------------------------------------
// big capture, a fixed ctx buffer would cap it at 32 bytes, here it is malloc'd to size

typedef struct
{
    cc_u8 pad[100];
} big_ctx;

static void test_big_ctx(void)
{
    big_ctx b;
    cc_task_t *t;

    for (int i = 0; i < (int)sizeof(b.pad); i++)
    {
        b.pad[i] = (cc_u8)i;
    }

    t = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        $(b).pad[99] = b.pad[0] + 0x42;
        CC_END(t, b.pad[99]);
    });

    CHECK(t->ctx_size == sizeof(big_ctx));
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(cc_task_ret() == 0x42);
}

//--------------------------------------------------------------------------------------------------------
// exec: replace the body, reuse the task and its allocated capture capacity

static void test_exec(void)
{
    static CC_ALIGN(16) cc_u8 mem[CC_TASK_SIZE(sizeof(big_ctx))];
    big_ctx big = {{ 0 }};
    probe_ctx small = { 1, 0x21 };
    probe_ctx other = { 4, 0x31 };
    struct { cc_u8 pad[120]; } oversized = {{ 0 }};
    cc_task_t *t = cc_task_init(mem, sizeof(mem), CC_TASK(t)
    {
        CC_BEGIN(t);
        $(big).pad[0] = 0x77;
        CC_END(t, 0x20);
    });

    CHECK(t != NULL);
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);
    CHECK(cc_task_ret() == 0x20);
    CHECK(t->ctx_size == sizeof(big_ctx));

    CHECK(cc_task_exec(t, CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, small.rc);
    }) == cc_tr_end);
    CHECK(cc_task_ret() == 0x21);
    CHECK(t->ctx_size == sizeof(big_ctx)); // smaller body does not shrink capacity

    cc_task_func next = CC_TASK(t)
    {
        CC_BEGIN(t);
        $(other).step++;
        CC_YIELD(t);
        CC_END(t, other.rc);
    };
    CHECK(cc_task_exec(t, next) == cc_tr_yield);  // an ended task can be exec'd again
    CHECK(cc_task_ended(t) == CC_FALSE);
    CHECK(CC_CTX(t, probe_ctx)->step == 5);

    CHECK(cc_task_exec(t, CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, oversized.pad[0]);
    }) == cc_tr_error);                         // too large, current body is untouched
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);
    CHECK(cc_task_ret() == 0x31);
    CHECK(cc_task_ended(t) == CC_TRUE);          // retained storage remains inspectable
}

//--------------------------------------------------------------------------------------------------------
// symmetric transfer

static cc_task_t *g_target;

static cc_task_t *new_target(void)
{
    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, 0x55);
    });
}

static cc_task_t *new_trans(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step++;
        CC_TRANS(t, g_target);
        $(c).step++;
        s_probe = c;
        CC_END(t, 0x41);
    });
}

static void test_trans(void)
{
    cc_task_t *a;

    g_target = new_target();
    a        = new_trans();

    CHECK(cc_task_call(a, a, a->ctx) == cc_tr_end);  // target ends, control goes to the owner
    CHECK(cc_task_ret() == 0x55);
    CHECK(cc_task_ended(a) == CC_FALSE);    // a stays parked at CC_TRANS
    CHECK(CC_CTX(a, probe_ctx)->step == 1);

    CHECK(cc_task_resume(&a) == cc_tr_end);       // a continues after CC_TRANS
    CHECK(a == CC_NULL);
    CHECK(s_probe.step == 2);
    CHECK(cc_task_ret() == 0x41);
}

//--------------------------------------------------------------------------------------------------------
// await, the parent gets the child end value

static cc_task_t *g_child;

static cc_task_t *new_child(void)
{
    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, 0x55);
    });
}

static cc_task_t *new_parent(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_AWAIT(t, g_child, $(c).step);
        s_probe = c;
        CC_END(t, 0x21);
    });
}

static void test_await(void)
{
    cc_task_t *p;

    g_child = new_child();
    p       = new_parent();

    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);                         // parent and child were both released
    CHECK(s_probe.step == 0x55);
    CHECK(cc_task_ret() == 0x21);
}

//--------------------------------------------------------------------------------------------------------
// an awaited child is released when it ends: cc_task_init() storage stays ours

static CC_ALIGN(16) cc_u8 s_child_mem[64];

static void test_await_release(void)
{
    probe_ctx c = { 0, 0 };
    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step++;
        CC_END(t, 0x66);
    };
    cc_task_t *child = cc_task_init(s_child_mem, sizeof(s_child_mem), blk);
    cc_task_t *p     = new_parent();

    CHECK(child != NULL);

    g_child = child;

    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);
    CHECK(s_probe.step == 0x66);
    CHECK(cc_task_ended(child) == CC_TRUE);         // released, but the storage is still ours
    CHECK(CC_CTX(child, probe_ctx)->step == 1);     // and its ctx is still readable
    CHECK(child->next == NULL);                     // ended child is detached from its parent

    cc_task_t *resume = child;
    CHECK(cc_task_resume(&resume) == cc_tr_end);
    CHECK(resume == CC_NULL);                       // must not resume the parent through next
    CHECK(CC_CTX(child, probe_ctx)->step == 1);
}

//--------------------------------------------------------------------------------------------------------
// abort: the body jumps to CC_CATCH() and ends with cc_tr_end

static int s_cleaned;

static cc_task_t *new_abort(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_THROW_ON(t, 0);      // a false condition falls through
        $(c).step = 1;
        CC_THROW_ON(t, c.step == 1);
        $(c).step = 2;          // skipped
        CC_CATCH(t):
        s_cleaned++;
        s_probe = c;
        CC_END(t, 0x81);
    });
}

static void test_abort(void)
{
    cc_task_t *t = new_abort();

    s_cleaned = 0;

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(s_cleaned == 1);
    CHECK(cc_task_ret() == 0x81);
}

//--------------------------------------------------------------------------------------------------------
// abort a parked task from the outside

static cc_task_t *new_park(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step = 1;
        CC_YIELD(t);
        $(c).step = 2;     // skipped by the abort
        CC_CATCH(t):
        $(c).step = 9;
        s_probe = c;
        CC_END(t, 0x82);
    });
}

static void test_abort_external(void)
{
    cc_task_t *t = new_park();

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(CC_CTX(t, probe_ctx)->step == 1);

    cc_task_abort(t);
    CHECK(cc_task_aborted(t) == CC_TRUE);
    CHECK(cc_task_current() == t);          // marking does not call or transfer the task
    CHECK(CC_CTX(t, probe_ctx)->step == 1); // cleanup has not run yet

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 9);
    CHECK(cc_task_ret() == 0x82);
}

//--------------------------------------------------------------------------------------------------------
// an aborted child aborts the parent waiting for it

static cc_task_t *g_abort_child;

static cc_task_t *new_aborted_child(void)
{
    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_THROW_ON(t, 1);      // no catch here, the child ends aborted immediately
        CC_END(t);
    });
}

static cc_task_t *new_catch_parent(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_AWAIT(t, g_abort_child);
        $(c).step = 1;     // skipped, the child aborted
        CC_CATCH(t):
        $(c).step = 2;
        s_probe = c;
        CC_END(t, 0x83);
    });
}

static void test_abort_await(void)
{
    cc_task_t *p;

    g_abort_child = new_aborted_child();
    p             = new_catch_parent();

    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);
    CHECK(s_probe.step == 2);
    CHECK(cc_task_ret() == 0x83);
}

//--------------------------------------------------------------------------------------------------------
// a pending abort survives the transfer used by await and fires when the parent is re-entered

static void test_abort_trans(void)
{
    probe_ctx c = { 0, 0 };
    cc_task_t *parent;
    cc_task_t *p;

    g_child = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_YIELD(t);
        CC_END(t, 0x86);
    });
    parent = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_AWAIT(t, g_child);
        $(c).step = 1;              // must not run when the aborted parent is re-entered
        CC_CATCH(t):
        $(c).step = 2;
        s_probe = c;
        CC_END(t, 0x87);
    });

    s_probe.step = 0;
    p = parent;
    CHECK(cc_task_resume(&p) == cc_tr_yield);
    CHECK(p == g_child);

    cc_task_abort(parent);          // abort the parent waiting for its child, not the running child
    CHECK(cc_task_current() == g_child);
    CHECK(s_probe.step == 0);

    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);
    CHECK(s_probe.step == 2);
    CHECK(cc_task_ret() == 0x87);
}

//--------------------------------------------------------------------------------------------------------
// exit: end the body right here, not only at the end

static cc_task_t *new_exit(int with_ret)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step = 1;
        s_probe = c;
        if (with_ret)
        {
            CC_EXIT(t, 0x84);
        }
        CC_EXIT(t);             // no end value, the transient slot stays as it is
        $(c).step = 2;     // skipped
        CC_END(t, 0x85);
    });
}

static void test_exit(void)
{
    cc_task_t *t = new_exit(1);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(cc_task_ret() == 0x84);

    t = new_exit(0);

    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(cc_task_ret() == 0x84); // no new end value: call / resume preserve the slot
}

//--------------------------------------------------------------------------------------------------------
// resume: follow the continuation after a transfer / await

static cc_task_t *new_yield_child(void)
{
    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_YIELD(t);
        CC_END(t, 0x91);
    });
}

static cc_task_t *new_sched_parent(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_AWAIT(t, g_child);
        $(c).step = 1;     // runs after the child ends
        s_probe = c;
        CC_END(t, 0x92);
    });
}

static void test_resume(void)
{
    cc_task_t *p;

    g_child = new_yield_child();
    p       = new_sched_parent();

    // the child yields while awaited, so it holds the continuation
    CHECK(cc_task_resume(&p) == cc_tr_yield);
    CHECK(p == g_child);
    CHECK(cc_task_current() == g_child);

    // the child ends, control comes back to the parent, the parent ends too
    CHECK(cc_task_resume(&p) == cc_tr_end);
    CHECK(p == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(cc_task_ret() == 0x92);
}

//--------------------------------------------------------------------------------------------------------
// yield until

static int g_ready;

static cc_task_t *new_until(void)
{
    probe_ctx c = { 0, 0 };

    return cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_YIELD_UNTIL(t, g_ready);
        $(c).step++;
        s_probe = c;
        CC_END(t, 0x61);
    });
}

static void test_until(void)
{
    cc_task_t *t;

    g_ready = 0;
    t       = new_until();

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(CC_CTX(t, probe_ctx)->step == 0);

    g_ready = 1;
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 1);
    CHECK(cc_task_ret() == 0x61);
}

//--------------------------------------------------------------------------------------------------------
// retained storage keeps the abort flag after cc_tr_end, which is how the caller tells abort from a normal end

static void test_abort_retained(void)
{
    static CC_ALIGN(16) cc_u8 mem[128];
    probe_ctx c = { 0, 0 };
    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step = 1;
        CC_YIELD(t);
        $(c).step = 2;          // skipped by the abort
        CC_CATCH(t):
        $(c).step = 9;
        s_probe = c;
        CC_END(t, 0x91);
    };
    cc_size    need = cc_task_size(blk);
    cc_task_t *kept = cc_task_init(mem, sizeof(mem), blk);
    cc_task_t *t    = kept;

    CHECK(need != 0 && need <= sizeof(mem));
    CHECK(kept != CC_NULL);
    if (kept == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_aborted(kept) == CC_FALSE);

    cc_task_abort(kept);
    CHECK(cc_task_resume(&t) == cc_tr_end);
    CHECK(t == CC_NULL);
    CHECK(s_probe.step == 9);
    CHECK(cc_task_ret() == 0x91);
    CHECK(cc_task_ended(kept) == CC_TRUE);
    CHECK(cc_task_aborted(kept) == CC_TRUE);
}

//--------------------------------------------------------------------------------------------------------
// task in caller provided storage

static CC_ALIGN(16) cc_u8 s_mem[64];

static void test_init(void)
{
    probe_ctx c = { 0, 0 };
    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        $(c).step++;
        CC_YIELD(t);
        CC_END(t, 0x71);
    };
    cc_size    need = cc_task_size(blk);
    cc_task_t *t    = cc_task_init(s_mem, sizeof(s_mem), blk);

    CHECK(need == CC_TASK_SIZE(sizeof(probe_ctx)));
    CHECK(t != NULL);

    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_yield);
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);
    CHECK(cc_task_ret() == 0x71);
    CHECK(cc_task_ended(t) == CC_TRUE); // automatic release kept the caller storage

    cc_task_abort(t);                     // ended tasks are not marked again
    CHECK(cc_task_aborted(t) == CC_FALSE);
    CHECK(cc_task_call(t, t, t->ctx) == cc_tr_end);

    s_mem[0] = 0xAA;            // storage is still ours
    CHECK(s_mem[0] == 0xAA);

    CHECK(cc_task_init(s_mem, need - 1, blk) == NULL);
}

static int s_runner_step;

static void test_runner_block(void)
{
    s_runner_step = 0;
    cc_task_t *a = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        s_runner_step = s_runner_step * 10 + 1;
        CC_YIELD(t);
        s_runner_step = s_runner_step * 10 + 3;
        CC_END(t, 0x31);
    });
    cc_task_t *b = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        s_runner_step = s_runner_step * 10 + 2;
        CC_END(t, 0x32);
    });
    cc_task_t *runner = cc_runner_new(a, b);
    CHECK(runner != NULL);
    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == NULL);
    CHECK(s_runner_step == 123);
    CHECK(cc_task_ret() == 0x31);
}

//--------------------------------------------------------------------------------------------------------

int main(void)
{
    test_size();
    test_yield();
    test_wait();
    test_dollar();
    test_copy_dispose();
    test_big_ctx();
    test_exec();
    test_trans();
    test_await();
    test_await_release();
    test_abort();
    test_abort_external();
    test_abort_await();
    test_abort_trans();
    test_abort_retained();
    test_exit();
    test_resume();
    test_until();
    test_init();
    test_runner_block();

    printf("cc_task: %s (%d failure(s))\n", s_fail ? "FAILED" : "all checks passed", s_fail);

    return s_fail ? 1 : 0;
}
