# cc_task Runner 设计：多任务并发与 join-all

状态：设计草案，尚未实现  
基线：无内建调度器，使用现有 `cc_task_resume()` / `CC_AWAIT()` 推动任务。

## 1. 概述

新增一个 runner task，用于把多个无栈任务组成一个并发任务组：

```c
cc_task_t *runner = cc_runner_new(a, b, c);
```

有两种推动方式：

```c
// 直接作为根任务运行
cc_task_t *cur = runner;
cc_task_resume(&cur);

// 或在另一个任务中等待整组结束；这就是 join-all
CC_AWAIT(t, runner);
```

runner 内部维护：

- 一条 READY 链，只包含当前可以运行的任务；
- `alive`，表示尚未返回 `cc_tr_end` 的组内任务数。

组内任务返回：

- `yield`：重新挂到 READY 尾部；
- `wait`：从 READY 链摘除，等待外部单独 resume；
- `end`：resume_hook 中 `alive--`，停用内联 node 或释放动态 node。

所有组内任务都 end 后，`alive == 0`，runner 自己 end。若 runner 正被 `CC_AWAIT`，其结束会自然恢复 await 它的 parent；无需另写 join 原语或 root continuation 特判。

## 2. 首版范围

### 2.1 目标

- 单线程、协作式并发。
- 一次创建包含多个任务的 runner。
- 直接 resume runner，或使用现有 `CC_AWAIT` join 整组任务。
- 组内任务支持 yield/wait/end，包括无栈和有栈任务。
- WAIT 任务不在 READY 链中，resume runner 不会调度它。
- WAIT 任务可由外部直接 resume；resume_hook 能找到 node 和 runner。
- 不改变 `cc_task_t` 的大小、ctx 偏移和 Blocks 调用布局。
- 支持 Blocks 与 conventional 无栈任务。
- 不改变原任务既有的 end/free 行为。
- `size_n/init_n` 不依赖 malloc，堆分配 `new_n/new` 由 `CC_CFG_USE_MALLOC=1` 控制。

### 2.2 首版非目标

- 多线程调度或多核并行。
- 对每个 child 分别 join 或取得已经结束的 child 返回值。
- detach、后台任务、取消树和结构化异常聚合。

首版只提供 **join-all**：`CC_AWAIT(parent, runner)` 等待整组结束。子任务结束值仍遵循现有 `cc_task_ret()` 的瞬态规则；若业务需要长期保存某个子任务结果，应由该子任务自身写入外部上下文。

## 3. 数据结构

### 3.1 将 `_desc` 改名为 `ext`

`cc_task_t` 中当前未使用、用于保留 Blocks descriptor 槽位的 `_desc` 改名为 `ext`：

```c
void *ext; // extension context; layout unchanged
```

要求：

- 字段位置、大小和对齐完全不变；
- 所有初始化代码将 `ext` 置为 NULL；
- 现有静态布局断言继续通过；
- Blocks invoke 不依赖该槽位的值；
- 普通未扩展任务保持 `ext == NULL`。

runner 挂接任务时：

```c
task->fn  = resume_hook;
task->ext = node;
```

`ext` 作为通用扩展槽使用；首版只有 runner 扩展。已被其他扩展占用或已经属于 runner 的任务不得重复挂接。

### 3.2 node

node 位于 runner 的统一存储块中，与原任务分离：

```c
#include "cc_list.h"

typedef struct _cc_run_node_t cc_run_node_t;
typedef cc_list_head_t(_cc_run_node_t) cc_run_list_t;

struct _cc_run_node_t
{
    cc_run_list_t   link;
    cc_task_t      *task;
    cc_tconv_func   original_fn;
    cc_task_t      *runner;
    cc_u8           flags; // cc_rnf_e
    cc_u8           state; // cc_run_state_e
};
```

READY 链统一使用 `cc_list.h` 中的 `cc_list_*` 宏实现，不再手写双向链插入、删除和遍历逻辑。本文中的 READY 头尾操作分别对应 `cc_list_*` 的 head/tail 接口。

不需要：

- ALL 链；
- parent node；
- join_target；
- children 计数；
- DONE 状态；
- retain 标记；
- 保存 child 返回值。

node 只负责把原任务、原 fn 和所属 runner 关联起来。

