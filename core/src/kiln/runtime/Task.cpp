#include "kiln/runtime/Task.h"

#include <algorithm>

namespace kiln::rt {

    Dispatcher& Dispatcher::Main() {
        thread_local Dispatcher d;
        return d;
    }

    void Dispatcher::Post(std::function<void()> fn) {
        m_Queue.push_back(std::move(fn));
        if (wake) wake();
    }

    void Dispatcher::RunPending() {
        // Drain including work posted while draining (trampoline, AndroidUiDispatcher:77-96).
        while (!m_Queue.empty()) {
            auto queue = std::move(m_Queue);
            m_Queue.clear();
            for (auto& fn : queue) fn();
        }
        const auto now = Now();
        std::vector<Timer> due;
        for (auto it = m_Timers.begin(); it != m_Timers.end();) {
            if (it->at <= now) { due.push_back(*it); it = m_Timers.erase(it); }
            else ++it;
        }
        std::sort(due.begin(), due.end(), [](const Timer& a, const Timer& b) { return a.at < b.at || (a.at == b.at && a.id < b.id); });
        for (auto& t : due) {
            auto job = t.job.lock();
            if (!job || job->cancelled) continue;
            job->unregister = nullptr;
            t.h.resume();
        }
    }

    void Dispatcher::RunFrame(std::int64_t frameTimeNanos) {
        m_LastFrameNanos = frameTimeNanos;
        auto waiters = std::move(m_FrameWaiters);
        m_FrameWaiters.clear();
        for (auto& w : waiters) {
            auto job = w.job.lock();
            if (!job || job->cancelled) continue;
            job->unregister = nullptr;
            *w.out = frameTimeNanos;
            w.h.resume();
        }
    }

    std::optional<Dispatcher::Clock::time_point> Dispatcher::NextTimer() const {
        std::optional<Clock::time_point> next;
        for (const auto& t : m_Timers)
            if (!next || t.at < *next) next = t.at;
        return next;
    }

    void Dispatcher::AddFrameWaiter(std::coroutine_handle<> h, std::int64_t* out, const std::shared_ptr<JobState>& job) {
        const std::uint64_t id = m_NextId++;
        m_FrameWaiters.push_back({ id, h, out, job });
        job->unregister = [this, id] {
            std::erase_if(m_FrameWaiters, [id](const FrameWaiter& w) { return w.id == id; });
        };
        if (requestFrame) requestFrame();
    }

    void Dispatcher::AddTimer(Clock::time_point at, std::coroutine_handle<> h, const std::shared_ptr<JobState>& job) {
        const std::uint64_t id = m_NextId++;
        m_Timers.push_back({ id, at, h, job });
        job->unregister = [this, id] {
            std::erase_if(m_Timers, [id](const Timer& t) { return t.id == id; });
        };
        if (wake) wake();
    }

    void Job::Cancel() {
        if (!m_State || m_State->cancelled) return;
        m_State->cancelled = true;
        if (m_State->unregister) { m_State->unregister(); m_State->unregister = nullptr; }
        // Destroying the root frame destroys its locals, including any awaited child Task, which
        // destroys the child's frame in turn.
        if (m_State->root) { m_State->root.destroy(); m_State->root = {}; }
    }

    Job Launch(Dispatcher& dispatcher, Task<> task) {
        auto state = std::make_shared<JobState>();
        state->dispatcher = &dispatcher;
        auto h = task.Release();
        h.promise().job = state;
        state->root = h;
        Job job(state);
        h.resume();
        if (state->done) {   // completed synchronously: free the frame now
            state->root.destroy();
            state->root = {};
        }
        return job;
    }

}
