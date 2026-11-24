#include "cc_port.h"

#include <stdio.h>

static int s_panics;

// strong symbol, overrides the weak default in cc_port.c
void cc_port_panic(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
    s_panics++;
}

int main(void)
{
#if CC_CFG_USE_MALLOC
    cc_u8 *p = (cc_u8 *)CC_PORT_MALLOC(16);

    if (p == CC_NULL)
    {
        return 1;
    }

    CC_PORT_MEMSET(p, 0x5A, 16);
    if (p[0] != 0x5A || p[15] != 0x5A)
    {
        return 1;
    }
    CC_PORT_FREE(p);
#endif

    cc_task_t task;
    if (CC_PORT_CURRENT_TASK() != CC_NULL || CC_PORT_SET_CURRENT_TASK(&task) != CC_NULL ||
        CC_PORT_CURRENT_TASK() != &task)
    {
        return 1;
    }

    CC_PORT_ASSERT(1);
    if (s_panics != 0)
    {
        return 1;
    }

#if defined(CC_NDEBUG)
    printf("cc_port: defaults ok, asserts compiled out (CC_NDEBUG)\n");
#else
    CC_PORT_ASSERT(0);
    if (s_panics != 1)
    {
        return 1;
    }
    printf("cc_port: defaults + panic hook ok\n");
#endif

    return 0;
}
