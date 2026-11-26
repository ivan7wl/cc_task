# cc_task：一条 CPU cache line 放得下的 C 语言协程，async/await 与闭包

[English](README.md) | **简体中文**

cc_task 给 C 语言带来 async/await 风格的协程和闭包。

无栈任务体可以是闭包（clang block）或普通函数指针；协程状态存在闭包捕获变量里，可以跨恢复点。典型任务不到 64B，一条 CPU cache line 就可以放下。

无栈和有栈协程可以混用。闭包任务体需要 clang `-fblocks`，普通函数体任何编译器都行；依赖极小，从常见操作系统到裸机都能跑。

适用于高性能、高并发、分布式、事件驱动、嵌入式等场景。

# 示例

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

输出：

```text
A: await B ...
B: c = 11
B: c = 12
A: B end, code = 12
```

这个例子里，协程 B 捕获变量 `c`，把当时的值复制进闭包，因此可以跨越 `CC_YIELD()` 继续使用；之后在外面把 `c` 改成 0，不会影响 B 里的副本。协程 A 通过 await 等待 B 结束，并取得 B 用 `CC_END(t, c)` 返回的值。

## TCP echo server / client

见 `examples/` 子目录中的 `echo.c`，是用 cc_task 实现的单线程多并发 TCP echo server / client，Linux 上由 `epoll` 驱动、macOS 上由 `kqueue` 驱动（`make -C examples`，然后 `./examples/echo server 9000`）。`echo.cpp` 是标准 C++20 coroutine 版本（构建为 `echo_cpp`），`echo.js` 是对应的使用 async/await 实现的 JavaScript 版本。

### C 与 C++ 资源占用对比

以下为 Linux x86-64、Clang 16、`-O2` 下使用 `bench` 模式测得的对比。

| 指标 | C 服务端 | C++ 服务端 | C 客户端 | C++ 客户端 | C++ / C（服务端 / 客户端） |
|---|---:|---:|---:|---:|---:|
| 空闲 RSS | 960 KiB | 1,880 KiB | 960 KiB | 1,880 KiB | 1.96x / 1.96x |
| 64 B 每连接新增 RSS | 4.68 KiB | 5.25 KiB | 0.67 KiB | 1.29 KiB | 1.12x / 1.93x |
| 16 KiB 每连接新增 RSS | 16.49 KiB | 17.11 KiB | 16.59 KiB | 17.12 KiB | 1.04x / 1.03x |
| 64 B、1024 连接总 RSS | 5.62 MiB | 7.09 MiB | 1.61 MiB | 3.13 MiB | 1.26x / 1.94x |
| 16 KiB、1024 连接总 RSS | 17.43 MiB | 18.95 MiB | 17.53 MiB | 18.96 MiB | 1.09x / 1.08x |
| 每 64 B 消息用户态指令数 | 401 | 593 | 439 | 979 | 1.48x / 2.23x |
| 每 16 KiB 消息用户态指令数 | 721 | 778 | 518 | 1,166 | 1.08x / 2.25x |

# 特点

* 支持无栈协程和有栈协程
* 任务体可以是 C 语言闭包（clang blocks），也可以是普通函数
* async/await 调用风格，支持 yield、wait、transfer，以及可选的返回值（promise/future）
* 内存开销极小，无栈协程对象为 32 字节（LP64）/ 20 字节（ILP32），再加上实际捕获变量的大小
* 支持类似 throw/catch 的异常处理，以及外部 abort
* 依赖极少，不包含标准库头文件；库函数只依赖 malloc 等，可替换或裁剪

# 支持平台

有栈协程使用架构相关汇编指令切换上下文，编译时自动识别目标平台，目前支持以下平台：

* x86_64：SysV（Linux、macOS、BSD）、Windows x64，不支持 CET 影子栈
* i386：SysV cdecl（Linux、BSD）、Windows i386，不支持 CET 影子栈
* AArch64：AAPCS64（Linux、Android、macOS、裸机），不支持 Arm GCS
* ARM32：AAPCS（Linux、Android、裸机）的 ARM 和 Thumb-2
* Thumb-1：Cortex-M0 / M0+ / M23（裸机）
* RISC-V：RV64、RV32、RV32E（Linux、裸机）

`CC_CFG_TASK_STACKFUL=0` 关掉有栈协程，可以支持所有平台。无栈协程只要求 LP64 或 ILP32（含 Windows LLP64）。

# 快速开始

把 `cc_task/` 子目录放进工程，include `cc_task.h` 即可。

```c
#include "cc_task.h"
```

## 编译配置

建议使用 clang 编译，因为 clang 支持 blocks 闭包（`-fblocks`）。

```shell
clang -std=gnu11 -O2 -fblocks -Icc_task -Wuninitialized -Werror=uninitialized \
    -Wconditional-uninitialized -Werror=conditional-uninitialized your_app.c \
    cc_task/cc_task.c cc_task/cc_task_conv.c cc_task/cc_task_stackful.c cc_task/cc_port.c
```

