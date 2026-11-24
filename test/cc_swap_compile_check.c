#include "cc_swap.h"

#if CC_CFG_TASK_STACKFUL

// libc-free compile fixture: make cross uses -c so both naked functions are emitted.
static void compile_check_entry(void *arg)
{
    (void)arg;
    __builtin_trap();
}

void *cc_swap_compile_check_swap(void **save, void *load, void *arg)
{
    return cc_swap(save, load, arg);
}

void *cc_swap_compile_check_start(void **save, void *top, cc_size size)
{
    return cc_swap_start(save, top, size, compile_check_entry, save);
}

#endif // CC_CFG_TASK_STACKFUL
