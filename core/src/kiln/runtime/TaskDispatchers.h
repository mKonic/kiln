#pragma once

// Work off the main thread (ui platform/TaskDispatchers.kt, kotlinx Dispatchers.Default / IO,
// withContext): pools of worker threads, and an awaitable that runs a function on one and
// resumes the waiting task on its own dispatcher with the result.
//
// The function runs on another thread: it must not touch composition state, nodes or anything
// else of the UI; hand it copies and return a value. A task cancelled meanwhile is not resumed
// (the function still runs to its end and its result is dropped).

#include "kiln/runtime/Task.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace kiln::rt {

    // A pool of worker threads, started as work arrives (up to `maxThreads`) and joined on
    // destruction after the queued work ran.
    class BackgroundDispatcher {
    public:
        BackgroundDispatcher(unsigned maxThreads, std::string name);
        ~BackgroundDispatcher();
        BackgroundDispatcher(const BackgroundDispatcher&) = delete;
        BackgroundDispatcher& operator=(const BackgroundDispatcher&) = delete;
        void Execute(std::function<void()> work);
        unsigned MaxThreads() const { return m_Max; }
        const std::string& Name() const { return m_Name; }

    private:
        void Run();
        unsigned m_Max;
        std::string m_Name;
        std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::deque<std::function<void()>> m_Queue;
        std::vector<std::thread> m_Threads;
        unsigned m_Idle = 0;
        bool m_Stop = false;
    };

    // TaskDispatchers: Default for CPU-bound work (a thread per core), IO for blocking I/O (at
    // least 64 threads, as Dispatchers.IO).
    class TaskDispatchers {
    public:
        virtual ~TaskDispatchers() = default;
        virtual BackgroundDispatcher& Default() const = 0;
        virtual BackgroundDispatcher& IO() const = 0;
        // The process-wide pools.
        static const TaskDispatchers& Platform();
    };

    namespace detail {
        template<typename T>
        struct BackgroundResult {
            using Value = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
            std::optional<Value> value;
            std::exception_ptr error;
            std::atomic<bool> abandoned{ false };
        };
    }

    // co_await WithContext(pool, fn): fn() on the pool; the task resumes on its dispatcher.
    template<typename T>
    struct WithContextAwaitable {
        BackgroundDispatcher* pool;
        std::function<T()> fn;
        std::shared_ptr<detail::BackgroundResult<T>> result = std::make_shared<detail::BackgroundResult<T>>();

        bool await_ready() const noexcept { return false; }
        template<typename P>
        void await_suspend(std::coroutine_handle<P> h) {
            std::shared_ptr<JobState> job = h.promise().job;
            Dispatcher* home = job && job->dispatcher ? job->dispatcher : &Dispatcher::Main();
            std::weak_ptr<JobState> weak = job;
            if (job) job->unregister = [r = result] { r->abandoned = true; };
            pool->Execute([r = result, f = std::move(fn), home, weak, h]() mutable {
                try {
                    if constexpr (std::is_void_v<T>) { f(); r->value.emplace(); }
                    else r->value.emplace(f());
                } catch (...) {
                    r->error = std::current_exception();
                }
                home->PostFromAnyThread([r, weak, h] {
                    if (r->abandoned) return;
                    auto j = weak.lock();
                    if (j) {
                        if (j->cancelled) return;
                        j->unregister = nullptr;
                    }
                    ResumeJob(j, h);
                });
            });
        }
        T await_resume() {
            if (result->error) std::rethrow_exception(result->error);
            if constexpr (!std::is_void_v<T>) return std::move(*result->value);
        }
    };

    template<typename F>
    auto WithContext(BackgroundDispatcher& pool, F&& fn) {
        using T = std::invoke_result_t<F>;
        return WithContextAwaitable<T>{ &pool, std::function<T()>(std::forward<F>(fn)) };
    }

}
