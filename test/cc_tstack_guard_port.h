#ifndef CC_TSTACK_GUARD_PORT_H
#define CC_TSTACK_GUARD_PORT_H

void cc_tstack_guard_panic(const char *file, int line, const char *expr);
#define CC_PORT_PANIC(e) cc_tstack_guard_panic(__FILE__, __LINE__, (e))

#endif
