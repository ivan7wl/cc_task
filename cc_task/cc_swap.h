// SPDX-License-Identifier: Apache-2.0

#ifndef CC_SWAP_H
#define CC_SWAP_H

// context switch primitives for stackful coroutines

#include "cc_types.h"

// only the stackful variants switch contexts, CC_CFG_TASK_STACKFUL=0 leaves this header empty
#if CC_CFG_TASK_STACKFUL

//--------------------------------------------------------------------------------------------------------
// stackful context switch
//
// cc_swap(save, load, arg):
//   save callee-saved state on the current stack and store its sp in *save;
//   resume the context at load, where arg becomes this call's return value.
// cc_swap_start(save, top, size, entry, arg):
//   save the current context the same way, then enter entry(arg) on the new stack.
//   top is 16 byte aligned and covers [top - size, top); entry must not return.
//   size is used by Windows to initialize the TEB stack range and is ignored elsewhere.
// A saved context must be resumed on the same thread. Shadow stacks are not switched.
//
// supported targets:
// - x86_64: SysV and Windows x64 (Win64)
// - i386: SysV cdecl and Windows i386 (Win32)
// - aarch64: AAPCS64, with or without FP/SIMD
// - ARM32: AAPCS ARM and Thumb-2, with optional VFP
// - Thumb-1: Cortex-M0 / M0+ / M23
// - RISC-V: RV64, RV32 and RV32E, with optional F/D on RV64/RV32

#if defined(__x86_64__) && defined(_WIN32)

// Win64: save=rcx, load=rdx, arg=r8. The frame below the eight GPR pushes
// contains aligned XMM6-XMM15, control words and the TEB fiber/stack fields.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr64\n"
#endif
        "pushq %rbx\n"
        "pushq %rbp\n"
        "pushq %rdi\n"
        "pushq %rsi\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "subq $200, %rsp\n"
        "movaps %xmm6, 0(%rsp)\n"
        "movaps %xmm7, 16(%rsp)\n"
        "movaps %xmm8, 32(%rsp)\n"
        "movaps %xmm9, 48(%rsp)\n"
        "movaps %xmm10, 64(%rsp)\n"
        "movaps %xmm11, 80(%rsp)\n"
        "movaps %xmm12, 96(%rsp)\n"
        "movaps %xmm13, 112(%rsp)\n"
        "movaps %xmm14, 128(%rsp)\n"
        "movaps %xmm15, 144(%rsp)\n"
        "stmxcsr 160(%rsp)\n"
        "fnstcw 164(%rsp)\n"
        "movq %gs:0x20, %rax\n"
        "movq %rax, 168(%rsp)\n"               // FiberData
        "movq %gs:0x1478, %rax\n"
        "movq %rax, 176(%rsp)\n"               // DeallocationStack
        "movq %gs:0x10, %rax\n"
        "movq %rax, 184(%rsp)\n"               // StackLimit
        "movq %gs:0x08, %rax\n"
        "movq %rax, 192(%rsp)\n"               // StackBase
        "movq %rsp, (%rcx)\n"
        "movq %rdx, %rsp\n"

        "movaps 0(%rsp), %xmm6\n"
        "movaps 16(%rsp), %xmm7\n"
        "movaps 32(%rsp), %xmm8\n"
        "movaps 48(%rsp), %xmm9\n"
        "movaps 64(%rsp), %xmm10\n"
        "movaps 80(%rsp), %xmm11\n"
        "movaps 96(%rsp), %xmm12\n"
        "movaps 112(%rsp), %xmm13\n"
        "movaps 128(%rsp), %xmm14\n"
        "movaps 144(%rsp), %xmm15\n"
        "ldmxcsr 160(%rsp)\n"
        "fldcw 164(%rsp)\n"
        "movq 168(%rsp), %rax\n"
        "movq %rax, %gs:0x20\n"
        "movq 176(%rsp), %rax\n"
        "movq %rax, %gs:0x1478\n"
        "movq 184(%rsp), %rax\n"
        "movq %rax, %gs:0x10\n"
        "movq 192(%rsp), %rax\n"
        "movq %rax, %gs:0x08\n"
        "addq $200, %rsp\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %rsi\n"
        "popq %rdi\n"
        "popq %rbp\n"
        "popq %rbx\n"
        "movq %r8, %rax\n"
        "retq\n"
    );
}

