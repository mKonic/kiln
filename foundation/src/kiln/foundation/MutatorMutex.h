#pragma once

// MutatorMutex (MutatorMutex.kt): one mutation at a time. A new one with the same or a higher
// priority stops the one running (its interrupt runs at once, so its cleanup happens before the
// new one starts); one with a lower priority does not start.

#include <cstdint>
#include <kiln/Function.h>
#include <functional>
#include <utility>

namespace kiln::foundation {

    enum class MutatePriority : std::uint8_t { Default, UserInput, PreventUserInput };

    class MutatorMutex {
    public:
        // mutate(priority): 0 when the running mutation has a higher priority; otherwise the
        // running one is interrupted and the returned token holds the mutex until Release.
        std::uint64_t Acquire(MutatePriority priority, kiln::Function<void()> interrupt) {
            if (m_Token != 0 && priority < m_Priority) return 0;
            if (m_Token != 0) {
                auto previous = std::exchange(m_Interrupt, nullptr);
                m_Token = 0;
                if (previous) previous();
            }
            m_Priority = priority;
            m_Interrupt = std::move(interrupt);
            return m_Token = ++m_Next;
        }
        void Release(std::uint64_t token) {
            if (token == 0 || token != m_Token) return;
            m_Token = 0;
            m_Interrupt = nullptr;
        }
        bool IsHeld(std::uint64_t token) const { return token != 0 && token == m_Token; }

    private:
        kiln::Function<void()> m_Interrupt;
        MutatePriority m_Priority = MutatePriority::Default;
        std::uint64_t m_Token = 0, m_Next = 0;
    };

}
