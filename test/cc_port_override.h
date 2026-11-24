#ifndef CC_PORT_OVERRIDE_H
#define CC_PORT_OVERRIDE_H

// port header for cc_port_override_check: current and ret live in a project
// slot instead of the default TLS variable, and the panic is a project function

#include "cc_types.h"

cc_port_task_slot_t *cc_port_override_slot(void);
void *cc_port_override_malloc(cc_size n);
void  cc_port_override_free(void *p);
void  cc_port_override_panic(const char *file, int line, const char *expr);

#define CC_PORT_TASK_SLOT()         cc_port_override_slot()
#define CC_PORT_MALLOC(n)           cc_port_override_malloc(n)
#define CC_PORT_FREE(p)             cc_port_override_free(p)
#define CC_PORT_PANIC(e)            cc_port_override_panic(__FILE__, __LINE__, (e))

#endif // CC_PORT_OVERRIDE_H