// Start: save=rcx, top=rdx, size=r8, entry=r9, arg is the fifth argument.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr64\n"
#endif
        "pushq %rbx\n"
        "pushq %rbp\n"
        "pushq %rdi\n"
        "pushq %rsi\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "subq $200, %rsp\n"
        "movaps %xmm6, 0(%rsp)\n"
        "movaps %xmm7, 16(%rsp)\n"
        "movaps %xmm8, 32(%rsp)\n"
        "movaps %xmm9, 48(%rsp)\n"
        "movaps %xmm10, 64(%rsp)\n"
        "movaps %xmm11, 80(%rsp)\n"
        "movaps %xmm12, 96(%rsp)\n"
        "movaps %xmm13, 112(%rsp)\n"
        "movaps %xmm14, 128(%rsp)\n"
        "movaps %xmm15, 144(%rsp)\n"
        "stmxcsr 160(%rsp)\n"
        "fnstcw 164(%rsp)\n"
        "movq %gs:0x20, %rax\n"
        "movq %rax, 168(%rsp)\n"
        "movq %gs:0x1478, %rax\n"
        "movq %rax, 176(%rsp)\n"
        "movq %gs:0x10, %rax\n"
        "movq %rax, 184(%rsp)\n"
        "movq %gs:0x08, %rax\n"
        "movq %rax, 192(%rsp)\n"
        "movq %rsp, (%rcx)\n"
        "movq 304(%rsp), %r10\n"               // fifth argument, before changing stacks
        "movq %rdx, %rsp\n"
        "andq $-16, %rsp\n"
        "xorl %eax, %eax\n"
        "movq %rax, %gs:0x20\n"                // new fiber storage
        "movq %rdx, %rax\n"
        "subq %r8, %rax\n"
        "movq %rax, %gs:0x1478\n"
        "movq %rax, %gs:0x10\n"
        "movq %rdx, %gs:0x08\n"
        "subq $32, %rsp\n"                     // Win64 parameter/shadow area
        "movq %r10, %rcx\n"
        "xorl %ebp, %ebp\n"
        "callq *%r9\n"
        "ud2\n"
    );
}

#elif defined(__x86_64__)

// SysV: save=rdi, load=rsi, arg=rdx. Saved sp is 16 byte aligned.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr64\n"
#endif
        "pushq %rbp\n"
        "pushq %rbx\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "subq $8, %rsp\n"
        "stmxcsr 0(%rsp)\n"
        "fnstcw 4(%rsp)\n"
        "movq %rsp, (%rdi)\n"
        "movq %rsi, %rsp\n"
        "ldmxcsr 0(%rsp)\n"
        "fldcw 4(%rsp)\n"
        "addq $8, %rsp\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %rbx\n"
        "popq %rbp\n"
        "movq %rdx, %rax\n"
        "retq\n"
    );
}

// Start: save=rdi, top=rsi, size=rdx, entry=rcx, arg=r8.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr64\n"
#endif
        "pushq %rbp\n"
        "pushq %rbx\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "subq $8, %rsp\n"
        "stmxcsr 0(%rsp)\n"
        "fnstcw 4(%rsp)\n"
        "movq %rsp, (%rdi)\n"
        "movq %rsi, %rsp\n"
        "andq $-16, %rsp\n"
        "movq %r8, %rdi\n"
        "xorl %ebp, %ebp\n"
        "callq *%rcx\n"
        "ud2\n"
    );
}

#elif defined(__i386__)

// i386 cdecl: a common 44 byte frame leaves saved sp 16 byte aligned when the
// caller follows the modern ABI. Windows also saves fiber, stack and SEH state.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr32\n"
#endif
        "pushl %ebx\n"
        "pushl %esi\n"
        "pushl %edi\n"
        "pushl %ebp\n"
        "subl $28, %esp\n"
#if defined(__SSE__)
        "stmxcsr 0(%esp)\n"
#endif
        "fnstcw 4(%esp)\n"
