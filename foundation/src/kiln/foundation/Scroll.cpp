#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Overscroll.h"

#include "kiln/ui/BringIntoView.h"
#include "kiln/ui/Locals.h"
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
        Orientation orientation = Orientation::Vertical;
        bool reverse = false;
        NestedScrollNode* node = nullptr;
        Density density;
        float friction = 0.015f;
        FlingBehavior flingBehavior;
        rt::Job semanticsScroll;   // ScrollBy from accessibility (an animated scroll)
        IntSize viewport;          // ContentInViewNode's measured size (page keys)

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
        void StopFling() {
            m_FlingJob.Cancel();
            if (m_Flinging) { m_Flinging = false; state->EndScroll(m_Token); }
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
            const Offset pre = node ? node->DispatchPreFling(velocity) : Offset{};
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
                auto decay = std::make_shared<anim::SplineDecay>(density.density, friction);
                auto r = co_await a.AnimateDecay(remaining, decay, [&](float value) {
                    const float delta = value - last;
                    last = value;
                    const float consumed = Axis(Dispatch(Single(delta), NestedScrollSource::SideEffect));
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();   // hit an edge
                });
                remaining = r.reason == anim::EndReason::Interrupted ? a.Velocity()[0] : 0.0f;
            }
            Offset left = Single(remaining);
            if (node) left = left - node->DispatchPostFling(available - Single(remaining), Single(remaining));
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

        // BringIntoViewSpec.defaultCalculateScrollDistance.
        float ScrollDistance(float offset, float size, float container) {
            const float leading = offset, trailing = offset + size;
            if (leading >= 0 && trailing <= container) return 0;          // already visible
            if (leading < 0 && trailing > container) return 0;            // larger and covering
            return std::abs(leading) < std::abs(trailing - container) ? leading : trailing - container;
        }

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
                return Vertical() ? ScrollDistance(r.top - float(adjust.y), r.bottom - r.top, float(container.height))
                                  : ScrollDistance(r.left - float(adjust.x), r.right - r.left, float(container.width));
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
                const Rect b = self->LayoutModifierBoundsInRoot(this);
                const Offset p = n->PositionInRoot();
                const IntSize s = n->Size();
                return Rect{ p.x - b.left, p.y - b.top, p.x - b.left + float(s.width), p.y - b.top + float(s.height) };
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

        rt::Task<> ScrollGestures(PointerInputScope& s, std::shared_ptr<ScrollingLogic> logic) {
            VelocityTracker tracker;
            for (;;) {
                // A press during a fling catches it: stop and swallow the down so children don't
                // see a press (Scrollable startDragImmediately while scrolling).
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                bool down = !e->changes.empty() && e->changes[0].ChangedToDownIgnoreConsumed();
                if (!down) continue;
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
            int lastValue = -1;   // scrolling changes bounds below: tell accessibility
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
    }

    Modifier Scrollable(Modifier m, ScrollableState& state, Orientation o, ScrollableOptions opt) {
        std::shared_ptr<ScrollingLogic> logic = std::make_shared<ScrollingLogic>();
        if (auto* c = rt::Composition::Current()) logic = c->Remember<std::shared_ptr<ScrollingLogic>>([&] { return logic; });
        logic->state = &state;
        logic->orientation = o;
        logic->reverse = opt.reverseDirection;
        logic->overscroll = opt.overscroll;
        logic->flingBehavior = opt.flingBehavior;
        m = m.With<NestedNode>(NestedParams{ logic });
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
            c.scrollBy = [logic, o](float x, float y) {
                const float amount = o == Orientation::Vertical ? y : x;
                if (amount == 0 || !logic->state) return false;
                logic->semanticsScroll = rt::Launch(rt::Dispatcher::Main(), SemanticsScrollBy(logic->state, amount));
                return true;
            };
        });
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