如果使用 gcc 等其他编译器，需要用 `-DCC_CFG_TASK_BLOCK=0` 关闭闭包，或修改 `cc_config.h`。

```shell
gcc -std=gnu11 -O2 -DCC_CFG_TASK_BLOCK=0 -Icc_task -Wuninitialized -Werror=uninitialized \
    -Wconditional-uninitialized -Werror=conditional-uninitialized your_app.c \
    cc_task/cc_task.c cc_task/cc_task_conv.c cc_task/cc_task_stackful.c cc_task/cc_port.c
```

更多说明见***平台适配和裁剪***。

为避免跨越恢复点误用栈变量，建议添加 `-Wuninitialized` 和 `-Wconditional-uninitialized` 和对应的`-Werror`。

```c
CC_BEGIN(t);    // on resume, jump to the last resume point; n = 10 may be skipped
int n = 10;
CC_YIELD(t);    // save the resume point and return; stack variable n is discarded
some_use_of(n); // using n after resume is a compiler error
CC_END(t);
```

## 任务类型选择

| 类型 | 创建接口 | 状态保存方式 | 挂起位置 | 编译要求 |
|---|---|---|---|---|
| 闭包无栈任务 | `cc_task_new()` | `CC_TASK()` 捕获变量 | 任务体内，使用无栈原语 | clang，`-fblocks` |
| 普通函数无栈任务 | `cc_tconv_new()` | `cc_tconv_new()` 提供的 `ctx` | 任务体内，使用无栈原语 | 无额外要求 |
| 有栈任务 | `cc_tstack_new()` | 独立栈，另可带捕获区或 `ctx` | 任意函数调用深度 | 目标架构须支持 `cc_swap`（见***平台适配和裁剪***） |

## 创建任务

创建任务有 `new`（`cc_task_new()` 等）和 `init`（`cc_task_init()` 等）两种接口。

* `new`：内部调用 `CC_PORT_MALLOC()` 分配任务内存，任务结束后自动释放
* `init`：由调用者提供任务内存，任务结束后不会自动释放

可以替换 `CC_PORT_MALLOC`、`CC_PORT_FREE` 宏，用来自定义内存分配和释放。

如果系统不支持动态内存分配，可以关闭 `CC_CFG_USE_MALLOC`，只使用 `init` 接口。

## 运行任务

用 `cc_task_resume(&t)` 推动任务 `*t`，返回值是这次的运行状态。`cc_tr_end` 表示任务链结束且无后继，此时 `*t` 为 `NULL`。返回 `cc_tr_yield` 或 `cc_tr_wait` 时，再次调用从恢复点继续。

```c
CC_INLINE cc_tr_e cc_task_resume(CC_INOUT cc_task_t **t);
```

参数 `t` 是二级指针，既是输入也是输出。任务是链式的，在运行中可能 transfer 或 await 到其他任务，接下来要继续推动的可能已经不是原来的任务。

## 任务返回值

`CC_EXIT(t[, ret])` 和 `CC_END(t[, ret])` 可以带一个可选参数，作为任务结束后的返回值，用来实现类似 promise 或 future 的语义。

`CC_AWAIT(t, child[, var])` 的第 3 个参数可选。子任务 `child` 结束后，它的返回值会写入 `var`。

`cc_task_ret()` 返回当前线程（thread local）上最近结束的任务的返回值。

## 注意事项

* 普通栈变量无法跨越恢复点保留。建议编译时打开 `-Wuninitialized` 和 `-Wconditional-uninitialized`。
* 要跨恢复点保存的状态，用捕获变量，或放在普通函数任务的 `ctx` 里。不能捕获 `__block` 变量或 block。
* 不能捕获数组。可以把数组放进结构体，再捕获这个结构体。
* 捕获变量是只读的，写入要用 `$()`，例如 `for ($(i) = 0; i != 10; $(i)++)`。
* 捕获的是变量的**当前值**：之后在闭包外修改它，不会影响闭包里的副本。
* 无栈协程恢复用 switch-case 跳转实现，`CC_BEGIN()` 和 `CC_END()` 之间不能再写 switch-case。
* await 和 transfer 本质上是函数尾调用，建议至少使用 `-O1`，以保证尾调用优化；否则任务转移可能导致爆栈。

# 开发参考手册

## 数据结构

### `cc_tr_e`

每次**恢复**任务的返回值，指示了任务的状态。

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

| 值 | 含义 |
|---|---|
| `cc_tr_end` | 任务链结束 (end或abort)，且无后继。`cc_task_resume()` 返回它时，`*t` 为 `NULL`。 |
| `cc_tr_yield` | 主动让出，可继续恢复 |
| `cc_tr_wait` | 挂起等待，由调用方决定何时恢复 |
| `cc_tr_trans` | 有栈任务转移时在内部使用，外部调用方通常看不到 |
| `cc_tr_error` | 操作被拒绝，任务保持原样。例如 `exec` 的参数不满足要求 |

### `cc_task_t`

表示一个任务对象，可以是无栈闭包任务，无栈普通函数指针任务，或者有栈任务。头部按 clang block 字面量排布，所以闭包任务可以直接把 `cc_task_t` 当作 block 调用，捕获变量落在末尾的 `ctx` 里。头部大小是 LP64 下 32 字节，ILP32 下 20 字节。

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

