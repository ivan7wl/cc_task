// C++20 coroutines + epoll (Linux) or kqueue (macOS): one task per connection.
// This is the standard coroutine counterpart of echo.c, with no external dependencies.

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <optional>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>
#include <utility>

#if defined(__APPLE__)
#include <sys/event.h>
#include <sys/sysctl.h>
#else
#include <sys/epoll.h>
#endif

#include <unistd.h>

namespace {

constexpr std::size_t kBufferSize = 16 * 1024 - 64;
constexpr int kMaxMessageSize = 16 * 1024;
constexpr int kEventBatch = 128;
constexpr int kListenBacklog = 4096;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

[[noreturn]] void die(const char *message)
{
    std::perror(message);
    std::exit(EXIT_FAILURE);
}

bool would_block(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK;
}

// Lazy child task. Awaiting it starts it; its final suspension resumes the parent.
template <typename T>
class [[nodiscard]] Task {
public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr error;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        auto final_suspend() const noexcept
        {
            struct FinalAwaiter {
                bool await_ready() const noexcept { return false; }

                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> handle) const noexcept
                {
                    auto continuation = handle.promise().continuation;
                    return continuation ? continuation : std::noop_coroutine();
                }

                void await_resume() const noexcept {}
            };
            return FinalAwaiter{};
        }

        template <typename U>
        void return_value(U &&result)
        {
            value.emplace(std::forward<U>(result));
        }

        void unhandled_exception() noexcept { error = std::current_exception(); }
    };

    using Handle = std::coroutine_handle<promise_type>;

    Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Task &operator=(Task &&other) noexcept
    {
        if (this != &other) {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    auto operator co_await() const noexcept
    {
        struct Awaiter {
            Handle handle;

            bool await_ready() const noexcept { return handle.done(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                handle.promise().continuation = continuation;
                return handle;
            }

            T await_resume()
            {
                if (handle.promise().error)
                    std::rethrow_exception(handle.promise().error);
                return std::move(handle.promise().value.value());
            }
        };
        return Awaiter{handle_};
    }

private:
    explicit Task(Handle handle) noexcept : handle_(handle) {}

    Handle handle_;
};

// Eager fire-and-forget task. Its frame destroys itself on completion.
class DetachedTask {
public:
    struct promise_type {
        DetachedTask get_return_object() const noexcept { return {}; }
        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_never final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };
};

enum class Io {
    read,
    write,
};

class EventLoop {
public:
    class IoAwaiter {
    public:
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> task)
        {
            loop_.waiters_.insert_or_assign(fd_, task);
            loop_.arm(fd_, io_);
        }

        void await_resume() const noexcept {}

    private:
        friend class EventLoop;
        IoAwaiter(EventLoop &loop, int fd, Io io) noexcept : loop_(loop), fd_(fd), io_(io) {}

        EventLoop &loop_;
        int fd_;
        Io io_;
    };

    EventLoop() : fd_(poll_open())
    {
        if (fd_ < 0)
            die("poll_open");
    }

    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;

    ~EventLoop()
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

    IoAwaiter wait(int fd, Io io) noexcept { return IoAwaiter{*this, fd, io}; }

    void close_fd(int fd) noexcept
    {
        if (fd < 0)
            return;
        waiters_.erase(fd);
#if !defined(__APPLE__)
        ::epoll_ctl(fd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
        ::close(fd); // kqueue drops the filters of a closed descriptor.
    }

    // remaining == nullptr means a server; otherwise stop when all clients finish.
    int run(int timeout_ms, const int *remaining)
    {
        std::array<int, kEventBatch> ready{};

        for (;;) {
            if (remaining != nullptr && *remaining == 0)
                return 0;

            int count = poll_wait(ready.data(), static_cast<int>(ready.size()), timeout_ms);
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0)
                return -1;
            if (count == 0) {
                errno = ETIMEDOUT;
                return -1;
            }

            for (int i = 0; i < count; ++i) {
                auto found = waiters_.find(ready[i]);
                if (found == waiters_.end())
                    continue; // Stale readiness after a socket was closed or reused.
                auto task = found->second;
                waiters_.erase(found);
                task.resume();
            }
        }
    }

private:
    static int poll_open()
    {
#if defined(__APPLE__)
        int fd = ::kqueue();
        if (fd >= 0 && ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD, 0) | FD_CLOEXEC) < 0) {
            int error = errno;
            ::close(fd);
            errno = error;
            return -1;
        }
        return fd;
#else
        return ::epoll_create1(EPOLL_CLOEXEC);
#endif
    }

    void arm(int fd, Io io)
    {
#if defined(__APPLE__)
        struct kevent change;
        EV_SET(&change, static_cast<uintptr_t>(fd), io == Io::write ? EVFILT_WRITE : EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(fd_, &change, 1, nullptr, 0, nullptr) < 0)
            die("kevent add");
        EV_SET(&change, static_cast<uintptr_t>(fd), io == Io::write ? EVFILT_READ : EVFILT_WRITE,
               EV_DELETE, 0, 0, nullptr);
        ::kevent(fd_, &change, 1, nullptr, 0, nullptr);
#else
        struct epoll_event event {};
        event.events = io == Io::write ? EPOLLOUT : EPOLLIN;
        event.data.fd = fd;
        if (::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
            if (errno != ENOENT || ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &event) < 0)
                die("epoll_ctl");
        }