#if defined(_WIN32)
        "movl %fs:0x10, %eax\n"
        "movl %eax, 8(%esp)\n"                // FiberData
        "movl %fs:0xe0c, %eax\n"
        "movl %eax, 12(%esp)\n"               // DeallocationStack
        "movl %fs:0x08, %eax\n"
        "movl %eax, 16(%esp)\n"               // StackLimit
        "movl %fs:0x04, %eax\n"
        "movl %eax, 20(%esp)\n"               // StackBase
        "movl %fs:0x00, %eax\n"
        "movl %eax, 24(%esp)\n"               // SEH chain head
#endif
        "movl 56(%esp), %eax\n"               // arg
        "movl 52(%esp), %edx\n"               // load
        "movl 48(%esp), %ecx\n"               // save
        "movl %esp, (%ecx)\n"
        "movl %edx, %esp\n"
#if defined(_WIN32)
        "movl 8(%esp), %ecx\n"
        "movl %ecx, %fs:0x10\n"
        "movl 12(%esp), %ecx\n"
        "movl %ecx, %fs:0xe0c\n"
        "movl 16(%esp), %ecx\n"
        "movl %ecx, %fs:0x08\n"
        "movl 20(%esp), %ecx\n"
        "movl %ecx, %fs:0x04\n"
        "movl 24(%esp), %ecx\n"
        "movl %ecx, %fs:0x00\n"
#endif
#if defined(__SSE__)
        "ldmxcsr 0(%esp)\n"
#endif
        "fldcw 4(%esp)\n"
        "addl $28, %esp\n"
        "popl %ebp\n"
        "popl %edi\n"
        "popl %esi\n"
        "popl %ebx\n"
        "ret\n"
    );
}

// Start arguments are above the 44 byte saved frame. Win32 installs a minimal
// SEH record using the OS final handler, matching fiber-context requirements.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
#if defined(__CET__) && (__CET__ & 1)
        "endbr32\n"
#endif
        "pushl %ebx\n"
        "pushl %esi\n"
        "pushl %edi\n"
        "pushl %ebp\n"
        "subl $28, %esp\n"
#if defined(__SSE__)
        "stmxcsr 0(%esp)\n"
#endif
        "fnstcw 4(%esp)\n"
#if defined(_WIN32)
        "movl %fs:0x10, %eax\n"
        "movl %eax, 8(%esp)\n"
        "movl %fs:0xe0c, %eax\n"
        "movl %eax, 12(%esp)\n"
        "movl %fs:0x08, %eax\n"
        "movl %eax, 16(%esp)\n"
        "movl %fs:0x04, %eax\n"
        "movl %eax, 20(%esp)\n"
        "movl %fs:0x00, %eax\n"
        "movl %eax, 24(%esp)\n"
#endif
        "movl 64(%esp), %eax\n"               // arg
        "movl 60(%esp), %edx\n"               // entry
        "movl 56(%esp), %edi\n"               // size
        "movl 52(%esp), %ebx\n"               // top
        "movl 48(%esp), %ecx\n"               // save
        "movl %esp, (%ecx)\n"
#if defined(_WIN32)
        "movl %fs:0x00, %esi\n"
        "1:\n"
        "movl (%esi), %ecx\n"
        "incl %ecx\n"
        "jz 2f\n"
        "decl %ecx\n"
        "movl %ecx, %esi\n"
        "jmp 1b\n"
        "2:\n"
        "movl 4(%esi), %esi\n"                // OS final exception handler
#endif
        "movl %ebx, %esp\n"
        "andl $-16, %esp\n"
#if defined(_WIN32)
        "xorl %ecx, %ecx\n"
        "movl %ecx, %fs:0x10\n"
        "movl %ebx, %ecx\n"
        "subl %edi, %ecx\n"
        "movl %ecx, %fs:0xe0c\n"
        "movl %ecx, %fs:0x08\n"
        "movl %ebx, %fs:0x04\n"
        "subl $16, %esp\n"
        "movl $-1, 0(%esp)\n"
        "movl %esi, 4(%esp)\n"
        "movl %esp, %fs:0x00\n"
#endif
        "xorl %ebp, %ebp\n"
        "subl $12, %esp\n"
        "pushl %eax\n"
        "call *%edx\n"
        "ud2\n"
    );
}

#elif defined(__aarch64__)

