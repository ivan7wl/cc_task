#include "cc_task.h"

#include <stdio.h>

#if CC_CFG_USE_MALLOC && CC_CFG_TASK_CONV

static int s_fail;
static int s_len;
static char s_trace[128];
static cc_task_t *g_child;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void reset_trace(void)
{
    s_len = 0;
    s_trace[0] = '\0';
}

static void mark(char c)
{
    if (s_len < (int)sizeof(s_trace) - 1)
    {
        s_trace[s_len++] = c;
        s_trace[s_len] = '\0';
    }
}

static void expect_trace(const char *want)
{
    const char *got = s_trace;
    int i = 0;
    while (want[i] != '\0' && got[i] == want[i])
    {
        i++;
    }
    if (want[i] != got[i])
    {
        printf("FAIL trace: got '%s', want '%s'\n", got, want);
        s_fail++;
    }
}

static cc_tr_e yield_a(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_YIELD(t);
    mark('A');
    CC_END(t, 0xA1);
}

static cc_tr_e yield_b(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('b');
    CC_YIELD(t);
    mark('B');
    CC_END(t, 0xB1);
}

static void test_ready_fifo(void)
{
    reset_trace();
    cc_task_t *a = cc_tconv_new(yield_a, CC_NULL, 0);
    cc_task_t *b = cc_tconv_new(yield_b, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a, b);
    CHECK(a != CC_NULL && b != CC_NULL && runner != CC_NULL);
    CHECK(a->ext == CC_NULL && b->ext == CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    expect_trace("ab");
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("abAB");
}

static cc_tr_e wait_a(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_WAIT(t);
    mark('b');
    CC_YIELD(t);
    mark('c');
    CC_END(t, 0xA2);
}

static cc_tr_e wait_b(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('x');
    CC_WAIT(t);
    mark('y');
    CC_END(t, 0xB2);
}

static void test_wait_wake(void)
{
    reset_trace();
    cc_task_t *a = cc_tconv_new(wait_a, CC_NULL, 0);
    cc_task_t *b = cc_tconv_new(wait_b, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a, b);
    CHECK(runner != CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(a->ext != CC_NULL);
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    CHECK(b->ext != CC_NULL);
    expect_trace("ax");
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    expect_trace("ax");

    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(a->ext == CC_NULL);
    expect_trace("axb");
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    expect_trace("axbc");

    cur = b;
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("axbcy");
}

static cc_tr_e await_child(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('x');
    CC_YIELD(t);
    mark('y');
    CC_WAIT(t);
    mark('z');
    CC_END(t, 0xC3);
}

static cc_tr_e await_parent(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_AWAIT(t, g_child);
    mark('b');
    CC_YIELD(t);
    mark('c');
    CC_END(t, 0xA3);
}

static void test_await_migration(void)
{
    reset_trace();
    g_child = cc_tconv_new(await_child, CC_NULL, 0);
    cc_task_t *a = cc_tconv_new(await_parent, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a);
    CHECK(g_child != CC_NULL && a != CC_NULL && runner != CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    expect_trace("ax");
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    CHECK(g_child->ext != CC_NULL);
    expect_trace("axy");

    cur = g_child;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    expect_trace("axyzb");
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("axyzbc");
}

static cc_task_t *g_inner;
static cc_task_t *g_target;
static cc_task_t *g_runner;
static cc_task_t *g_added;

static cc_tr_e nested_a(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    g_inner = cc_runner_new(
        cc_tconv_new(yield_a, CC_NULL, 0),
        cc_tconv_new(yield_b, CC_NULL, 0));
    CHECK(g_inner != CC_NULL);
    CC_AWAIT(t, g_inner);
    mark('N');
    CC_END(t, 0xA4);
}

static void test_nested_runner(void)
{
    reset_trace();
    // Inner children use traces a/A/b/B; the outer A contributes a/N.
    cc_task_t *a = cc_tconv_new(nested_a, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a);
    CHECK(runner != CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    expect_trace("aabA");
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("aabABN");
}

static cc_tr_e trans_source(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_TRANS(t, g_target);
    mark('T');
    CC_END(t, 0xA5);
}

static cc_tr_e trans_target(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('x');
    CC_WAIT(t);
    mark('y');
    CC_YIELD(t);
    mark('z');
    CC_END(t, 0xD5);
}

static void test_trans_detaches_source(void)
{
    reset_trace();
    g_target = cc_tconv_new(trans_target, CC_NULL, 0);
    cc_task_t *a = cc_tconv_new(trans_source, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a);
    CHECK(runner != CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    expect_trace("ax");
    cur = g_target;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("axyz");

    // The detached source retains its original fn/ext and remains independently
    // resumable, exactly as with ordinary cc_task_resume current-chain transfer.
    CHECK(a->ext == CC_NULL);
    cur = a;
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("axyzT");
}

static cc_tr_e end_b(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('x');
    CC_END(t, 0xB6);
}

static cc_tr_e end_added(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('m');
    CC_END(t, 0xD6);
}

static cc_tr_e add_parent(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_YIELD(t);
    CHECK(cc_runner_add(g_runner, g_added) == CC_TRUE);
    mark('b');
    CC_YIELD(t);
    mark('c');
    CC_END(t, 0xA6);
}

static void test_add_reuses_inline_node(void)
{
    reset_trace();
    g_added = cc_tconv_new(end_added, CC_NULL, 0);
    cc_task_t *a = cc_tconv_new(add_parent, CC_NULL, 0);
    cc_task_t *b = cc_tconv_new(end_b, CC_NULL, 0);
    g_runner = cc_runner_new(a, b);
    CHECK(g_runner != CC_NULL && g_added != CC_NULL);
    CHECK(cc_runner_add(g_runner, b) == CC_FALSE); // already READY

    cc_task_t *cur = g_runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cc_task_resume(&cur) == cc_tr_yield); // B end leaves one inline FREE node
    CHECK(cc_task_resume(&cur) == cc_tr_yield); // A adds C and yields
    CHECK(cc_task_resume(&cur) == cc_tr_yield); // C end
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("axbmc");
}

static cc_task_t *g_source;

static cc_tr_e self_a(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    g_target = cc_tconv_new(end_b, CC_NULL, 0);
    g_runner = cc_runner_new(t, g_target);
    CHECK(g_runner != CC_NULL);
    CC_TRANS(t, g_runner);
    mark('b');
    CC_YIELD(t);
    mark('c');
    CC_END(t, 0xA7);
}

static cc_tr_e self_parent(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    CC_AWAIT(t, g_source);
    mark('p');
    CC_END(t, 0x97);
}

static void test_self_inclusive_runner(void)
{
    reset_trace();
    g_source = cc_tconv_new(self_a, CC_NULL, 0);
    cc_task_t *parent = cc_tconv_new(self_parent, CC_NULL, 0);
    CHECK(g_source != CC_NULL && parent != CC_NULL);

    cc_task_t *cur = parent;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == g_runner);
    expect_trace("ab");
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == g_runner);
    expect_trace("abx");
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("abxcp");
    CHECK(cc_task_ret() == 0x97);
}

static void test_init_and_invalid_creation(void)
{
    CHECK(cc_runner_size_n(0) == 0);
    CHECK(cc_runner_size_n(-1) == 0);
    CHECK(cc_runner_size_n(1) > 0);

    static CC_ALIGN(16) cc_u8 mem[4096];
    cc_size size = cc_runner_size_n(2);
    CHECK(size != 0 && size <= sizeof(mem));
    cc_task_t *a = cc_tconv_new(yield_a, CC_NULL, 0);
    cc_task_t *b = cc_tconv_new(yield_b, CC_NULL, 0);
    cc_task_t *runner = cc_runner_init_n(mem, size, 2, a, b);
    CHECK(runner == (cc_task_t *)mem);
    CHECK(runner != CC_NULL && (runner->flags & cc_tf_retain));

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    CHECK(cc_task_ended(runner) == CC_TRUE);
    CHECK(runner->ext == CC_NULL);

    cc_task_t *c = cc_tconv_new(yield_a, CC_NULL, 0);
    cc_task_t *d = cc_tconv_new(yield_b, CC_NULL, 0);
    void (*cfn)(void) = c->fn;
    CHECK(cc_runner_init_n(mem, size - 1, 2, c, d) == CC_NULL);
    CHECK(c->fn == cfn && c->ext == CC_NULL && c->next == CC_NULL);
    CHECK(cc_runner_new(c, c) == CC_NULL);
    CHECK(c->fn == cfn && c->ext == CC_NULL && c->next == CC_NULL);
    cc_task_free(c);
    cc_task_free(d);
}

#if CC_CFG_TASK_STACKFUL
static cc_tr_e runner_stack_body(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(t, _, ctx);
    int local = 7;
    mark('s');
    if (!cc_tstack_yield())
    {
        return cc_tr_end;
    }
    local++;
    mark('t');
    if (!cc_tstack_wait())
    {
        return cc_tr_end;
    }
    if (local != 8)
    {
        s_fail++;
    }
    mark('u');
    cc_tstack_exit(0x57);
}

static void test_stackful_member(void)
{
    reset_trace();
    cc_task_t *task = cc_tstack_conv_new(runner_stack_body, CC_NULL, 0, 2048);
    cc_task_t *runner = cc_runner_new(task);
    CHECK(task != CC_NULL && runner != CC_NULL);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield && cur == runner);
    CHECK(cc_tstack_is_stackful(task) == CC_TRUE && task->ext == CC_NULL);
    CHECK(cc_task_resume(&cur) == cc_tr_wait && cur == runner);
    CHECK(task->ext != CC_NULL);

    cur = task;
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("stu");
    CHECK(cc_task_ret() == 0x57);
}
#endif // CC_CFG_TASK_STACKFUL

static cc_task_t *g_add_early;

static cc_tr_e early_a(cc_task_t *t, void *_, void *ctx)
{
    CC_UNUSED(ctx);
    CC_BEGIN(t);
    mark('a');
    CC_YIELD(t);
    mark('b');
    CC_END(t, 0xA8);
}

static void test_add_allocates_when_free_empty(void)
{
    reset_trace();
    g_add_early = cc_tconv_new(end_added, CC_NULL, 0);
    cc_task_t *a = cc_tconv_new(early_a, CC_NULL, 0);
    cc_task_t *runner = cc_runner_new(a);
    CHECK(runner != CC_NULL && g_add_early != CC_NULL);
    CHECK(cc_runner_add(runner, g_add_early) == CC_TRUE); // FREE empty: dynamic node
    CHECK(cc_runner_add(runner, runner) == CC_FALSE);

    cc_task_t *cur = runner;
    CHECK(cc_task_resume(&cur) == cc_tr_yield);
    CHECK(cc_task_resume(&cur) == cc_tr_yield); // dynamic C ends and is freed
    CHECK(cc_task_resume(&cur) == cc_tr_end && cur == CC_NULL);
    expect_trace("amb");
}

#endif // CC_CFG_USE_MALLOC && CC_CFG_TASK_CONV

int main(void)
{
#if CC_CFG_USE_MALLOC && CC_CFG_TASK_CONV
    test_ready_fifo();
    test_wait_wake();
    test_await_migration();
    test_nested_runner();
    test_trans_detaches_source();
    test_add_reuses_inline_node();
    test_add_allocates_when_free_empty();
    test_self_inclusive_runner();
    test_init_and_invalid_creation();
#if CC_CFG_TASK_STACKFUL
    test_stackful_member();
#endif
    if (s_fail == 0)
    {
        printf("cc_runner: basic checks ok\n");
    }
    return s_fail != 0;
#else
    return 0;
#endif
}