#endif
    }


    int poll_wait(int *ready, int max, int timeout_ms)
    {
#if defined(__APPLE__)
        std::array<struct kevent, kEventBatch> events{};
        struct timespec timeout;
        struct timespec *timeout_ptr = nullptr;

        if (max > kEventBatch)
            max = kEventBatch;
        if (timeout_ms >= 0) {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_nsec = static_cast<long>(timeout_ms % 1000) * 1000000L;
            timeout_ptr = &timeout;
        }
        int count = ::kevent(fd_, nullptr, 0, events.data(), max, timeout_ptr);
        for (int i = 0; i < count; ++i)
            ready[i] = static_cast<int>(events[i].ident);
        return count;
#else
        std::array<struct epoll_event, kEventBatch> events{};
        if (max > kEventBatch)
            max = kEventBatch;
        int count = ::epoll_wait(fd_, events.data(), max, timeout_ms);
        for (int i = 0; i < count; ++i)
            ready[i] = events[i].data.fd;
        return count;
#endif
    }

    int fd_;
    std::unordered_map<int, std::coroutine_handle<>> waiters_;
};

class Socket {
public:
    Socket() noexcept = default;
    Socket(EventLoop &loop, int fd) noexcept : loop_(&loop), fd_(fd) {}

    Socket(Socket &&other) noexcept
        : loop_(std::exchange(other.loop_, nullptr)), fd_(std::exchange(other.fd_, -1))
    {
    }

