# cc_task: C coroutines that fit in a single CPU cache line, with async/await and closures

**English** | [简体中文](README.zh-CN.md)

cc_task brings async/await-style coroutines and closures to C.

A stackless task body can be a closure (a clang block) or a plain function pointer; the state is kept in the closure, so it survives suspension points. A typical task is under 64 bytes — one CPU cache line.

Stackless and stackful coroutines are supported side by side and can be mixed in one program. Closure bodies need clang `-fblocks`, plain function bodies any compiler; dependencies are minimal, from common OSes down to bare metal.

It suits high-performance, highly concurrent, distributed, event-driven, and embedded applications.

# Example

```c
#include "cc_task.h"
#include <stdio.h>

int main(void)
{
    int c = 10; // task_b captures c and uses it as task state
    cc_task_t *task_b = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        printf("B: c = %d\n", ++$(c));
        CC_YIELD(t);
        printf("B: c = %d\n", ++$(c));
        CC_END(t, c);
    });

    cc_task_t *task_a = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        printf("A: await B ...\n");

        cc_uintptr code;
        CC_AWAIT(t, task_b, code);
        printf("A: B end, code = %llu\n", (unsigned long long)code);
        CC_END(t);
    });

    c = 0; // modifying c does not change the copy captured by task_b
    cc_task_t *t = task_a;
    cc_tr_e ret;
    do
    {
        ret = cc_task_resume(&t);
    }
    while (ret != cc_tr_end);

    return 0;
}
```

Output:

```text
A: await B ...
B: c = 11
B: c = 12
A: B end, code = 12
```

In this example, coroutine B captures variable `c` by copying its current value into a closure, so it can continue using `c` after `CC_YIELD()`. Setting `c` to 0 outside the coroutine later does not affect B's captured copy. Coroutine A awaits B's completion and obtains B's return value through `CC_END(t, c)`.

## TCP echo server / client

See `echo.c` in the `examples/` subdirectory for a single-threaded, highly concurrent TCP echo server/client implemented with cc_task, driven by `epoll` on Linux or `kqueue` on macOS (`make -C examples`, then `./examples/echo server 9000`). `echo.cpp` is the standard C++20 coroutine variant (built as `echo_cpp`), and `echo.js` is the corresponding JavaScript implementation using async/await.

### C and C++ resource comparison

Measured on Linux x86-64 with Clang 16 at `-O2`, using `bench` mode.

| Metric | C server | C++ server | C client | C++ client | C++ / C (server / client) |
|---|---:|---:|---:|---:|---:|
| Idle RSS | 960 KiB | 1,880 KiB | 960 KiB | 1,880 KiB | 1.96x / 1.96x |
| Added RSS per connection, 64 B | 4.68 KiB | 5.25 KiB | 0.67 KiB | 1.29 KiB | 1.12x / 1.93x |
| Added RSS per connection, 16 KiB | 16.49 KiB | 17.11 KiB | 16.59 KiB | 17.12 KiB | 1.04x / 1.03x |
| Total RSS at 1024 connections, 64 B | 5.62 MiB | 7.09 MiB | 1.61 MiB | 3.13 MiB | 1.26x / 1.94x |
| Total RSS at 1024 connections, 16 KiB | 17.43 MiB | 18.95 MiB | 17.53 MiB | 18.96 MiB | 1.09x / 1.08x |
| User-mode instructions per 64 B message | 401 | 593 | 439 | 979 | 1.48x / 2.23x |
| User-mode instructions per 16 KiB message | 721 | 778 | 518 | 1,166 | 1.08x / 2.25x |

# Features

* Supports both stackless and stackful coroutines.
* Task bodies can be C closures (clang blocks) or ordinary functions.
* Async/await-style programming with yield, wait, transfer, and optional return values (promise/future).
* Minimal memory overhead: a stackless task object occupies 32 bytes (LP64) or 20 bytes (ILP32), plus the captured variables.
* Supports throw/catch-style exception handling and external abort.
* Minimal dependencies: no standard library headers are required, and library functions depend only on facilities such as malloc, which can be replaced or trimmed.

# Supported Platforms

Stackful coroutines use architecture-specific assembly to switch contexts. The target platform is detected automatically at compile time. The following platforms are currently supported:

* x86_64: SysV (Linux, macOS, BSD), Windows x64; CET shadow stacks are not supported.
* i386: SysV cdecl (Linux, BSD), Windows i386; CET shadow stacks are not supported.
* AArch64: AAPCS64 (Linux, Android, macOS, bare metal); Arm GCS is not supported.
* ARM32: AAPCS (Linux, Android, bare metal), in both ARM and Thumb-2 modes.
* Thumb-1: Cortex-M0 / M0+ / M23 (bare metal).
* RISC-V: RV64, RV32, RV32E (Linux, bare metal).

Set `CC_CFG_TASK_STACKFUL=0` to disable stackful coroutines and support all platforms. Stackless coroutines require only LP64 or ILP32 (including Windows LLP64).

# Getting Started

Place the `cc_task/` subdirectory in your project and include `cc_task.h`.

```c
#include "cc_task.h"
```

## Build Configuration

Clang is recommended because it supports blocks (`-fblocks`).