### 3.3 runner ctx

```c
typedef struct cc_runner_ctx_t
{
    cc_run_list_t ready; // standalone shadow head
    cc_run_list_t free;  // 可复用的内联 node
    cc_u32 alive;
    cc_u16 capacity;    // nodes[] 的原始内联容量
    cc_run_node_t nodes[];
} cc_runner_ctx_t;
```

`ready` 只是独立的链接字段，不是完整的 `cc_run_node_t`，也不代表额外的 root node。与 Linux `list_head` 一样，`cc_list_entry()` 按 entry 中 link 字段的实际偏移计算 sentinel；entry 的 link 可以位于任意字段偏移，不要求放在第一个字段。

runner 是普通 `cc_task_t`，其 `fn` 指向 runner body，ctx 保存上述结构。

### 3.4 统一存储布局

一个 runner 的 header、ctx 和初始 n 个 node 位于同一存储块，ctx 最后一个成员是柔性数组：

```text
[cc_task_t header][ready/alive][align padding][nodes 0 ... n-1]
```

```c
ctx_bytes = CC_OFFSETOF(cc_runner_ctx_t, nodes)
          + (cc_size)n * sizeof(cc_run_node_t); // must be <= 65535
total     = CC_TASK_SIZE(ctx_bytes);
```

`cc_runner_size_n()` 必须在转换 n 和乘法、加法前检查溢出，并在 `ctx_bytes > 65535` 时返回 0，因为 `runner.ctx_size` 是 `cc_u16`。runner.ctx_size 记录包含内联 nodes 的完整 ctx 大小。初始内联 node 数量因此有上限；超过该上限的任务通过动态 add 扩展。

`new_n` 分配 total 字节并把 runner 标记为自动释放；`init_n` 使用调用方 block 并标记 retain。初始内联 node 在任务链 end 时只停用，不单独 free；动态 add 的 node 则在对应任务链 end 时单独释放。

## 4. 创建接口

### 4.1 形式

```c
cc_size cc_runner_size_n(int n);

cc_task_t *cc_runner_init_n(void *mem, cc_size size, int n, ...);
cc_task_t *cc_runner_new_n(int n, ...);

#define cc_runner_new(...) \
    cc_runner_new_n(CC_NARG(__VA_ARGS__), __VA_ARGS__)
```

使用方式：

```c
cc_runner_new(a, b);
cc_runner_new(a, b, c, d);
cc_runner_new_n(4, a, b, c, d);
```

不再需要 NULL 哨兵。宏版本由 `CC_NARG` 计算数量，最多支持 `CC_NARG_MAX` 个任务；`new_n/init_n` 可直接传入运行时数量，不受宏参数数量限制。

至少允许一个有效任务，建议也允许单任务 runner。`n <= 0`、大小溢出或参数校验失败时返回 NULL/0。

### 4.2 无 malloc 初始化

```c
cc_size size = cc_runner_size_n(3);
void *mem = alloc_runner_storage(size); // 项目自己的 storage
cc_task_t *runner = cc_runner_init_n(mem, size, 3, a, b, c);
```

`size_n` 计算 runner header、ctx、对齐 padding 和 n 个 node 的总大小。`init_n` 要求 mem 按 `cc_task_t` 对齐，成功时返回 mem 对应的 retained runner；失败时不修改任何输入任务，mem 内容可视为未指定。

`new_n` 通过 `size_n + 单次 malloc + 内部 v-init` 实现，成功后清除 runner 的 retain 标记，使其沿用现有自动释放语义。`init_n/new_n` 共用同一套校验和布局代码。

### 4.3 创建条件

每个组内任务必须满足：

- 非 NULL，且组内没有重复指针；
- 是库支持的无栈或有栈 live task，且未结束；
- `next == NULL`，防止 end 时绕过 runner 恢复其他任务；
- `ext == NULL`，没有挂接其他扩展；
- 除 self-inclusive 模式下的当前任务外，其他任务没有正在运行；
- 首版支持 yield/wait/end，以及 await/trans 的 current-chain continuation 迁移。

### 4.4 原子性

创建必须遵循“先校验，再统一提交”：