| 字段 | 作用 |
|---|---|
| `next` | 后继任务。本任务结束时恢复它；没有后继时任务链结束 |
| `lc` | 恢复点。`0` 是入口，`CC_LC_DONE`（`0xFFFF`）表示已结束，`CC_LC_ABORT`（`0xFFFE`）表示已请求 abort |
| `ctx_size` | `ctx` 大小，单位字节，最大 65535。捕获变量或传统任务中指定的 `ctx` 上下文大小。 |
| `flags` | `cc_tf_abort`：已请求 abort。`cc_tf_retain`：调用方提供的内存，结束时不释放。`cc_tf_runner`：该 task 是 runner |
| `fn` | 保留给 block 的 invoke 槽，调用方不要直接写入 |
| `ext` | 通用扩展上下文；没有库扩展占用时为 NULL |
| `ctx` | 柔性数组，实际大小为 `ctx_size`。是捕获变量或传统任务的上下文。传统任务中可以用 `CC_CTX(t, type)` 访问 |

### `cc_lc_t`

无栈任务的恢复点类型。和 *protothread* 一样，用 `switch` / `case` 实现局部续延 (lc: local continuation)：`CC_LC_RESUME()` 通过 switch 跳转到 lc 标识的 case 处。

protothread 的 lc 保存的是行号。它的主要问题是**不连续**，各个 case 标签的值可能非常离散，导致编译器不好做优化；此外还受源码位置限制，行号超过 65535 就放不进 16 位容器。

`cc_lc_t` 的创新点是用连续递增的 `__COUNTER__` 做编号，每个函数都有自己的起点，lc 保存当前位置到函数起点的偏移。因此每个函数都是从 `0`（入口）单独递增编号，每个 case 标签的值是上一个的值+1，跟它在文件里的位置无关。16 位里 `0xFFFE`（`CC_LC_ABORT`）和 `0xFFFF`（`CC_LC_DONE`）留作结束和 abort，可用编号到 `0xFFFD`，编译期会检查有没有超。实际上，一个函数内部的协程状态基本不会超过255，因此 lc 容器甚至可以压缩到 8 位大小。

### `cc_port_task_slot_t`

存储当前任务信息的槽位，每个线程一份，同时放着当前任务和最近一次结束值。用线程局部存储：

```c
typedef struct _cc_port_task_slot_t
{
    cc_task_t *current;    // task entered most recently on this thread
    cc_uintptr ret;        // last end value saved on this thread
} cc_port_task_slot_t;

CC_PORT_THREAD_LOCAL cc_port_task_slot_t cc_port_task_slot_default;
```

`CC_PORT_THREAD_LOCAL` 在 C 里是 `_Thread_local`，在 C++ 里是 `thread_local`。每个线程各有各的槽，一个线程的 `cc_task_resume()` / `cc_task_ret()` 不会看到另一个线程的任务或结束值。

没有线程局部存储的环境，可以定义 `CC_PORT_THREAD_LOCAL` 为空，并重写 `CC_PORT_TASK_SLOT()` 等相关接口。

| 字段 | 作用 |
|---|---|
| `current` | 最后一次调用的任务。通过 `cc_task_current()` 获取；注意该值有时效性，每次有任务恢复时会被覆盖 |
| `ret` | `CC_END(t, value)`、`CC_EXIT(t, value)`、`cc_tstack_exit(value)` 中指定的可选的任务返回值。通过 `cc_task_ret()` 获取；注意该值有时效性，每次有任务结束时会被覆盖 |

## 主要接口

`#include "cc_task.h"` 即可使用本节接口。闭包相关接口在 `CC_CFG_TASK_BLOCK=1` 时声明，并且需要 clang `-fblocks`。`cc_task_new()` 还要求 `CC_CFG_USE_MALLOC=1`。

***控制原语***、***传统任务***和***有栈任务***分别见后面各节。

### `CC_TASK`

```c
#define CC_TASK(t)    ^cc_tr_e (cc_task_t *t, void *_)
```

构造一个闭包任务体，传给 `cc_task_new()`、`cc_task_init()` 或 `cc_task_exec()`。参数 `t` 是当前任务，`_` 为保留参数。

`CC_BEGIN(t)` 不必放在任务体第一行。写在它前面的局部变量，每次进入都会先定义并赋值，然后再跳到恢复点，所以每个恢复点都能用。这些赋值每次进入都会做。可恢复的代码放在 `CC_BEGIN(t)` 和 `CC_END(t)` 之间。

闭包按值捕获外面的变量；不能捕获 `__block` 变量、其他 block，也不能直接捕获数组。需要捕获数组时，可以把数组定义到结构体中，捕获结构体变量。

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

写闭包里的捕获变量。捕获变量本身是只读的，读直接用变量名，写才用 `$()`，例如 `for ($(i) = 0; i != 10; $(i)++)`。

### `cc_task_size`

```c
cc_size cc_task_size(cc_task_func blk);
```