```shell
clang -std=gnu11 -O2 -fblocks -Icc_task -Wuninitialized -Werror=uninitialized \
    -Wconditional-uninitialized -Werror=conditional-uninitialized your_app.c \
    cc_task/cc_task.c cc_task/cc_task_conv.c cc_task/cc_task_stackful.c cc_task/cc_port.c
```

With GCC or another compiler, use `-DCC_CFG_TASK_BLOCK=0` to disable closures, or modify `cc_config.h`.

```shell
gcc -std=gnu11 -O2 -DCC_CFG_TASK_BLOCK=0 -Icc_task -Wuninitialized -Werror=uninitialized \
    -Wconditional-uninitialized -Werror=conditional-uninitialized your_app.c \
    cc_task/cc_task.c cc_task/cc_task_conv.c cc_task/cc_task_stackful.c cc_task/cc_port.c
```

See ***Platform Porting and Feature Trimming*** for details.

To catch accidental use of stack variables across suspension points, add `-Wuninitialized` and `-Wconditional-uninitialized`, together with their corresponding `-Werror` options.

```c
CC_BEGIN(t);    // on resume, jump to the last resume point; n = 10 may be skipped
int n = 10;
CC_YIELD(t);    // save the resume point and return; stack variable n is discarded
some_use_of(n); // using n after resume is a compiler error
CC_END(t);
```

## Choosing a Task Type

| Scenario | Recommended interface |
| --- | --- |
| Want simple code and automatic variable capture | `CC_TASK` closure task |
| Cannot use clang blocks, or need to manage context explicitly | `cc_tconv` conventional task |
| Need to suspend inside an ordinary function call chain | `cc_tstack` stackful task |

## Creating Tasks

All `*_new()` functions also have `*_init()` variants that use caller-provided memory. Tasks created by `*_new()` are released automatically when they end; tasks created by `*_init()` are retained and are not released by the library, allowing custom allocation and deallocation.

If dynamic allocation is unavailable, disable `CC_CFG_USE_MALLOC` and use only the `init` interfaces.

## Running Tasks

Use `cc_task_resume(&t)` to drive task `*t`. The return value reports the result of this run. `cc_tr_end` means the task chain has ended with no continuation, and `*t` is then `NULL`. On `cc_tr_yield` or `cc_tr_wait`, calling it again resumes from the saved point.

```c
CC_INLINE cc_tr_e cc_task_resume(CC_INOUT cc_task_t **t);
```

The `t` parameter is a pointer-to-pointer used for both input and output. Tasks form chains and may transfer or await other tasks while running, so the next task to drive may no longer be the original one.

## Task Return Values

`CC_EXIT(t[, ret])` and `CC_END(t[, ret])` accept an optional completion value, providing promise/future-like semantics.

The third argument of `CC_AWAIT(t, child[, var])` is optional. When `child` ends, its completion value is assigned to `var`.

`cc_task_ret()` returns the most recently completed task's value on the current thread (thread-local).

## Important Notes

* Plain stack variables cannot be preserved across suspension points. Enable `-Wuninitialized` and `-Wconditional-uninitialized` when compiling.
* Store state that must survive suspension in captured variables or in a conventional task's `ctx`. `__block` variables and blocks cannot be captured.
* Arrays cannot be captured directly. Put the array in a struct and capture the struct instead.
* Captured variables are read-only. Use `$()` for writes, for example `for ($(i) = 0; i != 10; $(i)++)`.
* A capture stores the variable's **current value**. Later changes outside the closure do not affect its captured copy.
* Stackless resumption is implemented with switch-case jumps, so do not write another switch-case between `CC_BEGIN()` and `CC_END()`.
* Await and transfer are fundamentally tail calls. Use at least `-O1` to ensure tail-call optimization; otherwise, task transfers may overflow the stack.

# Development Reference Manual

## Data Structures

### `cc_tr_e`

The value returned each time a task is **resumed**, indicating its state.

```c
typedef enum _cc_tr_e
{
    cc_tr_end   = 0,    // task finished, including after an abort
    cc_tr_yield = 1,    // task yielded, resumable
    cc_tr_wait  = 2,    // task wants to be resumed later
    cc_tr_trans = 3,    // used for stackful task trans
    cc_tr_error = 4,    // operation rejected, the task is unchanged
} cc_tr_e;
```

| Value | Meaning |
|---|---|
| `cc_tr_end` | Task chain ended (end or abort), with no continuation. `*t` is `NULL` when `cc_task_resume()` returns this value. |
| `cc_tr_yield` | Voluntarily yielded; can be resumed. |
| `cc_tr_wait` | Suspended; the caller decides when to resume. |
| `cc_tr_trans` | Used internally during stackful transfers and normally invisible to external callers. |
| `cc_tr_error` | Operation rejected and task left unchanged, such as invalid `exec` arguments. |

### `cc_task_t`

Represents a task object: a stackless closure task, a conventional function-pointer task, or a stackful task. Its header follows the clang block-literal layout, allowing a closure task to be invoked directly as a block, with captured variables in the trailing `ctx`. The header is 32 bytes on LP64 and 20 bytes on ILP32.

```c
typedef struct _cc_task_t
{
    struct _cc_task_t *next;    // parent task, resume when ends
    cc_lc_t    lc;              // continuation label
    cc_u16     ctx_size;        // allocated ctx capacity in bytes
    cc_u32     flags;           // cc_tf_e
    void     (*fn)(void);       // block invoke slot, never called directly
    void       *ext;            // extension context, preserves the block descriptor slot
    cc_u8      ctx[];           // user/captured ctx storage
} cc_task_t;
```

