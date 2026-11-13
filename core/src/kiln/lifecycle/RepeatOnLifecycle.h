#pragma once

// lifecycle-runtime's coroutine helpers (RepeatOnLifecycle.kt, WithLifecycleState.kt, FlowExt.kt):
//   - RepeatOnLifecycle runs `block` each time the lifecycle reaches `state` and cancels it when
//     it drops below, until the lifecycle is destroyed (then it returns).
//   - WithStateAtLeast runs `block` once the lifecycle is at least `state`, inside the event
//     that got it there; LifecycleDestroyedException if it is destroyed first.
//   - FlowWithLifecycle collects an upstream stream (anything with Collect(collector) -> Task<>,
//     such as rt::SnapshotFlow) only while the lifecycle is at least `minActiveState`.
// Cancelling a task frees its frames at once in kiln, so a new block never overlaps the
// cancelled one (upstream serialises them with a Mutex).

#include "kiln/lifecycle/Lifecycle.h"
#include <kiln/Function.h>

#include <optional>
#include <stdexcept>
#include <type_traits>

namespace kiln::lifecycle {

    // LifecycleDestroyedException: the lifecycle was destroyed before the state was reached.
    class LifecycleDestroyedException : public std::runtime_error {
    public:
        LifecycleDestroyedException() : std::runtime_error("lifecycle destroyed") {}
    };

    // state must not be Initialized (std::invalid_argument).
    rt::Task<> RepeatOnLifecycle(Lifecycle& lifecycle, State state, kiln::Function<rt::Task<>()> block);
    inline rt::Task<> RepeatOnLifecycle(LifecycleOwner& owner, State state, kiln::Function<rt::Task<>()> block) {
        return RepeatOnLifecycle(owner.GetLifecycle(), state, std::move(block));
    }

    namespace detail {
        // withStateAtLeastUnchecked: runs `block` now or inside the event reaching `state`.
        rt::Task<> WithStateAtLeastUnchecked(Lifecycle& lifecycle, State state, kiln::Function<void()> block);

        template<typename F>
        rt::Task<std::invoke_result_t<F&>> WithState(Lifecycle& lifecycle, State state, F block) {
            using R = std::invoke_result_t<F&>;
            if constexpr (std::is_void_v<R>) {
                co_await WithStateAtLeastUnchecked(lifecycle, state, [&] { block(); });
            } else {
                std::optional<R> result;
                co_await WithStateAtLeastUnchecked(lifecycle, state, [&] { result.emplace(block()); });
                co_return std::move(*result);
            }
        }
    }

    // state must be Created or above (std::invalid_argument).
    template<typename F>
    rt::Task<std::invoke_result_t<F&>> WithStateAtLeast(Lifecycle& lifecycle, State state, F block) {
        if (state < State::Created) throw std::invalid_argument("target state must be CREATED or greater");
        return detail::WithState(lifecycle, state, std::move(block));
    }
    template<typename F> auto WithCreated(Lifecycle& l, F block) { return detail::WithState(l, State::Created, std::move(block)); }
    template<typename F> auto WithStarted(Lifecycle& l, F block) { return detail::WithState(l, State::Started, std::move(block)); }
    template<typename F> auto WithResumed(Lifecycle& l, F block) { return detail::WithState(l, State::Resumed, std::move(block)); }
    template<typename F> auto WithStateAtLeast(LifecycleOwner& o, State s, F block) { return WithStateAtLeast(o.GetLifecycle(), s, std::move(block)); }
    template<typename F> auto WithCreated(LifecycleOwner& o, F block) { return WithCreated(o.GetLifecycle(), std::move(block)); }
    template<typename F> auto WithStarted(LifecycleOwner& o, F block) { return WithStarted(o.GetLifecycle(), std::move(block)); }
    template<typename F> auto WithResumed(LifecycleOwner& o, F block) { return WithResumed(o.GetLifecycle(), std::move(block)); }

    // Flow<T>.flowWithLifecycle: `upstream` collected only while the lifecycle is at least
    // minActiveState; the stream ends when the lifecycle is destroyed.
    template<typename Flow>
    class LifecycleFlow {
    public:
        LifecycleFlow(Flow upstream, Lifecycle& lifecycle, State minActiveState)
            : m_Upstream(std::move(upstream)), m_Lifecycle(&lifecycle), m_Min(minActiveState) {}
        template<typename Collector>
        rt::Task<> Collect(Collector collector) const {
            return RepeatOnLifecycle(*m_Lifecycle, m_Min, [up = m_Upstream, collector]() { return up.Collect(collector); });
        }
    private:
        Flow m_Upstream;
        Lifecycle* m_Lifecycle;
        State m_Min;
    };
    template<typename Flow>
    LifecycleFlow<Flow> FlowWithLifecycle(Flow upstream, Lifecycle& lifecycle, State minActiveState = State::Started) {
        return LifecycleFlow<Flow>(std::move(upstream), lifecycle, minActiveState);
    }

}