返回装下闭包任务 `blk` 所需的字节数。捕获不支持（`__block` 或 block）或捕获区大于 65535 字节时返回 `0`。

`cc_task_init()` 的 `size` 不能小于这个值。

### `cc_task_new`

```c
cc_task_t *cc_task_new(cc_task_func blk);
```

分配并返回一个新任务，不立即运行。失败返回 `NULL`，原因与 `cc_task_size()` 返回 `0` 相同，或者 `CC_PORT_MALLOC()` 失败。

任务生命周期结束时库会自动释放内存。

### `cc_task_init`

```c
cc_task_t *cc_task_init(void *mem, cc_size size, cc_task_func blk);
```

在调用方提供的 `mem` 上初始化任务，不立即运行。成功时返回值就是 `mem`。`size` 小于 `cc_task_size(blk)`，或捕获不支持、过大时，返回 `NULL`，`mem` 不会被改写。

`mem` 在任务整个使用期间必须保持有效，并按 `cc_task_t` 对齐。内存完全由调用方管理，任务生命周期结束时不释放内存。

### `cc_task_exec`

```c
cc_tr_e cc_task_exec(cc_task_t *t, cc_task_func blk);
```

在任务 `t` 的内存上换一套闭包任务体，并立刻运行，返回新任务体的 `cc_tr_e`。不分配新的任务对象，而是复用 `t` 已有的内存空间。

以下情况返回 `cc_tr_error`，并且不修改 `t`：

- `t` 是有栈任务
- 捕获不支持或大于 65535 字节
- 新捕获区大于 `t` 现有的 `ctx` 容量

### `cc_task_resume`

```c
cc_tr_e cc_task_resume(cc_task_t **t);
```

协程的**恢复**操作，即推动 `*t` 运行：第一次从入口进入，之后从上次的恢复点继续，直到任务挂起 (yield, wait)，或这条任务链结束。

返回值是这次的运行状态 `cc_tr_e`。`cc_tr_end` 表示任务链结束，且无后继，此时 `*t` 也是 `NULL`。有后继时不会把 `cc_tr_end` 交回调用方，而是接着运行 `next`。`cc_tr_yield` 或 `cc_tr_wait` 表示还停在恢复点上，再次调用从那里继续。

`t` 是二级指针，既做输入也做输出。transfer 或 await 之后，`*t` 会改成当前应继续推动的任务。不要传入 `NULL`，也不要对已经自动释放的任务调用。这个返回值不是 `CC_END()` 保存的结束值。

### `cc_task_abort`

```c
void cc_task_abort(cc_task_t *t);
```

标记一个尚未结束的挂起任务，让它在下次 `cc_task_resume()` 时跳转到 `CC_CATCH():` 标签处指定的清理路径。已结束的任务（`lc` 已是完成状态）不做任何事。任务体没有 `CC_CATCH():` 时，任务直接结束。

无栈任务要中止自己，用 `CC_THROW_ON()`，不要在正在运行的任务体里调用 `cc_task_abort()`。

有栈任务没有 `CC_CATCH():`，`cc_task_abort()` 标记一个挂起任务后，下次恢复时，挂起点 `cc_tstack_yield()`、`cc_tstack_wait()`、`cc_tstack_trans()` 返回 `CC_FALSE`；任务体由此可以判断该任务已被abort，并做对应的清理，比如 `goto cleanup;`。有栈任务要中止自己，不需要调用 `cc_task_abort()`，直接 `goto` 运行清理代码并返回 `cc_tr_end` 就行了。

### `cc_task_ret`

```c
cc_uintptr cc_task_ret(void);
```

返回当前线程最近一次保存的任务结束值。任务中通过 `CC_END(t[, value])`、`CC_EXIT(t[, value])` 写入结束值。`value` 是可选参数，不带 `value` 时不改它，表示任务结束值是 `void`。`cc_task_ret()` 类似 `errno`，要在任务结束后马上读取。

### `cc_task_current`

```c
cc_task_t *cc_task_current(void);
```

返回当前线程最近运行的任务。还没有任务运行过，或任务结束且无后继时，返回 `NULL`。

### `cc_task_ended`

```c
cc_bool cc_task_ended(cc_task_t *t);
```

`t` 已经结束返回 `CC_TRUE`，否则返回 `CC_FALSE`。不能传入已自动释放的指针。`cc_task_init()` 的任务结束后指针仍然有效，可以查询。

### `cc_task_aborted`

```c
cc_bool cc_task_aborted(cc_task_t *t);
```

已经对 `t` 请求过 abort 时返回 `CC_TRUE`，否则返回 `CC_FALSE`。`cc_task_exec()` 成功替换任务体后，这个标记会被清掉。

### `CC_TASK_SIZE`

```c
#define CC_TASK_SIZE(n)    (CC_OFFSETOF(cc_task_t, ctx) + (cc_size)(n))
```

计算带 `n` 字节捕获区或用户 `ctx` 的任务对象大小。`n` 是 `ctx` 的字节数，不是任务总长。

### `CC_CTX`