| Field | Purpose |
|---|---|
| `next` | Continuation resumed when this task ends. The chain ends if there is no continuation. |
| `lc` | Resume point. `0` is the entry, `CC_LC_DONE` (`0xFFFF`) means ended, and `CC_LC_ABORT` (`0xFFFE`) means abort requested. |
| `ctx_size` | Size of `ctx` in bytes, up to 65535: captured-variable storage or a conventional task's explicit context size. |
| `flags` | `cc_tf_abort`: abort requested. `cc_tf_retain`: caller-provided memory, not freed on completion. |
| `fn` | Reserved for the block invoke slot; callers must not write it directly. |
| `ext` | Generic extension context; NULL unless a library extension owns it. |
| `ctx` | Flexible array of `ctx_size` bytes holding captures or conventional-task context. Use `CC_CTX(t, type)` for conventional tasks. |

### `cc_lc_t`

The resume-point type for stackless tasks. Like *protothreads*, it implements local continuations (lc) with `switch` / `case`: `CC_LC_RESUME()` jumps through a switch to the case identified by lc.

Protothreads store source line numbers in lc. Their main problem is that these values are **not consecutive** and may be widely dispersed, making compiler optimization more difficult. They are also constrained by source location: line numbers above 65535 do not fit in a 16-bit container.

`cc_lc_t` instead uses consecutive `__COUNTER__` values. Each function has its own starting point, and lc stores the offset from that point. Thus every function numbers its cases independently from `0` (the entry), and each case label is one greater than the previous one, regardless of its location in the file. In 16 bits, `0xFFFE` (`CC_LC_ABORT`) and `0xFFFF` (`CC_LC_DONE`) are reserved, leaving values through `0xFFFD`; overflow is checked at compile time. In practice, a function rarely has more than 255 coroutine states, so the lc container could even be reduced to 8 bits.

### `cc_port_task_slot_t`

A per-thread slot containing both the current task and the most recent completion value, using thread-local storage:

```c
typedef struct _cc_port_task_slot_t
{
    cc_task_t *current;    // task entered most recently on this thread
    cc_uintptr ret;        // last end value saved on this thread
} cc_port_task_slot_t;

CC_PORT_THREAD_LOCAL cc_port_task_slot_t cc_port_task_slot_default;
```

`CC_PORT_THREAD_LOCAL` is `_Thread_local` in C and `thread_local` in C++. Each thread has its own slot, so one thread's `cc_task_resume()` / `cc_task_ret()` does not observe another thread's task or completion value.

For environments without thread-local storage, define `CC_PORT_THREAD_LOCAL` as empty and override `CC_PORT_TASK_SLOT()` and the related APIs.

| Field | Purpose |
|---|---|
| `current` | Most recently entered task, obtained with `cc_task_current()`. This value is transient and is overwritten whenever a task is resumed. |
| `ret` | Optional completion value set by `CC_END(t, value)`, `CC_EXIT(t, value)`, or `cc_tstack_exit(value)`, obtained with `cc_task_ret()`. This value is transient and is overwritten whenever a task ends. |

## Main Interfaces

Include `cc_task.h` to use this section's interfaces. Closure-related interfaces are declared when `CC_CFG_TASK_BLOCK=1` and require clang `-fblocks`. `cc_task_new()` also requires `CC_CFG_USE_MALLOC=1`.

See ***Control Primitives***, ***Conventional Tasks***, and ***Stackful Tasks*** in the following sections.

### `CC_TASK`

```c
#define CC_TASK(t)    ^cc_tr_e (cc_task_t *t, void *_)
```

Constructs a closure task body for `cc_task_new()`, `cc_task_init()`, or `cc_task_exec()`. `t` is the current task, and `_` is reserved.

`CC_BEGIN(t)` does not have to be the first line of the body. Local variables defined before it are declared and initialized on every entry before jumping to the resume point, making them usable at every resume point. These assignments run on every entry. Resumable code belongs between `CC_BEGIN(t)` and `CC_END(t)`.

Closures capture outer variables by value. They cannot capture `__block` variables, other blocks, or arrays directly. To capture an array, put it in a struct and capture the struct variable.

```c
int step = 0; // captured; changes survive resume points
cc_task_t *task = cc_task_new(CC_TASK(t)
{
    int base = 10; // assigned on every entry, usable at every resume point

    CC_BEGIN(t);
    printf("%d %d\n", base, ++$(step)); // 10 1
    CC_YIELD(t);
    printf("%d %d\n", base, ++$(step)); // 10 2
    CC_END(t);
});
```

### `$(var)`

```c
#define $(var)    (*(__typeof__(&(var)))(&(var)))
```

Writes a captured variable in a closure. Captures themselves are read-only: read them directly by name, and use `$()` only for writes, for example `for ($(i) = 0; i != 10; $(i)++)`.

### `cc_task_size`

```c
cc_size cc_task_size(cc_task_func blk);
```

Returns the bytes required to store closure task `blk`. Returns `0` for unsupported captures (`__block` or block) or a capture area larger than 65535 bytes.

The `size` passed to `cc_task_init()` must not be smaller than this value.