1. 计算并检查总大小，准备 runner + ctx + node 数组的统一存储块；
2. 用 node 数组暂存并校验全部 task，包括重复指针检查；
3. 任一校验或分配失败，返回 NULL，不修改任何输入 task；
4. 全部成功后，一次性初始化 runner、resume_hook、ext、READY 链和 alive。

禁止出现“一半任务已包装，另一半因校验或分配失败未包装”的状态。

### 4.5 动态 add

```c
cc_bool cc_runner_add(cc_task_t *runner, cc_task_t *task);
```

`add` 在 `CC_CFG_USE_MALLOC=1` 时提供。这里区分两种操作：

- **Runner attach**：分配/复用 node，把任务纳入 READY 和 alive；new/add 必须执行。
- **resume_hook**：安装 task.fn = resume_hook、task.ext = node；只有 call 返回 wait、任务离开 READY 并等待外部唤醒时才执行。

因此 add 必须完成 Runner attach，但不安装 resume_hook。具体步骤：

1. 校验 runner 仍活跃、task 合法且尚未挂接；
2. 若 FREE 非空，直接取一个内联 node；否则 malloc 一个动态 node 并设置 `cc_rnf_dynamic`；
3. 设置 node.task = task、node.runner = runner，task.fn/ext 保持原样；
4. 将 node 加入 READY 尾部，最后 alive++。

任一失败不得修改 task/runner。若由组内任务调用 add，当前 runner 会在返回后继续看到新节点；若由外部在 runner WAIT 时调用，调用方随后 resume runner。

add 的 node 同样参与 await/trans/nested-runner current 迁移，因此其生命周期跟随整条逻辑任务链，而不是仅跟随最初传入的 task 指针。只有该 current 链最终 end 时才 alive-- 并 retire node。

`capacity` 描述 nodes[] 的固定边界，但 add 是否复用直接由 FREE 是否非空决定，不能通过 `alive < capacity` 推断：alive 同时包含内联/动态节点，且与 READY、WAIT、迁移状态不同。capacity 可用于内联范围校验、retire 分类或重新初始化；若保留 DYNAMIC flag，则 range 校验作为额外防护而非必需热路径。

内联 node retire 后清掉 task/original_fn 并加入 FREE，动态 node retire 后 free。这样 add 在有空闲内联 node 时无需 malloc。

无 malloc 构建不提供动态 add；固定容量的 init_n 仍可使用。后续如需动态添加但不用堆，可增加由调用方提供 node storage 的 add_init。

## 5. 运行与 join

### 5.1 直接运行

```c
cc_task_t *cur = cc_runner_new(a, b);
cc_tr_e ret = cc_task_resume(&cur);
```

runner 没有 parent 时，所有组内任务结束后 runner 作为根任务 end，`cur` 变为 NULL。

### 5.2 在任务中 join-all

```c
cc_task_t *runner = cc_runner_new(a, b);
if (runner == NULL) {
    return cc_tr_error;
}
CC_AWAIT(t, runner);
```

现有 `CC_AWAIT` 会设置：

```c
runner->next = t;
```

因此 runner 最后 end 时会自动恢复 t。runner 不需要额外保存 up，也不需要知道 parent 的内部状态。

`CC_AWAIT` 返回前，组内所有任务必然已经 end；这是外部 parent 等待 runner 的 join-all 语义。

### 5.3 当前任务 A 加入 runner

正在运行的 A 若要创建 B 并与 B 并发，可以：

```c
cc_task_t *runner = cc_runner_new(A, B);
if (runner == NULL) {
    return cc_tr_error;
}
CC_TRANS(A, runner);
```

此时 A 既是 runner 的创建者，也是组内任务。`cc_runner_new` 必须识别 `cc_task_current() == A` 并额外处理 continuation：

1. 保存 `up = A.next`；
2. 设置 `A.next = NULL`，防止 A 提前 end 时绕过 runner；
3. 设置 `runner.next = up`，由 runner 在 alive 归 0 后恢复原调用链；
4. 包装 A、B 并加入 READY 链。

随后必须使用 `CC_TRANS(A, runner)`：它既保存 A 在创建点的续体，又把 owner 交给 runner。runner 第一次调用 A 的 original_fn 时，A 从 `CC_TRANS` 之后继续执行，B 也按 READY 链获得执行机会。

```text
创建前：P → A

创建并 trans 后：
owner → runner.next = P
          ├── A.next = NULL
          └── B.next = NULL

A、B 都 end → runner end → 恢复 P
```

