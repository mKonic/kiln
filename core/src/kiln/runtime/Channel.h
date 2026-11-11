#pragma once

// A buffered single-receiver channel for coroutines (kotlinx Channel(BUFFERED) as used by
// PredictiveBackHandler): TrySend drops when the buffer is full, Close ends the stream after the
// buffered values, and a cancelled receiver simply stops waiting. Overflow::DropOldest makes
// room instead (Channel(CONFLATED) is capacity 1 with DropOldest).

#include "kiln/runtime/Task.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace kiln::rt {

    template<typename T>
    class Channel {
    public:
        // What TrySend does when the buffer is full: fail (BufferOverflow.SUSPEND as trySend sees
        // it), or drop the oldest value (DROP_OLDEST).
        enum class Overflow : std::uint8_t { Fail, DropOldest };
        explicit Channel(std::size_t capacity = 64, Overflow overflow = Overflow::Fail) : m_Capacity(capacity), m_Overflow(overflow) {}
        // Channel(CONFLATED): only the latest unreceived value is kept.
        static std::unique_ptr<Channel> Conflated() { return std::make_unique<Channel>(1, Overflow::DropOldest); }
        Channel(const Channel&) = delete;
        Channel& operator=(const Channel&) = delete;

        bool TrySend(T value) {
            if (m_Closed) return false;
            if (m_Buffer.size() >= m_Capacity) {
                if (m_Overflow != Overflow::DropOldest || m_Buffer.empty()) return false;
                m_Buffer.pop_front();
            }
            m_Buffer.push_back(std::move(value));
            ResumeReceiver();
            return true;
        }
        void Close() {
            if (m_Closed) return;
            m_Closed = true;
            ResumeReceiver();
        }
        bool Closed() const { return m_Closed; }

        struct Receive {
            Channel* ch;
            bool await_ready() const noexcept { return !ch->m_Buffer.empty() || ch->m_Closed; }
            template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                auto job = detail::JobOf(h);
                ch->m_Waiter = h;
                ch->m_WaiterJob = job;
                Channel* c = ch;
                job->unregister = [c] { c->m_Waiter = {}; c->m_WaiterJob.reset(); };
            }
            // nullopt once the channel is closed and drained.
            std::optional<T> await_resume() {
                if (ch->m_Buffer.empty()) return std::nullopt;
                T v = std::move(ch->m_Buffer.front());
                ch->m_Buffer.pop_front();
                return v;
            }
        };
        // co_await channel.Next() -> std::optional<T>
        Receive Next() { return { this }; }

    private:
        void ResumeReceiver() {
            if (!m_Waiter) return;
            auto h = std::exchange(m_Waiter, {});
            auto job = m_WaiterJob.lock();
            m_WaiterJob.reset();
            if (!job || job->cancelled) return;
            job->unregister = nullptr;
            ResumeJob(job, h);
        }

        std::size_t m_Capacity;
        Overflow m_Overflow;
        std::deque<T> m_Buffer;
        bool m_Closed = false;
        std::coroutine_handle<> m_Waiter;
        std::weak_ptr<JobState> m_WaiterJob;
    };

}
