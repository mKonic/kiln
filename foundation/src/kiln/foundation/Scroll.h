#pragma once

#include <memory>
#include <kiln/Function.h>

// Scrolling (spec 05 §Scrollable, §Scroll): ScrollableState, the Scrollable modifier (drag ->
// nested pre-scroll -> self -> nested post-scroll; fling with the platform spline; a press
// catches a running fling; mouse wheel), and verticalScroll/horizontalScroll.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/BringIntoView.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/NestedScroll.h"

namespace kiln::foundation {

    // ScrollIndicatorState: what a scroll indicator (a scrollbar) draws, in pixels along the
    // scroll axis. Lazy layouts estimate them from the visible items. Each is INT_MAX while
    // unknown; reading them subscribes.
    class ScrollIndicatorState {
    public:
        virtual ~ScrollIndicatorState() = default;
        // From the visual start (top; left, or right in RTL), whatever reverseLayout/reverseScrolling.
        virtual int ScrollOffset() const = 0;
        virtual int ContentSize() const = 0;
        virtual int ViewportSize() const = 0;
    };

    class ScrollableState {
    public:
        virtual ~ScrollableState() = default;
        // Consumes up to `delta` in scroll direction, returns what was consumed.
        virtual float ConsumeScrollDelta(float delta) = 0;
        virtual bool CanScrollForward() const { return true; }
        virtual bool CanScrollBackward() const { return true; }
        bool IsScrollInProgress() const { return m_InProgress.Get(); }
        // True when ConsumeScrollDelta lays the content out itself (lazy layouts remeasure now);
        // otherwise placement catches up at the next layout pass.
        virtual bool LaysOutOnScroll() const { return false; }
        // For a scroll indicator; null where the state has none.
        virtual const ScrollIndicatorState* ScrollIndicator() const { return nullptr; }

        // Stops a running fling or programmatic scroll (stopScroll()).
        // The stopped scroll ends at its next frame; to everyone else it has ended now.
        void StopScroll() {
            if (!m_Stop) return;
            auto stop = std::move(m_Stop);
            m_Stop = nullptr;
            ++m_ScrollToken;   // its EndScroll no longer matches
            stop();
            SetScrollInProgress(false);
        }
        bool HasRunningScroll() const { return bool(m_Stop); }
        void SetScrollInProgress(bool v) { if (m_InProgress.Peek() != v) m_InProgress.Set(v); }   // used by the gesture code

        // scroll { } (the scroll mutex): stops whatever runs, marks the scroll in progress and
        // returns a token; `stop` is how a later scroll or a press stops this one.
        std::uint64_t BeginScroll(kiln::Function<void()> stop) {
            StopScroll();
            m_Stop = std::move(stop);
            SetScrollInProgress(true);
            return ++m_ScrollToken;
        }
        bool IsCurrentScroll(std::uint64_t token) const { return token == m_ScrollToken; }
        // Expires when the state is destroyed (a detaching scrollable may outlive it).
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
        void EndScroll(std::uint64_t token) {
            if (token != m_ScrollToken) return;
            m_Stop = nullptr;
            SetScrollInProgress(false);
        }

    protected:
        friend class ScrollingLogic;
        rt::State<bool> m_InProgress{ false };
        kiln::Function<void()> m_Stop;
        std::uint64_t m_ScrollToken = 0;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    // Ends a BeginScroll when the coroutine frame holding it goes away (cancelled jobs included),
    // so the stop callback never outlives what it captures.
    struct ScrollSession {
        ScrollableState& state;
        std::uint64_t token;
        std::weak_ptr<const char> alive;
        ScrollSession(ScrollableState& s, kiln::Function<void()> stop) : state(s), token(s.BeginScroll(std::move(stop))), alive(s.AliveToken()) {}
        ~ScrollSession() { if (!alive.expired()) state.EndScroll(token); }
        bool Current() const { return state.IsCurrentScroll(token); }
        ScrollSession(const ScrollSession&) = delete;
        ScrollSession& operator=(const ScrollSession&) = delete;
    };

    // animateScrollBy: animates `value` px of scrolling; returns what was consumed.
    rt::Task<float> AnimateScrollBy(ScrollableState& state, float value, anim::SpecPtr spec = anim::Spring());

    // ScrollState for verticalScroll/horizontalScroll (ScrollState.kt).
    class ScrollState final : public ScrollableState {
    public:
        explicit ScrollState(int initial = 0) : m_Value(initial) {}
        int Value() const { return m_Value.Get(); }
        int MaxValue() const { return m_Max.Get(); }
        // Reading these subscribes (the last layout sets them).
        int ViewportSize() const { return m_Viewport.Get(); }
        int ContentSize() const { return m_Content.Get(); }
        float ConsumeScrollDelta(float delta) override;
        bool CanScrollForward() const override { return m_Value.Peek() < m_Max.Peek(); }
        bool CanScrollBackward() const override { return m_Value.Peek() > 0; }
        void ScrollTo(int value) { ConsumeScrollDelta(float(value - m_Value.Peek())); }
        rt::Task<> AnimateScrollTo(int value, anim::SpecPtr spec = anim::Spring());

        const ScrollIndicatorState* ScrollIndicator() const override { return &m_Indicator; }

        // Set by the scroll layout.
        void SetBounds(int maxValue, int viewport, int contentSize, bool reverseScrolling);

    private:
        struct Indicator final : ScrollIndicatorState {
            const ScrollState* s;
            explicit Indicator(const ScrollState* state) : s(state) {}
            int ScrollOffset() const override { return s->m_Reverse ? s->MaxValue() - s->Value() : s->Value(); }
            int ContentSize() const override { return s->ContentSize(); }
            int ViewportSize() const override { return s->ViewportSize(); }
        };
        rt::State<int> m_Value;
        rt::State<int> m_Max{ 1 << 30 };
        rt::State<int> m_Viewport{ 0 }, m_Content{ 0 };
        bool m_Reverse = false;
        Indicator m_Indicator{ this };
        float m_Accumulator = 0;
    };