不能使用 `CC_AWAIT(A, runner)`，因为这会把 runner.next 设为 A，而 A 同时在 runner 组内，形成 self-await 环。也不能在创建后直接 `resume(runner)` 而不保存 A 的当前续体，否则 A 再次执行时会回到错误位置。

self-inclusive 模式中 alive 包含 A 和 B。A 可以先于 B end，但 P 只有在 A、B 全部结束后才恢复；若要求 A 在 end 前显式等待 B，仍需另行定义组内等待协议。

## 6. READY 链不变量

READY 链只保存可运行任务：

- 新挂接且尚未执行的任务在链中；
- yield 后重新入队；
- WAIT 任务不在链中；
- 已 end 的任务不在链中，其 node 已回收；
- 一个 node 最多在链中出现一次。

由于只有 READY 链，runner 不保存 WAIT 或已结束任务清单。WAIT 任务仍由原任务指针和 node 关联，外部事件直接 resume 原任务即可找到 runner。

建议由 runner 先 pop node，再调用任务。调用期间 node 不在 READY 链中，任务返回后再按结果处理：

```text
pop node
  → original_fn 返回 yield：append 回尾部
  → original_fn 返回 wait：不 append
  → original_fn 返回 end：alive--，retire_node(node)
```

这样无需在 resume_hook 中搜索并摘除 READY 链中部节点，也避免链表迭代失效。

## 7. runner 调度步骤

首版建议每次 owner resume runner 时最多推进一个 READY 任务：

1. 如果 `alive == 0`，结束 runner。
2. 如果 READY 为空但 `alive > 0`，说明剩余任务都在 WAIT，runner 返回 `cc_tr_wait`。
3. 从 READY 头部 pop 一个 node。
4. 直接调用 `cc_task_call(node->task, node->task, node->task->ctx)`；此时 READY 任务保持原 fn/ext。
5. 返回 yield 时 requeue 最新 current；返回 wait 时才安装 resume_hook 并从 READY 移除；返回 end/current=NULL 时 alive-- 并 retire node。
6. 返回后 runner 不得再访问已经 retire 的 node。
7. 若 alive 变为 0，runner end；否则：
   - READY 非空：返回 `cc_tr_yield`；
   - READY 为空：返回 `cc_tr_wait`。

每次最多推进一个任务，可以避免若干任务持续 yield 时 runner 永不返回 owner。若后续需要一次 resume 推进一轮，可增加预算或初始队列快照，但不能无限循环。

runner 调用返回前必须把当前任务槽恢复为 runner；组内任务 end 后 runner 的当前任务槽再由 runner end/await 协议更新。

## 8. resume_hook：WAIT 外部唤醒入口

READY/RUNNING 阶段不修改 task.fn/ext，runner 直接调用原任务。只有任务返回 wait、需要离开 READY 并等待外部唤醒时，才安装：

```c
node->original_fn = (cc_tconv_func)current->fn;
current->fn = (void (*)(void))resume_hook;
current->ext = node;
```

外部单独 `cc_task_resume(current)` 时进入 resume_hook。它先恢复 current 的原 fn/ext，再调用：

```c
ret = node->original_fn(current, current, current->ctx);
```

不能通过 `cc_task_call(task, ...)` 再进入原 fn，因为此时 task.fn 已是 resume_hook，会递归。

### 8.1 yield

```text
original_fn 返回 cc_tr_yield
  → node 重新挂到 runner.ready_tail
  → 返回 runner
```

### 8.2 wait

```text
original_fn 返回 cc_tr_wait
  → node 不挂回 READY
  → node 和原任务保留
  → 返回 runner
```

此后 resume runner 不会调用该任务。

### 8.3 end

```text
original_fn 返回 cc_tr_end
  → 原 cc_task_end() 已完成原来的 free/retain 行为
  → resume_hook 只操作独立 node
  → runner.alive--
  → retire_node(node)：内联 node 停用，动态 node free
  → 返回 runner
```

自动释放任务可能已经不存在，因此 end 后 resume_hook 不得再读取 task。alive 必须在 resume_hook 中恰好减少一次，runner 不重复减少。

## 9. resume_hook：WAIT 外部唤醒入口

