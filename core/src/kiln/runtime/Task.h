#pragma once

// Main-thread coroutines (architecture 3.7): what Compose does with kotlinx.coroutines on
// AndroidUiDispatcher (14 §dispatcher). A Task is lazy; Launch() starts it on the dispatcher and
// returns a Job that cancels it. Cancelling destroys the suspended frames, so RAII is `finally`.
//
//   kiln::rt::Task<> Blink(State<bool>& on) {
//       for (;;) { co_await Delay(500); on.Set(!on.Peek()); }
//   }
//   Job job = Launch(dispatcher, Blink(on));   // job.Cancel() or ~Job stops it

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kiln::rt {

    class Dispatcher;

    // Shared by every frame of one launched task tree.
    struct JobState {
        Dispatcher* dispatcher = nullptr;
        bool cancelled = false;
        bool done = false;
        std::function<void()> unregister;   // removes the current suspension from the dispatcher
        std::coroutine_handle<> root;
    };

    template<typename T = void> class Task;

    namespace detail {
        struct PromiseBase {
            std::shared_ptr<JobState> job;
            std::coroutine_handle<> continuation;
            std::exception_ptr error;
            std::suspend_always initial_suspend() noexcept { return {}; }
            struct Final {
                bool await_ready() noexcept { return false; }
                template<typename P>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
                    auto& p = h.promise();
                    if (p.continuation) return p.continuation;
                    if (p.job) p.job->done = true;
                    return std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            Final final_suspend() noexcept { return {}; }
            void unhandled_exception() { error = std::current_exception(); }
        };
    }

    template<typename T>
    class Task {
    public:
        struct promise_type : detail::PromiseBase {
            std::optional<T> value;
            Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
            void return_value(T v) { value = std::move(v); }
        };
        Task(Task&& o) noexcept : m_H(std::exchange(o.m_H, {})) {}
        Task& operator=(Task&& o) noexcept { if (this != &o) { Reset(); m_H = std::exchange(o.m_H, {}); } return *this; }
        ~Task() { Reset(); }

        bool await_ready() const noexcept { return false; }
        template<typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
            m_H.promise().job = parent.promise().job;
            m_H.promise().continuation = parent;
            return m_H;
        }
        T await_resume() {
            if (m_H.promise().error) std::rethrow_exception(m_H.promise().error);
            return std::move(*m_H.promise().value);
        }
        std::coroutine_handle<promise_type> Handle() const { return m_H; }
        std::coroutine_handle<promise_type> Release() { return std::exchange(m_H, {}); }

    private:
        explicit Task(std::coroutine_handle<promise_type> h) : m_H(h) {}
        void Reset() { if (m_H) m_H.destroy(); m_H = {}; }
        std::coroutine_handle<promise_type> m_H;
    };

    template<>
    class Task<void> {
    public:
        struct promise_type : detail::PromiseBase {
            Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
            void return_void() {}
        };
        Task(Task&& o) noexcept : m_H(std::exchange(o.m_H, {})) {}
        Task& operator=(Task&& o) noexcept { if (this != &o) { Reset(); m_H = std::exchange(o.m_H, {}); } return *this; }
        ~Task() { Reset(); }

        bool await_ready() const noexcept { return false; }
        template<typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
            m_H.promise().job = parent.promise().job;
            m_H.promise().continuation = parent;
            return m_H;
        }
        void await_resume() { if (m_H.promise().error) std::rethrow_exception(m_H.promise().error); }
        std::coroutine_handle<promise_type> Handle() const { return m_H; }
        std::coroutine_handle<promise_type> Release() { return std::exchange(m_H, {}); }

    private:
        explicit Task(std::coroutine_handle<promise_type> h) : m_H(h) {}
        void Reset() { if (m_H) m_H.destroy(); m_H = {}; }
        std::coroutine_handle<promise_type> m_H;
    };

    // Owns a launched task. Destroying or cancelling it stops the task at its current suspension.
    class Job {
    public:
        Job() = default;
        explicit Job(std::shared_ptr<JobState> s) : m_State(std::move(s)) {}
        Job(Job&&) noexcept = default;
        Job& operator=(Job&& o) noexcept { if (this != &o) { Cancel(); m_State = std::move(o.m_State); } return *this; }
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        ~Job() { Cancel(); }

        void Cancel();
        bool Active() const { return m_State && !m_State->done && !m_State->cancelled; }
        bool Done() const { return !m_State || m_State->done; }

    private:
        std::shared_ptr<JobState> m_State;
    };

    // The main-thread dispatcher: a task queue drained before each frame and on idle, frame
    // waiters resumed together with the vsync time, and timers.
    class Dispatcher {
    public:
        using Clock = std::chrono::steady_clock;

        void Post(std::function<void()> fn);
        // Runs posted work, then due timers. The host calls this before each frame and when idle.
        void RunPending();
        // Resumes every NextFrame() waiter with the same frame time (animations step together).
        void RunFrame(std::int64_t frameTimeNanos);
        // The frame time RunFrame last ran with (AnimationUtils.currentAnimationTimeMillis while drawing).
        std::int64_t LastFrameTimeNanos() const { return m_LastFrameNanos; }
        bool HasFrameWaiters() const { return !m_FrameWaiters.empty(); }
        // Earliest timer deadline, if any (the host arms a wake-up for it).
        std::optional<Clock::time_point> NextTimer() const;
        // This dispatcher's time: the clock plus whatever a test advanced it by. Timers are due by it.
        Clock::time_point Now() const { return Clock::now() + m_Advanced; }
        // Tests: moves Now() forward without waiting (Compose's mainClock.advanceTimeBy). Due
        // timers run at the next RunPending.
        void AdvanceTimeBy(std::chrono::milliseconds ms) { m_Advanced += ms; }

        // Host hooks: request a frame when someone waits for one, wake when work is posted.
        std::function<void()> requestFrame;
        std::function<void()> wake;

        // Used by the awaitables.
        void AddFrameWaiter(std::coroutine_handle<> h, std::int64_t* out, const std::shared_ptr<JobState>& job);
        void AddTimer(Clock::time_point at, std::coroutine_handle<> h, const std::shared_ptr<JobState>& job);

        static Dispatcher& Main();   // the thread's dispatcher

    private:
        struct FrameWaiter { std::uint64_t id; std::coroutine_handle<> h; std::int64_t* out; std::weak_ptr<JobState> job; };
        struct Timer { std::uint64_t id; Clock::time_point at; std::coroutine_handle<> h; std::weak_ptr<JobState> job; };
        std::vector<std::function<void()>> m_Queue;
        std::vector<FrameWaiter> m_FrameWaiters;
        std::vector<Timer> m_Timers;
        std::uint64_t m_NextId = 1;
        std::int64_t m_LastFrameNanos = 0;
        Clock::duration m_Advanced{};
    };

    // Starts `task` now (it runs until its first suspension) under a new Job.
    // The Job must be kept: dropping it cancels the task.
    [[nodiscard]] Job Launch(Dispatcher& dispatcher, Task<> task);

    namespace detail {
        template<typename P> std::shared_ptr<JobState> JobOf(std::coroutine_handle<P> h) { return h.promise().job; }
    }

    // co_await NextFrame() -> frame time in nanoseconds (withFrameNanos).
    struct NextFrame {
        std::int64_t time = 0;
        bool await_ready() const noexcept { return false; }
        template<typename P> void await_suspend(std::coroutine_handle<P> h) {
            auto job = detail::JobOf(h);
            job->dispatcher->AddFrameWaiter(h, &time, job);
        }
        std::int64_t await_resume() const noexcept { return time; }
    };

    // co_await Delay(ms)
    struct Delay {
        std::chrono::milliseconds ms;
        explicit Delay(std::int64_t millis) : ms(millis) {}
        bool await_ready() const noexcept { return ms.count() <= 0; }
        template<typename P> void await_suspend(std::coroutine_handle<P> h) {
            auto job = detail::JobOf(h);
            job->dispatcher->AddTimer(job->dispatcher->Now() + ms, h, job);
        }
        void await_resume() const noexcept {}
    };

    // co_await Yield(): resume after the work already queued.
    struct Yield {
        bool await_ready() const noexcept { return false; }
        template<typename P> void await_suspend(std::coroutine_handle<P> h) {
            auto job = detail::JobOf(h);
            job->dispatcher->AddTimer(job->dispatcher->Now(), h, job);
        }
        void await_resume() const noexcept {}
    };

}