```c
#define CC_CTX(t, type)    ((type *)(t)->ctx)
```

把任务的 `ctx` 转成 `type *`。普通函数任务用它访问自己的上下文。对齐是 LP64 下 8 字节，ILP32 下 4 字节。

## 控制原语

控制原语是 `cc_primitive.h` 里用宏定义的一些协程操作，可在无栈任务体里使用，可用于闭包任务和普通函数任务。有栈协程需要使用 `cc_tstack_*` 的函数接口。

### `CC_BEGIN`

```c
CC_BEGIN(t);
```

协程任务体代码块开始，跳转到之前保存的恢复点。示例见 `CC_TASK()`。

`CC_BEGIN()` 本质是 switch-case 跳转语句，因此 `CC_BEGIN()` 到 `CC_END()` 之间不能再写 switch-case。

### `CC_END`

```c
CC_END(t);
CC_END(t, value);
```

协程任务体代码块结束。`value` 为可选的任务结束值，不指定时任务结束值为 `void`。返回值可以通过 `cc_task_ret()` 读取。

`CC_END()` 后如果有后继任务，会尾调用运行后继（参见***注意事项***章节），否则返回 `cc_tr_end` 给调用 `cc_task_resume()` 的调用者。

### `CC_EXIT`

```c
CC_EXIT(t);
CC_EXIT(t, value);
```

立刻结束，可以在任务体代码块中间任意位置使用，结束值的规则和 `CC_END` 相同。

### `CC_YIELD`

```c
CC_YIELD(t);
```

保存恢复点，向 `cc_task_resume()` 返回 `cc_tr_yield`。下次再调用 `cc_task_resume()` 时从恢复点继续运行。适用于主动出让时间片，先让其他任务运行的场景。

### `CC_WAIT`

```c
CC_WAIT(t);
```

与 `CC_YIELD()` 类似，只是返回的是 `cc_tr_wait`。适用于异步等待场景，协程提交操作并等待完成，在操作完成时调用者通过 `cc_task_resume()` 唤醒协程继续运行。

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

`cond` 为假就 `CC_YIELD()`，适用于非阻塞轮询某个条件。

### `CC_YIELD_WHILE`

```c
CC_YIELD_WHILE(t, cond);
```

`cond` 为真就 `CC_YIELD()`。

### `CC_TRANS`

```c
CC_TRANS(t, next);
```

协程的对称迁移：暂停并保存当前任务的恢复点，迁移到 `next` 继续运行。

迁移是直接尾调用 `next`，不会返回到 `cc_task_resume()` 的调用者，参见***注意事项***章节。

### `CC_AWAIT`

```c
CC_AWAIT(t, child);
CC_AWAIT(t, child, var);
```

协程的 await 操作：先保存当前任务的恢复点，迁移到 `child`，等 `child` 生命周期结束后，迁移回当前任务继续运行。

`var` 是可选参数，可用于接收 `child` 任务的结束值。

`child` 是 abort 结束的话，会导致父任务也被 abort。

迁移是直接尾调用 `next`，不会返回到 `cc_task_resume()` 的调用者，参见***注意事项***章节。

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

抛出”异常“：`cond` 为真时，当前无栈任务中止自己，跳到 `CC_CATCH():`。没有 `CC_CATCH():` 就直接结束。参见 `cc_task_abort()`。

### `CC_CATCH`

```c
CC_CATCH(t):
CC_CATCH(t, value):
```

承接”异常“：写在 `CC_END` 前面。注意冒号不能省掉。

正常流程跑到这里就结束，可选参数 `value` 是正常结束的任务结束值。`CC_CATCH():` 后面的代码是 abort 处理流程。参见 `cc_task_abort()`。

```c
    CC_BEGIN(t);
    CC_YIELD_UNTIL(t, uart_tx_done()); // yield while hardware busy
CC_CATCH(t, uart_tx_nbytes()):         // transfer done, end with tx bytes
    uart_tx_cancel();                  // someone abort transfer
    CC_END(t, (cc_uintptr)-1);         // return aborted to caller
```

## 传统任务

传统任务用普通函数做任务体，适用更广，编译时不需要 clang `-fblocks`。跨恢复点的状态放在 `ctx` 里，用 `CC_CTX(t, type)` 访问。控制原语和闭包任务相同。接口在 `CC_CFG_TASK_CONV=1` 时声明，`cc_tconv_new()` 还要求 `CC_CFG_USE_MALLOC=1`。

```c
typedef cc_tr_e (*cc_tconv_func)(cc_task_t *t, void *_, void *ctx);
```

传统任务和闭包任务本质上是一样的，一个闭包可以看作一个 `cc_tconv_func` 函数指针，加上大小为 `ctx_size` 的 `ctx` 作为任务上下文。

相比于闭包任务的自动捕获变量，传统任务需要手动管理 `ctx`。以下例子和 `CC_TASK` 章节的示例是等价的。

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

分配并返回一个新任务，不立即运行。`ctx != NULL` 时把 `ctx_size` 字节复制进任务；`ctx == NULL` 时把这段清零。`ctx_size` 大于 65535，或 `CC_PORT_MALLOC()` 失败时返回 `NULL`。