### `cc_task_new`

```c
cc_task_t *cc_task_new(cc_task_func blk);
```

Allocates and returns a new task without running it immediately. Returns `NULL` on failure, for the same reasons as `cc_task_size()` returning 0 or when `CC_PORT_MALLOC()` fails.

The library automatically releases task memory when the task's lifecycle ends.

### `cc_task_init`

```c
cc_task_t *cc_task_init(void *mem, cc_size size, cc_task_func blk);
```

Initializes a task in caller-provided `mem` without running it immediately. On success, the return value is `mem`. Returns `NULL` without modifying `mem` if `size` is smaller than `cc_task_size(blk)` or the captures are unsupported or oversized.

`mem` must remain valid throughout the task's use and be aligned for `cc_task_t`. The caller fully owns this memory; it is not released when the task ends.

### `cc_task_exec`

```c
cc_tr_e cc_task_exec(cc_task_t *t, cc_task_func blk);
```

Replaces the closure body in `t` and runs it immediately, returning the new body's `cc_tr_e`. No new task object is allocated; `t`'s existing storage is reused.

Returns `cc_tr_error` without modifying `t` in these cases:

- `t` is stackful.
- Captures are unsupported or exceed 65535 bytes.
- The new capture area exceeds `t`'s existing ctx capacity.

### `cc_task_resume`

```c
cc_tr_e cc_task_resume(cc_task_t **t);
```

The coroutine **resume** operation drives `*t`: the first call enters at the task entry, and subsequent calls continue from the latest resume point until the task suspends (yield or wait) or the chain ends.

The return value is the run's `cc_tr_e` state. `cc_tr_end` means the chain has ended with no continuation, and `*t` is `NULL`. With a continuation, `cc_tr_end` is not returned to the caller; `next` runs instead. `cc_tr_yield` or `cc_tr_wait` means the task remains at a resume point and can continue from there on the next call.

`t` is a pointer-to-pointer used for both input and output. After transfer or await, `*t` is updated to the task that should be driven next. Do not pass `NULL` or a pointer to an automatically freed task. This return value is not the completion value saved by `CC_END()`.

### `cc_task_abort`

```c
void cc_task_abort(cc_task_t *t);
```

Marks a suspended, unfinished task so that its next `cc_task_resume()` jumps to the cleanup path at `CC_CATCH():`. It has no effect on an ended task (`lc` is done). If the body has no `CC_CATCH():`, the task simply ends.

Stackless tasks should abort themselves with `CC_THROW_ON()`, not by calling `cc_task_abort()` from within a running body.

Stackful tasks have no `CC_CATCH():`. After a suspended stackful task is marked, its suspension point `cc_tstack_yield()`, `cc_tstack_wait()`, or `cc_tstack_trans()` returns `CC_FALSE` on the next resume. The body can then detect the abort and clean up, for example with `goto cleanup;`. A stackful task can abort itself without `cc_task_abort()` by jumping directly to cleanup and returning `cc_tr_end`.

### `cc_task_ret`

```c
cc_uintptr cc_task_ret(void);
```

Returns the most recently saved task completion value on this thread. Tasks set it through `CC_END(t[, value])` and `CC_EXIT(t[, value])`. The optional `value` leaves it unchanged when omitted, representing a `void` completion. Like `errno`, read `cc_task_ret()` immediately after the task ends.

### `cc_task_current`

```c
cc_task_t *cc_task_current(void);
```

Returns the task most recently run on this thread, or `NULL` if no task has run or a task ended with no continuation.

### `cc_task_ended`

```c
cc_bool cc_task_ended(cc_task_t *t);
```

Returns `CC_TRUE` if `t` has ended, otherwise `CC_FALSE`. Do not pass a pointer that has been automatically freed. A pointer to a task created by `cc_task_init()` remains valid and can be queried after completion.

### `cc_task_aborted`

```c
cc_bool cc_task_aborted(cc_task_t *t);
```

Returns `CC_TRUE` if abort has been requested for `t`, otherwise `CC_FALSE`. This flag is cleared after `cc_task_exec()` successfully replaces the task body.

### `CC_TASK_SIZE`

```c
#define CC_TASK_SIZE(n)    (CC_OFFSETOF(cc_task_t, ctx) + (cc_size)(n))
```

Computes the size of a task object with `n` bytes of captures or user ctx. `n` is the ctx size in bytes, not the total task length.

### `CC_CTX`

```c
#define CC_CTX(t, type)    ((type *)(t)->ctx)
```

Casts a task's `ctx` to `type *`. Conventional tasks use it to access their context. Alignment is 8 bytes on LP64 and 4 bytes on ILP32.

## Control Primitives

Control primitives are coroutine operations defined as macros in `cc_primitive.h`. They can be used in both closure and conventional stackless task bodies. Stackful coroutines use the `cc_tstack_*` function interfaces instead.

### `CC_BEGIN`

```c
CC_BEGIN(t);
```

Starts a coroutine body and jumps to the previously saved resume point. See `CC_TASK()` for an example.

`CC_BEGIN()` is fundamentally a switch-case jump, so do not write another switch-case between `CC_BEGIN()` and `CC_END()`.

### `CC_END`

```c
CC_END(t);
CC_END(t, value);
```

Ends a coroutine body. The optional `value` is the task's completion value, or `void` when omitted. Read it with `cc_task_ret()`.