    Socket &operator=(Socket &&other) noexcept
    {
        if (this != &other) {
            reset();
            loop_ = std::exchange(other.loop_, nullptr);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    ~Socket() { reset(); }

    explicit operator bool() const noexcept { return fd_ >= 0; }
    int get() const noexcept { return fd_; }

    void reset() noexcept
    {
        int fd = std::exchange(fd_, -1);
        if (fd < 0)
            return;
        if (loop_ != nullptr)
            loop_->close_fd(fd);
        else
            ::close(fd);
        loop_ = nullptr;
    }

private:
    EventLoop *loop_ = nullptr;
    int fd_ = -1;
};

int set_nonblocking_cloexec(int fd)
{
#if defined(__APPLE__)
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    flags = ::fcntl(fd, F_GETFD, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return -1;
#else
    (void)fd;
#endif
    return 0;
}

Socket create_socket(EventLoop &loop)
{
#if defined(__APPLE__)
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
#else
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif
    if (fd < 0)
        return {};
    if (set_nonblocking_cloexec(fd) < 0) {
        int error = errno;
        ::close(fd);
        errno = error;
        return {};
    }
    return Socket{loop, fd};
}

Socket accept_socket(EventLoop &loop, int listener)
{
#if defined(__APPLE__)
    int fd = ::accept(listener, nullptr, nullptr);
    if (fd < 0)
        return {};
    if (set_nonblocking_cloexec(fd) < 0) {
        int error = errno;
        ::close(fd);
        errno = error;
        return {};
    }
    return Socket{loop, fd};
#else
    int fd = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    return fd < 0 ? Socket{} : Socket{loop, fd};
#endif
}


void set_nodelay(int fd)
{
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

void set_nopipe(int fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}

void report_backlog()
{
#if defined(__APPLE__)
    int somaxconn = 0;
    std::size_t size = sizeof(somaxconn);
    if (::sysctlbyname("kern.ipc.somaxconn", &somaxconn, &size, nullptr, 0) == 0 &&
        somaxconn < kListenBacklog) {
        std::fprintf(stderr,
                     "note: kern.ipc.somaxconn is %d, so bursts above that are reset;"
                     " raise it with: sudo sysctl -w kern.ipc.somaxconn=1024\n",
                     somaxconn);
    }
#endif
}

void raise_nofile()
{
    struct rlimit limit;
    if (::getrlimit(RLIMIT_NOFILE, &limit) == 0) {
        limit.rlim_cur = limit.rlim_max;
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
}

Socket listen_on(EventLoop &loop, int port)
{
    Socket socket = create_socket(loop);
    if (!socket)
        return {};

    int on = 1;
    ::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(socket.get(), reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0 ||
        ::listen(socket.get(), kListenBacklog) < 0) {
        int error = errno;
        socket.reset();
        errno = error;
        return {};
    }
    return socket;
}

Task<ssize_t> make_read(EventLoop &loop, int fd, void *buffer, std::size_t size)
{
    for (;;) {
        ssize_t count = ::recv(fd, buffer, size, 0);
        if (count >= 0)
            co_return count;
        if (errno == EINTR)
            continue;
        if (!would_block(errno))
            co_return 0;
        co_await loop.wait(fd, Io::read);
    }
}

Task<bool> make_write(EventLoop &loop, int fd, const void *buffer, std::size_t size)
{
    std::size_t offset = 0;
    const auto *bytes = static_cast<const unsigned char *>(buffer);

    while (offset < size) {
        ssize_t count = ::send(fd, bytes + offset, size - offset, kSendFlags);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && would_block(errno)) {
            co_await loop.wait(fd, Io::write);
            continue;
        }
        co_return false;
    }
    co_return true;
}

int socket_error(int fd)
{
    int error = 0;
    socklen_t size = sizeof(error);
    return ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 ? errno : error;
}

Task<Socket> make_connect(EventLoop &loop, struct sockaddr_in addr)
{
    Socket socket = create_socket(loop);
    if (!socket)
        co_return Socket{};
    set_nodelay(socket.get());
    set_nopipe(socket.get());

    int result = ::connect(socket.get(), reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr));
    if (result < 0 && errno != EINPROGRESS && errno != EINTR)
        co_return Socket{};
    if (result < 0) {
        co_await loop.wait(socket.get(), Io::write);
        if (socket_error(socket.get()) != 0)
            co_return Socket{};
    }
    co_return std::move(socket);
}

DetachedTask serve_connection(EventLoop &loop, Socket socket)
{
    set_nodelay(socket.get());
    set_nopipe(socket.get());
    std::array<unsigned char, kBufferSize> buffer;

    for (;;) {
        ssize_t count = co_await make_read(loop, socket.get(), buffer.data(), buffer.size());
        if (count <= 0)
            co_return;
        bool written = co_await make_write(loop, socket.get(), buffer.data(),
                                           static_cast<std::size_t>(count));
        if (!written)
            co_return;
    }
}

DetachedTask accept_connections(EventLoop &loop, Socket listener)
{
    for (;;) {
        Socket socket = accept_socket(loop, listener.get());
        if (!socket && would_block(errno)) {
            co_await loop.wait(listener.get(), Io::read);
            continue;
        }
        if (!socket && (errno == EINTR || errno == ECONNABORTED))
            continue;
        if (!socket)
            die("accept");
        serve_connection(loop, std::move(socket));
    }
}


void fill_buf(unsigned char *buffer, int size, int round)
{
    for (int i = 0; i < size; ++i)
        buffer[i] = static_cast<unsigned char>((i + round) & 0xff);
}

bool check_buf(const unsigned char *buffer, int size, int round)
{
    for (int i = 0; i < size; ++i) {
        if (buffer[i] != static_cast<unsigned char>((i + round) & 0xff))
            return false;
    }
    return true;
}

struct ClientStats {
    int left = 0;
    int failed = 0;
    unsigned long long bytes = 0;
};

struct ClientCompletion {
    ClientStats &stats;
    bool success = false;

    ~ClientCompletion()
    {
        if (!success)
            ++stats.failed;
        --stats.left;
    }
};

DetachedTask run_connection(EventLoop &loop, struct sockaddr_in addr, int size, int rounds,
                            bool verify, ClientStats &stats)
{
    ClientCompletion completion{stats};

    try {
        Socket socket = co_await make_connect(loop, addr);
        if (!socket)
            co_return;

        const auto buffer_size = static_cast<std::size_t>(size);
        std::unique_ptr<unsigned char[]> buffer(new unsigned char[buffer_size]);
        fill_buf(buffer.get(), size, 0);
        for (int round = 0; round < rounds; ++round) {
            if (verify)
                fill_buf(buffer.get(), size, round);
            bool written = co_await make_write(loop, socket.get(), buffer.get(), buffer_size);
            if (!written)
                co_return;

            int offset = 0;
            while (offset < size) {
                ssize_t count = co_await make_read(loop, socket.get(), buffer.get() + offset,
                                                   static_cast<std::size_t>(size - offset));
                if (count <= 0)
                    co_return;
                offset += static_cast<int>(count);
            }
            if (verify && !check_buf(buffer.get(), size, round))
                co_return;
        }

        stats.bytes += static_cast<unsigned long long>(size) * static_cast<unsigned long long>(rounds);
        completion.success = true;
    } catch (...) {
        // Allocation and unexpected child-task failures count as a failed connection.
    }
}

int run_server(int port)
{
    raise_nofile();
    report_backlog();
    EventLoop loop;
    Socket listener = listen_on(loop, port);
    if (!listener)
        die("listen");
    accept_connections(loop, std::move(listener));

    std::printf("listening 0.0.0.0:%d\n", port);
    std::fflush(stdout);
    if (loop.run(-1, nullptr) < 0)
        die("poll_wait");
    return 0;
}

int run_client(const char *host, int port, int connections, int size, int rounds, bool verify)
{
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        std::fprintf(stderr, "client: host must be a numeric IPv4 address\n");
        return 2;
    }

    raise_nofile();
    EventLoop loop;
    ClientStats stats;
    stats.left = connections;
    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < connections; ++i)
        run_connection(loop, addr, size, rounds, verify, stats);

    if (loop.run(10000, &stats.left) < 0) {
        std::perror("client event loop");
        std::fprintf(stderr, "client: unfinished=%d failed=%d\n", stats.left, stats.failed);
        return 1;
    }

    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (seconds <= 0.0)
        seconds = 1e-9;
    std::printf("connections=%d failed=%d echoed=%llu bytes time=%.3fs rate=%.1f MiB/s\n",
                connections, stats.failed, stats.bytes, seconds,
                static_cast<double>(stats.bytes) / seconds / (1024.0 * 1024.0));
    return stats.failed == 0 ? 0 : 1;
}

int parse_number(const char *text, int min, int max)
{
    char *end;
    errno = 0;
    long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max)
        return -1;
    return static_cast<int>(value);
}

