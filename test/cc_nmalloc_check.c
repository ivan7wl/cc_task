#include "cc_nmalloc_port.h"
#include "cc_task.h"

#if CC_CFG_USE_MALLOC
#error "cc_nmalloc_check must be built with CC_CFG_USE_MALLOC=0"
#endif

static cc_port_task_slot_t s_slot;
static int s_fail;

#define CHECK(c)    do { if (!(c)) { s_fail++; } } while (0)

cc_port_task_slot_t *cc_nmalloc_task_slot(void)
{
    return &s_slot;
}

CC_NORETURN void cc_nmalloc_panic(const char *file, int line, const char *expr)
{
    CC_UNUSED(file, line, expr);
    __builtin_trap();
}

#if CC_CFG_TASK_BLOCK

static void test_block_init(void)
{
    static CC_ALIGN(16) cc_u8 mem[256];
    int value = 0x10;

    cc_task_func blk = CC_TASK(t)
    {
        CC_BEGIN(t);
        $(value)++;
        CC_YIELD(t);
        CC_END(t, value);
    };

    cc_task_t *retained = cc_task_init(mem, sizeof(mem), blk);
    cc_task_t *t = retained;
    CHECK(retained != CC_NULL);
    if (retained == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_resume(&t) == cc_tr_end && t == CC_NULL);
    CHECK(cc_task_ret() == 0x11 && value == 0x10);
    CHECK(cc_task_ended(retained) == CC_TRUE);
}

#endif // CC_CFG_TASK_BLOCK

#if CC_CFG_TASK_CONV || CC_CFG_TASK_STACKFUL
typedef struct
{
    cc_uintptr value;
} nm_ctx;
#endif

#if CC_CFG_TASK_CONV

static cc_tr_e conv_body(cc_task_t *t, void *_, void *ctx)
{
    nm_ctx *c = (nm_ctx *)ctx;

    CC_BEGIN(t);
    CC_UNUSED(_);
    c->value++;
    CC_END(t, c->value);
}

static void test_conv_init(void)
{
    static CC_ALIGN(16) cc_u8 mem[CC_TASK_SIZE(sizeof(nm_ctx))];
    nm_ctx initial = { 0x20 };
    cc_task_t *retained = cc_tconv_init(mem, sizeof(mem), conv_body, &initial, sizeof(initial));
    cc_task_t *t = retained;

    CHECK(retained != CC_NULL);
    if (retained == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_end && t == CC_NULL);
    CHECK(cc_task_ret() == 0x21);
    CHECK(cc_task_ended(retained) == CC_TRUE);
}

#endif // CC_CFG_TASK_CONV

#if CC_CFG_TASK_STACKFUL

static cc_tr_e stack_body(cc_task_t *t, void *_, void *ctx)
{
    nm_ctx *c = (nm_ctx *)ctx;
    cc_uintptr local = c->value;

    CC_UNUSED(t, _);
    if (!cc_tstack_yield())
    {
        return cc_tr_end;
    }
    cc_tstack_exit(local + 1);
}

static void test_stack_conv_init(void)
{
    static CC_ALIGN(16) cc_u8 mem[4096];
    nm_ctx initial = { 0x30 };
    cc_task_t *retained = cc_tstack_conv_init(mem, sizeof(mem), stack_body, &initial, sizeof(initial));
    cc_task_t *t = retained;

    CHECK(retained != CC_NULL);
    if (retained == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_resume(&t) == cc_tr_end && t == CC_NULL);
    CHECK(cc_task_ret() == 0x31);
    CHECK(cc_task_ended(retained) == CC_TRUE);
}

#if CC_CFG_TASK_BLOCK

static void test_stack_block_init(void)
{
    static CC_ALIGN(16) cc_u8 mem[4096];
    int value = 0x40;

    cc_task_func blk = CC_TASK(t)
    {
        CC_UNUSED(t, _);
        $(value)++;
        if (!cc_tstack_yield())
        {
            return cc_tr_end;
        }
        cc_tstack_exit(value);
    };

    cc_size size = cc_tstack_size(2048, blk);
    CHECK(size != 0 && size <= sizeof(mem));
    cc_task_t *retained = (size == 0) ? CC_NULL : cc_tstack_init(mem, size, blk);
    cc_task_t *t = retained;
    CHECK(retained != CC_NULL);
    if (retained == CC_NULL)
    {
        return;
    }

    CHECK(cc_task_resume(&t) == cc_tr_yield);
    CHECK(cc_task_resume(&t) == cc_tr_end && t == CC_NULL);
    CHECK(cc_task_ret() == 0x41 && value == 0x40);
    CHECK(cc_task_ended(retained) == CC_TRUE);
}

#endif // CC_CFG_TASK_BLOCK
#endif // CC_CFG_TASK_STACKFUL

int main(void)
{
#if CC_CFG_TASK_BLOCK
    test_block_init();
#endif
#if CC_CFG_TASK_CONV
    test_conv_init();
#endif
#if CC_CFG_TASK_STACKFUL
    test_stack_conv_init();
#if CC_CFG_TASK_BLOCK
    test_stack_block_init();
#endif
#endif

    return s_fail != 0;
}