外部直接恢复 WAIT 原任务：

```c
cc_task_t *cur = waiting_task;
cc_task_resume(&cur);
```

此时 task.fn 仍是 resume_hook，task.ext 仍指向 node。resume_hook 能取得：

```text
task → node → original_fn
           → runner
```

首版采用“唤醒时先执行原任务一次”的语义：

1. 调用 node.original_fn，从 WAIT 点后继续；
2. 如果再次 wait：不挂链；
3. 如果 yield：挂回 runner READY 尾部；
4. 如果 end：alive--，retire_node(node)；
5. 将 current 交还 runner，必要时进入 runner 完成调度或结束。

因此“wake 后挂链”只发生在原任务随后返回 yield 时；再次 wait 不挂链，end 也不挂链。

### 9.1 唤醒后交还 runner

resume_hook 可以把 current 设为 runner 并返回，使外部 resume 返回后的 `cur` 指向 runner；下一次 resume runner 再继续 READY 调度。

若最后一个任务在本次唤醒中 end，使 alive 变为 0，则 resume_hook 应直接进入 runner 的结束路径，确保 await parent 能被恢复，而不是留下一个没有 READY 任务但尚未结束的 runner。

实现也可以选择“唤醒时只挂链并立即 tail-call runner”的两阶段模型，但首版应固定一种语义，避免同一任务在一次外部 resume 中被意外推进两次。本文优先采用“先执行一次，yield 再挂链”。

### 9.2 重复唤醒

外部只能 resume 确实处于 WAIT 且已注册对应事件的任务。重复事件、过期事件或对 READY/RUNNING 任务的额外 resume 不属于正常唤醒协议。

可以在 resume_hook/node 增加一个轻量状态位防止重复挂链，但不需要重新引入 ALL 链或完整任务状态机。

## 10. runner 结束与 parent 恢复

`alive` 初始等于组内任务总数。每个任务只在 resume_hook 的 end 路径中减少一次。

当：

```text
alive == 0
READY 为空
所有逻辑 node 均已 retire
```

runner 调用现有 `cc_task_end(runner)`：

- runner.next == NULL：作为根任务结束，owner 指针变为 NULL；
- runner.next == parent：释放 runner 并恢复 parent，从 `CC_AWAIT` 后继续。

这直接复用现有 await 结束协议，不需要 runner.up、root node、children 或额外 join 状态。

## 11. 原任务 end/free 与 retained storage

node 不保存 retain 状态，原任务仍按原有 `cc_task_end()` 规则处理：

- 非 retain 任务：自动释放；
- retain 任务：调用方 storage 保留。

resume_hook 在 original_fn 返回 end 后只使用 node 和 runner，不访问原任务。调用 original_fn 前已暂时恢复当前 task 的原 fn/ext，因此 retained 任务结束后也不会留下指向已停用 node 的 ext，无需在 node 中保存 retain 状态。

自动释放任务的 block 由原 cc_task_end() 释放；retained task storage 仍归调用方。runner 初始 block 只在 runner end 时按 new/init 所有权整体处理；动态 add node 则在其逻辑任务链 end 时单独释放。


### 11.1 有栈任务支持

有栈任务挂接时，node.original_fn 保存的是原 `_cc_tstack_resume_hook`。虽然挂接状态下 task.fn 暂时是 resume_hook，但 resume_hook 必须在每次调用前恢复原 fn/ext，再进入 original_fn：

```text
resume_hook
  → task.fn = 原 _cc_tstack_resume_hook，task.ext = NULL
  → 调用 _cc_tstack_resume_hook
  → cc_swap_start/cc_swap 进入或恢复独立任务栈
  → cc_tstack_yield/wait 返回 owner 栈
  → resume_hook 根据最新 current 重新包装或迁移
```

这样 `_cc_tstack_wrapper` 内部的 `cc_tstack_is_stackful()`、`cc_tstack_exit()` 和 `cc_task_end()` 都看到原始有栈类型，不需要修改独立栈布局、running/saved 状态或 cc_swap。

有栈任务 end 后仍在 owner 栈进入 resume_hook 的 end 处理：自动释放任务不得再访问，retained 任务保持原 fn/ext。abort 仍按原规则在下次恢复时从 cc_tstack_yield/wait/trans 返回 CC_FALSE。

