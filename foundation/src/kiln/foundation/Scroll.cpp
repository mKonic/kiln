#include "kiln/foundation/Scroll.h"

#include "kiln/ui/VelocityTracker.h"

#include <cmath>

namespace kiln::foundation {

    // --- ScrollState ------------------------------------------------------------------------

    float ScrollState::ConsumeScrollDelta(float delta) {
        // ScrollState.kt scrollableState: fractional remainders accumulate between calls.
        const float absolute = float(m_Value.Peek()) + delta + m_Accumulator;
        const float newValue = std::clamp(absolute, 0.0f, float(m_Max.Peek()));
        const bool changed = absolute != newValue;
        const float consumed = newValue - float(m_Value.Peek());
        const int consumedInt = RoundJava(consumed);
        m_Value.Set(m_Value.Peek() + consumedInt);
        m_Accumulator = consumed - float(consumedInt);
        return changed ? consumed : delta;
    }

    void ScrollState::SetBounds(int maxValue, int viewport) {
        m_Viewport = viewport;
        m_Max.Set(std::max(0, maxValue));
        if (m_Value.Peek() > m_Max.Peek()) m_Value.Set(m_Max.Peek());
    }

    rt::Task<> ScrollState::AnimateScrollTo(int value, anim::SpecPtr spec) {
        anim::Animatable a(float(m_Value.Peek()));
        m_InProgress.Set(true);
        bool stopped = false;
        m_Stop = [&] { stopped = true; a.Stop(); };
        co_await a.AnimateTo(anim::Vec{ float(value), 0, 0, 0 }, spec, {}, [this](const anim::Vec& v) {
            ConsumeScrollDelta(v[0] - float(m_Value.Peek()));
        });
        m_Stop = nullptr;
        m_InProgress.Set(false);
        (void)stopped;
    }

    // --- ScrollingLogic (Scrollable.kt) -------------------------------------------------------

    class ScrollingLogic final : public NestedScrollConnection {
    public:
        ScrollableState* state = nullptr;
        Orientation orientation = Orientation::Vertical;
        bool reverse = false;
        NestedScrollNode* node = nullptr;
        Density density;
        float friction = 0.015f;

        float Axis(Offset o) const { return orientation == Orientation::Vertical ? o.y : o.x; }
        Offset Single(float v) const { return orientation == Orientation::Vertical ? Offset{ 0, v } : Offset{ v, 0 }; }
        float Reverse(float v) const { return reverse ? -v : v; }

        // performScroll: pre -> self -> post, all in pixel (drag) direction. Returns consumed.
        Offset Dispatch(Offset delta, NestedScrollSource src) {
            const Offset pre = node ? node->DispatchPreScroll(delta, src) : Offset{};
            const Offset left = delta - pre;
            const float selfConsumed = Reverse(state->ConsumeScrollDelta(Reverse(Axis(left))));
            const Offset self = Single(selfConsumed);
            const Offset post = node ? node->DispatchPostScroll(self, left - self, src) : Offset{};
            return pre + self + post;
        }

        // A child's leftover reaches us: scroll it unless we are scrolling ourselves.
        Offset OnPostScroll(Offset, Offset available, NestedScrollSource) override {
            if (state->IsScrollInProgress()) return {};
            return Single(Reverse(state->ConsumeScrollDelta(Reverse(Axis(available)))));
        }
        Offset OnPostFling(Offset, Offset available) override {
            if (m_Flinging) return {};
            StartFling(available);
            return available;
        }

        bool Flinging() const { return m_Flinging; }
        void StopFling() { m_FlingJob.Cancel(); m_Flinging = false; state->m_InProgress.Set(false); }

        void StartFling(Offset velocity) {
            m_FlingJob = rt::Launch(rt::Dispatcher::Main(), Fling(velocity));
        }

