// epoll (Linux) or kqueue (macOS) + stackless cc_task: one task per connection, no thread per connection.
// Compare this file with echo.js: arm(...) + CC_WAIT(t) is the C version of await.

#define _GNU_SOURCE

#include "cc_task.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/event.h>
#include <sys/sysctl.h>
#else
#include <sys/epoll.h>
#endif

#define BUF_SIZE (16 * 1024 - 64) // 64B io_mem + data buf = 16 KiB per server connection
#define MAX_MESSAGE_SIZE (16 * 1024)
#define EV_BATCH 128
#define LISTEN_BACKLOG 4096 // macOS clamps this to kern.ipc.somaxconn, 128 by default

// The only two wait directions this demo needs: each backend maps them to its own filters.
#define IO_READ 1
#define IO_WRITE 2

#if defined(MSG_NOSIGNAL)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0 // macOS reports a dead peer through SO_NOSIGPIPE, see set_nopipe()
#endif

static int g_pfd = -1; // epoll or kqueue descriptor
static int g_left, g_failed;
static unsigned long long g_bytes;

static void die(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

#if defined(__APPLE__)

// --- kqueue backend -----------------------------------------------------------

static int poll_open(void)
{
    int kq = kqueue();
    if (kq >= 0 && fcntl(kq, F_SETFD, fcntl(kq, F_GETFD, 0) | FD_CLOEXEC) < 0) {
        int error = errno;
        close(kq);
        errno = error;
        return -1;
    }
    return kq;
}

static void arm(int fd, cc_task_t *t, int io)
{
    struct kevent change;

    // level triggered like epoll: no EV_CLEAR, so a ready descriptor keeps firing
    EV_SET(&change, (uintptr_t)fd, io == IO_WRITE ? EVFILT_WRITE : EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, t);
    if (kevent(g_pfd, &change, 1, NULL, 0, NULL) < 0)
        die("kevent add");
    // kqueue keeps one filter per direction, so drop the other one; ENOENT means it was not armed
    EV_SET(&change, (uintptr_t)fd, io == IO_WRITE ? EVFILT_READ : EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(g_pfd, &change, 1, NULL, 0, NULL);
}

static void close_fd(int fd)
{
    if (fd >= 0)
        close(fd); // kqueue drops the filters of a closed descriptor
}

static int poll_wait(cc_task_t **ready, int max, int timeout_ms)
{
    struct kevent events[EV_BATCH];
    struct timespec timeout, *timeout_ptr = NULL;
    int n, i;

    if (max > EV_BATCH)
        max = EV_BATCH;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    n = kevent(g_pfd, NULL, 0, events, max, timeout_ptr);
    if (n < 0)
        return -1;
    for (i = 0; i < n; ++i)
        ready[i] = (cc_task_t *)events[i].udata;
    return n;
}

#else

// --- epoll backend ------------------------------------------------------------

static int poll_open(void)
{
    return epoll_create1(EPOLL_CLOEXEC);
}

static void arm(int fd, cc_task_t *t, int io)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = io == IO_WRITE ? EPOLLOUT : EPOLLIN; // level triggered keeps this small demo easy to reason about
    event.data.ptr = t;
    if (epoll_ctl(g_pfd, EPOLL_CTL_MOD, fd, &event) < 0) {
        if (errno != ENOENT || epoll_ctl(g_pfd, EPOLL_CTL_ADD, fd, &event) < 0)
            die("epoll_ctl");
    }
}

static void close_fd(int fd)
{
    if (fd >= 0) {
        epoll_ctl(g_pfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
    }
}

static int poll_wait(cc_task_t **ready, int max, int timeout_ms)
{
    struct epoll_event events[EV_BATCH];
    int n, i;

    if (max > EV_BATCH)
        max = EV_BATCH;
    n = epoll_wait(g_pfd, events, max, timeout_ms);
    if (n < 0)
        return -1;
    for (i = 0; i < n; ++i)
        ready[i] = (cc_task_t *)events[i].data.ptr;
    return n;
}

#endif

// SOCK_NONBLOCK and SOCK_CLOEXEC only exist on Linux; macOS sets both with fcntl().
static int set_nonblocking_cloexec(int fd)
{
#if defined(__APPLE__)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return -1;
#else
    (void)fd;
#endif
    return 0;
}

static int create_socket(void)
{
#if defined(__APPLE__)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
#else
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif

    if (fd < 0)
        return -1;
    if (set_nonblocking_cloexec(fd) < 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
}

static int accept_socket(int lfd)
{
#if defined(__APPLE__)
    int fd = accept(lfd, NULL, NULL);

    if (fd < 0)
        return -1;
    if (set_nonblocking_cloexec(fd) < 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
#else
    return accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
}

static void set_nodelay(int fd)
{
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

// MSG_NOSIGNAL does not exist on macOS; SO_NOSIGPIPE keeps send() from raising SIGPIPE instead.
static void set_nopipe(int fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}

// macOS clamps the backlog to kern.ipc.somaxconn, so a larger client burst is reset
// instead of queued. Say so once at startup rather than letting it look like a bug.
static void report_backlog(void)
{
#if defined(__APPLE__)
    int somaxconn = 0;
    size_t size = sizeof(somaxconn);
    if (sysctlbyname("kern.ipc.somaxconn", &somaxconn, &size, NULL, 0) == 0 &&
        somaxconn < LISTEN_BACKLOG) {
        fprintf(stderr, "note: kern.ipc.somaxconn is %d, so bursts above that are reset;"
                        " raise it with: sudo sysctl -w kern.ipc.somaxconn=1024\n",
                somaxconn);
    }
#endif
}

static void raise_nofile(void)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
        limit.rlim_cur = limit.rlim_max;
        setrlimit(RLIMIT_NOFILE, &limit);
    }
}

static cc_task_t *make_read(void *mem, int fd, void *buf, size_t len)
{
    cc_task_t *task = cc_task_init(mem, 64, CC_TASK(t)
    {
        cc_intptr n;

        CC_BEGIN(t);
        for (;;) {
            n = (cc_intptr)recv(fd, buf, len, 0);
            if (n >= 0)
                break;
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                n = 0;
                break;
            }
            arm(fd, t, IO_READ);
            CC_WAIT(t);
        }
        CC_END(t, n);
    });

    if (task == NULL)
        die("cc_task_init 64");
    return task;
}

static cc_task_t *make_write(void *mem, int fd, const void *buf, size_t len)
{
    cc_size off = 0;
    cc_task_t *task = cc_task_init(mem, 64, CC_TASK(t)
    {
        CC_BEGIN(t);
        while (off < len) {
            cc_intptr n = (cc_intptr)send(fd, (const unsigned char *)buf + off, len - off, SEND_FLAGS);
            if (n > 0) {
                $(off) += (cc_size)n;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                arm(fd, t, IO_WRITE);
                CC_WAIT(t);
                continue;
            }
            break;
        }
        CC_END(t, off == len);
    });

    if (task == NULL)
        die("cc_task_init 64");
    return task;
}

static cc_task_t *serve_connection(int fd)
{
    void *io_mem = aligned_alloc(64, 64 + BUF_SIZE);
    cc_task_t *task;

    if (io_mem == NULL)
        return NULL;
    set_nodelay(fd);
    set_nopipe(fd);

    task = cc_task_new(CC_TASK(t)
    {
        unsigned char *buf = (unsigned char *)io_mem + 64;

        CC_BEGIN(t);
        for (;;) {
            cc_uintptr ret;

            CC_AWAIT(t, make_read(io_mem, fd, buf, BUF_SIZE), ret);
            if (ret == 0) // EOF or a connection error
                break;

            CC_AWAIT(t, make_write(io_mem, fd, buf, (size_t)ret), ret);
            if (ret == 0)
                break;
        }
        close_fd(fd);
        free(io_mem);
        CC_END(t);
    });

    if (task != NULL) {
        arm(fd, task, IO_READ); // the first event also starts the task
    } else {
        free(io_mem);
    }
    return task;
}

static int listen_on(int port)
{
    int fd = create_socket();
    int on = 1;
    struct sockaddr_in addr;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, LISTEN_BACKLOG) < 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
}

static cc_task_t *make_listener(int port)
{
    int lfd = listen_on(port);
    cc_task_t *task;

    if (lfd < 0)
        die("listen");

    task = cc_task_new(CC_TASK(t)
    {
        CC_BEGIN(t);
        for (;;) {
            int fd = accept_socket(lfd);
            if (fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                arm(lfd, t, IO_READ);
                CC_WAIT(t); // await the next connection
                continue;
            }
            if (fd < 0 && (errno == EINTR || errno == ECONNABORTED))
                continue;
            if (fd < 0)
                die("accept");

            if (serve_connection(fd) == NULL)
                close(fd); // allocation failure only rejects this connection
        }
        CC_END(t);
    });

    if (task != NULL)
        arm(lfd, task, IO_READ); // first connection starts the listener task
    return task;
}

static int socket_error(int fd)
{
    int error = 0;
    socklen_t size = sizeof(error);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 ? errno : error;
}

static cc_task_t *make_connect(void *mem, struct sockaddr_in addr)
{
    int fd = -1;
    cc_task_t *task = cc_task_init(mem, 64, CC_TASK(t)
    {
        int n;

        CC_BEGIN(t);
        $(fd) = create_socket();
        if (fd < 0)
            CC_EXIT(t, 0);
        set_nodelay(fd);
        set_nopipe(fd);

        n = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
        if (n < 0 && errno != EINPROGRESS && errno != EINTR) {
            close_fd(fd);
            CC_EXIT(t, 0);
        }
        if (n < 0) {
            arm(fd, t, IO_WRITE);
            CC_WAIT(t);
            if (socket_error(fd) != 0) {
                close_fd(fd);
                CC_EXIT(t, 0);
            }
        }
        CC_END(t, fd + 1); // reserve 0 for failure
    });

    if (task == NULL)
        die("cc_task_init 64");
    return task;
}

static void fill_buf(unsigned char *buf, int len, int round)
{
    int i;
    for (i = 0; i < len; ++i)
        buf[i] = (unsigned char)((i + round) & 0xff);
}

static int check_buf(const unsigned char *buf, int len, int round)
{
    int i;
    for (i = 0; i < len; ++i)
        if (buf[i] != (unsigned char)((i + round) & 0xff))
            return -1;
    return 0;
}

static cc_task_t *run_connection(struct sockaddr_in addr, int len, int rounds)
{
    int fd = -1, off = 0, round = 0, err = 0;
    size_t alloc_size = (64 + (size_t)len + 63) & ~(size_t)63;
    void *io_mem = aligned_alloc(64, alloc_size);
    cc_task_t *task;

    if (io_mem == NULL)
        return NULL;

    task = cc_task_new(CC_TASK(t)
    {
        unsigned char *buf = (unsigned char *)io_mem + 64;
        cc_uintptr ret;

        CC_BEGIN(t);
        CC_AWAIT(t, make_connect(io_mem, addr), ret);
        if (ret == 0) {
            $(err) = 1;
            goto out;
        }
        $(fd) = (int)ret - 1;

        while (round < rounds) {
            fill_buf(buf, len, round);
            CC_AWAIT(t, make_write(io_mem, fd, buf, (size_t)len), ret);
            if (ret == 0) {
                $(err) = 1;
                goto out;
            }

            $(off) = 0;
            while (off < len) {
                CC_AWAIT(t, make_read(io_mem, fd, buf + off, (size_t)(len - off)), ret);
                if (ret == 0) {
                    $(err) = 1;
                    goto out;
                }
                $(off) += (int)ret;
            }
            if (check_buf(buf, len, round) < 0) {
                $(err) = 1;
                goto out;
            }
            $(round)++;
        }

    out:
        close_fd(fd);
        free(io_mem);
        if (err)
            g_failed++;
        else
            g_bytes += (unsigned long long)len * (unsigned long long)rounds;
        g_left--;
        CC_END(t);
    });

    if (task == NULL)
        free(io_mem);
    return task;
}

// Resume tasks directly at the top level; never nested inside another task.
// remaining == NULL means a server; otherwise stop when all clients have ended.
static int run_events(int timeout_ms, const int *remaining)
{
    cc_task_t *ready[EV_BATCH];

    for (;;) {
        int n, i;
        if (remaining != NULL && *remaining == 0)
            return 0;

        n = poll_wait(ready, EV_BATCH, timeout_ms);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -1;
        if (n == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        for (i = 0; i < n; ++i) {
            cc_task_t *t = ready[i];
            cc_task_resume(&t);
        }
    }
}

static int run_server(int port)
{
    raise_nofile();
    report_backlog();
    g_pfd = poll_open();
    if (g_pfd < 0)
        die("poll_open");
    if (make_listener(port) == NULL)
        die("cc_task_new");
    printf("listening 0.0.0.0:%d\n", port);
    fflush(stdout);
    if (run_events(-1, NULL) < 0)
        die("poll_wait");
    return 0;
}

static int run_client(const char *host, int port, int connections, int len, int rounds)
{
    struct sockaddr_in addr;
    struct timespec started, ended;
    double seconds;
    int i;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "client: host must be a numeric IPv4 address\n");
        return 2;
    }

    raise_nofile();
    g_pfd = poll_open();
    if (g_pfd < 0)
        die("poll_open");

    g_left = connections;
    clock_gettime(CLOCK_MONOTONIC, &started);
    for (i = 0; i < connections; ++i) {
        cc_task_t *client = run_connection(addr, len, rounds);
        if (client == NULL)
            die("cc_task_new");
        cc_task_resume(&client); // top-level start: run to the first CC_WAIT or end
    }

    if (run_events(10000, &g_left) < 0) {
        perror("client event loop");
        fprintf(stderr, "client: unfinished=%d failed=%d\n", g_left, g_failed);
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &ended);
    seconds =
        (double)(ended.tv_sec - started.tv_sec) + (double)(ended.tv_nsec - started.tv_nsec) / 1e9;
    if (seconds <= 0.0)
        seconds = 1e-9;
    printf("connections=%d failed=%d echoed=%llu bytes time=%.3fs rate=%.1f MiB/s\n", connections,
           g_failed, g_bytes, seconds, (double)g_bytes / seconds / (1024.0 * 1024.0));

    close(g_pfd);
    g_pfd = -1;
    return g_failed == 0 ? 0 : 1;
}

static int parse_number(const char *text, int min, int max)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max)
        return -1;
    return (int)value;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s server <port>\n"
            "  %s client <host> <port> <connections> <messageSize> <rounds>\n",
            program, program);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "server") == 0) {
        int port = parse_number(argv[2], 1, 65535);
        if (port >= 0)
            return run_server(port);
    } else if (argc == 7 && strcmp(argv[1], "client") == 0) {
        int port = parse_number(argv[3], 1, 65535);
        int connections = parse_number(argv[4], 1, 1000000);
        int len = parse_number(argv[5], 1, MAX_MESSAGE_SIZE);
        int rounds = parse_number(argv[6], 1, 1000000);
        if (port >= 0 && connections >= 0 && len >= 0 && rounds >= 0)
            return run_client(argv[2], port, connections, len, rounds);
    }
    usage(argv[0]);
    return 2;
}
