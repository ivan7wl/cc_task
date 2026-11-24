#include "cc_swap.h"

#include <stdio.h>

#if CC_CFG_TASK_STACKFUL

static cc_u8 s_swap_stack[16384] CC_ALIGN(16);
static void *s_owner;
static void *s_task;
static int s_swap_error;

// Read the architectural stack pointer, not a local address that may be on the ASan fake stack.
CC_INLINE cc_uintptr current_sp(void)
{
    cc_uintptr sp;
#if defined(__x86_64__)
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
#elif defined(__i386__)
    __asm__ volatile("movl %%esp, %0" : "=r"(sp));
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#elif defined(__riscv)
    __asm__ volatile("mv %0, sp" : "=r"(sp));
#else
#error "cc_swap_check: unsupported architecture"
#endif
    return sp;
}

static void *swap_nested(void *code)
{
    volatile cc_uintptr marker = 0x12345678;
    void *ret = cc_swap(&s_task, s_owner, code);

    if (marker != 0x12345678) // the suspended C frame must survive intact
    {
        s_swap_error = 1;
    }
    return ret;
}

static void check_swap_entry(void *arg)
{
    volatile cc_uintptr marker = 0x55AA33CC;
    cc_uintptr sp = current_sp();
    cc_uintptr lo = (cc_uintptr)s_swap_stack;
    cc_uintptr hi = (cc_uintptr)(s_swap_stack + sizeof(s_swap_stack));

    if (arg != (void *)(cc_uintptr)0x11 || sp < lo || sp >= hi)
    {
        s_swap_error = 1;
    }

    for (cc_uintptr i = 1; i <= 3; i++)
    {
        void *ret = swap_nested((void *)(0x20 + i));
        if (ret != (void *)(0x30 + i) || marker != 0x55AA33CC)
        {
            s_swap_error = 1;
        }
    }

    cc_swap(&s_task, s_owner, (void *)(cc_uintptr)0x40);
    __builtin_trap(); // a finished entry is never resumed
}

static int check_swap(void)
{
    volatile cc_uintptr marker = 0x89ABCDEF;
    void *top = s_swap_stack + sizeof(s_swap_stack);
    void *ret;

    s_owner = CC_NULL;
    s_task = CC_NULL;
    s_swap_error = 0;

    ret = cc_swap_start(&s_owner, top, sizeof(s_swap_stack), check_swap_entry,
                        (void *)(cc_uintptr)0x11);
    if (ret != (void *)(cc_uintptr)0x21 || s_task == CC_NULL || marker != 0x89ABCDEF)
    {
        return 1;
    }

    for (cc_uintptr i = 1; i <= 3; i++)
    {
        ret = cc_swap(&s_owner, s_task, (void *)(0x30 + i));
        void *want = (i == 3) ? (void *)(cc_uintptr)0x40 : (void *)(0x21 + i);
        if (ret != want || marker != 0x89ABCDEF)
        {
            return 1;
        }
    }

    return s_swap_error;
}

int main(void)
{
    if (check_swap() != 0)
    {
        printf("cc_swap: swap FAILED\n");
        return 1;
    }

    printf("cc_swap: start / resume / args / nested stack ok\n");
    return 0;
}

#else

int main(void)
{
    printf("cc_swap: CC_CFG_TASK_STACKFUL=0, header is empty\n");
    return 0;
}

#endif // CC_CFG_TASK_STACKFUL