挂接期间禁止 exec；`cc_tstack_is_stackful()`、`cc_tstack_canary_hit()` 等直接依赖 fn 的外部查询，应在解挂状态进行，或由 Runner 提供识别 original_fn 的辅助接口。Runner 本身仍是无栈 task，有栈支持来自被包装成员，而不是把调度器放进独立栈。

## 12. await/trans continuation 迁移

组内 A 执行普通 `CC_AWAIT(A, B)` 后，resume_hook 可能看到：

```text
original_fn(A) 返回 wait 或 yield
cc_task_current() == B，而不是 A
```

此时不能把 A 重新挂链，否则下一次会绕过 B。采用单个 node 的迁移方案：

### 12.1 从 A 迁移到 B

1. detach A：A 仍是挂起的 live task，恢复 `A.fn = node.original_fn`、`A.ext = NULL`。
2. attach B：保存 B 当前 fn 到 node.original_fn，设置 `B.fn = resume_hook`、`B.ext = node`。
3. node.task 改为 B。
4. 根据本次返回值处理：yield 则把 B 挂回 READY，wait 则保持摘链。
5. alive 不变，因为 A/B 是同一条逻辑任务链，不是新的并发成员。

此后单独 resume B 仍会进入 resume_hook，因此 B yield 能重新挂链、再次 wait 会保持摘链，不会绕过 runner。

### 12.2 B end 后迁回 A

B.next 仍指向 A。B 执行 cc_task_end 后会恢复 A；A 此时是 detach 状态，使用自己的原 fn 从 await 点后继续。

由于 A 的执行发生在 B original_fn 的返回路径中，B resume_hook 最终仍能取得本次调用的结果和最新 current：

- 如果 A yield/wait，resume_hook 把 node 从已结束的 B 迁回 A，重新包装 A，再按 yield/wait 处理；
- 如果 A 又 await C，resume_hook 可直接迁移到 C；
- 如果整个逻辑任务链 end，current 为 NULL，resume_hook 才执行 alive-- 并 retire_node(node)。

因此 node.original_fn 始终属于 node.task 当前指向的任务，而不是永久属于最初加入 runner 的 A。

### 12.3 raw TRANS 与 current-chain 所有权

普通 `cc_task_resume(&owner)` 本身只跟踪 current。A trans B 后，owner 变为 B；如果调用方没有另外保存 A，挂起的 A 同样会失去句柄并可能泄漏。Runner 沿用这一既有语义，不额外承诺回收所有脱离 current 链的 transfer source。

因此 await/trans 可以使用相同的 detach/attach 迁移：

- await：B.next 指回 A，B end 会恢复 A，resume_hook 随 current 迁回 A；
- raw trans：B.next 可以不指回 A，B end 后 current == NULL，resume_hook 将该逻辑 node 视为 end，执行 alive-- 并 retire_node(node)。

raw trans 中 detach 的 A 已恢复原 fn/ext，可以由调用方另行持有和恢复，但不再由该 runner node 跟踪。这与普通 `cc_task_resume()` 的 current-chain 规则一致，不承诺 structured join 覆盖脱离链的 A。

实现可在调用 original_fn 前暂时恢复当前 task 的原 fn/ext，返回后再包装最新 current。这样向下迁移时旧 source 已自然 detach，向上迁移时也不必解引用已自动释放的旧 child；retained 任务同样能保留原 fn/ext。

### 12.4 嵌套 runner

内层 runner 对外层而言只是 current 链上的普通 task。假设外层 R1 正在跟踪 A，A 创建 R2 并执行 `CC_AWAIT(A, R2)`：

1. R2.next = A，current 变为 R2。
2. 外层 resume_hook detach A，并把同一个外层 node attach 到 R2：保存 R2.fn，设置 R2.fn = resume_hook、R2.ext = 外层 node。
3. R2.ctx 仍是内层自己的 runner ctx，内层 READY/alive 完全独立；外层 alive 不因嵌套增加。
4. 外层 node 再被调度时，resume_hook 暂时恢复 R2.fn/ext，调用内层 runner body；内层正常推进自己的 READY 链。
5. 内层 child 的 WAIT 唤醒仍先找回 R2。即使 current 暂时是 R2，R2 自身已被外层包装，下一次恢复仍能回到 R1。
6. R2.alive 归 0 后，R2 按普通任务 end，通过 R2.next 恢复 A；外层 resume_hook 再随 current 迁回 A 或 A 的新 continuation。
7. 只有外层 A 所在 current 链最终 end，才减少 R1.alive。

