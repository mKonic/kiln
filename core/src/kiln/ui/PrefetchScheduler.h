#pragma once

// PrefetchScheduler (AndroidPrefetchScheduler): work done ahead of need, such as a lazy item
// composed and measured before it scrolls in, run in the time a frame leaves before the next
// vsync. The host runs it after each frame; what does not fit waits for the next frame, or for
// an idle one.

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>

namespace kiln::ui {

    class PrefetchRequestScope {
    public:
        using Clock = std::chrono::steady_clock;
        explicit PrefetchRequestScope(Clock::time_point deadline) : m_Deadline(deadline) {}
        // availableTimeNanos: until the next frame is due.
        std::int64_t AvailableTimeNanos() const {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(m_Deadline - Clock::now()).count();
        }
    private:
        Clock::time_point m_Deadline;
    };

    class PrefetchRequest {
    public:
        virtual ~PrefetchRequest() = default;
        // Does what fits in the scope's time; returns whether work is left for a later frame.
        virtual bool Execute(PrefetchRequestScope& scope) = 0;
    };

    // Average duration of a kind of work (calculateAverageTime): biased to recent runs, as
    // RecycledViewPool does.
    inline std::int64_t AverageNanos(std::int64_t current, std::int64_t latest) {
        return current == 0 ? latest : current / 4 * 3 + latest / 4;
    }

    class PrefetchScheduler {
    public:
        using Clock = std::chrono::steady_clock;
        static PrefetchScheduler& Main();   // the thread's scheduler

        void Schedule(std::shared_ptr<PrefetchRequest> request) { m_Requests.push_back(std::move(request)); }
        bool HasWork() const { return !m_Requests.empty(); }
        // Runs requests in order until one has work left or the deadline passes; returns whether
        // any are left.
        bool Run(Clock::time_point deadline);

    private:
        std::deque<std::shared_ptr<PrefetchRequest>> m_Requests;
    };

}