任务生命周期结束时库会自动释放内存。

### `cc_tconv_init`

```c
cc_task_t *cc_tconv_init(void *mem, cc_size size, cc_tconv_func func,
                         const void *ctx, cc_size ctx_size);
```

在调用方提供的 `mem` 上初始化任务，不立即运行。成功时返回值就是 `mem`。`ctx` 的复制或清零和 `cc_tconv_new()` 相同。`ctx_size` 大于 65535，或 `size` 小于 `CC_TASK_SIZE(ctx_size)` 时返回 `NULL`，`mem` 不会被改写。

`mem` 在任务整个使用期间必须保持有效，并按 `cc_task_t` 对齐。内存完全由调用方管理，任务生命周期结束时不释放内存。

### `cc_tconv_exec`

```c
cc_tr_e cc_tconv_exec(cc_task_t *t, cc_tconv_func func,
                      const void *ctx, cc_size ctx_size);
```

在任务 `t` 的内存上换一个函数，并立刻运行，返回新任务体的 `cc_tr_e`。不分配新的任务对象。

`ctx_size == 0` 时保留现有 `ctx`。非 0 时按上面的规则复制或清零，但不能大于 `t` 现有的 `ctx` 容量。替换会重置恢复点、清掉 abort 标记。`cc_tf_retain` 和后继链接保持不变，`ctx` 容量不缩小。

`t` 是有栈任务，或 `ctx_size` 大于现有容量时，返回 `cc_tr_error`，并且不修改 `t`。

## 有栈任务

有栈任务适合需要在普通函数调用链中挂起的代码。局部变量和嵌套调用帧会保留在独立栈上。任务体不要写 `CC_BEGIN()`、`CC_YIELD()`、`CC_AWAIT()` 这些无栈宏。函数返回时只能返回 `cc_tr_end`。仍用 `cc_task_resume()` 推动。

接口在 `CC_CFG_TASK_STACKFUL=1` 时声明。闭包那一组还要 `CC_CFG_TASK_BLOCK=1`。`cc_tstack_new()` 和 `cc_tstack_conv_new()` 还要 `CC_CFG_USE_MALLOC=1`。`CC_CFG_TASK_STACKFUL=0` 时仍保留 `cc_tstack_is_stackful()`，它始终返回 `CC_FALSE`。支持的架构见***支持平台***。

挂起接口返回 `CC_FALSE` 表示这个任务已被 abort。这时清理资源并返回 `cc_tr_end`，不要再次挂起。有栈任务要中止自己，直接走清理并返回 `cc_tr_end`，见 `cc_task_abort()`。

有栈协程可以在任意深度的嵌套调用函数里挂起，没有无栈协程 async/await 传染的问题。下面这个例子等价于***示例***章节，但是 `func_b` 是普通函数也可以挂起，不必做成子协程。

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

输出：

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

`t` 是有栈任务返回 `CC_TRUE`，否则返回 `CC_FALSE`。有栈支持打开时不要传 `NULL`。

### `cc_tstack_size`

```c
cc_size cc_tstack_size(cc_size stack_size, cc_task_func blk);
```

返回装下闭包有栈任务所需的字节数，含任务头、捕获区和 `stack_size` 字节的栈。捕获不支持（`__block` 或 block）或捕获区大于 65535 字节，或 `stack_size` 为 0、总长溢出时返回 `0`。

`cc_tstack_init()` 的 `size` 不能小于这个值。

### `cc_tstack_new`

```c
cc_task_t *cc_tstack_new(cc_size stack_size, cc_task_func blk);
```

分配并返回一个新任务，不立即运行。失败返回 `NULL`，原因与 `cc_tstack_size()` 返回 `0` 相同，或者 `CC_PORT_MALLOC()` 失败。

任务生命周期结束时库会自动释放内存。闭包捕获规则和 `cc_task_new()` 相同。

### `cc_tstack_init`

```c
cc_task_t *cc_tstack_init(void *mem, cc_size size, cc_task_func blk);
```

在调用方提供的 `mem` 上初始化任务，不立即运行。成功时返回值就是 `mem`。`mem` 必须按 8 字节对齐。`size` 小于 `cc_tstack_size()` 给出的大小，或捕获不支持、过大时，返回 `NULL`，`mem` 不会被改写。

`mem` 在任务整个使用期间必须保持有效。内存完全由调用方管理，任务生命周期结束时不释放内存。

### `cc_tstack_exec`

```c
cc_tr_e cc_tstack_exec(cc_task_t *t, cc_task_func blk);
```

在任务 `t` 的内存上换一套闭包任务体，并立刻运行，返回新任务体的 `cc_tr_e`。不分配新的任务对象，而是复用 `t` 已有的内存空间。栈的容量不变。

以下情况返回 `cc_tr_error`，并且不修改 `t`：

- `t` 不是有栈任务，或正在运行
- 捕获不支持或大于 65535 字节
- 新捕获区大于 `t` 现有的 `ctx` 容量

### `cc_tstack_conv_new`

