// ConstraintLayout's animateChangesSpec: the two ConstraintLayout overloads' animated branches
// (ConstraintLayout.kt) and LateMotionLayout (LateMotionLayout.kt), which animates between the
// previous and the new constraints through MotionMeasurer.

#include "kiln/constraintlayout/MotionInternal.h"

#include "kiln/animation/Animatable.h"
#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Locals.h"

namespace kiln::constraintlayout {

    using detail::CompositionSource;
    using detail::MotionMeasurer;
    using detail::TransitionImpl;

    namespace {

        using SetState = rt::State<std::shared_ptr<ConstraintSet>>;
        using SetChannel = rt::Channel<std::shared_ptr<ConstraintSet>>;

        bool SameSet(const std::shared_ptr<ConstraintSet>& a, const std::shared_ptr<ConstraintSet>& b) {
            if (a == b) return true;
            return a && b && a->Equals(*b);
        }

        struct LateContent {
            rt::State<int, rt::NeverEqual> tracker{ 0 };
            CompositionSource source = CompositionSource::Unknown;
        };

        class LateMotionLayoutPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& scope, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                (void)content->tracker.Get();
                const std::shared_ptr<ConstraintSet>& startSet = start->Get();
                const std::shared_ptr<ConstraintSet>& endSet = end->Get();
                const float progress = motionProgress->Float();
                if (!startSet || !endSet) return scope.Layout(c.minWidth, c.minHeight, [](ui::PlacementScope&) {});
                const ui::IntSize size = measurer->PerformInterpolationMeasure(c, scope.Direction(), *startSet, *endSet, *TransitionImpl::Empty(), children,
                                                                               optimizationLevel, progress, content->source, nullptr);
                content->source = CompositionSource::Unknown;
                std::vector<ui::Measurable*> measured(children.begin(), children.end());
                return scope.Layout(size.width, size.height, [this, measured = std::move(measured)](ui::PlacementScope& place) {
                    measurer->PerformLayout(place, measured);
                });
            }

