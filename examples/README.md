# Echo: async/await and cc_task

**English** | [简体中文](README.zh-CN.md)

This demo implements a TCP echo server and client. The server echoes every received byte unchanged, while clients establish connections concurrently, repeatedly send messages of a specified size, validate the echoed data, and report the byte count, elapsed time, and throughput.

`echo.js` is the JavaScript reference implementation. `echo.c` implements the same server and client using `cc_task + Linux epoll`. Both use a single-threaded, highly concurrent model: an independent asynchronous task drives each connection and yields while waiting for IO, allowing one thread to handle many connections at once.

## Design

- After accepting a connection, the listener creates a connection task and registers it for `EPOLLIN`. The first IO event also starts the task, so no separate ready queue is needed.
- connect, read, and write are shared child tasks. Their parents use `CC_AWAIT` to wait for completion and obtain the result.
- read performs one non-blocking read. write handles complete writes, partial writes, and backpressure. The client loops with an offset until it has read a full message.
- Each C connection uses one aligned allocation: the first 64 bytes hold `io_mem`, followed by the data buffer. connect/read/write reuse `io_mem` through `cc_task_init()`, and the parent connection releases the allocation when it ends.
- The C implementation uses non-blocking sockets and level-triggered `epoll`, focusing on clear concurrent asynchronous code rather than peak performance.

## Code Mapping

| JavaScript | C / cc_task |
| --- | --- |
| `makeConnect()` | `make_connect()` |
| `makeRead()` / `makeWrite()` | `make_read()` / `make_write()` |
| `makeListener()` | `make_listener()` |
| `serveConnection()` | `serve_connection()` |
| `runConnection()` | `run_connection()` |
| `runServer()` / `runClient()` | `run_server()` / `run_client()` |
| `readExactly()` returned by `makeReader()` | The client's offset loop + `CC_AWAIT(make_read(...))` |
| `fillBuf()` / `checkBuf()` | `fill_buf()` / `check_buf()` |
| `Promise.all(jobs)` | Start all client tasks and drive them with `epoll` and `CC_AWAIT` |
| Node.js event loop | `epoll_wait()` + `cc_task_resume()` |

Start with `serveConnection()` and `runConnection()` in `echo.js`, then compare them with their C counterparts.

## Build and Run

The C version requires Linux, clang, and make:

```sh
make -C examples
./examples/echo server 9000
```

Run the client in another terminal:

```sh
./examples/echo client 127.0.0.1 9000 1000 1024 100
```

The JavaScript version requires only Node.js, with no additional packages:

```sh
node examples/echo.js server 9000
node examples/echo.js client 127.0.0.1 9000 1000 1024 100
```

The client parameters are the connection count, message size, and rounds per connection, in that order.
