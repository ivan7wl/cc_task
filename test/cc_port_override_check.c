// cc_port_override_check: CC_PORT_INCLUDE replaces the task slot accessor and
// the panic, so this binary is linked without cc_port.c at all

#include "cc_port_override.h" // our own port header, also forced in by -DCC_PORT_INCLUDE
#include "cc_task.h"

#include <stdio.h>
#include <stdlib.h>

static cc_port_task_slot_t s_slot;
static int                 s_slots;
static int                 s_panics;
static int                 s_allocs;
static int                 s_frees;

cc_port_task_slot_t *cc_port_override_slot(void)
{
    s_slots++;
    return &s_slot;
}

void *cc_port_override_malloc(cc_size n)
{
    void *p = malloc(n);
    if (p != CC_NULL)
    {
        s_allocs++;
    }
    return p;
}

void cc_port_override_free(void *p)
{
    s_frees++;
    free(p);
}

void cc_port_override_panic(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
    s_panics++;
}

int main(void)
{
    cc_task_t task;

    CC_PORT_MEMSET(&task, 0, sizeof(task));

    // cc_task_current() must go through CC_PORT_CURRENT_TASK() as well
    if (CC_PORT_CURRENT_TASK() != CC_NULL || CC_PORT_SET_CURRENT_TASK(&task) != CC_NULL ||
        CC_PORT_CURRENT_TASK() != &task || cc_task_current() != &task)
    {
        printf("cc_port_override: accessor FAILED\n");
        return 1;
    }

    CC_PORT_SET_TASK_RET(0x1234);
    if (CC_PORT_TASK_RET() != 0x1234 || cc_task_ret() != 0x1234 || s_slots != 7)
    {
        printf("cc_port_override: shared ret slot FAILED (slots=%d)\n", s_slots);
        return 1;
    }

    // a root heap task must free itself through the overridden allocator at end
    cc_task_t *t = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        CC_END(t, 0x42);
    });
    if (t == CC_NULL || cc_task_resume(&t) != cc_tr_end || t != CC_NULL ||
        cc_task_ret() != 0x42 || s_allocs != 1 || s_frees != 1)
    {
        printf("cc_port_override: root auto free FAILED (alloc=%d free=%d)\n", s_allocs, s_frees);
        return 1;
    }

#if !defined(CC_NDEBUG)
    CC_PORT_ASSERT(0);
    if (s_panics != 1)
    {
        printf("cc_port_override: default panic used (panics=%d)\n", s_panics);
        return 1;
    }
#endif

    printf("cc_port_override: task slot + panic + root auto free replaced, cc_swap not used\n");
    return 0;
}
