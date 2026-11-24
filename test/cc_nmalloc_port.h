#ifndef CC_NMALLOC_PORT_H
#define CC_NMALLOC_PORT_H

// No CC_PORT_MALLOC/CC_PORT_FREE: this build must not need an allocator.
#include "cc_types.h"

cc_port_task_slot_t *cc_nmalloc_task_slot(void);
CC_NORETURN void cc_nmalloc_panic(const char *file, int line, const char *expr);

#define CC_PORT_TASK_SLOT()     cc_nmalloc_task_slot()
#define CC_PORT_PANIC(e)        cc_nmalloc_panic(__FILE__, __LINE__, (e))

#endif // CC_NMALLOC_PORT_H