If a continuation exists after `CC_END()`, it is tail-called (see ***Important Notes***); otherwise, `cc_tr_end` is returned to the caller of `cc_task_resume()`.

### `CC_EXIT`

```c
CC_EXIT(t);
CC_EXIT(t, value);
```

Ends immediately from anywhere in the task body. Completion values follow the same rules as `CC_END`.

### `CC_YIELD`

```c
CC_YIELD(t);
```

Saves the resume point and returns `cc_tr_yield` to `cc_task_resume()`. The next `cc_task_resume()` continues from that point. Use it to voluntarily yield a time slice so other tasks can run first.

### `CC_WAIT`

```c
CC_WAIT(t);
```

Similar to `CC_YIELD()`, but returns `cc_tr_wait`. For asynchronous waiting, a coroutine submits an operation and suspends; the caller wakes it with `cc_task_resume()` when the operation completes.

```c
CC_BEGIN(t);
uart_tx_start(buf, len);     // submit io
CC_WAIT(t);                  // on tx done, call cc_task_resume() to wake this task
CC_END(t, uart_tx_nbytes());
```

### `CC_YIELD_UNTIL`

```c
CC_YIELD_UNTIL(t, cond);
```

Calls `CC_YIELD()` while `cond` is false. Useful for non-blocking polling of a condition.

### `CC_YIELD_WHILE`

```c
CC_YIELD_WHILE(t, cond);
```

Calls `CC_YIELD()` while `cond` is true.

### `CC_TRANS`

```c
CC_TRANS(t, next);
```

Symmetric coroutine transfer: suspends the current task, saves its resume point, and transfers to `next`.

The transfer directly tail-calls `next` instead of returning to the caller of `cc_task_resume()`; see ***Important Notes***.

### `CC_AWAIT`

```c
CC_AWAIT(t, child);
CC_AWAIT(t, child, var);
```

Coroutine await: saves the current task's resume point and transfers to `child`. When `child`'s lifecycle ends, control transfers back to the current task.

The optional `var` receives `child`'s completion value.

If `child` ends through abort, its parent is aborted as well.

The transfer directly tail-calls `next` instead of returning to the caller of `cc_task_resume()`; see ***Important Notes***.

```c
cc_uintptr got;
CC_AWAIT(t, cc_task_new(CC_TASK(t)
{
    CC_BEGIN(t);
    CC_YIELD_UNTIL(t, uart_rx_ready());
    CC_END(t, uart_rx_byte());
}), got);
```

### `CC_THROW_ON`

```c
CC_THROW_ON(t, cond);
```

Throws an "exception": when `cond` is true, the current stackless task aborts itself and jumps to `CC_CATCH():`. Without `CC_CATCH():`, it simply ends. See `cc_task_abort()`.

### `CC_CATCH`

```c
CC_CATCH(t):
CC_CATCH(t, value):
```

Catches an "exception". Place it before `CC_END`, and do not omit the colon.

Reaching this point through normal control flow ends the task; the optional `value` is then the normal completion value. Code after `CC_CATCH():` handles abort. See `cc_task_abort()`.

```c
    CC_BEGIN(t);
    CC_YIELD_UNTIL(t, uart_tx_done()); // yield while hardware busy
CC_CATCH(t, uart_tx_nbytes()):         // transfer done, end with tx bytes
    uart_tx_cancel();                  // someone abort transfer
    CC_END(t, (cc_uintptr)-1);         // return aborted to caller
```

## Conventional Tasks

Conventional tasks use ordinary functions as bodies and are more portable; clang `-fblocks` is not required. State preserved across resume points lives in `ctx`, accessed with `CC_CTX(t, type)`. They use the same control primitives as closure tasks. Their interfaces are declared when `CC_CFG_TASK_CONV=1`, and `cc_tconv_new()` also requires `CC_CFG_USE_MALLOC=1`.

```c
typedef cc_tr_e (*cc_tconv_func)(cc_task_t *t, void *_, void *ctx);
```

Conventional and closure tasks share the same underlying model: a closure can be viewed as a `cc_tconv_func` function pointer plus a `ctx` of `ctx_size` bytes as task context.

Unlike automatic closure captures, conventional tasks require explicit ctx management. This example is equivalent to the one in the `CC_TASK` section.

```c
typedef struct
{
    int step;
} step_ctx;

static cc_tr_e step_body(cc_task_t *t, void *_, void *ctx)
{
    step_ctx *c = CC_CTX(t, step_ctx);
    int base = 10; // assigned on every entry, usable at every resume point

    CC_BEGIN(t);
    printf("%d %d\n", base, ++c->step); // 10 1
    CC_YIELD(t);
    printf("%d %d\n", base, ++c->step); // 10 2
    CC_END(t);
}

static cc_task_t *start_step(void)
{
    step_ctx st = { 0 };
    return cc_tconv_new(step_body, &st, sizeof(st));
}
```

### `cc_tconv_new`

```c
cc_task_t *cc_tconv_new(cc_tconv_func func, const void *ctx, cc_size ctx_size);
```

Allocates and returns a new task without running it immediately. If `ctx != NULL`, copies `ctx_size` bytes into the task; otherwise, zero-fills that area. Returns `NULL` if `ctx_size` exceeds 65535 or `CC_PORT_MALLOC()` fails.