```text
R1 node: A → R2 → A
                │
                └── R2 独立调度 C、D，alive 只统计 C、D
```

因此嵌套 runner 不需要 owner 冒泡或特殊 continuation 类型；它复用同一套 detach/attach/current 迁移。实现只需允许 current 是另一个 runner，并确保其 ext 未被其他扩展占用。

## 13. API 配置与错误处理

### 13.1 配置

以下接口始终可用，不依赖 malloc：

```c
cc_size cc_runner_size_n(int n);
cc_task_t *cc_runner_init_n(void *mem, cc_size size, int n, ...);
```

仅以下堆分配接口放在 `CC_CFG_USE_MALLOC=1` 下：

```c
cc_task_t *cc_runner_new_n(int n, ...);
cc_bool cc_runner_add(cc_task_t *runner, cc_task_t *task);

#define cc_runner_new(...) \
    cc_runner_new_n(CC_NARG(__VA_ARGS__), __VA_ARGS__)
```

`CC_CFG_USE_MALLOC=0` 时，runner body、resume_hook、size/init、READY 链和 alive 调度仍可完整使用，符合现有 init-only 构建方式。

不需要新的 `CC_RUN` / `CC_JOIN` 宏；现有 `CC_AWAIT` 就是 join-all。

### 13.2 可恢复创建失败

以下情况返回 NULL，不修改任何输入任务：

- runner block 分配失败、size_n 溢出、init mem 对齐或容量不足；
- 参数为 NULL 或存在重复任务；
- task 已挂接 ext；
- task.next 非 NULL；
- 除 self-inclusive 当前任务外，task 正在运行、已经结束或属于不支持的任务类型。

### 13.3 编程错误，直接 panic

- runner 调度时 node.task/ext/runner 关系损坏；
- 同一 node 重复进入 READY；
- alive 下溢；
- alive 为 0 时仍有 READY node；
- 组内任务产生无法挂接的 continuation（例如 ext 冲突）；
- 组内任务 exec 覆盖 resume_hook，或在挂接状态误用依赖原 fn 的外部扩展查询；
- WAIT 外部唤醒发现 node/runner 已失效；
- 组内任务结束后仍有未取消的外部唤醒注册。

## 14. 关键不变量

1. READY/RUNNING 任务保持原 fn/ext；只有 WAIT 状态安装 `task.fn == resume_hook`、`task.ext == node`。
2. node.original_fn 只在 WAIT hook 安装时保存当前任务的真实 fn，不得再次保存 resume_hook。
3. READY 链中每个 node 最多出现一次。
4. WAIT 任务和已 end 任务绝不在 READY 链。
5. runner 只通过 READY 链调度，不扫描或猜测 WAIT 任务。
6. WAIT 唤醒通过 `task.ext → node → runner` 找回所属 runner。
7. 每个组内任务 end 时，resume_hook 恰好执行一次 alive--。
8. 自动释放任务 end 后，不再解引用其 task 指针。
9. 逻辑任务链 end 后，内联 node 仅停用并保留在 runner block，动态 node 则 free；两者都不得继续出现在 READY。
10. alive == 0 表示全部组内任务已 end，runner 可以 end。
11. runner.next 完全沿用现有 `CC_AWAIT` parent 协议，不另建 continuation 字段。
12. await/trans 迁移期间 alive 不变，node.task/original_fn 始终描述当前被包装的 continuation；current 链最终 end 时才减少 alive。

## 15. 测试计划

### 15.1 创建与布局

- `cc_runner_new(a, b)` 创建成功，alive 初始为 2。
- 多任务和单任务 runner；CC_NARG 宏计数与显式 new_n 数量一致。
- size_n 检查负数量、乘法/加法溢出、node 对齐和 size-1 容量拒绝。
- init_n 使用对齐的调用方 block 创建 retained runner，结束时不释放该 block。
- NULL、重复任务、非空 ext/next、ended task 均返回 NULL。
- 注入统一 runner block 分配失败，验证所有输入任务保持原 fn/ext/next。
- `_desc → ext` 改名后，原有任务头大小和 ctx 偏移断言不变。