    class OverscrollEffect;
    // FlingBehavior.performFling: `scrollBy` takes and returns scroll-direction pixels (through
    // nested scroll); velocity is in the scroll direction; returns the velocity left over.
    // Implement it with a free coroutine function (captures of a coroutine lambda dangle).
    using FlingBehavior = kiln::Function<rt::Task<float>(kiln::Function<float(float)> scrollBy, float velocity)>;
    // TargetedFlingBehavior: a fling that knows where it is going and reports the distance left
    // (onRemainingDistanceUpdated) as it moves. AsFlingBehavior drops the reports.
    using TargetedFlingBehavior =
        kiln::Function<rt::Task<float>(kiln::Function<float(float)> scrollBy, float velocity, kiln::Function<void(float remaining)> onRemainingDistanceUpdated)>;
    inline FlingBehavior AsFlingBehavior(TargetedFlingBehavior f) {
        return [f = std::move(f)](kiln::Function<float(float)> scrollBy, float velocity) { return f(std::move(scrollBy), velocity, [](float) {}); };
    }

    struct ScrollableOptions {
        bool enabled = true;
        bool reverseDirection = false;   // already resolved (ScrollableDefaults.reverseDirection)
        std::shared_ptr<OverscrollEffect> overscroll;   // null: no overscroll
        FlingBehavior flingBehavior;                     // null: the platform spline decay
        std::shared_ptr<const BringIntoViewSpec> bringIntoViewSpec;   // null: LocalBringIntoViewSpec
        std::shared_ptr<MutableInteractionSource> interactionSource;  // gets drag interactions
    };
    Modifier Scrollable(Modifier m, ScrollableState& state, Orientation orientation, ScrollableOptions options = {});

    // scrollableArea: a scrolling container's input and visuals around content that lays itself
    // out from the state (clipScrollableContainer, the overscroll effect, scrollable with the
    // direction verticalScroll / horizontalScroll use: content follows the finger, mirrored in
    // RTL for horizontal areas, reversed by reverseScrolling).
    struct ScrollableAreaOptions {
        bool enabled = true;
        bool reverseScrolling = false;
        std::optional<std::shared_ptr<OverscrollEffect>> overscroll;   // OverscrollParam: unset uses rememberOverscrollEffect(); null: none
        FlingBehavior flingBehavior;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        std::shared_ptr<const BringIntoViewSpec> bringIntoViewSpec;
    };
    Modifier ScrollableArea(Modifier m, ScrollableState& state, Orientation orientation, ScrollableAreaOptions options = {});

    // checkScrollableContainerConstraints: throws when a container scrolling along
    // `orientation` is measured with an unbounded maximum on that axis (a LazyColumn inside a
    // verticalScroll Column, wrapContentSize(unbounded = true), ...).
    void CheckScrollableContainerConstraints(const Constraints& c, Orientation orientation);
    // clipScrollableContainer: clips to the bounds along the scroll axis only; across it the
    // clip leaves room for the content's shadows (30 dp, MaxSupportedElevation).
    Modifier ClipScrollableContainer(Modifier m, Orientation orientation);

    // nestedScroll(connection): `connection` sees the scrolls and flings of scrollables inside
    // (before them: pre, after them: post) and may consume part of each.
    Modifier NestedScroll(Modifier m, std::shared_ptr<NestedScrollConnection> connection);
    // NestedScrollDispatcher: sends a scroll that is not a scrollable's (a custom gesture) to
    // the nested-scroll ancestors of the node it is attached to; it answers zero while detached.
    class NestedScrollDispatcher {
    public:
        Offset DispatchPreScroll(Offset available, ui::NestedScrollSource source) const;
        Offset DispatchPostScroll(Offset consumed, Offset available, ui::NestedScrollSource source) const;
        rt::Task<Offset> DispatchPreFling(Offset available) const;
        rt::Task<Offset> DispatchPostFling(Offset consumed, Offset available) const;
        ui::NestedScrollNode* node = nullptr;   // internal: set while attached
    };
    // nestedScroll(connection, dispatcher)
    Modifier NestedScroll(Modifier m, std::shared_ptr<NestedScrollConnection> connection, std::shared_ptr<NestedScrollDispatcher> dispatcher);

    // rememberNestedScrollInteropConnection(): passes nested scroll on to the host view's
    // nested-scrolling parents (a CoordinatorLayout's app bar), in View pixels and signs. Use
    // it with NestedScroll around a scrollable. Without a host parent it consumes nothing.
    std::shared_ptr<NestedScrollConnection> RememberNestedScrollInteropConnection(rt::Key key = {});

    // verticalScroll / horizontalScroll: clip, scrollable, and a layout that measures the content
    // unbounded on the scroll axis and offsets it by the scroll value. A null flingBehavior is
    // the platform default.
    Modifier VerticalScroll(Modifier m, ScrollState& state, bool enabled = true, bool reverseScrolling = false,
                            std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect = std::nullopt,
                            FlingBehavior flingBehavior = {});
    Modifier HorizontalScroll(Modifier m, ScrollState& state, bool enabled = true, bool reverseScrolling = false,
                              std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect = std::nullopt,
                              FlingBehavior flingBehavior = {});

    // rememberScrollState: the scroll position survives re-creation (ScrollState.Saver).
    ScrollState& RememberScrollState(int initial = 0, rt::Key key = {});

}
