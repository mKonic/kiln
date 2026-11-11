#include "kiln/foundation/IndirectDrag.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Overscroll.h"

#include "kiln/ui/BringIntoView.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/VelocityTracker.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

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

    void ScrollState::SetBounds(int maxValue, int viewport, int contentSize, bool reverseScrolling) {
        if (m_Viewport.Peek() != viewport) m_Viewport.Set(viewport);
        if (m_Content.Peek() != contentSize) m_Content.Set(contentSize);
        m_Reverse = reverseScrolling;
        m_Max.Set(std::max(0, maxValue));
        if (m_Value.Peek() > m_Max.Peek()) m_Value.Set(m_Max.Peek());
    }

    rt::Task<> ScrollState::AnimateScrollTo(int value, anim::SpecPtr spec) {
        co_await AnimateScrollBy(*this, float(value - m_Value.Peek()), std::move(spec));
    }

    rt::Task<float> AnimateScrollBy(ScrollableState& state, float value, anim::SpecPtr spec) {
        anim::Animatable a(0.0f);
        bool stopped = false;
        ScrollSession session(state, [&] { stopped = true; a.Stop(); });
        float previous = 0, consumed = 0;
        co_await a.AnimateTo(anim::Vec{ value, 0, 0, 0 }, std::move(spec), {}, [&](const anim::Vec& v) {
            if (stopped) return;
            const float delta = v[0] - previous;
            const float used = state.ConsumeScrollDelta(delta);
            previous += delta;
            consumed += used;
            if (std::abs(delta - used) > 0.5f) a.Stop();   // reached an edge
        });
        co_return consumed;
    }

    // --- ScrollingLogic (Scrollable.kt) -------------------------------------------------------

    class ScrollingLogic final : public NestedScrollConnection {
    public:
        ScrollableState* state = nullptr;
        std::weak_ptr<const char> stateAlive;
        Orientation orientation = Orientation::Vertical;
        bool reverse = false;
        NestedScrollNode* node = nullptr;
        Density density;
        float friction = 0.015f;
        FlingBehavior flingBehavior;
        rt::Job semanticsScroll;   // ScrollBy from accessibility (an animated scroll)
        IntSize viewport;          // ContentInViewNode's measured size (page keys)
        std::shared_ptr<IndirectDragDetector> indirect;   // touch-navigation pad drags
        std::shared_ptr<const BringIntoViewSpec> bringIntoViewSpec = DefaultBringIntoViewSpec();

        float Axis(Offset o) const { return orientation == Orientation::Vertical ? o.y : o.x; }
        Offset Single(float v) const { return orientation == Orientation::Vertical ? Offset{ 0, v } : Offset{ v, 0 }; }
        float Reverse(float v) const { return reverse ? -v : v; }

        // performScroll: pre -> self -> post, all in pixel (drag) direction. Returns consumed.
        std::shared_ptr<OverscrollEffect> overscroll;

        // performScroll, through the overscroll effect when there is one.
        Offset Dispatch(Offset delta, NestedScrollSource src) {
            if (overscroll) return overscroll->ApplyToScroll(delta, src, [this, src](Offset d) { return DispatchNested(d, src); });
            return DispatchNested(delta, src);
        }

        Offset DispatchNested(Offset delta, NestedScrollSource src) {
            const Offset pre = node ? node->DispatchPreScroll(delta, src) : Offset{};
            const Offset left = delta - pre;
            const float selfConsumed = Reverse(state->ConsumeScrollDelta(Reverse(Axis(left))));
            const Offset self = Single(selfConsumed);
            if (node) node->DispatchOnScrollChanged(self);
            const Offset post = node ? node->DispatchPostScroll(self, left - self, src) : Offset{};
            return pre + self + post;
        }

        // A child's leftover reaches us: scroll it unless we are scrolling ourselves.
        Offset OnPostScroll(Offset, Offset available, NestedScrollSource) override {
            if (state->IsScrollInProgress()) return {};
            return Single(Reverse(state->ConsumeScrollDelta(Reverse(Axis(available)))));
        }
        // A child's leftover fling: fling it here, in this node's own (cancellable) job.
        rt::Task<Offset> OnPostFling(Offset, Offset available) override {
            if (m_Flinging) co_return Offset{};
            StartFling(available);
            co_return available;
        }

        bool Flinging() const { return m_Flinging; }
        void StopFling() {
            m_FlingJob.Cancel();
            if (m_Flinging) { m_Flinging = false; if (!stateAlive.expired()) state->EndScroll(m_Token); }
        }

        void StartFling(Offset velocity) {
            m_FlingJob = rt::Launch(rt::Dispatcher::Main(), Fling(velocity));
        }

    private:
        rt::Task<> Fling(Offset velocity) {
            m_Flinging = true;
            m_Token = state->BeginScroll([this] { m_FlingJob.Cancel(); m_Flinging = false; state->SetScrollInProgress(false); });
            // The stretch may take the velocity to relax (applyToFling's first half).
            const Offset relaxed = overscroll ? overscroll->ConsumePreFling(velocity) : Offset{};
            velocity = velocity - relaxed;
            const Offset pre = node ? co_await node->DispatchPreFling(velocity) : Offset{};
            const Offset available = velocity - pre;
            float remaining = Axis(available);
            if (flingBehavior) {
                // Custom (snapping) flings run even at zero velocity: a release settles.
                std::function<float(float)> scrollBy = [this](float d) {
                    return Reverse(Axis(Dispatch(Single(Reverse(d)), NestedScrollSource::SideEffect)));
                };
                remaining = Reverse(co_await flingBehavior(std::move(scrollBy), Reverse(remaining)));
            } else if (std::abs(remaining) > 1) {   // DefaultFlingBehavior: below 1 px/s the spline gives NaNs
                anim::Animatable a(0.0f);
                float last = 0;
                // velocityLeft: the last frame's velocity (stopping the animation zeroes its own).
                float velocityLeft = remaining;
                auto decay = std::make_shared<anim::SplineDecay>(density.density, friction);
                co_await a.AnimateDecay(remaining, decay, [&](float value) {
                    const float delta = value - last;
                    last = value;
                    const float consumed = Axis(Dispatch(Single(delta), NestedScrollSource::SideEffect));
                    velocityLeft = a.Velocity()[0];
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();   // hit an edge
                });
                remaining = velocityLeft;
            }
            Offset left = Single(remaining);
            if (node) left = left - co_await node->DispatchPostFling(available - Single(remaining), Single(remaining));
            // Whatever is left hits the edge (applyToFling's second half; also releases a pull).
            if (overscroll) overscroll->AbsorbPostFling(left);
            m_Flinging = false;
            state->EndScroll(m_Token);
        }

        rt::Job m_FlingJob;
        std::uint64_t m_Token = 0;
        bool m_Flinging = false;
    };

    // --- ContentInViewNode (bring into view for scroll containers) ----------------------------

    namespace {
        constexpr float kMinScrollThreshold = 0.5f;

        struct ContentInViewParams {
            std::shared_ptr<ScrollingLogic> logic;
            bool operator==(const ContentInViewParams& o) const { return logic == o.logic; }
        };

        struct ContentInViewNode final : ModifierNode, LayoutModifier, ui::BringIntoViewResponder {
            ContentInViewParams params;
            IntSize viewport{ -1, -1 };
            std::vector<std::function<std::optional<Rect>()>> requests;
            bool running = false, tracking = false;
            rt::Job job;

            explicit ContentInViewNode(ContentInViewParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            LayoutModifier* AsLayout() override { return this; }
            ui::BringIntoViewResponder* AsBringIntoViewResponder() override { return this; }

            bool Vertical() const { return params.logic->orientation == Orientation::Vertical; }
            IntSize ViewportOrZero() const { return viewport.width < 0 ? IntSize{} : viewport; }
            float AxisOf(IntSize s) const { return float(Vertical() ? s.height : s.width); }

            float Relocation(const Rect& r, IntSize container, IntOffset adjust) const {
                const BringIntoViewSpec& spec = *params.logic->bringIntoViewSpec;
                return Vertical() ? spec.CalculateScrollDistance(r.top - float(adjust.y), r.bottom - r.top, float(container.height))
                                  : spec.CalculateScrollDistance(r.left - float(adjust.x), r.right - r.left, float(container.width));
            }
            bool MaxVisible(const Rect& r, IntSize container, IntOffset adjust = {}) const {
                return std::abs(Relocation(r, container, adjust)) <= kMinScrollThreshold;
            }

            Rect CalculateRectForParent(Rect r) override {
                const float d = Relocation(r, ViewportOrZero(), {});
                return Vertical() ? Rect{ r.left, r.top - d, r.right, r.bottom - d } : Rect{ r.left - d, r.top, r.right - d, r.bottom };
            }

            void BringChildIntoView(std::function<std::optional<Rect>()> localRect) override {
                auto r = localRect();
                if (!r || MaxVisible(*r, ViewportOrZero())) return;
                requests.push_back(std::move(localRect));
                if (!running) Launch({});
            }

            // The focused descendant's bounds in ours (getFocusedRect).
            std::optional<Rect> FocusedRect() const {
                LayoutNode* self = Owner();
                if (!self || !self->GetOwner()) return std::nullopt;
                ui::FocusTargetNode* f = self->GetOwner()->ActiveFocus();
                LayoutNode* n = f ? f->Owner() : nullptr;
                if (!n || n == self) return std::nullopt;
                bool inside = false;
                for (LayoutNode* a = n->Parent(); a; a = a->Parent()) if (a == self) { inside = true; break; }
                if (!inside) return std::nullopt;
                const Rect b = self->LayoutModifierRectInRoot(this);
                const Rect r = n->BoundsInRoot();
                return Rect{ r.left - b.left, r.top - b.top, r.right - b.left, r.bottom - b.top };
            }

            // findBringIntoViewRequest: from the smallest, the largest that still fits.
            std::optional<Rect> FindRequest() const {
                std::vector<Rect> rects;
                for (const auto& q : requests) if (auto r = q()) rects.push_back(*r);
                std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) {
                    return (a.right - a.left) * (a.bottom - a.top) < (b.right - b.left) * (b.bottom - b.top);
                });
                std::optional<Rect> chosen;
                const IntSize v = ViewportOrZero();
                for (const Rect& r : rects) {
                    const bool fits = Vertical() ? r.bottom - r.top <= float(v.height) : r.right - r.left <= float(v.width);
                    if (fits) chosen = r;
                    else return chosen ? chosen : r;
                }
                return chosen;
            }

            float ScrollDelta(IntOffset adjust) const {
                if (viewport.width < 0) return 0;
                std::optional<Rect> r = FindRequest();
                if (!r && tracking) r = FocusedRect();
                if (!r) return 0;
                const float d = Relocation(*r, viewport, adjust);
                return std::abs(d) <= kMinScrollThreshold ? 0.0f : d;
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                OnRemeasured({ p.Width(), p.Height() });
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }

            // The viewport shrank (an IME came up): keep a focused child that was visible in view.
            void OnRemeasured(IntSize size) {
                const IntSize previous = ViewportOrZero();
                viewport = size;
                params.logic->viewport = size;
                if (AxisOf(size) >= AxisOf(previous)) return;
                // Content anchored at the far edge (reverseDirection false) moves with the shrink.
                const IntOffset adjust = !params.logic->reverse
                    ? (Vertical() ? IntOffset{ 0, previous.height - size.height } : IntOffset{ previous.width - size.width, 0 })
                    : IntOffset{};
                if (auto f = FocusedRect(); f && !running && !tracking && MaxVisible(*f, previous) && !MaxVisible(*f, size, adjust)) {
                    tracking = true;
                    Launch(adjust);
                }
            }

            void Launch(IntOffset adjust) { job = rt::Launch(rt::Dispatcher::Main(), Animate(adjust)); }

            // UpdatableAnimationState.animateToZero with the default spring, retargeted after
            // every frame to what is still left to scroll.
            rt::Task<> Animate(IntOffset adjust) {
                ScrollableState* state = params.logic->state;
                if (!state) co_return;
                struct Reset {
                    ContentInViewNode* n;
                    ~Reset() { n->running = false; n->tracking = false; n->requests.clear(); }
                } reset{ this };
                running = true;
                bool stopped = false;
                ScrollSession session(*state, [&stopped] { stopped = true; });
                float value = ScrollDelta(adjust), velocity = 0;
                std::int64_t last = -1;
                const float sign = params.logic->reverse ? 1.0f : -1.0f;
                auto scroll = [&](float d) {
                    // Content coordinates to the state's: reverseDirection containers scroll forward.
                    const float consumed = state->ConsumeScrollDelta(sign * d) * sign;
                    // Children must sit where the scroll put them before the next rect is read.
                    if (!state->LaysOutOnScroll() && Owner()) Owner()->ForceRemeasure();
                    return std::abs(consumed) + 0.01f >= std::abs(d);
                };
                auto afterFrame = [&] {
                    std::erase_if(requests, [&](const auto& q) { auto r = q(); return !r || MaxVisible(*r, ViewportOrZero()); });
                    if (tracking) if (auto f = FocusedRect(); f && MaxVisible(*f, ViewportOrZero())) tracking = false;
                    value = ScrollDelta({});
                };
                while (std::abs(value) >= 0.01f) {
                    const std::int64_t now = co_await rt::NextFrame();
                    if (stopped) co_return;
                    if (last < 0) last = now;
                    const float scale = anim::DurationScale();
                    float next = 0;
                    if (scale > 0) {
                        const anim::SpringMotion mo = anim::SpringStep(1.0f, anim::spring::StiffnessMedium, value, velocity, 0,
                                                                       double(now - last) * 1e-9 / scale);
                        next = mo.value;
                        velocity = mo.velocity;
                    }
                    last = now;
                    const float d = value - next;
                    value = next;
                    if (!scroll(d)) co_return;   // not consumed: an edge
                    afterFrame();
                    if (scale == 0) break;
                }
                if (value != 0) {
                    co_await rt::NextFrame();
                    if (stopped) co_return;
                    scroll(value);
                }
            }
        };
    }

    namespace {
        struct NestedParams { std::shared_ptr<ScrollingLogic> logic; bool operator==(const NestedParams& o) const { return logic == o.logic; } };
        struct NestedNode final : NestedScrollNode {
            NestedParams params;
            explicit NestedNode(NestedParams p) : params(std::move(p)) { Apply(); }
            void OnParamsChanged() { Apply(); }
            void Apply() { connection = params.logic.get(); params.logic->node = this; }
            bool IsScrollableContainer() const override { return true; }
            void OnAttach() override { params.logic->node = this; }
            void OnDetach() override { if (params.logic->node == this) params.logic->node = nullptr; params.logic->StopFling(); }
        };

        // ScrollableNode's FocusTargetModifierNode(Focusability.Never): a focus group around the
        // content (focus searches inside it first; lazy layouts generate items beyond it).
        struct FocusGroupParams { bool operator==(const FocusGroupParams&) const = default; };
        struct ScrollFocusGroup final : ui::FocusTargetNode {
            FocusGroupParams params;
            explicit ScrollFocusGroup(FocusGroupParams) { focusability = ui::Focusability::Never; }
            void OnParamsChanged() {}
        };

        rt::Task<> ScrollGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            VelocityTracker tracker;
            for (;;) {
                // A press during a fling catches it: stop and swallow the down so children don't
                // see a press (Scrollable startDragImmediately while scrolling).
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                bool down = !e->changes.empty() && e->changes[0].ChangedToDownIgnoreConsumed();
                // CanDragCalculation: a mouse never drags a scrollable (it has the wheel).
                if (!down || e->changes[0].type == PointerType::Mouse) continue;
                // Also stops a programmatic scroll (the drag takes the scroll mutex).
                if (logic->Flinging()) { logic->StopFling(); e->changes[0].Consume(); }
                else if (logic->state->HasRunningScroll()) { logic->state->StopScroll(); e->changes[0].Consume(); }
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
                        // The down itself is usually consumed by a clickable child; only consumed
                        // movement means a child took the gesture (awaitFirstDown requireUnconsumed=false).
                        if (c->ChangedToDownIgnoreConsumed()) continue;
                        if (c->IsConsumed()) break;
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

        // --- MouseWheelScrollingLogic -----------------------------------------------------
        // Wheel deltas queue up; one coroutine animates them: a notch scrolls 6 dp at once and the
        // rest linearly at 1 dp/ms (at most 100 ms), new notches retarget the animation, and the
        // scroll ends 50 ms after the last one (isScrollInProgress meanwhile). Only a custom
        // fling behavior (snapping, pagers) runs afterwards. Times come from frames.
        constexpr float kWheelThresholdDp = 6;      // AnimationThreshold
        constexpr float kWheelSpeedDp = 1;          // AnimationSpeed, dp per ms
        constexpr int kWheelMaxDurationMs = 100;    // MaxAnimationDuration
        constexpr std::int64_t kWheelProgressTimeoutMs = 50;   // ScrollProgressTimeout
        bool Low(float v) { return std::abs(v) < 0.5f; }

        struct WheelDelta { float value; std::int64_t time; };
        struct WheelLogic {
            std::deque<WheelDelta> queue;
            ui::VelocityTracker1D velocity{ true };
            rt::Job job;
            bool running = false, scrolling = false;
            void Track(const WheelDelta& d) { velocity.AddDataPoint(d.time, d.value); }
            std::optional<WheelDelta> Sum() {
                if (queue.empty()) return std::nullopt;
                WheelDelta sum{ 0, 0 };
                for (const auto& d : queue) { sum.value += d.value; sum.time = std::max(sum.time, d.time); }
                queue.clear();
                return sum;
            }
        };

        rt::Task<> RunWheel(std::shared_ptr<ScrollingLogic> logic, std::shared_ptr<WheelLogic> w) {
            w->running = true;
            while (!w->queue.empty()) {
                const float density = logic->density.density;
                const float threshold = kWheelThresholdDp * density, speed = kWheelSpeedDp * density;
                WheelDelta first = w->queue.front();
                w->queue.pop_front();
                w->Track(first);
                float target = first.value;
                if (auto rest = w->Sum()) { w->Track(*rest); target += rest->value; }
                if (Low(target)) continue;
                float animValue = 0;
                bool stopped = false;
                auto dispatch = [&](float d) { return logic->Axis(logic->DispatchNested(logic->Single(d), NestedScrollSource::UserInput)); };
                // waitNextScrollDelta: the next notch within `timeout` ms starts over from it.
                auto waitNext = [&](std::int64_t timeout) -> rt::Task<bool> {
                    if (timeout < 0) co_return false;
                    const std::int64_t start = co_await rt::NextFrame();
                    for (;;) {
                        if (stopped) co_return false;
                        if (!w->queue.empty()) {
                            const WheelDelta next = w->queue.front();
                            w->queue.pop_front();
                            target = next.value;
                            animValue = 0;
                            w->Track(next);
                            co_return !Low(target);
                        }
                        const std::int64_t t = co_await rt::NextFrame();
                        if ((t - start) / 1'000'000 >= timeout) co_return false;
                    }
                };
                {
                    ScrollSession session(*logic->state, [&] { stopped = true; });
                    w->scrolling = true;
                    bool required = true;
                    while (required && !stopped) {
                        required = false;
                        const float leftover = target - animValue;
                        if (std::abs(leftover) < threshold) {
                            dispatch(leftover);
                            required = co_await waitNext(kWheelProgressTimeoutMs);
                        } else {
                            // The animation starts next frame: apply the threshold now.
                            const float instant = (leftover > 0 ? 1.0f : -1.0f) * threshold;
                            dispatch(instant);
                            const float from = animValue + instant;
                            const int duration = std::min(int(std::lround(std::abs(target - from) / speed)), kWheelMaxDurationMs);
                            float last = from, value = from;
                            bool cancelled = false;
                            std::int64_t t0 = -1;
                            while (!cancelled && !stopped) {
                                const std::int64_t t = co_await rt::NextFrame();
                                if (stopped) break;
                                if (t0 < 0) t0 = t;
                                const float f = duration <= 0 ? 1.0f : std::min(1.0f, float(t - t0) / 1e6f / float(duration));
                                value = from + (target - from) * f;
                                const float delta = value - last;
                                if (!Low(delta)) {
                                    const float consumed = dispatch(delta);
                                    if (!Low(delta - consumed)) { cancelled = true; break; }   // an edge
                                    last += delta;
                                }
                                if (auto more = w->Sum()) {
                                    w->Track(*more);
                                    target += more->value;
                                    required = !Low(target - last);
                                    cancelled = true;
                                    break;
                                }
                                if (f >= 1.0f) break;
                            }
                            animValue = value;
                            if (!required && !stopped) required = co_await waitNext(kWheelProgressTimeoutMs - duration);
                        }
                    }
                    w->scrolling = false;
                }
                float v = w->velocity.CalculateVelocity(std::numeric_limits<float>::max());
                if (v == 0) {
                    // A single notch: the animation speed in the delta's direction.
                    const float pxPerMs = std::min(std::abs(target) / float(kWheelMaxDurationMs), speed);
                    v = (target > 0 ? 1.0f : target < 0 ? -1.0f : 0.0f) * pxPerMs * 1000.0f;
                }
                // shouldBeTriggeredByMouseWheel: the default decay never runs for a wheel.
                if (logic->flingBehavior && !stopped) logic->StartFling(logic->Single(v));
            }
            w->running = false;
        }

        rt::Task<> WheelGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            auto w = std::make_shared<WheelLogic>();
            // The handler restarting or leaving cancels a wheel scroll in progress.
            struct Guard { std::shared_ptr<WheelLogic> w; ~Guard() { w->job.Cancel(); w->running = false; w->scrolling = false; } } guard{ w };
            // onScrollingEvent: queue the pixel delta (drag direction) if we can scroll that way.
            auto offer = [&s, logic, w](PointerEvent& e) {
                const auto& cfg = s.Config();
                const Offset d = e.changes[0].scrollDelta;
                // AndroidConfig.calculateMouseWheelScroll: scrollDelta * -factor (wheel up = finger
                // moving down = towards the start).
                const float px = -(logic->orientation == Orientation::Vertical ? d.y * cfg.verticalScrollFactor : d.x * cfg.horizontalScrollFactor);
                const float directional = logic->Reverse(px);
                const bool can = directional < 0 ? logic->state->CanScrollBackward() : directional > 0 && logic->state->CanScrollForward();
                if (!can) return w->scrolling;
                w->queue.push_back({ px, e.changes[0].uptimeMillis });
                if (!w->running) w->job = rt::Launch(rt::Dispatcher::Main(), RunWheel(logic, w));
                return true;
            };
            auto isConsumed = [](const PointerEvent& e) {
                for (const auto& c : e.changes) if (c.IsConsumed()) return true;
                return false;
            };
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                if (e->type != PointerEventType::Scroll || e->changes.empty() || isConsumed(*e)) continue;
                // Already scrolling from earlier notches: take it first (priority over children).
                if (w->scrolling) {
                    offer(*e);
                    for (auto& c : e->changes) c.Consume();
                    continue;
                }
                auto m = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (m->type != PointerEventType::Scroll || isConsumed(*m) || w->scrolling) continue;
                if (offer(*m)) for (auto& c : m->changes) c.Consume();
            }
        }

        // --- TrackpadScrollingLogic --------------------------------------------------------
        // A two-finger swipe's pan offsets scroll directly (with overscroll) until PanEnd, then
        // fling with their velocity.
        struct PanDelta { float value; std::int64_t time; bool end; };
        struct PanLogic {
            std::deque<PanDelta> queue;
            ui::VelocityTracker1D velocity{ true };
            rt::Job job;
            bool running = false, scrolling = false;
            std::optional<PanDelta> Sum() {
                if (queue.empty()) return std::nullopt;
                PanDelta sum{ 0, 0, false };
                for (const auto& d : queue) { sum.value += d.value; sum.time = std::max(sum.time, d.time); sum.end = sum.end || d.end; }
                queue.clear();
                return sum;
            }
        };

        rt::Task<> RunPan(std::shared_ptr<ScrollingLogic> logic, std::shared_ptr<PanLogic> p) {
            p->running = true;
            while (!p->queue.empty()) {
                PanDelta d = *p->Sum();
                p->velocity.AddDataPoint(d.time, d.value);
                bool stopped = false;
                {
                    ScrollSession session(*logic->state, [&] { stopped = true; });
                    p->scrolling = true;
                    logic->Dispatch(logic->Single(d.value), NestedScrollSource::UserInput);
                    while (!d.end && !stopped) {
                        while (p->queue.empty() && !stopped) co_await rt::NextFrame();
                        if (stopped) break;
                        d = *p->Sum();
                        p->velocity.AddDataPoint(d.time, d.value);
                        logic->Dispatch(logic->Single(d.value), NestedScrollSource::UserInput);
                    }
                    p->scrolling = false;
                }
                const float v = p->velocity.CalculateVelocity(std::numeric_limits<float>::max());
                p->velocity.Reset();
                if (!stopped) logic->StartFling(logic->Single(v));
            }
            p->running = false;
        }

        rt::Task<> TrackpadGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            auto p = std::make_shared<PanLogic>();
            struct Guard { std::shared_ptr<PanLogic> p; ~Guard() { p->job.Cancel(); p->running = false; p->scrolling = false; } } guard{ p };
            auto can = [logic](float px) {
                const float directional = logic->Reverse(px);
                return directional < 0 ? logic->state->CanScrollBackward() : directional > 0 && logic->state->CanScrollForward();
            };
            // onScrollingEvent: queue the (negated) pan offsets this container can take.
            auto offer = [logic, p, can](PointerEvent& e) {
                if (e.changes.empty()) return p->scrolling;
                const PointerInputChange& c = e.changes[0];
                bool sent = false;
                if (e.type == PointerEventType::PanStart && c.panOffset.x == 0 && c.panOffset.y == 0 &&
                    std::all_of(c.historical.begin(), c.historical.end(), [](const HistoricalChange& h) { return h.panOffset.x == 0 && h.panOffset.y == 0; }))
                    return p->scrolling;   // nothing yet: a descendant may want it
                for (const auto& h : c.historical) {
                    const float d = -logic->Axis(h.panOffset);
                    if (can(d)) { p->queue.push_back({ d, h.uptimeMillis, false }); sent = true; }
                }
                const float d = -logic->Axis(c.panOffset);
                const bool end = e.type == PointerEventType::PanEnd;
                if (can(d) || end) { p->queue.push_back({ d, c.uptimeMillis, end }); sent = true; }
                if (sent && !p->running) p->job = rt::Launch(rt::Dispatcher::Main(), RunPan(logic, p));
                return sent || p->scrolling;
            };
            auto isPan = [](const PointerEvent& e) {
                return e.type == PointerEventType::PanStart || e.type == PointerEventType::PanMove || e.type == PointerEventType::PanEnd;
            };
            auto isConsumed = [](const PointerEvent& e) {
                for (const auto& c : e.changes) if (c.IsConsumed()) return true;
                return false;
            };
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                if (!isPan(*e) || isConsumed(*e)) continue;
                if (p->scrolling) {
                    offer(*e);
                    for (auto& c : e->changes) c.Consume();
                    continue;
                }
                auto m = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (!isPan(*m) || isConsumed(*m) || p->scrolling) continue;
                if (offer(*m)) for (auto& c : m->changes) c.Consume();
            }
        }

        struct ScrollLayoutParams { ScrollState* state; bool vertical; bool reverse; bool operator==(const ScrollLayoutParams&) const = default; };
        struct ScrollLayoutNode final : ModifierNode, LayoutModifier {
            ScrollLayoutParams params;
            explicit ScrollLayoutNode(ScrollLayoutParams p) : params(p) {}
            int lastValue = -1;   // scrolling changes bounds below: tell accessibility
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                // checkScrollableContainerConstraints: an unbounded scroll axis is a programming error.
                Constraints child = c;
                // The minimums pass through: content that fits fills the viewport (ScrollNode).
                if (params.vertical) child.maxHeight = Constraints::Infinity;
                else child.maxWidth = Constraints::Infinity;
                Placeable& p = m.Measure(child);
                const int w = std::min(p.Width(), c.maxWidth), h = std::min(p.Height(), c.maxHeight);
                const int scrollHeight = p.Height() - h, scrollWidth = p.Width() - w;
                const int side = params.vertical ? h : w;
                params.state->SetBounds(params.vertical ? scrollHeight : scrollWidth, side, params.vertical ? p.Height() : p.Width(), params.reverse);
                ScrollState* state = params.state;
                const bool vertical = params.vertical, reverse = params.reverse;
                return s.Layout(w, h, [this, &p, state, vertical, reverse, scrollHeight, scrollWidth](PlacementScope& place) {
                    // Reading the value here subscribes placement only: scrolling re-places, it
                    // does not re-measure.
                    const int value = std::clamp(state->Value(), 0, vertical ? scrollHeight : scrollWidth);
                    if (value != lastValue) { lastValue = value; InvalidateSemantics(); }
                    const int abs = reverse ? value - (vertical ? scrollHeight : scrollWidth) : -value;
                    if (vertical) place.PlaceRelative(p, 0, abs);
                    else place.PlaceRelative(p, abs, 0);
                });
            }
        };
    }

    namespace {
        rt::Task<> SemanticsScrollBy(ScrollableState* state, float amount) { co_await AnimateScrollBy(*state, amount); }

        // ScrollByOffset: what scroll capture drives. During a capture animations are off
        // (DisableAnimationMotionDurationScale) and nested scrolling is skipped, so the delta
        // goes to this state at once; otherwise it animates like ScrollBy.
        rt::Task<Offset> SemanticsScrollByOffset(std::shared_ptr<ScrollingLogic> logic, std::weak_ptr<const char> alive, Offset offset,
                                                 bool inCapture) {
            const float amount = logic->Axis(offset);
            if (amount == 0 || alive.expired() || !logic->state) co_return Offset{};
            if (inCapture) co_return logic->Single(logic->state->ConsumeScrollDelta(amount));
            const float consumed = co_await AnimateScrollBy(*logic->state, amount);
            co_return logic->Single(consumed);
        }
    }

    Modifier Scrollable(Modifier m, ScrollableState& state, Orientation o, ScrollableOptions opt) {
        std::shared_ptr<ScrollingLogic> logic = std::make_shared<ScrollingLogic>();
        if (auto* c = rt::Composition::Current()) logic = c->Remember<std::shared_ptr<ScrollingLogic>>([&] { return logic; });
        logic->state = &state;
        logic->stateAlive = state.AliveToken();
        logic->orientation = o;
        logic->reverse = opt.reverseDirection;
        logic->overscroll = opt.overscroll;
        logic->flingBehavior = opt.flingBehavior;
        logic->bringIntoViewSpec = opt.bringIntoViewSpec ? opt.bringIntoViewSpec : CurrentBringIntoViewSpec();
        m = m.With<NestedNode>(NestedParams{ logic });
        m = m.With<ScrollFocusGroup>(FocusGroupParams{});
        m = m.With<ContentInViewNode>(ContentInViewParams{ logic });
        if (!opt.enabled) return m;
        // PageUp / PageDown (ScrollableNode.onKeyEvent): a viewport at once, unless Ctrl is held.
        m = OnKeyEvent(std::move(m), [logic](const ui::KeyEvent& e) {
            if ((e.keyCode != ui::keys::PageUp && e.keyCode != ui::keys::PageDown) || e.type != ui::KeyEventType::KeyDown || e.Ctrl())
                return false;
            const IntSize v = logic->viewport;
            const float size = float(logic->orientation == Orientation::Vertical ? v.height : v.width);
            const float amount = e.keyCode == ui::keys::PageUp ? size : -size;   // in drag direction
            ScrollSession session(*logic->state, [] {});
            logic->Dispatch(logic->Single(amount), NestedScrollSource::UserInput);
            return true;
        });
        // SemanticsActions.ScrollBy (semanticsScrollBy): an animated scroll along our axis.
        m = Semantics(std::move(m), [logic, o](SemanticsConfiguration& c) {
            c.scrollByOffset = [logic](Offset offset) {
                ui::Owner* owner = logic->node && logic->node->Owner() ? logic->node->Owner()->GetOwner() : nullptr;
                return SemanticsScrollByOffset(logic, logic->stateAlive, offset, owner && owner->ScrollCaptureInProgress());
            };
            c.scrollBy = [logic, o](float x, float y) {
                const float amount = o == Orientation::Vertical ? y : x;
                if (amount == 0 || !logic->state) return false;
                logic->semanticsScroll = rt::Launch(rt::Dispatcher::Main(), SemanticsScrollBy(logic->state, amount));
                return true;
            };
        });
        // Pad drags scroll the way the finger moves (Scrollable undoes its usual inversion).
        if (!logic->indirect) {
            logic->indirect = std::make_shared<IndirectDragDetector>();
            std::weak_ptr<ScrollingLogic> w = logic;
            auto& cb = logic->indirect->callbacks;
            cb.startDragImmediately = [w] { auto l = w.lock(); return l && (l->Flinging() || l->state->HasRunningScroll()); };
            cb.onStart = [w](Offset) {
                auto l = w.lock();
                if (!l) return;
                if (l->Flinging()) l->StopFling();
                else if (l->state->HasRunningScroll()) l->state->StopScroll();
                l->state->SetScrollInProgress(true);
            };
            cb.onDelta = [w](Offset d) {
                if (auto l = w.lock()) l->Dispatch(l->Single(-l->Axis(d)), NestedScrollSource::UserInput);
            };
            cb.onStop = [w](Offset v) {
                auto l = w.lock();
                if (!l) return;
                l->state->SetScrollInProgress(false);
                l->StartFling(l->Single(-l->Axis(v)));
            };
            cb.onCancel = [w] { if (auto l = w.lock()) l->state->SetScrollInProgress(false); };
        }
        logic->indirect->orientation = o;
        m = IndirectDrag(std::move(m), logic->indirect);
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(o)), [logic](PointerInputScope& s) {
            logic->density = s.GetDensity();
            logic->friction = s.Config().scrollFriction;
            return ScrollGestures(s, logic);
        });
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(o), 1), [logic](PointerInputScope& s) {
            return WheelGestures(s, logic);
        });
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(o), 2), [logic](PointerInputScope& s) {
            return TrackpadGestures(s, logic);
        });
        return m;
    }

    namespace {
        Modifier ScrollModifier(Modifier m, ScrollState& state, bool vertical, bool enabled, bool reverseScrolling,
                                OverscrollParam overscroll) {
            // ScrollableDefaults.reverseDirection: dragging moves content with the finger, so the
            // drag delta is reversed unless reverseScrolling (RTL flips horizontal again; TODO when
            // the scrollable reads the layout direction).
            ScrollableOptions opt;
            opt.enabled = enabled;
            opt.reverseDirection = !reverseScrolling;
            // ScrollableDefaults.reverseDirection: RTL flips horizontal scrolling again.
            if (!vertical && rt::Composition::Current() &&
                rt::Composition::Current()->Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl)
                opt.reverseDirection = !opt.reverseDirection;
            // rememberOverscrollEffect(): Android's default is the stretch.
            opt.overscroll = overscroll ? *overscroll : RememberOverscrollEffect();
            m = ClipToBounds(std::move(m));
            // Vertical/HorizontalScrollAxisRange (ScrollingContainer semantics).
            ScrollState* st = &state;
            m = Semantics(std::move(m), [st, vertical, reverseScrolling](SemanticsConfiguration& c) {
                ui::ScrollAxisRange range{ [st] { return float(st->Value()); }, [st] { return float(st->MaxValue()); }, reverseScrolling };
                if (vertical) c.verticalScrollAxisRange = range; else c.horizontalScrollAxisRange = range;
            });
            m = Overscroll(std::move(m), opt.overscroll);
            m = Scrollable(std::move(m), state, vertical ? Orientation::Vertical : Orientation::Horizontal, opt);
            return m.With<ScrollLayoutNode>(ScrollLayoutParams{ &state, vertical, reverseScrolling });
        }
    }

    Modifier VerticalScroll(Modifier m, ScrollState& s, bool enabled, bool reverse, OverscrollParam o) {
        return ScrollModifier(std::move(m), s, true, enabled, reverse, std::move(o));
    }
    Modifier HorizontalScroll(Modifier m, ScrollState& s, bool enabled, bool reverse, OverscrollParam o) {
        return ScrollModifier(std::move(m), s, false, enabled, reverse, std::move(o));
    }

}
