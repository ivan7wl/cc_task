// A plain task cannot suspend as a stackful task. The panic hook exits with a
// distinctive status so the check target can distinguish a guard from a crash.
#include "cc_task.h"

#include <stdio.h>
#include <stdlib.h>

#define GUARD_EXIT 86

#if CC_CFG_TASK_STACKFUL

void cc_tstack_guard_panic(const char *file, int line, const char *expr)
{
    printf("cc_tstack_guard: panic at %s:%d: %s\n", file, line, expr);
    exit(GUARD_EXIT);
}

int main(void)
{
    cc_task_t plain;

    setvbuf(stdout, CC_NULL, _IONBF, 0);    // keep the trace visible when the guard fires
    CC_PORT_MEMSET(&plain, 0, sizeof(plain));
    CC_PORT_SET_CURRENT_TASK(&plain);       // a valid plain task reaches the stackful guard

    printf("cc_tstack_guard: calling cc_tstack_yield() outside a stackful task\n");
    (void)cc_tstack_yield();                // must panic instead of swapping to a garbage sp
    printf("cc_tstack_guard: guard missing, the call returned\n");
    return 0;
}

#else

int main(void)
{
    printf("cc_tstack_guard: CC_CFG_TASK_STACKFUL=0, nothing to check\n");
    return 0;
}

#endif