### 15.2 yield 调度

- 两个任务交替 yield，检查执行顺序和 owner 始终为 runner。
- 三个任务验证 FIFO 轮转。
- 持续 yield 的任务不会让单次 runner resume 陷入无限循环。

### 15.3 wait 唤醒

- A、B 都 WAIT 后 READY 为空，resume runner 返回 wait，A/B 均不执行。
- 单独 resume B：B 从 WAIT 后继续；A 不执行。
- B 再次 wait：不挂链。
- B yield：重新挂到 READY 尾部。
- 再单独 resume A，验证相同行为。
- WAIT 唤醒后 owner 正确回到 runner。

### 15.4 end 与 join-all

- 所有任务直接 end，alive 递减到 0，runner end。
- 部分任务先 end，其余继续 yield/wait，alive 准确。
- 最后一个任务在 runner 调度路径 end，runner 正确结束。
- 最后一个任务在外部 WAIT 唤醒路径 end，runner 也能结束并恢复 await parent。
- 根 runner end 后 owner 为 NULL。
- `CC_AWAIT(parent, runner)` 只在全部任务结束后恢复 parent。

### 15.5 原任务类型与存储

- Blocks、conventional 和有栈任务混合在一个 runner 中。
- 有栈任务多次 yield/wait 后原栈帧保持，abort 清理和最终 end 正常。
- 有栈任务 await/trans 迁移及 retained 有栈 storage 的 fn/ext 恢复正确。
- 自动释放任务 end 后无 use-after-free。
- retained 任务按选定策略恢复原 fn/ext，或在创建时被明确拒绝。
- 初始 new block 只分配一次；动态 add 的 node 各自分配/释放匹配；init_n 固定数组不调用分配器。
- 动态 add 成功时 alive++ 并加入 READY，分配/校验失败时 runner/task 不变。
- 动态 node 经 await/trans/nested runner 迁移后，仍只在整条逻辑任务链 end 时释放。

### 15.6 构建与动态检查

- 默认 `make -C test check`。
- `CC_CFG_USE_MALLOC=0` 编译通过，new/new_n/add 不可见，size_n/init_n 可正常运行。
- BLOCK=0、CONV=0、STACKFUL=0 等配置组合。
- 所有交叉目标语法/代码生成检查。
- ASan/UBSan 覆盖 yield/wait/end、外部唤醒、最后任务结束和 parent 恢复。

## 16. 复杂度与内存

- READY pop/push：O(1)。
- yield 重新入队：O(1)。
- wait：出队后不挂回，O(1)。
- end：alive--、retire_node(node)，O(1)；内联 node 不 free，动态 node free。
- 创建：O(n) 布局与初始化；首版朴素重复指针校验可为 O(n²)。
- add：O(1) 挂链；FREE 非空时无分配，否则一次动态 node 分配。
- 内存：初始 runner block 内含 ctx/nodes[]；动态 add node 独立分配，没有代理 task 或 ALL 链。

相比代理树方案，本设计没有 owner 冒泡、动态 joiner、逐 child join 或嵌套 backlink 恢复。

## 17. 建议实现顺序

1. 将 `_desc` 改名为 `ext`，更新初始化并运行现有布局测试。
2. 实现 size_n、统一 block 布局、init_n/new_n、单次 malloc 和 READY 链。
3. 实现 resume_hook 的 yield/end、alive-- 和 runner 结束。
4. 实现 WAIT 不挂链及外部直接 resume 唤醒。
5. 验证直接 resume runner 与 `CC_AWAIT(parent, runner)` 两种 join-all 路径。
6. 实现动态 add、内联/动态 node 的不同 retire 策略，再补分配失败、Blocks/conventional、retained 策略和动态检查。
7. 实现 await/trans 的 detach/attach continuation 迁移，补充 A→B→A、多层 await、raw trans 脱离链，以及 B wait/yield/end 测试。
8. 补嵌套 runner 测试：外层 node 迁移到内层 runner，内层独立调度并在 join-all 后恢复外层 A。

首版应坚持“一个 READY 链、一个 alive 计数、现有 AWAIT join-all”的最小模型，不再增加逐任务 join 或代理层级。

