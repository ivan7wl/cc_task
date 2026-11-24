// SPDX-License-Identifier: Apache-2.0

#include "cc_port.h"

// default port: no console, no reset, just trap
// a project that wants readable failures provides a strong cc_port_panic()

// current task and transient result of this thread
CC_PORT_THREAD_LOCAL cc_port_task_slot_t cc_port_task_slot_default;

CC_WEAK void cc_port_panic(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
    __builtin_trap();
}