int parse_mode(const char *text)
{
    if (std::strcmp(text, "verify") == 0)
        return 1;
    if (std::strcmp(text, "bench") == 0)
        return 0;
    return -1;
}

void usage(const char *program)
{
    std::fprintf(stderr,
                 "usage:\n"
                 "  %s server <port>\n"
                 "  %s client <host> <port> <connections> <messageSize> <rounds> [verify|bench]\n"
                 "\n"
                 "verify (default) refills and checks every echoed byte, as the example does.\n"
                 "bench fills the payload once and only counts the echoed bytes, so a timing\n"
                 "run does not also measure payload generation.\n",
                 program, program);
}


} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc == 3 && std::strcmp(argv[1], "server") == 0) {
            int port = parse_number(argv[2], 1, 65535);
            if (port >= 0)
                return run_server(port);
        } else if ((argc == 7 || argc == 8) && std::strcmp(argv[1], "client") == 0) {
            int port = parse_number(argv[3], 1, 65535);
            int connections = parse_number(argv[4], 1, 1000000);
            int size = parse_number(argv[5], 1, kMaxMessageSize);
            int rounds = parse_number(argv[6], 1, 1000000);
            int verify = argc == 8 ? parse_mode(argv[7]) : 1;
            if (port >= 0 && connections >= 0 && size >= 0 && rounds >= 0 && verify >= 0)
                return run_client(argv[2], port, connections, size, rounds, verify != 0);
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "echo_cpp: %s\n", error.what());
        return 1;
    }

    usage(argv[0]);
    return 2;
}