```c
cc_task_t *cc_tstack_conv_new(cc_tconv_func func, const void *ctx,
                              cc_size ctx_size, cc_size stack_size);
```

分配并返回一个新任务，不立即运行。`ctx != NULL` 时把 `ctx_size` 字节复制进任务；`ctx == NULL` 时把这段清零。`ctx_size` 大于 65535，`stack_size` 为 0，总长溢出，或 `CC_PORT_MALLOC()` 失败时返回 `NULL`。

任务生命周期结束时库会自动释放内存。

### `cc_tstack_conv_init`

```c
cc_task_t *cc_tstack_conv_init(void *mem, cc_size size, cc_tconv_func func,
                               const void *ctx, cc_size ctx_size);
```

在调用方提供的 `mem` 上初始化任务，不立即运行。成功时返回值就是 `mem`。`ctx` 的复制或清零和 `cc_tconv_new()` 相同。`mem` 必须按 8 字节对齐。`ctx_size` 大于 65535，或 `size` 不够放下任务、`ctx` 和栈时，返回 `NULL`，`mem` 不会被改写。

`mem` 在任务整个使用期间必须保持有效。内存完全由调用方管理，任务生命周期结束时不释放内存。

### `cc_tstack_conv_exec`

```c
cc_tr_e cc_tstack_conv_exec(cc_task_t *t, cc_tconv_func func,
                            const void *ctx, cc_size ctx_size);
```

在任务 `t` 的内存上换一个函数，并立刻运行，返回新任务体的 `cc_tr_e`。不分配新的任务对象，而是复用 `t` 已有的内存空间。栈的容量不变。

`ctx_size == 0` 时保留现有 `ctx`。非 0 时按上面的规则复制或清零，但不能大于 `t` 现有的 `ctx` 容量。替换会重置恢复点、清掉 abort 标记。`cc_tf_retain` 和后继链接保持不变，`ctx` 容量不缩小。

`t` 不是有栈任务、正在运行，或 `ctx_size` 大于现有容量时，返回 `cc_tr_error`，并且不修改 `t`。

### `cc_tstack_yield`

```c
cc_bool cc_tstack_yield(void);
```

保存恢复点，向 `cc_task_resume()` 返回 `cc_tr_yield`。下次再调用 `cc_task_resume()` 时从恢复点继续运行。适用于主动出让时间片，先让其他任务运行的场景。可以写在嵌套函数里。

正常恢复返回 `CC_TRUE`。任务已被 abort 时返回 `CC_FALSE`。

### `cc_tstack_wait`

```c
cc_bool cc_tstack_wait(void);
```

与 `cc_tstack_yield()` 类似，只是返回的是 `cc_tr_wait`。适用于异步等待场景，协程提交操作并等待完成，在操作完成时调用者通过 `cc_task_resume()` 唤醒协程继续运行。

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

`cond` 为假就 `cc_tstack_yield()`，适用于非阻塞轮询某个条件。`cond` 为真返回 `CC_TRUE`。被 abort 返回 `CC_FALSE`。

### `cc_tstack_yield_while`

```c
cc_bool cc_tstack_yield_while(cond);
```

`cond` 为真就 `cc_tstack_yield()`。`cond` 变成假返回 `CC_TRUE`。被 abort 返回 `CC_FALSE`。

### `cc_tstack_trans`

```c
cc_bool cc_tstack_trans(cc_task_t *next);
```

协程的对称迁移：暂停并保存当前任务的恢复点，迁移到 `next` 继续运行。

正常恢复返回 `CC_TRUE`。被 abort 返回 `CC_FALSE`。`next` 如果也是有栈任务，不能正在运行，也不能已经结束。

### `cc_tstack_exit`

```c
void cc_tstack_exit(cc_uintptr ret);
```

立刻结束，可以在任意调用深度使用。`ret` 为任务结束值，规则和 `CC_END(t, value)` 相同，可以通过 `cc_task_ret()` 读取。函数不返回。被跳过的 C 帧不会展开。

### `cc_tstack_eh`

```c
cc_tstack_eh(op, label);
```

执行一次 `op`。结果是 `CC_FALSE` 就 `goto label`。

```c
cc_tstack_eh(cc_tstack_wait(), cleanup);
cleanup:
    return cc_tr_end;
```

### `cc_tstack_canary_hit`

```c
cc_bool cc_tstack_canary_hit(cc_task_t *t);
```

栈底 16 字节标记被覆盖时返回 `CC_TRUE`，否则返回 `CC_FALSE`。只用于发现栈已使用到底部区域，不能替代完整的栈溢出防护。

## 平台适配和裁剪

### `cc_config.h`

功能开关，默认都是 `1`，只能是 `0` 或 `1`。开关可以在编译时用 `-D` 覆盖，比如`-DCC_CFG_TASK_STACKFUL=0`。

`CC_CONFIG_INCLUDE` 可以引入自己工程的配置文件，例如 `-DCC_CONFIG_INCLUDE='"my_config.h"'`。