The library automatically releases task memory when the task's lifecycle ends.

### `cc_tconv_init`

```c
cc_task_t *cc_tconv_init(void *mem, cc_size size, cc_tconv_func func,
                         const void *ctx, cc_size ctx_size);
```

Initializes a task in caller-provided `mem` without running it immediately. On success, the return value is `mem`. Context copying and zero-filling follow `cc_tconv_new()`. Returns `NULL` without modifying `mem` if `ctx_size` exceeds 65535 or `size` is smaller than `CC_TASK_SIZE(ctx_size)`.

`mem` must remain valid throughout the task's use and be aligned for `cc_task_t`. The caller fully owns this memory; it is not released when the task ends.

### `cc_tconv_exec`

```c
cc_tr_e cc_tconv_exec(cc_task_t *t, cc_tconv_func func,
                      const void *ctx, cc_size ctx_size);
```

Replaces the function in `t` and runs it immediately, returning the new body's `cc_tr_e`. No new task object is allocated.

`ctx_size == 0` preserves the existing ctx. A nonzero size copies or zero-fills as described above but must not exceed `t`'s current ctx capacity. Replacement resets the resume point and clears the abort flag. `cc_tf_retain` and continuation links are preserved, and ctx capacity does not shrink.

Returns `cc_tr_error` without modifying `t` if `t` is stackful or `ctx_size` exceeds its current capacity.

## Stackful Tasks

Stackful tasks are suitable when suspension is needed inside an ordinary function call chain. Local variables and nested call frames are preserved on an independent stack. Do not use stackless macros such as `CC_BEGIN()`, `CC_YIELD()`, or `CC_AWAIT()` in a stackful body. A body can return only `cc_tr_end`. Execution is still driven by `cc_task_resume()`.

Interfaces are declared when `CC_CFG_TASK_STACKFUL=1`. Closure variants also require `CC_CFG_TASK_BLOCK=1`. `cc_tstack_new()` and `cc_tstack_conv_new()` additionally require `CC_CFG_USE_MALLOC=1`. With `CC_CFG_TASK_STACKFUL=0`, `cc_tstack_is_stackful()` remains available and always returns `CC_FALSE`. See ***Supported Platforms*** for supported architectures.

A suspension API returning `CC_FALSE` means the task has been aborted. Clean up resources and return `cc_tr_end`; do not suspend again. A stackful task can abort itself by proceeding directly to cleanup and returning `cc_tr_end`; see `cc_task_abort()`.

Stackful coroutines can suspend in nested functions at any call depth, avoiding the async/await propagation requirement of stackless coroutines. This example is equivalent to the one in ***Example***, but `func_b` is an ordinary function that can suspend without becoming a child coroutine.

```c
static int func_b(int *c)
{
    printf("B: c = %d\n", ++*c);
    if (!cc_tstack_yield())
    {
        cc_tstack_exit((cc_uintptr)-1);
    }
    printf("B: c = %d\n", ++*c);
    return *c;
}

int main(void)
{
    int c = 10; // copied into the task and used as task state
    cc_task_t *task_a = cc_tstack_new(4096, CC_TASK(t)
    {
        int state = c;
        int code;

        CC_UNUSED(_);
        printf("A: call B ...\n");
        code = func_b(&state);
        printf("A: B return, code = %d\n", code);
        return cc_tr_end;
    });

    c = 0; // modifying c does not change the copy in task_a
    cc_task_t *cur = task_a;
    cc_tr_e ret;
    do
    {
        ret = cc_task_resume(&cur);
    }
    while (ret != cc_tr_end);

    return 0;
}
```

Output:

```text
A: call B ...
B: c = 11
B: c = 12
A: B return, code = 12
```

### `cc_tstack_is_stackful`

```c
cc_bool cc_tstack_is_stackful(const cc_task_t *t);
```

Returns `CC_TRUE` if `t` is stackful, otherwise `CC_FALSE`. Do not pass `NULL` when stackful support is enabled.

### `cc_tstack_size`

```c
cc_size cc_tstack_size(cc_size stack_size, cc_task_func blk);
```

Returns the bytes required for a closure stackful task, including the task header, capture area, and `stack_size` bytes of stack. Returns `0` for unsupported captures (`__block` or block), captures larger than 65535 bytes, zero `stack_size`, or total-size overflow.

The `size` passed to `cc_tstack_init()` must not be smaller than this value.

### `cc_tstack_new`

```c
cc_task_t *cc_tstack_new(cc_size stack_size, cc_task_func blk);
```

Allocates and returns a new task without running it immediately. Returns `NULL` on failure, for the same reasons as `cc_tstack_size()` returning 0 or when `CC_PORT_MALLOC()` fails.

The library automatically releases task memory when the task's lifecycle ends. Closure capture rules are the same as for `cc_task_new()`.

### `cc_tstack_init`

```c
cc_task_t *cc_tstack_init(void *mem, cc_size size, cc_task_func blk);
```

Initializes a task in caller-provided `mem` without running it immediately. On success, the return value is `mem`. `mem` must be 8-byte aligned. Returns `NULL` without modifying `mem` if `size` is smaller than `cc_tstack_size()` or the captures are unsupported or oversized.

`mem` must remain valid throughout the task's use. The caller fully owns this memory; it is not released when the task ends.

