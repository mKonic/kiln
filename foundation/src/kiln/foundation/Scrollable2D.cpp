#include "kiln/foundation/Scrollable2D.h"

#include "kiln/animation/Decay.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/NestedScroll.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    using namespace ui;

    LambdaScrollableState& RememberScrollableState(std::function<float(float)> consume, rt::Key key) {
        auto& s = rt::Composition::Current()->RememberNew<LambdaScrollableState>({}, key, consume);
        s.SetConsume(std::move(consume));
        return s;
    }

    Scrollable2DState& RememberScrollable2DState(std::function<Offset(Offset)> consume, rt::Key key) {
        auto& s = rt::Composition::Current()->RememberNew<Scrollable2DState>({}, key, consume);
        s.SetConsume(std::move(consume));
        return s;
    }

    namespace {
        // Scrollable2DNode's scrolling logic: deltas in drag direction (the state gets them
        // reversed unless reverseDirection).
        struct Logic2D final : NestedScrollConnection {
            Scrollable2DState* state = nullptr;
            bool reverse = false;
            std::shared_ptr<OverscrollEffect> overscroll;
            NestedScrollNode* node = nullptr;
            float density = 1, friction = 0.015f;
            rt::Job flingJob;
            bool flinging = false;

            Offset Reverse(Offset o) const { return reverse ? o : Offset{ -o.x, -o.y }; }
            Offset Dispatch(Offset delta, NestedScrollSource src) {
                if (overscroll) return overscroll->ApplyToScroll(delta, src, [this, src](Offset d) { return DispatchNested(d, src); });
                return DispatchNested(delta, src);
            }
            Offset DispatchNested(Offset delta, NestedScrollSource src) {
                const Offset pre = node ? node->DispatchPreScroll(delta, src) : Offset{};
                const Offset left = delta - pre;
                const Offset self = Reverse(state->ConsumeScrollDelta(Reverse(left)));
                if (node) node->DispatchOnScrollChanged(self);
                const Offset post = node ? node->DispatchPostScroll(self, left - self, src) : Offset{};
                return pre + self + post;
            }
            Offset OnPostScroll(Offset, Offset available, NestedScrollSource) override {
                if (state->IsScrollInProgress()) return {};
                return Reverse(state->ConsumeScrollDelta(Reverse(available)));
            }
            rt::Task<Offset> OnPostFling(Offset, Offset available) override {
                if (flinging) co_return Offset{};
                StartFling(available);
                co_return available;
            }
            void StopFling() {
                flingJob.Cancel();
                if (flinging) { flinging = false; state->SetScrollInProgress(false); }
            }
            void StartFling(Offset velocity) { flingJob = rt::Launch(rt::Dispatcher::Main(), Fling(velocity)); }

            rt::Task<> Fling(Offset velocity) {
                flinging = true;
                state->SetStop([this] { flingJob.Cancel(); flinging = false; });
                state->SetScrollInProgress(true);
                const Offset relaxed = overscroll ? overscroll->ConsumePreFling(velocity) : Offset{};
                velocity = velocity - relaxed;
                const Offset pre = node ? co_await node->DispatchPreFling(velocity) : Offset{};
                const Offset available = velocity - pre;
                // DefaultFling2DBehavior: each axis decays along the spline on its own.
                Offset left = available;
                if (std::hypot(available.x, available.y) > 1) {
                    const anim::SplineDecay decay(density, friction);
                    const std::int64_t durX = decay.DurationNanos(0, available.x), durY = decay.DurationNanos(0, available.y);
                    const std::int64_t duration = std::max(durX, durY);
                    std::int64_t start = -1;
                    Offset last{};
                    for (;;) {
                        const std::int64_t frame = co_await rt::NextFrame();
                        if (start < 0) start = frame;
                        const std::int64_t t = std::min(frame - start, duration);
                        const Offset value{ decay.ValueAt(std::min(t, durX), 0, available.x), decay.ValueAt(std::min(t, durY), 0, available.y) };
                        const Offset delta = value - last;
                        last = value;
                        const Offset consumed = Dispatch(delta, NestedScrollSource::SideEffect);
                        left = { t < durX ? decay.VelocityAt(t, 0, available.x) : 0, t < durY ? decay.VelocityAt(t, 0, available.y) : 0 };
                        // An axis that hit an edge stops; the fling ends when both have.
                        if (std::abs(delta.x - consumed.x) > 0.5f && std::abs(delta.y - consumed.y) > 0.5f) break;
                        if (t >= duration) { left = {}; break; }
                    }
                }
                if (node) left = left - co_await node->DispatchPostFling(available - left, left);
                if (overscroll) overscroll->AbsorbPostFling(left);
                flinging = false;
                state->SetStop(nullptr);
                state->SetScrollInProgress(false);
            }
        };

        struct NestedParams2D { std::shared_ptr<Logic2D> logic; bool operator==(const NestedParams2D& o) const { return logic == o.logic; } };
        struct NestedNode2D final : NestedScrollNode {
            NestedParams2D params;
            explicit NestedNode2D(NestedParams2D p) : params(std::move(p)) { Apply(); }
            void OnParamsChanged() { Apply(); }
            void Apply() { connection = params.logic.get(); params.logic->node = this; }
            bool IsScrollableContainer() const override { return true; }
            void OnAttach() override { params.logic->node = this; }
            void OnDetach() override { if (params.logic->node == this) params.logic->node = nullptr; params.logic->StopFling(); }
        };
    }

    Modifier Scrollable2D(Modifier m, Scrollable2DState& state, Scrollable2DOptions options) {
        auto logic = std::make_shared<Logic2D>();
        auto callbacks = std::make_shared<Drag2DCallbacks>();
        if (auto* c = rt::Composition::Current()) {
            logic = c->Remember<std::shared_ptr<Logic2D>>([&] { return logic; });
            callbacks = c->Remember<std::shared_ptr<Drag2DCallbacks>>([&] { return callbacks; });
        }
        logic->state = &state;
        logic->reverse = options.reverseDirection;
        logic->overscroll = options.overscroll;
        m = m.With<NestedNode2D>(NestedParams2D{ logic });
        if (!options.enabled) return m;
        std::weak_ptr<Logic2D> w = logic;
        // A press during a fling catches it; dragging scrolls; the release flings.
        callbacks->startImmediately = [w] { auto l = w.lock(); return l && l->flinging; };
        callbacks->onStart = [w](Offset) {
            auto l = w.lock();
            if (!l) return;
            l->StopFling();
            l->state->SetScrollInProgress(true);
        };
        callbacks->onDelta = [w](Offset d) { if (auto l = w.lock()) l->Dispatch(d, NestedScrollSource::UserInput); };
        callbacks->onStop = [w](Offset v) {
            auto l = w.lock();
            if (!l) return;
            l->state->SetScrollInProgress(false);
            l->StartFling(v);
        };
        callbacks->onCancel = [w] { if (auto l = w.lock()) l->state->SetScrollInProgress(false); };
        return PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state)), [logic, callbacks](PointerInputScope& s) {
            logic->density = s.GetDensity().density;
            logic->friction = s.Config().scrollFriction;
            return DetectDrag2D(s, callbacks);
        });
    }

}
