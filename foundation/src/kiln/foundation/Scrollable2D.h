#pragma once

// scrollable2D (Scrollable2D.kt, Scrollable2DState.kt): scrolling in any direction, such as a
// pannable map or canvas. Drags after touch slop scroll through nested scrolling and the
// overscroll effect; a release flings, each axis decaying on its own (DefaultFling2DBehavior).
// rememberScrollableState is the one-axis state from a lambda.

#include "kiln/foundation/Draggable.h"
#include <kiln/Function.h>
#include "kiln/foundation/Overscroll.h"
#include "kiln/foundation/Scroll.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    // rememberScrollableState(consumeScrollDelta): a ScrollableState that hands each delta to
    // the lambda (the latest one), which returns what it consumed.
    class LambdaScrollableState final : public ScrollableState {
    public:
        explicit LambdaScrollableState(kiln::Function<float(float)> consume) : m_Consume(std::move(consume)) {}
        float ConsumeScrollDelta(float delta) override { return m_Consume ? m_Consume(delta) : 0; }
        void SetConsume(kiln::Function<float(float)> f) { m_Consume = std::move(f); }

    private:
        kiln::Function<float(float)> m_Consume;
    };
    LambdaScrollableState& RememberScrollableState(kiln::Function<float(float)> consumeScrollDelta, rt::Key key = {});

    // Scrollable2DState: consumes 2D deltas (returns what it used).
    class Scrollable2DState {
    public:
        explicit Scrollable2DState(kiln::Function<Offset(Offset)> consume) : m_Consume(std::move(consume)) {}
        Offset ConsumeScrollDelta(Offset delta) { return m_Consume ? m_Consume(delta) : Offset{}; }
        // dispatchRawDelta: outside of a scroll.
        Offset DispatchRawDelta(Offset delta) { return ConsumeScrollDelta(delta); }
        bool IsScrollInProgress() const { return m_InProgress.Get(); }
        // scroll { scrollBy(...) }: scrolls from code (stops a fling first), in progress while
        // the block runs.
        struct Scroll2DScope { kiln::Function<Offset(Offset)> scrollBy; Offset ScrollBy(Offset pixels) const { return scrollBy(pixels); } };
        rt::Task<> Scroll(kiln::Function<rt::Task<>(const Scroll2DScope&)> block) {
            StopScroll();
            SetScrollInProgress(true);
            Scroll2DScope scope{ [this](Offset d) { return ConsumeScrollDelta(d); } };
            co_await block(scope);
            SetScrollInProgress(false);
        }
        // Stops a running fling.
        void StopScroll() { if (m_Stop) { auto stop = std::move(m_Stop); m_Stop = nullptr; stop(); } SetScrollInProgress(false); }
        // internal
        void SetConsume(kiln::Function<Offset(Offset)> f) { m_Consume = std::move(f); }
        void SetScrollInProgress(bool v) { if (m_InProgress.Peek() != v) m_InProgress.Set(v); }
        void SetStop(kiln::Function<void()> stop) { m_Stop = std::move(stop); }

    private:
        kiln::Function<Offset(Offset)> m_Consume;
        kiln::Function<void()> m_Stop;
        rt::State<bool> m_InProgress{ false };
    };
    Scrollable2DState& RememberScrollable2DState(kiln::Function<Offset(Offset)> consumeScrollDelta, rt::Key key = {});

    struct Scrollable2DOptions {
        bool enabled = true;
        std::shared_ptr<OverscrollEffect> overscroll;   // null: none
        bool reverseDirection = false;   // false: dragging moves the content with the finger
    };
    Modifier Scrollable2D(Modifier m, Scrollable2DState& state, Scrollable2DOptions options = {});

}