### `cc_tstack_exec`

```c
cc_tr_e cc_tstack_exec(cc_task_t *t, cc_task_func blk);
```

Replaces the closure body in `t` and runs it immediately, returning the new body's `cc_tr_e`. No new task object is allocated; `t`'s existing storage and stack capacity are reused.

Returns `cc_tr_error` without modifying `t` in these cases:

- `t` is not stackful or is currently running.
- Captures are unsupported or exceed 65535 bytes.
- The new capture area exceeds `t`'s existing ctx capacity.

### `cc_tstack_conv_new`

```c
cc_task_t *cc_tstack_conv_new(cc_tconv_func func, const void *ctx,
                              cc_size ctx_size, cc_size stack_size);
```

Allocates and returns a new task without running it immediately. If `ctx != NULL`, copies `ctx_size` bytes into the task; otherwise, zero-fills that area. Returns `NULL` if `ctx_size` exceeds 65535, `stack_size` is zero, the total size overflows, or `CC_PORT_MALLOC()` fails.

The library automatically releases task memory when the task's lifecycle ends.

### `cc_tstack_conv_init`

```c
cc_task_t *cc_tstack_conv_init(void *mem, cc_size size, cc_tconv_func func,
                               const void *ctx, cc_size ctx_size);
```

Initializes a task in caller-provided `mem` without running it immediately. On success, the return value is `mem`. Context copying and zero-filling follow `cc_tconv_new()`. `mem` must be 8-byte aligned. Returns `NULL` without modifying `mem` if `ctx_size` exceeds 65535 or `size` cannot hold the task, ctx, and stack.

`mem` must remain valid throughout the task's use. The caller fully owns this memory; it is not released when the task ends.

### `cc_tstack_conv_exec`

```c
cc_tr_e cc_tstack_conv_exec(cc_task_t *t, cc_tconv_func func,
                            const void *ctx, cc_size ctx_size);
```

Replaces the function in `t` and runs it immediately, returning the new body's `cc_tr_e`. No new task object is allocated; `t`'s existing storage and stack capacity are reused.

`ctx_size == 0` preserves the existing ctx. A nonzero size copies or zero-fills as described above but must not exceed `t`'s current ctx capacity. Replacement resets the resume point and clears the abort flag. `cc_tf_retain` and continuation links are preserved, and ctx capacity does not shrink.

Returns `cc_tr_error` without modifying `t` if `t` is not stackful, is currently running, or `ctx_size` exceeds its current capacity.

### `cc_tstack_yield`

```c
cc_bool cc_tstack_yield(void);
```

Saves the resume point and returns `cc_tr_yield` to `cc_task_resume()`. The next `cc_task_resume()` continues from that point. Use it to voluntarily yield a time slice so other tasks can run first. It can be used in nested functions.

Returns `CC_TRUE` on normal resume and `CC_FALSE` if the task was aborted.

### `cc_tstack_wait`

```c
cc_bool cc_tstack_wait(void);
```

Similar to `cc_tstack_yield()`, but returns `cc_tr_wait`. For asynchronous waiting, a coroutine submits an operation and suspends; the caller wakes it with `cc_task_resume()` when the operation completes.

```c
uart_tx_start(buf, len); // submit io
if (!cc_tstack_wait())   // on tx done, call cc_task_resume() to wake this task
{
    uart_tx_cancel();               // someone abort transfer
    cc_tstack_exit((cc_uintptr)-1); // return aborted to caller
}
cc_tstack_exit(uart_tx_nbytes());
```

### `cc_tstack_yield_until`

```c
cc_bool cc_tstack_yield_until(cond);
```

Calls `cc_tstack_yield()` while `cond` is false, making it useful for non-blocking polling. Returns `CC_TRUE` when `cond` becomes true and `CC_FALSE` on abort.

### `cc_tstack_yield_while`

```c
cc_bool cc_tstack_yield_while(cond);
```

Calls `cc_tstack_yield()` while `cond` is true. Returns `CC_TRUE` when `cond` becomes false and `CC_FALSE` on abort.

### `cc_tstack_trans`

```c
cc_bool cc_tstack_trans(cc_task_t *next);
```

Symmetric coroutine transfer: suspends and saves the current task's resume point, then transfers to `next`.

Returns `CC_TRUE` on normal resume and `CC_FALSE` on abort. If `next` is also stackful, it must not be running or already ended.

### `cc_tstack_exit`

```c
void cc_tstack_exit(cc_uintptr ret);
```

Ends immediately at any call depth. `ret` follows the same completion-value rules as `CC_END(t, value)` and can be read with `cc_task_ret()`. This function does not return, and skipped C frames are not unwound.

### `cc_tstack_eh`

```c
cc_tstack_eh(op, label);
```

Evaluates `op` once. If its result is `CC_FALSE`, executes `goto label`.

```c
cc_tstack_eh(cc_tstack_wait(), cleanup);
cleanup:
    return cc_tr_end;
```

### `cc_tstack_canary_hit`

```c
cc_bool cc_tstack_canary_hit(cc_task_t *t);
```

Returns `CC_TRUE` if the 16-byte canary at the bottom of the coroutine stack has been overwritten, otherwise `CC_FALSE`. This only detects that stack usage has reached the bottom region and does not replace full stack-overflow protection.

## Platform Porting and Feature Trimming

