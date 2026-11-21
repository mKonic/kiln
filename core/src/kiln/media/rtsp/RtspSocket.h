#pragma once

// The sockets RTSP needs (java.net.Socket and UdpDataSource's DatagramSocket), over POSIX, and a
// handler thread (android.os.Handler on a looper) the client and session run their events on.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace kiln::media::rtsp {

    // An I/O failure (IOException); bind failures say so (BindException).
    class RtspIoException : public std::runtime_error {
    public:
        explicit RtspIoException(const std::string& message, bool bindFailure = false) : std::runtime_error(message), bind(bindFailure) {}
        bool bind;
    };

    class TcpSocket {
    public:
        // Connects (throws RtspIoException).
        static TcpSocket Connect(const std::string& host, int port, int timeoutMs = 8000);
        explicit TcpSocket(int fd = -1) : m_Fd(fd) {}
        TcpSocket(TcpSocket&& o) noexcept : m_Fd(o.m_Fd) { o.m_Fd = -1; }
        TcpSocket& operator=(TcpSocket&& o) noexcept;
        ~TcpSocket() { Close(); }
        bool IsOpen() const { return m_Fd >= 0; }
        // Writes all of it (throws RtspIoException).
        void Write(const void* data, std::size_t size);
        // Reads exactly size bytes; false at the end of the stream (or after Shutdown).
        bool ReadFully(void* data, std::size_t size);
        // Wakes a blocked reader (the stream ends for it).
        void Shutdown();
        void Close();
        int Fd() const { return m_Fd; }

    private:
        int m_Fd = -1;
    };

    // A listening socket for tests (RtspServer's ServerSocket).
    class TcpListener {
    public:
        TcpListener();   // on 127.0.0.1, any port
        ~TcpListener();
        int Port() const { return m_Port; }
        // The next connection; an unopened socket once closed.
        TcpSocket Accept();
        void Close();

    private:
        int m_Fd = -1, m_Port = 0;
    };

    class UdpSocket {
    public:
        UdpSocket() = default;
        UdpSocket(UdpSocket&&) = delete;
        ~UdpSocket();
        // Binds to the port (0: any) on all addresses (throws RtspIoException with bind set).
        void Bind(int port);
        int LocalPort() const { return m_Port; }
        // One datagram: its size, 0 after the timeout, -1 once closed.
        int Receive(std::uint8_t* buffer, int capacity, int timeoutMs);
        // A datagram to 127.0.0.1:port (tests).
        void SendTo(int port, const std::uint8_t* data, int size);
        void Close();

    private:
        int m_Fd = -1, m_Port = -1;
        int m_Wake[2] = { -1, -1 };   // written by Close to wake a receiver
    };

    // Runs tasks in order on its own thread, now or after a delay; delayed ones can be cancelled.
    class TaskRunner {
    public:
        TaskRunner();
        ~TaskRunner();
        void Post(std::function<void()> task) { PostDelayed(std::move(task), 0); }
        // An id for Cancel.
        std::uint64_t PostDelayed(std::function<void()> task, std::int64_t delayMs);
        void Cancel(std::uint64_t id);
        // Runs the task on the runner and waits for it (directly when already on it).
        void RunAndWait(const std::function<void()>& task);
        bool IsCurrentThread() const { return std::this_thread::get_id() == m_Thread.get_id(); }
        // Stops after the tasks already due; later ones are dropped.
        void Stop();

    private:
        struct Task {
            std::chrono::steady_clock::time_point due;
            std::uint64_t id;
            std::function<void()> run;
        };
        void Loop();
        std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::deque<Task> m_Tasks;
        std::uint64_t m_NextId = 1;
        bool m_Quit = false;
        std::thread m_Thread;
    };

}
