// POSIX sockets for RTSP, and the task runner.
#include "kiln/media/rtsp/RtspSocket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <future>

namespace kiln::media::rtsp {

    namespace {
        std::string Errno(const std::string& what) { return what + ": " + std::strerror(errno); }
    }

    // --- TcpSocket ------------------------------------------------------------------------------
    TcpSocket TcpSocket::Connect(const std::string& host, int port, int timeoutMs) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        const std::string service = std::to_string(port);
        if (const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &result); rc != 0)
            throw RtspIoException("cannot resolve " + host + ": " + gai_strerror(rc));
        std::string error = "cannot connect to " + host + ":" + service;
        for (addrinfo* a = result; a; a = a->ai_next) {
            const int fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
            if (fd < 0) continue;
            const int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            int rc = connect(fd, a->ai_addr, a->ai_addrlen);
            if (rc != 0 && errno == EINPROGRESS) {
                pollfd p{ fd, POLLOUT, 0 };
                rc = poll(&p, 1, timeoutMs) == 1 ? 0 : -1;
                int soError = 0;
                socklen_t len = sizeof soError;
                if (rc == 0 && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0 || soError != 0)) {
                    errno = soError;
                    rc = -1;
                }
            }
            if (rc == 0) {
                fcntl(fd, F_SETFL, flags);
                const int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                freeaddrinfo(result);
                return TcpSocket(fd);
            }
            error = Errno(error);
            close(fd);
        }
        freeaddrinfo(result);
        throw RtspIoException(error);
    }

    TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
        if (this != &o) {
            Close();
            m_Fd = o.m_Fd;
            o.m_Fd = -1;
        }
        return *this;
    }

    void TcpSocket::Write(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        while (size > 0) {
            const ssize_t n = send(m_Fd, p, size, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw RtspIoException(Errno("RTSP send"));
            }
            p += n;
            size -= std::size_t(n);
        }
    }

    bool TcpSocket::ReadFully(void* data, std::size_t size) {
        auto* p = static_cast<std::uint8_t*>(data);
        while (size > 0) {
            const ssize_t n = recv(m_Fd, p, size, 0);
            if (n == 0) return false;
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += n;
            size -= std::size_t(n);
        }
        return true;
    }

    void TcpSocket::Shutdown() {
        if (m_Fd >= 0) shutdown(m_Fd, SHUT_RDWR);
    }

    void TcpSocket::Close() {
        if (m_Fd >= 0) close(m_Fd);
        m_Fd = -1;
    }

    // --- TcpListener ----------------------------------------------------------------------------
    TcpListener::TcpListener() {
        m_Fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof a;
        if (m_Fd < 0 || bind(m_Fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(m_Fd, 1) != 0 ||
            getsockname(m_Fd, reinterpret_cast<sockaddr*>(&a), &len) != 0)
            throw RtspIoException(Errno("listen"));
        m_Port = ntohs(a.sin_port);
    }

    TcpListener::~TcpListener() { Close(); }

    TcpSocket TcpListener::Accept() {
        const int fd = accept4(m_Fd, nullptr, nullptr, SOCK_CLOEXEC);
        return TcpSocket(fd < 0 ? -1 : fd);
    }

    void TcpListener::Close() {
        if (m_Fd >= 0) {
            shutdown(m_Fd, SHUT_RDWR);
            close(m_Fd);
        }
        m_Fd = -1;
    }

    // --- UdpSocket ------------------------------------------------------------------------------
    UdpSocket::~UdpSocket() {
        Close();
        for (int& fd : m_Wake)
            if (fd >= 0) close(fd), fd = -1;
    }

    void UdpSocket::Bind(int port) {
        m_Fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (m_Fd < 0) throw RtspIoException(Errno("UDP socket"));
        if (pipe2(m_Wake, O_CLOEXEC | O_NONBLOCK) != 0) m_Wake[0] = m_Wake[1] = -1;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(std::uint16_t(port));
        if (bind(m_Fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            const std::string message = Errno("UDP bind to " + std::to_string(port));
            Close();
            throw RtspIoException(message, true);
        }
        socklen_t len = sizeof a;
        getsockname(m_Fd, reinterpret_cast<sockaddr*>(&a), &len);
        m_Port = ntohs(a.sin_port);
        // Room for a burst of video packets.
        const int buffer = 1 << 20;
        setsockopt(m_Fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof buffer);
    }

    int UdpSocket::Receive(std::uint8_t* buffer, int capacity, int timeoutMs) {
        if (m_Fd < 0) return -1;
        pollfd p[2] = { { m_Fd, POLLIN, 0 }, { m_Wake[0], POLLIN, 0 } };
        const int rc = poll(p, m_Wake[0] >= 0 ? 2 : 1, timeoutMs);
        if (rc == 0) return 0;
        if (rc < 0 || (p[0].revents & (POLLERR | POLLNVAL)) || (m_Wake[0] >= 0 && p[1].revents)) return -1;
        const ssize_t n = recv(m_Fd, buffer, std::size_t(capacity), 0);
        return n < 0 ? -1 : int(n);
    }

    void UdpSocket::SendTo(int port, const std::uint8_t* data, int size) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(std::uint16_t(port));
        const int fd = m_Fd >= 0 ? m_Fd : socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        sendto(fd, data, std::size_t(size), 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
        if (fd != m_Fd) close(fd);
    }

    void UdpSocket::Close() {
        if (m_Wake[1] >= 0) {
            const char wake = 1;
            (void)!write(m_Wake[1], &wake, 1);
        }
        if (m_Fd >= 0) {
            shutdown(m_Fd, SHUT_RDWR);
            close(m_Fd);
        }
        m_Fd = -1;
        // The pipe stays until the socket goes (a receiver may still be polling it).
    }

    // --- TaskRunner -----------------------------------------------------------------------------
    TaskRunner::TaskRunner() : m_Thread([this] { Loop(); }) {}

    TaskRunner::~TaskRunner() { Stop(); }

    std::uint64_t TaskRunner::PostDelayed(std::function<void()> task, std::int64_t delayMs) {
        std::lock_guard lock(m_Mutex);
        const std::uint64_t id = m_NextId++;
        const auto due = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max<std::int64_t>(delayMs, 0));
        // Ordered by due time, then posting order.
        auto it = std::upper_bound(m_Tasks.begin(), m_Tasks.end(), due, [](const auto& d, const Task& t) { return d < t.due; });
        m_Tasks.insert(it, Task{ due, id, std::move(task) });
        m_Cv.notify_all();
        return id;
    }

    void TaskRunner::Cancel(std::uint64_t id) {
        std::lock_guard lock(m_Mutex);
        m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(), [id](const Task& t) { return t.id == id; }), m_Tasks.end());
    }

    void TaskRunner::RunAndWait(const std::function<void()>& task) {
        if (IsCurrentThread()) {
            task();
            return;
        }
        std::promise<void> done;
        auto future = done.get_future();
        Post([&] {
            try {
                task();
                done.set_value();
            } catch (...) {
                done.set_exception(std::current_exception());
            }
        });
        future.get();
    }

    void TaskRunner::Stop() {
        {
            std::lock_guard lock(m_Mutex);
            if (m_Quit) return;
            m_Quit = true;
            m_Tasks.clear();
        }
        m_Cv.notify_all();
        if (m_Thread.joinable() && !IsCurrentThread()) m_Thread.join();
        else if (m_Thread.joinable()) m_Thread.detach();
    }

    void TaskRunner::Loop() {
        std::unique_lock lock(m_Mutex);
        while (!m_Quit) {
            if (m_Tasks.empty()) {
                m_Cv.wait(lock);
                continue;
            }
            const auto due = m_Tasks.front().due;
            if (std::chrono::steady_clock::now() < due) {
                m_Cv.wait_until(lock, due);
                continue;
            }
            Task task = std::move(m_Tasks.front());
            m_Tasks.pop_front();
            lock.unlock();
            try {
                task.run();
            } catch (...) {
                // A task's failure is its own; the runner goes on.
            }
            lock.lock();
        }
    }

}