### `cc_config.h`

Feature switches default to `1` and must be either `0` or `1`. Override them at compile time with `-D`, for example `-DCC_CFG_TASK_STACKFUL=0`.

`CC_CONFIG_INCLUDE` can include a project-provided configuration file, for example `-DCC_CONFIG_INCLUDE='"my_config.h"'`.

| Macro | Effect when set to `0` |
|---|---|
| `CC_CFG_USE_MALLOC` | No `new`; only `init` remains. malloc/free are not required |
| `CC_CFG_TASK_BLOCK` | No closure tasks |
| `CC_CFG_TASK_CONV` | No conventional tasks |
| `CC_CFG_TASK_STACKFUL` | No stackful tasks. `cc_tstack_is_stackful()` remains available and always returns `CC_FALSE`; `cc_swap.h` is empty |

`CC_CFG_TASK_BLOCK=1` requires clang `-fblocks`; compilation fails when `__BLOCKS__` is unavailable.

With `CC_CFG_TASK_STACKFUL=0`, no architecture assembly is used. Stackless coroutines require only LP64 or ILP32 (including Windows LLP64). When enabled, supported architectures are listed under ***Supported Platforms***.

### `cc_port.h`

All platform services used by the library go through these macros. Defaults are the C runtime's `malloc` and `free`, plus the compiler's `__builtin_memcpy` and `__builtin_memset`. Failure traps.

Individual macros can be overridden with `-D`, or a project port file can be included with `-DCC_PORT_INCLUDE='"my_port.h"'`.

| Macro | Default | Purpose |
|---|---|---|
| `CC_PORT_MALLOC(n)` | `malloc(n)` | Allocates memory. Used only with `CC_CFG_USE_MALLOC=1` |
| `CC_PORT_FREE(p)` | `free(p)` | Frees memory |
| `CC_PORT_MEMCPY(d, s, n)` | `__builtin_memcpy` | Copies memory |
| `CC_PORT_MEMSET(d, c, n)` | `__builtin_memset` | Zero-fills memory |
| `CC_PORT_THREAD_LOCAL` | `_Thread_local` in C, `thread_local` in C++ | Storage class for the task slot |
| `CC_PORT_TASK_SLOT()` | Default slot `cc_port_task_slot_default` | Per-thread task slot |
| `CC_PORT_CURRENT_TASK()` | `current` in the slot | Returns the current task |
| `CC_PORT_SET_CURRENT_TASK(t)` | Writes `current` and returns the previous value | Sets the current task |
| `CC_PORT_TASK_RET()` | `ret` in the slot | Most recent completion value |
| `CC_PORT_SET_TASK_RET(ret)` | Writes `ret` | Sets the completion value |
| `CC_PORT_PANIC(e)` | `cc_port_panic(__FILE__, __LINE__, (e))` | Failure exit with file and line information |
| `CC_PORT_ASSERT(c)` | Calls `CC_PORT_PANIC` if the condition is false | Empty when `-DCC_NDEBUG` is defined |

The slot fields are defined by `cc_port_task_slot_t`. In environments without thread-local storage, define `CC_PORT_THREAD_LOCAL` as empty and override `CC_PORT_TASK_SLOT()` and the related APIs. If only `CC_PORT_TASK_SLOT()` is replaced, `current` and `ret` both use the new slot.

`cc_port.c` provides the default slot and a weak `cc_port_panic()` that prints nothing and immediately executes `__builtin_trap()`. A project can provide a strong function with the same name that prints the location and then resets. If both `CC_PORT_TASK_SLOT()` and `CC_PORT_PANIC()` are replaced, `cc_port.c` does not need to be linked.

### `cc_swap.h`

When `CC_CFG_TASK_STACKFUL=1`, this header expands for the compilation target. Stackful tasks use it internally to switch contexts without calling the operating system. It is empty when `CC_CFG_TASK_STACKFUL=0`. See ***Supported Platforms*** for the architecture list.

`cc_swap(save, load, arg)` saves the callee-saved registers to the current stack, writes the stack pointer to `*save`, and then resumes the `load` context. `arg` is the value returned by this swap. `cc_swap_start(save, top, size, entry, arg)` also saves the current context and then enters `entry(arg)` on the new stack. `top` is 16-byte aligned, and the stack range is `[top - size, top)`. `entry` must not return. On Windows, `size` is used to populate the TEB stack limits; other platforms ignore it.

A saved context can be resumed only on its original thread. CET shadow stacks and Arm GCS are not switched. On x86, `endbr` is emitted only for IBT.

An unsupported architecture causes a compile-time `cc_swap: unsupported architecture` error. If RISC-V `__riscv_flen` is neither 32 nor 64, compilation fails with `cc_swap: unsupported RISC-V FLEN`. With F (`flen == 32`) and D (`flen == 64`), registers `fs0`–`fs11` are saved. RV32E never saves floating-point registers. AArch64 saves `d8`–`d15` when `__ARM_FP` is defined. ARM32 saves VFP registers when `__ARM_FP` is defined and `__SOFTFP__` is not.

# License

cc_task is licensed under [Apache License 2.0](LICENSE), allowing commercial use, modification, closed-source integration, and static linking. Downstream projects distributing products containing this library must comply with the license and retain the copyright and attribution notices in [NOTICE](NOTICE).