| 宏 | 为 `0` 时 |
|---|---|
| `CC_CFG_USE_MALLOC` | 没有 `new`，只剩 `init`。不依赖 malloc/free |
| `CC_CFG_TASK_BLOCK` | 没有闭包任务 |
| `CC_CFG_TASK_CONV` | 没有传统任务 |
| `CC_CFG_TASK_STACKFUL` | 没有有栈任务。`cc_tstack_is_stackful()` 仍在，始终返回 `CC_FALSE`。`cc_swap.h` 为空 |

`CC_CFG_TASK_BLOCK=1` 需要 clang `-fblocks`。没有 `__BLOCKS__` 时编译失败。

`CC_CFG_TASK_STACKFUL=0` 时不再用架构汇编。无栈协程只要求 LP64 或 ILP32（含 Windows LLP64）。打开时的架构见***支持平台***。

### `cc_port.h`

库用到的平台服务都走这些宏。默认是 C 运行库的 `malloc`、`free`，以及编译器的 `__builtin_memcpy`、`__builtin_memset`。失败时 trap。

`-D` 可以覆盖单个宏。用 `-DCC_PORT_INCLUDE='"my_port.h"'` 可以引入自己工程的 port 文件。

| 宏 | 默认 | 作用 |
|---|---|---|
| `CC_PORT_MALLOC(n)` | `malloc(n)` | 分配内存。仅 `CC_CFG_USE_MALLOC=1` |
| `CC_PORT_FREE(p)` | `free(p)` | 释放内存 |
| `CC_PORT_MEMCPY(d, s, n)` | `__builtin_memcpy` | 复制内存 |
| `CC_PORT_MEMSET(d, c, n)` | `__builtin_memset` | 清零内存 |
| `CC_PORT_THREAD_LOCAL` | C 里 `_Thread_local`，C++ 里 `thread_local` | 任务槽的存储类 |
| `CC_PORT_TASK_SLOT()` | 默认槽 `cc_port_task_slot_default` | 本线程的任务槽 |
| `CC_PORT_CURRENT_TASK()` | 槽里的 `current` | 当前任务 |
| `CC_PORT_SET_CURRENT_TASK(t)` | 写入 `current`，返回原来的值 | 设置当前任务 |
| `CC_PORT_TASK_RET()` | 槽里的 `ret` | 最近一次结束值 |
| `CC_PORT_SET_TASK_RET(ret)` | 写入 `ret` | 设置结束值 |
| `CC_PORT_PANIC(e)` | `cc_port_panic(__FILE__, __LINE__, (e))` | 失败出口，带上调用点的文件和行 |
| `CC_PORT_ASSERT(c)` | 条件为假时 `CC_PORT_PANIC` | `-DCC_NDEBUG` 后为空 |

槽的字段见 `cc_port_task_slot_t`。没有线程局部存储的环境，可以定义 `CC_PORT_THREAD_LOCAL` 为空，并重写 `CC_PORT_TASK_SLOT()` 等相关接口。只替换 `CC_PORT_TASK_SLOT()` 时，`current` 和 `ret` 都从新槽取。

`cc_port.c` 提供默认槽，以及 weak 的 `cc_port_panic()`：不打印，直接 `__builtin_trap()`。工程里可以提供同名强符号，打印位置后复位。`CC_PORT_TASK_SLOT()` 和 `CC_PORT_PANIC()` 都换成自己的之后，可以不链接 `cc_port.c`。

### `cc_swap.h`

`CC_CFG_TASK_STACKFUL=1` 时按编译目标展开。有栈任务内部用它切换上下文，不调用操作系统。`CC_CFG_TASK_STACKFUL=0` 时这个头是空的。架构清单见***支持平台***。

`cc_swap(save, load, arg)` 把被调用者保存寄存器压到当前栈，把栈指针写进 `*save`，再恢复 `load` 指向的上下文。`arg` 是这次调用的返回值。`cc_swap_start(save, top, size, entry, arg)` 同样先保存当前上下文，然后在新栈上进入 `entry(arg)`。`top` 按 16 字节对齐，栈区间是 `[top - size, top)`。`entry` 不能返回。`size` 在 Windows 上用来填写 TEB 的栈范围，其他平台忽略。

保存下来的上下文只能在原来的线程上恢复。不切换 CET 影子栈和 Arm GCS。x86 上的 `endbr` 只做 IBT。

不认识的架构编译失败，报 `cc_swap: unsupported architecture`。RISC-V 的 `__riscv_flen` 既不是 32 也不是 64 时，报 `cc_swap: unsupported RISC-V FLEN`。F（`flen == 32`）和 D（`flen == 64`）会保存 `fs0`–`fs11`。RV32E 不保存浮点寄存器。AArch64 在有 `__ARM_FP` 时保存 `d8`–`d15`。ARM32 在有 `__ARM_FP` 且没有 `__SOFTFP__` 时保存 VFP。

# 开源协议

cc_task 采用 [Apache License 2.0](LICENSE)，允许商用、修改、闭源集成和静态链接。下游项目分发包含本库的产品时，应遵守协议并保留 [NOTICE](NOTICE) 中的版权与归属说明。