#if defined(__ARM_FP)
#define CC_SWAP_A64_SWAP_FRAME 160
#else
#define CC_SWAP_A64_SWAP_FRAME 96
#endif
#define CC_SWAP_A64_STR_(x) #x
#define CC_SWAP_A64_STR(x) CC_SWAP_A64_STR_(x)

// AAPCS64: save=x0, load=x1, arg=x2.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "bti c\n"
        "sub sp, sp, #" CC_SWAP_A64_STR(CC_SWAP_A64_SWAP_FRAME) "\n"
        "stp x19, x20, [sp, #0]\n"
        "stp x21, x22, [sp, #16]\n"
        "stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n"
        "stp x27, x28, [sp, #64]\n"
        "stp x29, x30, [sp, #80]\n"
#if defined(__ARM_FP)
        "stp d8, d9, [sp, #96]\n"
        "stp d10, d11, [sp, #112]\n"
        "stp d12, d13, [sp, #128]\n"
        "stp d14, d15, [sp, #144]\n"
#endif
        "mov x9, sp\n"
        "str x9, [x0]\n"
        "mov sp, x1\n"
        "ldp x19, x20, [sp, #0]\n"
        "ldp x21, x22, [sp, #16]\n"
        "ldp x23, x24, [sp, #32]\n"
        "ldp x25, x26, [sp, #48]\n"
        "ldp x27, x28, [sp, #64]\n"
        "ldp x29, x30, [sp, #80]\n"
#if defined(__ARM_FP)
        "ldp d8, d9, [sp, #96]\n"
        "ldp d10, d11, [sp, #112]\n"
        "ldp d12, d13, [sp, #128]\n"
        "ldp d14, d15, [sp, #144]\n"
#endif
        "add sp, sp, #" CC_SWAP_A64_STR(CC_SWAP_A64_SWAP_FRAME) "\n"
        "mov x0, x2\n"
        "ret\n"
    );
}

// Start: save=x0, top=x1, size=x2, entry=x3, arg=x4.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "bti c\n"
        "sub sp, sp, #" CC_SWAP_A64_STR(CC_SWAP_A64_SWAP_FRAME) "\n"
        "stp x19, x20, [sp, #0]\n"
        "stp x21, x22, [sp, #16]\n"
        "stp x23, x24, [sp, #32]\n"
        "stp x25, x26, [sp, #48]\n"
        "stp x27, x28, [sp, #64]\n"
        "stp x29, x30, [sp, #80]\n"
#if defined(__ARM_FP)
        "stp d8, d9, [sp, #96]\n"
        "stp d10, d11, [sp, #112]\n"
        "stp d12, d13, [sp, #128]\n"
        "stp d14, d15, [sp, #144]\n"
#endif
        "mov x9, sp\n"
        "str x9, [x0]\n"
        "and x9, x1, #0xfffffffffffffff0\n"
        "mov sp, x9\n"
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "mov x0, x4\n"
        "blr x3\n"
        "brk #0\n"
    );
}

#undef CC_SWAP_A64_STR
#undef CC_SWAP_A64_STR_
#undef CC_SWAP_A64_SWAP_FRAME

#elif defined(__arm__) && (defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__))

// Thumb-1: high registers are saved through r4-r7. The saved frame is 48 bytes:
// r8-r11, r4-r7, lr and 12 bytes of alignment padding.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "push {r4-r7, lr}\n"
        "mov r4, r8\n"
        "mov r5, r9\n"
        "mov r6, r10\n"
        "mov r7, r11\n"
        "push {r4-r7}\n"
        "sub sp, sp, #12\n"
        "mov r4, sp\n"
        "str r4, [r0]\n"
        "mov sp, r1\n"
        "add sp, sp, #12\n"
        "pop {r4-r7}\n"
        "mov r8, r4\n"
        "mov r9, r5\n"
        "mov r10, r6\n"
        "mov r11, r7\n"
        "mov r0, r2\n"
        "pop {r4-r7, pc}\n"
    );
}

CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "ldr r4, [sp]\n"                        // fifth argument
        "mov r12, r4\n"
        "push {r4-r7, lr}\n"
        "mov r4, r8\n"
        "mov r5, r9\n"
        "mov r6, r10\n"
        "mov r7, r11\n"
        "push {r4-r7}\n"
        "sub sp, sp, #12\n"
        "mov r4, sp\n"
        "str r4, [r0]\n"
        "movs r4, #15\n"
        "bics r1, r4\n"
        "mov sp, r1\n"
        "movs r4, #0\n"
        "push {r4, lr}\n"                       // terminating frame record
        "mov r7, sp\n"
        "mov r0, r12\n"
        "blx r3\n"
        "udf #0\n"
    );
}

#elif defined(__arm__)

#if defined(__ARM_FP) && !defined(__SOFTFP__)
#define CC_SWAP_ARM_SWAP_FRAME 112
#define CC_SWAP_ARM_SWAP_VFP 1
#else
#define CC_SWAP_ARM_SWAP_FRAME 48
#define CC_SWAP_ARM_SWAP_VFP 0
#endif
#define CC_SWAP_ARM_STR_(x) #x
#define CC_SWAP_ARM_STR(x) CC_SWAP_ARM_STR_(x)

// ARM / Thumb-2: save=r0, load=r1, arg=r2.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "sub sp, sp, #" CC_SWAP_ARM_STR(CC_SWAP_ARM_SWAP_FRAME) "\n"
        "str r4, [sp, #0]\n"
        "str r5, [sp, #4]\n"
        "str r6, [sp, #8]\n"
        "str r7, [sp, #12]\n"
        "str r8, [sp, #16]\n"
        "str r9, [sp, #20]\n"
        "str r10, [sp, #24]\n"
        "str r11, [sp, #28]\n"
        "str lr, [sp, #32]\n"
#if CC_SWAP_ARM_SWAP_VFP
        "vstr d8, [sp, #48]\n"
        "vstr d9, [sp, #56]\n"
        "vstr d10, [sp, #64]\n"
        "vstr d11, [sp, #72]\n"
        "vstr d12, [sp, #80]\n"
        "vstr d13, [sp, #88]\n"
        "vstr d14, [sp, #96]\n"
        "vstr d15, [sp, #104]\n"
#endif
        "mov r12, sp\n"
        "str r12, [r0]\n"
        "mov sp, r1\n"
        "ldr r4, [sp, #0]\n"
        "ldr r5, [sp, #4]\n"
        "ldr r6, [sp, #8]\n"
        "ldr r7, [sp, #12]\n"
        "ldr r8, [sp, #16]\n"
        "ldr r9, [sp, #20]\n"
        "ldr r10, [sp, #24]\n"
        "ldr r11, [sp, #28]\n"
        "ldr lr, [sp, #32]\n"
#if CC_SWAP_ARM_SWAP_VFP
        "vldr d8, [sp, #48]\n"
        "vldr d9, [sp, #56]\n"
        "vldr d10, [sp, #64]\n"
        "vldr d11, [sp, #72]\n"
        "vldr d12, [sp, #80]\n"
        "vldr d13, [sp, #88]\n"
        "vldr d14, [sp, #96]\n"
        "vldr d15, [sp, #104]\n"
#endif
        "add sp, sp, #" CC_SWAP_ARM_STR(CC_SWAP_ARM_SWAP_FRAME) "\n"
        "mov r0, r2\n"
        "bx lr\n"
    );
}

// Start: save=r0, top=r1, size=r2, entry=r3; arg is the fifth argument on stack.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "ldr r12, [sp]\n"
        "sub sp, sp, #" CC_SWAP_ARM_STR(CC_SWAP_ARM_SWAP_FRAME) "\n"
        "str r4, [sp, #0]\n"
        "str r5, [sp, #4]\n"
        "str r6, [sp, #8]\n"
        "str r7, [sp, #12]\n"
        "str r8, [sp, #16]\n"
        "str r9, [sp, #20]\n"
        "str r10, [sp, #24]\n"
        "str r11, [sp, #28]\n"
        "str lr, [sp, #32]\n"
#if CC_SWAP_ARM_SWAP_VFP
        "vstr d8, [sp, #48]\n"
        "vstr d9, [sp, #56]\n"
        "vstr d10, [sp, #64]\n"
        "vstr d11, [sp, #72]\n"
        "vstr d12, [sp, #80]\n"
        "vstr d13, [sp, #88]\n"
        "vstr d14, [sp, #96]\n"
        "vstr d15, [sp, #104]\n"