    private:
        rt::Task<> Fling(Offset velocity) {
            m_Flinging = true;
            state->m_InProgress.Set(true);
            state->m_Stop = [this] { StopFling(); };
            const Offset pre = node ? node->DispatchPreFling(velocity) : Offset{};
            const Offset available = velocity - pre;
            float remaining = Axis(available);
            if (std::abs(remaining) > 1) {   // DefaultFlingBehavior: below 1 px/s the spline gives NaNs
                anim::Animatable a(0.0f);
                float last = 0;
                auto decay = std::make_shared<anim::SplineDecay>(density.density, friction);
                auto r = co_await a.AnimateDecay(remaining, decay, [&](float value) {
                    const float delta = value - last;
                    last = value;
                    const float consumed = Axis(Dispatch(Single(delta), NestedScrollSource::SideEffect));
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();   // hit an edge
                });
                remaining = r.reason == anim::EndReason::Interrupted ? a.Velocity()[0] : 0.0f;
            }
            if (node) node->DispatchPostFling(available - Single(remaining), Single(remaining));
            m_Flinging = false;
            state->m_Stop = nullptr;
            state->m_InProgress.Set(false);
        }

        rt::Job m_FlingJob;
        bool m_Flinging = false;
    };

    namespace {
        struct NestedParams { std::shared_ptr<ScrollingLogic> logic; bool operator==(const NestedParams& o) const { return logic == o.logic; } };
        struct NestedNode final : NestedScrollNode {
            NestedParams params;
            explicit NestedNode(NestedParams p) : params(std::move(p)) { Apply(); }
            void OnParamsChanged() { Apply(); }
            void Apply() { connection = params.logic.get(); params.logic->node = this; }
            void OnAttach() override { params.logic->node = this; }
            void OnDetach() override { if (params.logic->node == this) params.logic->node = nullptr; params.logic->StopFling(); }
        };

        rt::Task<> ScrollGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            VelocityTracker tracker;
            for (;;) {
                // A press during a fling catches it: stop and swallow the down so children don't
                // see a press (Scrollable startDragImmediately while scrolling).
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                bool down = !e->changes.empty() && e->changes[0].ChangedToDownIgnoreConsumed();
                if (!down) continue;
                if (logic->Flinging()) { logic->StopFling(); e->changes[0].Consume(); }
                tracker.Reset();
                tracker.AddPointerInputChange(e->changes[0]);
                // Slop then drag, locked to our axis.
                const std::int64_t id = e->changes[0].id;
                const float slop = PointerSlop(s.Config(), e->changes[0].type);
                float total = 0;
                bool dragging = false;
                for (;;) {
                    auto m = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    PointerInputChange* c = nullptr;
                    for (auto& ch : m->changes) if (ch.id == id) c = &ch;
                    if (!c) break;
                    tracker.AddPointerInputChange(*c);
                    if (c->ChangedToUpIgnoreConsumed()) {
                        if (dragging) {
                            const auto& cfg = s.Config();
                            Offset v = tracker.CalculateVelocity({ cfg.maximumFlingVelocity, cfg.maximumFlingVelocity });
                            logic->state->SetScrollInProgress(false);
                            logic->StartFling(logic->Single(logic->Axis(v)));
                        }
                        break;
                    }
                    if (!dragging) {
                        if (c->IsConsumed()) break;   // a child took the gesture
                        total += logic->Axis(c->PositionChangeIgnoreConsumed());
                        if (std::abs(total) >= slop && std::abs(total) > 0) {
                            dragging = true;
                            logic->state->SetScrollInProgress(true);
                            const float over = total - (total > 0 ? slop : -slop);
                            logic->Dispatch(logic->Single(over), NestedScrollSource::UserInput);
                            c->Consume();
                        }
                        continue;
                    }
                    if (c->IsConsumed()) { logic->state->SetScrollInProgress(false); break; }
                    logic->Dispatch(logic->Single(logic->Axis(c->PositionChangeIgnoreConsumed())), NestedScrollSource::UserInput);
                    c->Consume();
                }
            }
        }

