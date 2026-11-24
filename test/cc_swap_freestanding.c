#include "cc_swap.h"

// Minimal Linux/_start test for targets without a matching libc sysroot.
// Build with -nostdlib -static; the only syscall is exit(code).
#if !defined(__arm__) && !defined(__riscv)
#error "cc_swap_freestanding: ARM or RISC-V only"
#endif

static cc_u8 s_stack[4096] CC_ALIGN(16);
static void *s_owner;
static void *s_task;
static int s_error;

static void *swap_nested(void *code)
{
    volatile cc_uintptr marker = 0x1234;
    void *ret = cc_swap(&s_task, s_owner, code);
    if (marker != 0x1234)
    {
        s_error = 1;
    }
    return ret;
}

static void swap_entry(void *arg)
{
    volatile cc_uintptr marker = 0x5678;
    if (arg != (void *)(cc_uintptr)0x11)
    {
        s_error = 1;
    }

    for (cc_uintptr i = 1; i <= 2; i++)
    {
        void *ret = swap_nested((void *)(0x20 + i));
        if (ret != (void *)(0x30 + i) || marker != 0x5678)
        {
            s_error = 1;
        }
    }

    cc_swap(&s_task, s_owner, (void *)(cc_uintptr)0x40);
    __builtin_trap();
}

CC_NORETURN static void process_exit(int code)
{
#if defined(__arm__)
    register cc_uintptr r0 __asm__("r0") = (cc_uintptr)code;
    register cc_uintptr r7 __asm__("r7") = 1; // __NR_exit
    __asm__ __volatile__("svc 0" : : "r"(r0), "r"(r7) : "memory");
#else
    register cc_uintptr a0 __asm__("a0") = (cc_uintptr)code;
#if defined(__riscv_32e)
    // qemu-user has no Linux syscall ABI for RV32E (a7 does not exist).
    // Stop with the result in a0 and inspect it with -d cpu for this check.
    __asm__ __volatile__("unimp" : : "r"(a0));
#else
    register cc_uintptr a7 __asm__("a7") = 93; // __NR_exit
    __asm__ __volatile__("ecall" : : "r"(a0), "r"(a7) : "memory");
#endif
#endif
    __builtin_unreachable();
}

CC_NORETURN void _start(void)
{
    void *top = s_stack + sizeof(s_stack);
    void *ret = cc_swap_start(&s_owner, top, sizeof(s_stack), swap_entry,
                                   (void *)(cc_uintptr)0x11);
    if (ret != (void *)(cc_uintptr)0x21 || s_task == CC_NULL)
    {
        process_exit(10);
    }

    ret = cc_swap(&s_owner, s_task, (void *)(cc_uintptr)0x31);
    if (ret != (void *)(cc_uintptr)0x22)
    {
        process_exit(11);
    }

    ret = cc_swap(&s_owner, s_task, (void *)(cc_uintptr)0x32);
    if (ret != (void *)(cc_uintptr)0x40 || s_error)
    {
        process_exit(12);
    }

    process_exit(0);
}