#endif
        "mov r4, sp\n"
        "str r4, [r0]\n"
        "bic r1, r1, #15\n"
        "mov sp, r1\n"
        "mov r11, #0\n"
        "push {r11, lr}\n"
        "mov r11, sp\n"
        "mov r0, r12\n"
        "blx r3\n"
        "udf #0\n"
    );
}

#undef CC_SWAP_ARM_STR
#undef CC_SWAP_ARM_STR_
#undef CC_SWAP_ARM_SWAP_VFP
#undef CC_SWAP_ARM_SWAP_FRAME

#elif defined(__riscv) && (__riscv_xlen == 64 || __riscv_xlen == 32)

#if __riscv_xlen == 64
#define CC_SWAP_RV_LD "ld"
#define CC_SWAP_RV_ST "sd"
#define CC_SWAP_RV_WORD 8
#define CC_SWAP_RV_GPR_FRAME 112
#define CC_SWAP_RV_E 0
#else
#define CC_SWAP_RV_LD "lw"
#define CC_SWAP_RV_ST "sw"
#define CC_SWAP_RV_WORD 4
#if defined(__riscv_32e)
#define CC_SWAP_RV_GPR_FRAME 16
#define CC_SWAP_RV_E 1
#else
#define CC_SWAP_RV_GPR_FRAME 64
#define CC_SWAP_RV_E 0
#endif
#endif

#if defined(__riscv_flen) && !CC_SWAP_RV_E
#if __riscv_flen == 64
#define CC_SWAP_RV_FLD "fld"
#define CC_SWAP_RV_FST "fsd"
#define CC_SWAP_RV_FP_SLOT 8
#define CC_SWAP_RV_FP_FRAME 96
#elif __riscv_flen == 32
#define CC_SWAP_RV_FLD "flw"
#define CC_SWAP_RV_FST "fsw"
#define CC_SWAP_RV_FP_SLOT 4
#define CC_SWAP_RV_FP_FRAME 48
#else
#error "cc_swap: unsupported RISC-V FLEN"
#endif
#else
#define CC_SWAP_RV_FP_SLOT 0
#define CC_SWAP_RV_FP_FRAME 0
#endif
#define CC_SWAP_RV_FRAME (CC_SWAP_RV_GPR_FRAME + CC_SWAP_RV_FP_FRAME)
#define CC_SWAP_RV_STR_(x) #x
#define CC_SWAP_RV_STR(x) CC_SWAP_RV_STR_(x)

// RISC-V: save=a0, load=a1, arg=a2. RV32E saves only ra/s0/s1.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap(void **save CC_MAYBE_UNUSED, void *load CC_MAYBE_UNUSED,
                     void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "addi sp, sp, -" CC_SWAP_RV_STR(CC_SWAP_RV_FRAME) "\n"
        CC_SWAP_RV_ST " ra, 0(sp)\n"
#if __riscv_xlen == 64
        CC_SWAP_RV_ST " s0, 8(sp)\n"
        CC_SWAP_RV_ST " s1, 16(sp)\n"
        CC_SWAP_RV_ST " s2, 24(sp)\n"
        CC_SWAP_RV_ST " s3, 32(sp)\n"
        CC_SWAP_RV_ST " s4, 40(sp)\n"
        CC_SWAP_RV_ST " s5, 48(sp)\n"
        CC_SWAP_RV_ST " s6, 56(sp)\n"
        CC_SWAP_RV_ST " s7, 64(sp)\n"
        CC_SWAP_RV_ST " s8, 72(sp)\n"
        CC_SWAP_RV_ST " s9, 80(sp)\n"
        CC_SWAP_RV_ST " s10, 88(sp)\n"
        CC_SWAP_RV_ST " s11, 96(sp)\n"
#elif CC_SWAP_RV_E
        CC_SWAP_RV_ST " s0, 4(sp)\n"
        CC_SWAP_RV_ST " s1, 8(sp)\n"