        rt::Task<> WheelGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (e->type != PointerEventType::Scroll || e->changes.empty() || e->changes[0].IsConsumed()) continue;
                const auto& cfg = s.Config();
                const Offset d = e->changes[0].scrollDelta;
                // AndroidScrollable AndroidConfig: pixel delta = scrollDelta * -factor, then handled
                // like a drag delta (wheel up = finger moving down = towards the start).
                const float px = -(logic->orientation == Orientation::Vertical ? d.y * cfg.verticalScrollFactor : d.x * cfg.horizontalScrollFactor);
                const Offset consumed = logic->Dispatch(logic->Single(px), NestedScrollSource::UserInput);
                if (logic->Axis(consumed) != 0) e->changes[0].Consume();
            }
        }

        struct ScrollLayoutParams { ScrollState* state; bool vertical; bool reverse; bool operator==(const ScrollLayoutParams&) const = default; };
        struct ScrollLayoutNode final : ModifierNode, LayoutModifier {
            ScrollLayoutParams params;
            explicit ScrollLayoutNode(ScrollLayoutParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                // checkScrollableContainerConstraints: an unbounded scroll axis is a programming error.
                Constraints child = c;
                if (params.vertical) { child.maxHeight = Constraints::Infinity; child.minHeight = 0; }
                else { child.maxWidth = Constraints::Infinity; child.minWidth = 0; }
                Placeable& p = m.Measure(child);
                const int w = std::min(p.Width(), c.maxWidth), h = std::min(p.Height(), c.maxHeight);
                const int scrollHeight = p.Height() - h, scrollWidth = p.Width() - w;
                const int side = params.vertical ? h : w;
                params.state->SetBounds(params.vertical ? scrollHeight : scrollWidth, side);
                ScrollState* state = params.state;
                const bool vertical = params.vertical, reverse = params.reverse;
                return s.Layout(w, h, [&p, state, vertical, reverse, scrollHeight, scrollWidth](PlacementScope& place) {
                    // Reading the value here subscribes placement only: scrolling re-places, it
                    // does not re-measure.
                    const int value = std::clamp(state->Value(), 0, vertical ? scrollHeight : scrollWidth);
                    const int abs = reverse ? value - (vertical ? scrollHeight : scrollWidth) : -value;
                    if (vertical) place.PlaceRelative(p, 0, abs);
                    else place.PlaceRelative(p, abs, 0);
                });
            }
        };
    }

    Modifier Scrollable(Modifier m, ScrollableState& state, Orientation o, ScrollableOptions opt) {
        std::shared_ptr<ScrollingLogic> logic = std::make_shared<ScrollingLogic>();
        if (auto* c = rt::Composition::Current()) logic = c->Remember<std::shared_ptr<ScrollingLogic>>([&] { return logic; });
        logic->state = &state;
        logic->orientation = o;
        logic->reverse = opt.reverseDirection;
        m = m.With<NestedNode>(NestedParams{ logic });
        if (!opt.enabled) return m;
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(o)), [logic](PointerInputScope& s) {
            logic->density = s.GetDensity();
            logic->friction = s.Config().scrollFriction;
            return ScrollGestures(s, logic);
        });
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(o), 1), [logic](PointerInputScope& s) {
            return WheelGestures(s, logic);
        });
        return m;
    }

    namespace {
        Modifier ScrollModifier(Modifier m, ScrollState& state, bool vertical, bool enabled, bool reverseScrolling) {
            // ScrollableDefaults.reverseDirection: dragging moves content with the finger, so the
            // drag delta is reversed unless reverseScrolling (RTL flips horizontal again; TODO when
            // the scrollable reads the layout direction).
            ScrollableOptions opt;
            opt.enabled = enabled;
            opt.reverseDirection = !reverseScrolling;
            m = ClipToBounds(std::move(m));
            m = Scrollable(std::move(m), state, vertical ? Orientation::Vertical : Orientation::Horizontal, opt);
            return m.With<ScrollLayoutNode>(ScrollLayoutParams{ &state, vertical, reverseScrolling });
        }
    }

    Modifier VerticalScroll(Modifier m, ScrollState& s, bool enabled, bool reverse) { return ScrollModifier(std::move(m), s, true, enabled, reverse); }
    Modifier HorizontalScroll(Modifier m, ScrollState& s, bool enabled, bool reverse) { return ScrollModifier(std::move(m), s, false, enabled, reverse); }

}
