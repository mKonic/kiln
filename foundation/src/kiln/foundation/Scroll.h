#pragma once

#include <memory>

// Scrolling (spec 05 §Scrollable, §Scroll): ScrollableState, the Scrollable modifier (drag ->
// nested pre-scroll -> self -> nested post-scroll; fling with the platform spline; a press
// catches a running fling; mouse wheel), and verticalScroll/horizontalScroll.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/NestedScroll.h"

namespace kiln::foundation {

    class ScrollableState {
    public:
        virtual ~ScrollableState() = default;
        // Consumes up to `delta` in scroll direction, returns what was consumed.
        virtual float ConsumeScrollDelta(float delta) = 0;
        virtual bool CanScrollForward() const { return true; }
        virtual bool CanScrollBackward() const { return true; }
        bool IsScrollInProgress() const { return m_InProgress.Get(); }

        // Stops a running fling or programmatic scroll (stopScroll()).
        void StopScroll() { if (m_Stop) m_Stop(); }
        void SetScrollInProgress(bool v) { m_InProgress.Set(v); }   // used by the gesture code

    protected:
        friend class ScrollingLogic;
        rt::State<bool> m_InProgress{ false };
        std::function<void()> m_Stop;
    };

    // ScrollState for verticalScroll/horizontalScroll (ScrollState.kt).
    class ScrollState final : public ScrollableState {
    public:
        explicit ScrollState(int initial = 0) : m_Value(initial) {}
        int Value() const { return m_Value.Get(); }
        int MaxValue() const { return m_Max.Get(); }
        int ViewportSize() const { return m_Viewport; }
        float ConsumeScrollDelta(float delta) override;
        bool CanScrollForward() const override { return m_Value.Peek() < m_Max.Peek(); }
        bool CanScrollBackward() const override { return m_Value.Peek() > 0; }
        void ScrollTo(int value) { ConsumeScrollDelta(float(value - m_Value.Peek())); }
        rt::Task<> AnimateScrollTo(int value, anim::SpecPtr spec = anim::Spring());

        // Set by the scroll layout.
        void SetBounds(int maxValue, int viewport);

    private:
        rt::State<int> m_Value;
        rt::State<int> m_Max{ 1 << 30 };
        int m_Viewport = 0;
        float m_Accumulator = 0;
    };

    class OverscrollEffect;
    struct ScrollableOptions {
        bool enabled = true;
        bool reverseDirection = false;   // already resolved (ScrollableDefaults.reverseDirection)
        std::shared_ptr<OverscrollEffect> overscroll;   // null: no overscroll
    };
    Modifier Scrollable(Modifier m, ScrollableState& state, Orientation orientation, ScrollableOptions options = {});

    // verticalScroll / horizontalScroll: clip, scrollable, and a layout that measures the content
    // unbounded on the scroll axis and offsets it by the scroll value.
    Modifier VerticalScroll(Modifier m, ScrollState& state, bool enabled = true, bool reverseScrolling = false);
    Modifier HorizontalScroll(Modifier m, ScrollState& state, bool enabled = true, bool reverseScrolling = false);

}