#else
        CC_SWAP_RV_ST " s0, 4(sp)\n"
        CC_SWAP_RV_ST " s1, 8(sp)\n"
        CC_SWAP_RV_ST " s2, 12(sp)\n"
        CC_SWAP_RV_ST " s3, 16(sp)\n"
        CC_SWAP_RV_ST " s4, 20(sp)\n"
        CC_SWAP_RV_ST " s5, 24(sp)\n"
        CC_SWAP_RV_ST " s6, 28(sp)\n"
        CC_SWAP_RV_ST " s7, 32(sp)\n"
        CC_SWAP_RV_ST " s8, 36(sp)\n"
        CC_SWAP_RV_ST " s9, 40(sp)\n"
        CC_SWAP_RV_ST " s10, 44(sp)\n"
        CC_SWAP_RV_ST " s11, 48(sp)\n"
#endif
#if defined(__riscv_flen) && !CC_SWAP_RV_E
        CC_SWAP_RV_FST " fs0, " CC_SWAP_RV_STR(CC_SWAP_RV_GPR_FRAME) "(sp)\n"
        CC_SWAP_RV_FST " fs1, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs2, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 2 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs3, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 3 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs4, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 4 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs5, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 5 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs6, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 6 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs7, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 7 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs8, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 8 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs9, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 9 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs10, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 10 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs11, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 11 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
#endif
        "mv t0, sp\n"
        CC_SWAP_RV_ST " t0, 0(a0)\n"
        "mv sp, a1\n"
        CC_SWAP_RV_LD " ra, 0(sp)\n"
#if __riscv_xlen == 64
        CC_SWAP_RV_LD " s0, 8(sp)\n"
        CC_SWAP_RV_LD " s1, 16(sp)\n"
        CC_SWAP_RV_LD " s2, 24(sp)\n"
        CC_SWAP_RV_LD " s3, 32(sp)\n"
        CC_SWAP_RV_LD " s4, 40(sp)\n"
        CC_SWAP_RV_LD " s5, 48(sp)\n"
        CC_SWAP_RV_LD " s6, 56(sp)\n"
        CC_SWAP_RV_LD " s7, 64(sp)\n"
        CC_SWAP_RV_LD " s8, 72(sp)\n"
        CC_SWAP_RV_LD " s9, 80(sp)\n"
        CC_SWAP_RV_LD " s10, 88(sp)\n"
        CC_SWAP_RV_LD " s11, 96(sp)\n"
#elif CC_SWAP_RV_E
        CC_SWAP_RV_LD " s0, 4(sp)\n"
        CC_SWAP_RV_LD " s1, 8(sp)\n"
#else
        CC_SWAP_RV_LD " s0, 4(sp)\n"
        CC_SWAP_RV_LD " s1, 8(sp)\n"
        CC_SWAP_RV_LD " s2, 12(sp)\n"
        CC_SWAP_RV_LD " s3, 16(sp)\n"
        CC_SWAP_RV_LD " s4, 20(sp)\n"
        CC_SWAP_RV_LD " s5, 24(sp)\n"
        CC_SWAP_RV_LD " s6, 28(sp)\n"
        CC_SWAP_RV_LD " s7, 32(sp)\n"
        CC_SWAP_RV_LD " s8, 36(sp)\n"
        CC_SWAP_RV_LD " s9, 40(sp)\n"
        CC_SWAP_RV_LD " s10, 44(sp)\n"
        CC_SWAP_RV_LD " s11, 48(sp)\n"
#endif
#if defined(__riscv_flen) && !CC_SWAP_RV_E
        CC_SWAP_RV_FLD " fs0, " CC_SWAP_RV_STR(CC_SWAP_RV_GPR_FRAME) "(sp)\n"
        CC_SWAP_RV_FLD " fs1, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs2, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 2 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs3, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 3 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs4, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 4 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs5, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 5 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs6, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 6 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs7, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 7 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs8, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 8 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs9, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 9 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs10, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 10 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FLD " fs11, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 11 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
#endif
        "addi sp, sp, " CC_SWAP_RV_STR(CC_SWAP_RV_FRAME) "\n"
        "mv a0, a2\n"
        "ret\n"
    );
}

