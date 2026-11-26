// SPDX-License-Identifier: Apache-2.0

#ifndef CC_RUNNER_H
#define CC_RUNNER_H

// A runner runs several tasks concurrently as one awaitable task group.
// Resume it with cc_task_resume() to advance one task in the group at a time,
// or use CC_AWAIT(t, runner) to wait until all tasks end.

#include "cc_pp.h"
#include "cc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Return bytes required for n initial tasks, or 0 on failure. */
cc_size cc_runner_size(int n);

/** Initialize a retained runner on mem, with n variadic tasks.
 *  size must be >= cc_runner_size(n). Return NULL on failure. */
cc_task_t *cc_runner_init_n(void *mem, cc_size size, int n, ...);

/** Array form of cc_runner_init_n(). Return NULL on failure. */
cc_task_t *cc_runner_init_a(void *mem, cc_size size, int n, cc_task_t *const tasks[]);

#if CC_CFG_USE_MALLOC

/** Return a runner with n variadic tasks, or NULL on failure. */
cc_task_t *cc_runner_new_n(int n, ...);

/** Array form of cc_runner_new_n(). Return NULL on failure. */
cc_task_t *cc_runner_new_a(int n, cc_task_t *const tasks[]);

#if !defined(__cplusplus)
/** Convenience wrapper of cc_runner_new_n() for a variadic task list */
#define cc_runner_new(...) cc_runner_new_n(CC_NARG(__VA_ARGS__), ##__VA_ARGS__)
#endif

/** Add a task into runner. Return CC_FALSE on failure. */
cc_bool cc_runner_add(cc_task_t *runner, cc_task_t *task);

#endif // CC_CFG_USE_MALLOC

#ifdef __cplusplus
}
#endif

#endif // CC_RUNNER_H
