# Echo：async/await 与 cc_task

[English](README.md) | **简体中文**

这个 demo 实现 TCP echo server/client：server 原样发送收到的数据，client 并发建立连接，反复发送指定长度的消息并校验回显，最后输出字节数、耗时和吞吐。

`echo.js` 是 JavaScript 参考实现，`echo.c` 使用 `cc_task + Linux epoll` 实现相同的 server/client。两者都是单线程多并发模型，每个连接由独立的异步任务推进，等待 IO 时让出执行权，因此可以同时进行大量连接。

## 设计

- listener 接收连接后创建 connection task 并注册 `EPOLLIN`；第一次 IO 事件同时负责首次启动，不需要额外的就绪队列。
- connect/read/write 是公共子任务，父任务通过 `CC_AWAIT` 等待完成并读取返回值。
- read 处理一次非阻塞读取，write 处理完整写入、部分写和背压；client 通过偏移循环读满一条消息。
- 每个 C 连接只做一次对齐分配：前 64B 是 `io_mem`，后面是数据 buffer。connect/read/write 通过 `cc_task_init()` 复用 `io_mem`，父连接结束时统一释放。
- C 使用非阻塞 socket 和 level-triggered `epoll`，重点展示并发异步代码的结构，而不是极限性能。

## 代码对应

| JavaScript | C / cc_task |
| --- | --- |
| `makeConnect()` | `make_connect()` |
| `makeRead()` / `makeWrite()` | `make_read()` / `make_write()` |
| `makeListener()` | `make_listener()` |
| `serveConnection()` | `serve_connection()` |
| `runConnection()` | `run_connection()` |
| `runServer()` / `runClient()` | `run_server()` / `run_client()` |
| `makeReader()` 返回的 `readExactly()` | client 的偏移循环 + `CC_AWAIT(make_read(...))` |
| `fillBuf()` / `checkBuf()` | `fill_buf()` / `check_buf()` |
| `Promise.all(jobs)` | 启动全部 client task，由 `epoll` 和 `CC_AWAIT` 推进 |
| Node.js event loop | `epoll_wait()` + `cc_task_resume()` |

建议先读 `echo.js` 的 `serveConnection()` 和 `runConnection()`，再对照 C 中的同名语义函数。

## 编译运行

C 版需要 Linux、clang 和 make：

```sh
make -C examples
./examples/echo server 9000
```

运行 client：

```sh
./examples/echo client 127.0.0.1 9000 1000 1024 100
```

JavaScript 版只需要 Node.js，无需安装依赖：

```sh
node examples/echo.js server 9000
node examples/echo.js client 127.0.0.1 9000 1000 1024 100
```

client 参数依次为连接数、消息长度和每连接轮次。