// Start: save=a0, top=a1, size=a2, entry=a3, arg=a4.
CC_NAKED CC_NOINLINE CC_MAYBE_UNUSED
static void *cc_swap_start(void **save CC_MAYBE_UNUSED, void *top CC_MAYBE_UNUSED,
                           cc_size size CC_MAYBE_UNUSED,
                           void (*entry)(void *) CC_MAYBE_UNUSED, void *arg CC_MAYBE_UNUSED)
{
    __asm__ __volatile__
    (
        "addi sp, sp, -" CC_SWAP_RV_STR(CC_SWAP_RV_FRAME) "\n"
        CC_SWAP_RV_ST " ra, 0(sp)\n"
#if __riscv_xlen == 64
        CC_SWAP_RV_ST " s0, 8(sp)\n"
        CC_SWAP_RV_ST " s1, 16(sp)\n"
        CC_SWAP_RV_ST " s2, 24(sp)\n"
        CC_SWAP_RV_ST " s3, 32(sp)\n"
        CC_SWAP_RV_ST " s4, 40(sp)\n"
        CC_SWAP_RV_ST " s5, 48(sp)\n"
        CC_SWAP_RV_ST " s6, 56(sp)\n"
        CC_SWAP_RV_ST " s7, 64(sp)\n"
        CC_SWAP_RV_ST " s8, 72(sp)\n"
        CC_SWAP_RV_ST " s9, 80(sp)\n"
        CC_SWAP_RV_ST " s10, 88(sp)\n"
        CC_SWAP_RV_ST " s11, 96(sp)\n"
#elif CC_SWAP_RV_E
        CC_SWAP_RV_ST " s0, 4(sp)\n"
        CC_SWAP_RV_ST " s1, 8(sp)\n"
#else
        CC_SWAP_RV_ST " s0, 4(sp)\n"
        CC_SWAP_RV_ST " s1, 8(sp)\n"
        CC_SWAP_RV_ST " s2, 12(sp)\n"
        CC_SWAP_RV_ST " s3, 16(sp)\n"
        CC_SWAP_RV_ST " s4, 20(sp)\n"
        CC_SWAP_RV_ST " s5, 24(sp)\n"
        CC_SWAP_RV_ST " s6, 28(sp)\n"
        CC_SWAP_RV_ST " s7, 32(sp)\n"
        CC_SWAP_RV_ST " s8, 36(sp)\n"
        CC_SWAP_RV_ST " s9, 40(sp)\n"
        CC_SWAP_RV_ST " s10, 44(sp)\n"
        CC_SWAP_RV_ST " s11, 48(sp)\n"
#endif
#if defined(__riscv_flen) && !CC_SWAP_RV_E
        CC_SWAP_RV_FST " fs0, " CC_SWAP_RV_STR(CC_SWAP_RV_GPR_FRAME) "(sp)\n"
        CC_SWAP_RV_FST " fs1, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs2, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 2 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs3, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 3 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs4, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 4 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs5, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 5 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs6, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 6 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs7, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 7 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs8, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 8 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs9, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 9 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs10, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 10 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
        CC_SWAP_RV_FST " fs11, " CC_SWAP_RV_STR((CC_SWAP_RV_GPR_FRAME + 11 * CC_SWAP_RV_FP_SLOT)) "(sp)\n"
#endif
        "mv t0, sp\n"
        CC_SWAP_RV_ST " t0, 0(a0)\n"
        "andi t0, a1, -16\n"
        "mv sp, t0\n"
        "addi sp, sp, -16\n"
        CC_SWAP_RV_ST " s0, 0(sp)\n"
        CC_SWAP_RV_ST " ra, " CC_SWAP_RV_STR(CC_SWAP_RV_WORD) "(sp)\n"
        "mv s0, sp\n"
        "mv a0, a4\n"
        "jalr ra, 0(a3)\n"
        ".word 0\n"
    );
}

#undef CC_SWAP_RV_STR
#undef CC_SWAP_RV_STR_
#undef CC_SWAP_RV_FRAME
#undef CC_SWAP_RV_FP_FRAME
#undef CC_SWAP_RV_FP_SLOT
#undef CC_SWAP_RV_FST
#undef CC_SWAP_RV_FLD
#undef CC_SWAP_RV_E
#undef CC_SWAP_RV_GPR_FRAME
#undef CC_SWAP_RV_WORD
#undef CC_SWAP_RV_ST
#undef CC_SWAP_RV_LD

#else
#error "cc_swap: unsupported architecture"
#endif // supported cc_swap architectures

#endif // CC_CFG_TASK_STACKFUL

#endif // CC_SWAP_H