            std::unique_ptr<MotionMeasurer> measurer;
            std::shared_ptr<LateContent> content;
            std::shared_ptr<SetState> start, end;
            std::shared_ptr<anim::Animatable> motionProgress;
            int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD;
        };

        void LateMotionLayout(const std::shared_ptr<SetState>& start, const std::shared_ptr<SetState>& end, anim::SpecPtr animationSpec,
                              const std::shared_ptr<SetChannel>& channel, const std::shared_ptr<LateContent>& content, int optimizationLevel,
                              kiln::Function<void()> finishedAnimationListener, const Modifier& modifier, kiln::Function<void()> composable, rt::Key key) {
            auto* c = rt::Composition::Current();
            const float density = c->Read(ui::LocalDensity).density;
            struct Holder {
                std::shared_ptr<LateMotionLayoutPolicy> policy = std::make_shared<LateMotionLayoutPolicy>();
                std::shared_ptr<anim::Animatable> progress = std::make_shared<anim::Animatable>(0.0f);
                std::shared_ptr<rt::State<int>> direction = std::make_shared<rt::State<int>>(1);
            };
            auto& holder = c->Remember<std::shared_ptr<Holder>>([&] {
                auto h = std::make_shared<Holder>();
                h->policy->measurer = std::make_unique<MotionMeasurer>(density);
                return h;
            });
            Holder* h = holder.get();
            h->policy->content = content;
            h->policy->start = start;
            h->policy->end = end;
            h->policy->motionProgress = h->progress;
            h->policy->optimizationLevel = optimizationLevel;
            foundation::Layout(modifier, h->policy, std::move(composable), key);

            std::shared_ptr<anim::Animatable> progress = h->progress;
            std::shared_ptr<rt::State<int>> direction = h->direction;
            auto listener = std::make_shared<kiln::Function<void()>>(std::move(finishedAnimationListener));
            c->LaunchedEffect(rt::Keys(channel), [channel, start, end, content, progress, direction, animationSpec, listener]() -> rt::Task<> {
                while (std::optional<std::shared_ptr<ConstraintSet>> constraints = co_await channel->Next()) {
                    std::shared_ptr<ConstraintSet> newConstraints = channel->TryReceive().value_or(*constraints);
                    const bool forward = direction->Get() == 1;
                    const std::shared_ptr<ConstraintSet>& currentConstraints = forward ? start->Get() : end->Get();
                    if (!SameSet(newConstraints, currentConstraints)) {
                        if (forward) end->Set(newConstraints);
                        else start->Set(newConstraints);
                        content->source = CompositionSource::Content;
                        co_await progress->AnimateTo(float(direction->Get()), animationSpec);
                        direction->Set(forward ? 0 : 1);
                        if (*listener) (*listener)();
                    }
                }
            });
        }

    }

    void ConstraintLayout(const Modifier& modifier, kiln::Function<void(ConstraintLayoutScope&)> content, AnimateChanges animateChanges,
                          int optimizationLevel, rt::Key key) {
        if (!animateChanges.spec) {
            ConstraintLayout(modifier, std::move(content), optimizationLevel, key);
            return;
        }
        auto* c = rt::Composition::Current();
        struct Holder {
            std::shared_ptr<SetState> start = std::make_shared<SetState>(nullptr);
            std::shared_ptr<SetState> end = std::make_shared<SetState>(nullptr);
            ConstraintLayoutScope scope;
            std::shared_ptr<LateContent> content = std::make_shared<LateContent>();
            std::shared_ptr<SetChannel> channel = std::shared_ptr<SetChannel>(SetChannel::Conflated());
            Holder() { scope.isAnimateChanges = true; }
        };
        auto& holder = c->Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); }, {}, key);
        std::shared_ptr<Holder> h = holder;
        auto contentDelegate = [h, content = std::move(content)] {
            rt::Observation::Untracked([&] { h->content->tracker.Set(h->content->tracker.Get() + 1); });
            if (h->content->source == CompositionSource::Unknown) h->content->source = CompositionSource::Content;
            // Resetting the scope also resets the underlying ConstraintSet.
            h->scope.Reset();
            content(h->scope);   // the ConstraintSet is built here
            rt::Composition::Current()->SideEffect([h] {
                // A copy of the constraints: the first ones start and end, later ones animate.
                std::shared_ptr<ConstraintSet> cSet = detail::MakeRawConstraintSet(h->scope.ContainerObject()->cloneObject());
                if (h->start->Get() == nullptr || h->end->Get() == nullptr) {
                    h->start->Set(cSet);
                    h->end->Set(h->start->Get());
                } else {
                    h->channel->TrySend(cSet);
                }
            });
        };
        LateMotionLayout(h->start, h->end, animateChanges.spec, h->channel, h->content, optimizationLevel, std::move(animateChanges.finishedAnimationListener),
                         modifier, std::move(contentDelegate), key);
    }

    void ConstraintLayout(std::shared_ptr<ConstraintSet> constraintSet, const Modifier& modifier, kiln::Function<void()> content,
                          AnimateChanges animateChanges, int optimizationLevel, rt::Key key) {
        if (!animateChanges.spec) {
            ConstraintLayout(std::move(constraintSet), modifier, std::move(content), optimizationLevel, key);
            return;
        }
        auto* c = rt::Composition::Current();
        struct Holder {
            std::shared_ptr<SetState> startConstraint, endConstraint;
            std::shared_ptr<anim::Animatable> progress = std::make_shared<anim::Animatable>(0.0f);
            std::shared_ptr<SetChannel> channel = std::shared_ptr<SetChannel>(SetChannel::Conflated());
            std::shared_ptr<rt::State<int>> direction = std::make_shared<rt::State<int>>(1);
        };
        auto& holder = c->Remember<std::shared_ptr<Holder>>([&] {
            auto h = std::make_shared<Holder>();
            h->startConstraint = std::make_shared<SetState>(constraintSet);
            h->endConstraint = std::make_shared<SetState>(constraintSet);
            return h;
        }, {}, key);
        std::shared_ptr<Holder> h = holder;
        c->SideEffect([h, constraintSet] { h->channel->TrySend(constraintSet); });
        auto listener = std::make_shared<kiln::Function<void()>>(std::move(animateChanges.finishedAnimationListener));
        anim::SpecPtr spec = animateChanges.spec;
        c->LaunchedEffect(rt::Keys(h->channel), [h, spec, listener]() -> rt::Task<> {
            while (std::optional<std::shared_ptr<ConstraintSet>> constraints = co_await h->channel->Next()) {
                std::shared_ptr<ConstraintSet> newConstraints = h->channel->TryReceive().value_or(*constraints);
                const bool forward = h->direction->Get() == 1;
                const std::shared_ptr<ConstraintSet>& currentConstraints = forward ? h->startConstraint->Get() : h->endConstraint->Get();
                if (!SameSet(newConstraints, currentConstraints)) {
                    if (forward) h->endConstraint->Set(newConstraints);
                    else h->startConstraint->Set(newConstraints);
                    co_await h->progress->AnimateTo(float(h->direction->Get()), spec);
                    h->direction->Set(forward ? 0 : 1);
                    if (*listener) (*listener)();
                }
            }
        }, key);
        MotionLayout(h->startConstraint->Get(), h->endConstraint->Get(), h->progress->Float(), modifier,
                     [content = std::move(content)](MotionLayoutScope&) { content(); }, nullptr, MotionLayoutOptions{}, key);   // upstream passes no optimization level
    }

}
