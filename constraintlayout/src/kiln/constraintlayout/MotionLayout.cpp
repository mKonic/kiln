// constraintlayout-compose's MotionLayout: MotionMeasurer.kt (the measurer), MotionLayout.kt
// (the composables, MotionLayoutScope, the measure policy, DebugFlags, InvalidationStrategy),
// TransitionHandler.kt and MotionDragHandler.kt (OnSwipe).

#include "kiln/constraintlayout/MotionLayout.h"
#include "kiln/constraintlayout/MotionInternal.h"

#include "kiln/animation/Animatable.h"
#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/VelocityTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace kiln::constraintlayout {

    using detail::CompositionSource;
    using detail::MotionMeasurer;
    using detail::TransitionImpl;

    // --- MotionMeasurer ------------------------------------------------------------------------

    namespace detail {

        MotionMeasurer::MotionMeasurer(float density) : transition([density](float dp) { return dp * density; }) {}

        void MotionMeasurer::ApplyRootSize(const ui::Constraints& constraints) {
            m_Root.setWidth(constraints.maxWidth);
            m_Root.setHeight(constraints.maxHeight);
        }

        void MotionMeasurer::MeasureConstraintSet(int optimizationLevel, ConstraintSet& constraintSet, std::span<ui::Measurable* const> measurables,
                                                  const ui::Constraints& constraints) {
            m_State.reset();
            constraintSet.ApplyTo(m_State, measurables);
            BuildMapping(measurables);
            m_State.apply(m_Root);
            for (ConstraintWidget* child : m_Root.getChildren()) child->setAnimated(true);
            ApplyRootSize(constraints);
            m_Root.updateHierarchy();
            m_Root.setOptimizationLevel(optimizationLevel);
            m_Root.measure(core::Optimizer::OPTIMIZATION_NONE, 0, 0, 0, 0, 0, 0, 0, 0);
        }

        ui::IntSize MotionMeasurer::PerformInterpolationMeasure(const ui::Constraints& constraints, ui::LayoutDirection layoutDirection,
                                                                ConstraintSet& constraintSetStart, ConstraintSet& constraintSetEnd,
                                                                const TransitionImpl& transitionImpl, std::span<ui::Measurable* const> measurables,
                                                                int optimizationLevel, float progress, CompositionSource compositionSource,
                                                                const ShouldInvalidateCallback* invalidateOnConstraintsCallback) {
            // placeables = placeableMap: this pass's map starts empty.
            m_Placeables.clear();
            const bool needsRemeasure = NeedsRemeasure(constraints, compositionSource, invalidateOnConstraintsCallback);
            if (m_LastProgressInInterpolation != progress || needsRemeasure) {
                RecalculateInterpolation(constraints, layoutDirection, constraintSetStart, constraintSetEnd, &transitionImpl, measurables,
                                         optimizationLevel, progress, needsRemeasure);
            } else {
                // Nothing to interpolate (the content changed under an invalidation strategy that
                // ignores it): upstream's empty map would leave every child unplaced. Each child
                // keeps its frame instead, measured again at its size.
                for (ui::Measurable* measurable : measurables) {
                    const auto frame = m_FrameCache.find(AnyOrNullId(*measurable));
                    if (frame == m_FrameCache.end()) continue;
                    m_Placeables[measurable] = &measurable->Measure(ui::Constraints::Fixed(frame->second.width(), frame->second.height()));
                }
            }
            m_OldConstraints = constraints;
            return { m_Root.getWidth(), m_Root.getHeight() };
        }

        bool MotionMeasurer::NeedsRemeasure(const ui::Constraints& constraints, CompositionSource source,
                                            const ShouldInvalidateCallback* invalidateOnConstraintsCallback) {
            if (transition.isEmpty() || m_FrameCache.empty()) return true;
            if (m_OldConstraints && invalidateOnConstraintsCallback != nullptr && *invalidateOnConstraintsCallback) {
                if ((*invalidateOnConstraintsCallback)(*m_OldConstraints, constraints)) return true;
            } else if ((constraints.HasFixedHeight() && !m_State.sameFixedHeight(constraints.maxHeight)) ||
                       (constraints.HasFixedWidth() && !m_State.sameFixedWidth(constraints.maxWidth))) {
                return true;
            }
            return source == CompositionSource::Content;
        }

        void MotionMeasurer::RecalculateInterpolation(const ui::Constraints& constraints, ui::LayoutDirection layoutDirection,
                                                      ConstraintSet& constraintSetStart, ConstraintSet& constraintSetEnd, const TransitionImpl* transitionImpl,
                                                      std::span<ui::Measurable* const> measurables, int optimizationLevel, float progress,
                                                      bool remeasure) {
            m_LastProgressInInterpolation = progress;
            if (remeasure) {
                transition.clear();
                ResetMeasureState();
                m_State.width(constraints.HasFixedWidth() ? core::state::Dimension::createFixed(constraints.maxWidth)
                                                          : core::state::Dimension::createWrap().min(constraints.minWidth));
                m_State.height(constraints.HasFixedHeight() ? core::state::Dimension::createFixed(constraints.maxHeight)
                                                            : core::state::Dimension::createWrap().min(constraints.minHeight));
                m_State.rootIncomingConstraints = constraints;
                m_State.setRtl(layoutDirection == ui::LayoutDirection::Rtl);
                MeasureConstraintSet(optimizationLevel, constraintSetStart, measurables, constraints);
                transition.updateFrom(m_Root, core::state::Transition::START);
                MeasureConstraintSet(optimizationLevel, constraintSetEnd, measurables, constraints);
                transition.updateFrom(m_Root, core::state::Transition::END);
                if (transitionImpl != nullptr) transitionImpl->ApplyKeyFramesTo(transition);
            } else {
                BuildMapping(measurables);
            }
            transition.interpolate(m_Root.getWidth(), m_Root.getHeight(), progress);
            m_Root.setWidth(transition.getInterpolatedWidth());
            m_Root.setHeight(transition.getInterpolatedHeight());
            for (ConstraintWidget* child : m_Root.getChildren()) {
                auto* measurable = static_cast<ui::Measurable*>(child->getCompanionWidget());
                if (measurable == nullptr || !m_Known.count(measurable)) continue;
                WidgetFrame& interpolatedFrame = transition.getInterpolated(child);
                m_Placeables[measurable] = &measurable->Measure(ui::Constraints::Fixed(interpolatedFrame.width(), interpolatedFrame.height()));
                m_FrameCache[AnyOrNullId(*measurable)] = interpolatedFrame;
            }
        }

        Color MotionMeasurer::GetCustomColor(const std::string& id, const std::string& name, float progress) {
            if (!transition.contains(id)) return Color::Unspecified();
            transition.interpolate(m_Root.getWidth(), m_Root.getHeight(), progress);
            WidgetFrame* interpolatedFrame = transition.getInterpolated(id);
            if (!interpolatedFrame->containsCustom(name)) return Color::Unspecified();
            return Color(std::uint32_t(interpolatedFrame->getCustomColor(name)));
        }

        float MotionMeasurer::GetCustomFloat(const std::string& id, const std::string& name, float progress) {
            if (!transition.contains(id)) return NAN;
            transition.interpolate(m_Root.getWidth(), m_Root.getHeight(), progress);
            return transition.getInterpolated(id)->getCustomFloat(name);
        }

        void MotionMeasurer::ClearConstraintSets() {
            transition.clear();
            m_FrameCache.clear();
        }

        void MotionMeasurer::InitWith(ConstraintSet& start, ConstraintSet& end, ui::LayoutDirection layoutDirection, const TransitionImpl& transitionImpl,
                                      float progress) {
            ClearConstraintSets();
            m_State.setRtl(layoutDirection == ui::LayoutDirection::Rtl);
            start.ApplyTo(m_State, {});
            start.ApplyTo(transition, core::state::Transition::START);
            m_State.apply(m_Root);
            transition.updateFrom(m_Root, core::state::Transition::START);
            // Upstream applies the start set again before the end's custom values.
            start.ApplyTo(m_State, {});
            end.ApplyTo(transition, core::state::Transition::END);
            m_State.apply(m_Root);
            transition.updateFrom(m_Root, core::state::Transition::END);
            transition.interpolate(0, 0, progress);
            transitionImpl.ApplyAllTo(transition);
        }

    }

    // --- DebugFlags and InvalidationStrategy -------------------------------------------------

    std::string DebugFlags::ToString() const {
        auto b = [](bool v) { return v ? "true" : "false"; };
        return std::string("DebugFlags(showBounds = ") + b(ShowBounds()) + ", showPaths = " + b(ShowPaths()) + ", showKeyPositions = " +
               b(ShowKeyPositions()) + ")";
    }

    bool InvalidationStrategySpecification::ShouldInvalidateOnFixedWidth(const ui::Constraints& oldConstraints, const ui::Constraints& newConstraints,
                                                                         int skipCount, int threshold) {
        if (oldConstraints.HasFixedWidth() && newConstraints.HasFixedWidth()) {
            const int diff = std::abs(newConstraints.maxWidth - oldConstraints.maxWidth);
            if (diff >= threshold) {
                m_WidthRateCount = 0;
                return true;
            }
            if (diff != 0 && ++m_WidthRateCount > skipCount) {
                m_WidthRateCount = 0;
                return true;
            }
        } else {
            m_WidthRateCount = 0;
        }
        return false;
    }

    bool InvalidationStrategySpecification::ShouldInvalidateOnFixedHeight(const ui::Constraints& oldConstraints, const ui::Constraints& newConstraints,
                                                                          int skipCount, int threshold) {
        if (oldConstraints.HasFixedHeight() && newConstraints.HasFixedHeight()) {
            const int diff = std::abs(newConstraints.maxHeight - oldConstraints.maxHeight);
            if (diff >= threshold) {
                m_HeightRateCount = 0;
                return true;
            }
            if (diff != 0 && ++m_HeightRateCount > skipCount) {
                m_HeightRateCount = 0;
                return true;
            }
        } else {
            m_HeightRateCount = 0;
        }
        return false;
    }

    std::shared_ptr<InvalidationStrategy> InvalidationStrategy::Default() {
        static const std::shared_ptr<InvalidationStrategy> strategy = std::make_shared<InvalidationStrategy>(nullptr, nullptr);
        return strategy;
    }

    // --- MotionLayoutScope -----------------------------------------------------------------------

    Color MotionLayoutScope::CustomColor(const std::string& id, const std::string& name) const {
        return m_Measurer->GetCustomColor(id, name, m_Progress->Get());
    }
    float MotionLayoutScope::CustomFloat(const std::string& id, const std::string& name) const {
        return m_Measurer->GetCustomFloat(id, name, m_Progress->Get());
    }
    // Float.toInt(): NaN is 0, out-of-range values saturate.
    int MotionLayoutScope::CustomInt(const std::string& id, const std::string& name) const { return core::JavaInt(CustomFloat(id, name)); }
    Dp MotionLayoutScope::CustomDistance(const std::string& id, const std::string& name) const { return Dp(CustomFloat(id, name)); }
    TextUnit MotionLayoutScope::CustomFontSize(const std::string& id, const std::string& name) const { return text::Sp(CustomFloat(id, name)); }

    Color MotionLayoutScope::CustomProperties::GetColor(const std::string& name) const { return m_Scope->CustomColor(m_Id, name); }
    float MotionLayoutScope::CustomProperties::GetFloat(const std::string& name) const { return m_Scope->CustomFloat(m_Id, name); }
    int MotionLayoutScope::CustomProperties::GetInt(const std::string& name) const { return m_Scope->CustomInt(m_Id, name); }
    Dp MotionLayoutScope::CustomProperties::Distance(const std::string& name) const { return m_Scope->CustomDistance(m_Id, name); }
    TextUnit MotionLayoutScope::CustomProperties::FontSize(const std::string& name) const { return m_Scope->CustomFontSize(m_Id, name); }

    Color MotionLayoutScope::MotionProperties::GetColor(const std::string& name) const { return m_Scope->CustomColor(m_Id, name); }
    float MotionLayoutScope::MotionProperties::GetFloat(const std::string& name) const { return m_Scope->CustomFloat(m_Id, name); }
    int MotionLayoutScope::MotionProperties::GetInt(const std::string& name) const { return m_Scope->CustomInt(m_Id, name); }
    Dp MotionLayoutScope::MotionProperties::Distance(const std::string& name) const { return m_Scope->CustomDistance(m_Id, name); }
    TextUnit MotionLayoutScope::MotionProperties::FontSize(const std::string& name) const { return m_Scope->CustomFontSize(m_Id, name); }

    namespace {

        // onStartEndBoundsChanged: the child's start and end frames, compared on each placement.
        struct BoundsChangedParams {
            MotionMeasurer* measurer;
            std::string id;
            std::shared_ptr<kiln::Function<void(const ui::Rect&, const ui::Rect&)>> onBoundsChanged;
            bool operator==(const BoundsChangedParams& o) const { return measurer == o.measurer && id == o.id && onBoundsChanged == o.onBoundsChanged; }
        };
        struct BoundsChangedNode final : ui::ModifierNode, ui::LayoutAwareModifier {
            BoundsChangedParams params;
            int startPoints[4] = {};
            int endPoints[4] = {};
            ui::Rect startBounds, endBounds;
            explicit BoundsChangedNode(BoundsChangedParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }
            void OnPlaced(const ui::LayoutCoordinates&) override {
                core::state::Transition& transition = params.measurer->transition;
                bool changed = false;
                if (const core::WidgetFrame* startFrame = transition.getStart(params.id)) {
                    if (startFrame->left != startPoints[0] || startFrame->top != startPoints[1] || startFrame->right != startPoints[2] ||
                        startFrame->bottom != startPoints[3]) {
                        startPoints[0] = startFrame->left;
                        startPoints[1] = startFrame->top;
                        startPoints[2] = startFrame->right;
                        startPoints[3] = startFrame->bottom;
                        startBounds = { float(startPoints[0]), float(startPoints[1]), float(startPoints[2]), float(startPoints[3]) };
                        changed = true;
                    }
                }
                if (const core::WidgetFrame* endFrame = transition.getEnd(params.id)) {
                    if (endFrame->left != endPoints[0] || endFrame->top != endPoints[1] || endFrame->right != endPoints[2] ||
                        endFrame->bottom != endPoints[3]) {
                        endPoints[0] = endFrame->left;
                        endPoints[1] = endFrame->top;
                        endPoints[2] = endFrame->right;
                        endPoints[3] = endFrame->bottom;
                        endBounds = { float(endPoints[0]), float(endPoints[1]), float(endPoints[2]), float(endPoints[3]) };
                        changed = true;
                    }
                }
                if (changed) (*params.onBoundsChanged)(startBounds, endBounds);
            }
        };

    }

    Modifier MotionLayoutScope::OnStartEndBoundsChanged(Modifier modifier, const std::string& layoutId,
                                                        kiln::Function<void(const ui::Rect&, const ui::Rect&)> onBoundsChanged) const {
        return modifier.With<BoundsChangedNode>(BoundsChangedParams{
            m_Measurer, layoutId, std::make_shared<kiln::Function<void(const ui::Rect&, const ui::Rect&)>>(std::move(onBoundsChanged)) });
    }

    // --- The composables -------------------------------------------------------------------------

    namespace {

        // contentTracker and compositionSource: written by the content as it composes, read by
        // the measure policy.
        struct ContentState {
            rt::State<int, rt::NeverEqual> tracker{ 0 };
            CompositionSource source = CompositionSource::Unknown;
            void Touch() {
                rt::Observation::Untracked([this] { tracker.Set(tracker.Get() + 1); });
            }
        };

        class MotionLayoutPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& scope, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                (void)content->tracker.Get();
                detail::ShouldInvalidateCallback shouldInvalidate;
                if (invalidationStrategy && invalidationStrategy->onIncomingConstraints) {
                    InvalidationStrategy* strategy = invalidationStrategy.get();
                    shouldInvalidate = [strategy](const ui::Constraints& old, const ui::Constraints& now) {
                        return strategy->onIncomingConstraints(strategy->scope, old, now);
                    };
                }
                const ui::IntSize size = measurer->PerformInterpolationMeasure(c, scope.Direction(), *start, *end, *transition, children, optimizationLevel,
                                                                               motionProgress->Get(), content->source,
                                                                               shouldInvalidate ? &shouldInvalidate : nullptr);
                content->source = CompositionSource::Unknown;
                std::vector<ui::Measurable*> measured(children.begin(), children.end());
                return scope.Layout(size.width, size.height, [this, measured = std::move(measured)](ui::PlacementScope& place) {
                    measurer->PerformLayout(place, measured);
                });
            }

            MotionMeasurer* measurer = nullptr;
            std::shared_ptr<ContentState> content;
            std::shared_ptr<ConstraintSet> start, end;
            std::shared_ptr<TransitionImpl> transition;
            rt::State<float>* motionProgress = nullptr;
            int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD;
            std::shared_ptr<InvalidationStrategy> invalidationStrategy;
        };

        // Snapshot.observe(readObserver) over onObservedStateChange: a state the block reads
        // changing marks the content changed, as the content composing does.
        struct ObservedStateWatch final : rt::Subscriber {
            std::shared_ptr<ContentState> content;
            ui::LayoutNode* node = nullptr;
            void Run(const kiln::Function<void()>& block) {
                rt::Observation::Track(*this, block);
                if (!Reads().empty()) Mark();
            }
            void OnStateChanged() override {
                Mark();
                if (node) node->RequestRemeasure();
            }
            void Mark() {
                content->Touch();
                if (content->source == CompositionSource::Unknown) content->source = CompositionSource::Content;
            }
        };

        struct TransitionHandler {
            MotionMeasurer* measurer;
            rt::State<float>* motionProgress;

            bool OnAcceptFirstDownForOnSwipe(ui::Offset offset) const { return measurer->transition.isFirstDownAccepted(offset.x, offset.y); }
            void UpdateProgressOnDrag(ui::Offset dragAmount) const {
                const float progressDelta = measurer->transition.dragToProgress(motionProgress->Get(), measurer->LayoutCurrentWidth(),
                                                                                measurer->LayoutCurrentHeight(), dragAmount.x, dragAmount.y);
                motionProgress->Set(std::clamp(motionProgress->Get() + progressDelta, 0.0f, 1.0f));
            }
            bool PendingProgressWhileTouchUp() const { return measurer->transition.isTouchNotDone(motionProgress->Get()); }
        };

        struct MotionDragState {
            bool isDragging = false;
            ui::Offset dragAmount;
            ui::Offset velocity;
        };

        // Modifier.motionPointerInput: with OnSwipe, drags drive the progress and a release
        // animates it by the transition's touch-up rule.
        Modifier MotionPointerInput(Modifier modifier, const std::shared_ptr<Transition>& key, rt::State<float>* motionProgress, MotionMeasurer* measurer) {
            if (!measurer->transition.hasOnSwipe()) return modifier;
            auto* c = rt::Composition::Current();
            struct Swipe {
                TransitionHandler handler;
                std::shared_ptr<rt::Channel<MotionDragState>> channel = rt::Channel<MotionDragState>::Conflated();
            };
            auto& swipe = c->Remember<std::shared_ptr<Swipe>>([&] { return std::make_shared<Swipe>(Swipe{ TransitionHandler{ measurer, motionProgress } }); },
                                                              rt::Keys(key));
            std::shared_ptr<Swipe> s = swipe;
            c->LaunchedEffect(rt::Keys(key), [s]() -> rt::Task<> {
                bool isTouchUp = false;
                std::optional<MotionDragState> dragState;
                for (;;) {
                    if (isTouchUp && s->handler.PendingProgressWhileTouchUp()) {
                        // updateProgressWhileTouchUp
                        const std::int64_t timeNanos = co_await rt::NextFrame();
                        s->handler.motionProgress->Set(s->handler.measurer->transition.getTouchUpProgress(timeNanos));
                    } else {
                        if (!dragState) {
                            std::optional<MotionDragState> received = co_await s->channel->Next();
                            if (!received) co_return;
                            dragState = received;
                        }
                        isTouchUp = !dragState->isDragging;
                        if (isTouchUp) {
                            // onTouchUp: withFrameNanos { setTouchUp(...) }
                            const std::int64_t timeNanos = co_await rt::NextFrame();
                            s->handler.measurer->transition.setTouchUp(s->handler.motionProgress->Get(), timeNanos, dragState->velocity.x,
                                                                       dragState->velocity.y);
                        } else {
                            s->handler.UpdateProgressOnDrag(dragState->dragAmount);
                        }
                        dragState.reset();
                    }
                    if (std::optional<MotionDragState> receivedState = s->channel->TryReceive()) {
                        if (receivedState->isDragging) isTouchUp = false;
                        dragState = receivedState;
                    }
                }
            });
            return foundation::PointerInput(std::move(modifier), rt::Keys(key), [s](foundation::PointerInputScope& scope) -> rt::Task<> {
                ui::VelocityTracker velocityTracker;
                // detectDragGesturesWhenNeeded
                for (;;) {
                    std::optional<ui::PointerInputChange> down = co_await foundation::AwaitFirstDown(scope, true);
                    if (down && s->handler.OnAcceptFirstDownForOnSwipe(down->position)) {
                        std::optional<ui::PointerInputChange> drag;
                        ui::Offset overSlop;
                        do {
                            drag = co_await foundation::AwaitTouchSlopOrCancellation(scope, down->id, [&](ui::PointerInputChange& change, ui::Offset over) {
                                change.Consume();
                                overSlop = over;
                            });
                        } while (drag && !drag->IsConsumed());
                        if (drag) {
                            velocityTracker.Reset();   // onDragStart
                            velocityTracker.AddPointerInputChange(*drag);
                            s->channel->TrySend(MotionDragState{ true, overSlop, {} });
                            std::optional<ui::PointerInputChange> up = co_await foundation::DragPointer(scope, drag->id, [&](ui::PointerInputChange& change) {
                                velocityTracker.AddPointerInputChange(change);
                                s->channel->TrySend(MotionDragState{ true, change.PositionChange(), {} });
                                change.Consume();
                            });
                            (void)up;   // cancelled or ended: both report the release velocity
                            s->channel->TrySend(MotionDragState{ false, {}, velocityTracker.CalculateVelocity() });
                        }
                    }
                    // awaitEachGesture: wait until every pointer is up before the next gesture.
                    co_await foundation::AwaitAllPointersUp(scope);
                }
            });
        }

        // createAndUpdateMotionProgress: the progress argument, unless OnSwipe moved it since.
        rt::State<float>* CreateAndUpdateMotionProgress(rt::Composition* c, float progress) {
            struct Progress {
                rt::State<float> state;
                float last;
                explicit Progress(float p) : state(p), last(p) {}
            };
            auto& holder = c->Remember<std::shared_ptr<Progress>>([&] { return std::make_shared<Progress>(progress); });
            if (holder->last != progress) {
                holder->last = progress;
                holder->state.Set(progress);
            }
            return &holder->state;
        }

        bool SameSet(const std::shared_ptr<ConstraintSet>& a, const std::shared_ptr<ConstraintSet>& b) {
            if (a == b) return true;
            return a && b && a->Equals(*b);
        }
        bool SameTransition(const std::shared_ptr<Transition>& a, const std::shared_ptr<Transition>& b) {
            if (a == b) return true;
            return a && b && a->Equals(*b);
        }

        void MotionLayoutCore(std::shared_ptr<ConstraintSet> start, std::shared_ptr<ConstraintSet> end, const std::shared_ptr<Transition>& transition,
                              float progress, const DebugFlags& debugFlags, int optimizationLevel, const Modifier& modifier,
                              const std::shared_ptr<ContentState>& content, const std::shared_ptr<InvalidationStrategy>& invalidationStrategy,
                              kiln::Function<void(MotionLayoutScope&)> scopedContent, rt::Key key) {
            auto* c = rt::Composition::Current();
            rt::State<float>* motionProgress = CreateAndUpdateMotionProgress(c, progress);
            std::shared_ptr<TransitionImpl> transitionImpl = std::dynamic_pointer_cast<TransitionImpl>(transition);
            if (!transitionImpl) transitionImpl = TransitionImpl::Empty();
            const float density = c->Read(ui::LocalDensity).density;
            const ui::LayoutDirection layoutDirection = c->Read(ui::LocalLayoutDirection);

            struct Holder {
                std::unique_ptr<MotionMeasurer> measurer;
                std::unique_ptr<MotionLayoutScope> scope;
                std::shared_ptr<MotionLayoutPolicy> policy = std::make_shared<MotionLayoutPolicy>();
                ObservedStateWatch watch;
                std::shared_ptr<ConstraintSet> lastStart, lastEnd;
                std::shared_ptr<Transition> lastTransition;
                bool initialized = false;
                ui::LayoutNode* node = nullptr;
            };
            auto& holder = c->Remember<std::shared_ptr<Holder>>([&] {
                auto h = std::make_shared<Holder>();
                h->measurer = std::make_unique<MotionMeasurer>(density);
                h->scope = std::make_unique<MotionLayoutScope>(h->measurer.get(), motionProgress);
                return h;
            });
            Holder* h = holder.get();
            // remember(start, end, transition) { measurer.initWith(...) }
            if (!h->initialized || !SameSet(h->lastStart, start) || !SameSet(h->lastEnd, end) || !SameTransition(h->lastTransition, transition)) {
                h->measurer->InitWith(*start, *end, layoutDirection, *transitionImpl, motionProgress->Get());
                h->lastStart = start;
                h->lastEnd = end;
                h->lastTransition = transition;
                h->initialized = true;
            }
            if (invalidationStrategy->onObservedStateChange) {
                h->watch.content = content;
                h->watch.Run(invalidationStrategy->onObservedStateChange);
            }
            MotionLayoutPolicy& policy = *h->policy;
            const bool changed = policy.start != start || policy.end != end || policy.transition != transitionImpl || policy.optimizationLevel != optimizationLevel;
            policy.measurer = h->measurer.get();
            policy.content = content;
            policy.start = std::move(start);
            policy.end = std::move(end);
            policy.transition = transitionImpl;
            policy.motionProgress = motionProgress;
            policy.optimizationLevel = optimizationLevel;
            policy.invalidationStrategy = invalidationStrategy;
            if (changed && h->node) h->node->RequestRemeasure();

            Modifier m = modifier;
            // motionDebug (the forced scale factor is Studio's)
            if (debugFlags.ShowBounds() || debugFlags.ShowPaths() || debugFlags.ShowKeyPositions()) {
                MotionMeasurer* measurer = h->measurer.get();
                const bool showBounds = debugFlags.ShowBounds(), showPaths = debugFlags.ShowPaths(), showKeyPositions = debugFlags.ShowKeyPositions();
                m = foundation::DrawBehind(std::move(m), [measurer, showBounds, showPaths, showKeyPositions](ui::DrawScope& scope) {
                    measurer->DrawDebug(scope, showBounds, showPaths, showKeyPositions);
                });
            }
            m = MotionPointerInput(std::move(m), transition ? transition : std::shared_ptr<Transition>(TransitionImpl::Empty()), motionProgress,
                                   h->measurer.get());
            MotionLayoutScope* scope = h->scope.get();
            h->node = &foundation::Layout(m, h->policy, [scope, scopedContent = std::move(scopedContent)] { scopedContent(*scope); }, key);
            h->watch.node = h->node;
        }

        // The content delegate of the public overloads: composing it marks the content changed
        // (unless the invalidation strategy observes state itself).
        kiln::Function<void(MotionLayoutScope&)> ContentDelegate(const std::shared_ptr<ContentState>& content,
                                                                  const std::shared_ptr<InvalidationStrategy>& invalidationStrategy,
                                                                  kiln::Function<void(MotionLayoutScope&)> userContent) {
            const bool observes = bool(invalidationStrategy->onObservedStateChange);
            return [content, observes, userContent = std::move(userContent)](MotionLayoutScope& scope) {
                content->Touch();
                if (!observes && content->source == CompositionSource::Unknown) content->source = CompositionSource::Content;
                userContent(scope);
            };
        }

        std::shared_ptr<ContentState> RememberContentState(rt::Composition* c) {
            return c->Remember<std::shared_ptr<ContentState>>([] { return std::make_shared<ContentState>(); });
        }

    }

    void MotionLayout(std::shared_ptr<ConstraintSet> start, std::shared_ptr<ConstraintSet> end, float progress, const Modifier& modifier,
                      kiln::Function<void(MotionLayoutScope&)> content, std::shared_ptr<Transition> transition, const MotionLayoutOptions& options, rt::Key key) {
        auto* c = rt::Composition::Current();
        std::shared_ptr<ContentState> contentState = RememberContentState(c);
        MotionLayoutCore(std::move(start), std::move(end), transition, progress, options.debugFlags, options.optimizationLevel, modifier, contentState,
                         options.invalidationStrategy, ContentDelegate(contentState, options.invalidationStrategy, std::move(content)), key);
    }

    void MotionLayout(std::shared_ptr<MotionScene> motionScene, float progress, const Modifier& modifier, kiln::Function<void(MotionLayoutScope&)> content,
                      const std::string& transitionName, const MotionLayoutOptions& options, rt::Key key) {
        auto* c = rt::Composition::Current();
        std::shared_ptr<ContentState> contentState = RememberContentState(c);
        const std::shared_ptr<Transition>& transition =
            c->Remember<std::shared_ptr<Transition>>([&] { return motionScene->GetTransitionInstance(transitionName); }, rt::Keys(motionScene, transitionName));
        const std::shared_ptr<ConstraintSet>& start = c->Remember<std::shared_ptr<ConstraintSet>>(
            [&] { return motionScene->GetConstraintSetInstance(transition ? transition->GetStartConstraintSetId() : "start"); },
            rt::Keys(motionScene, transition));
        const std::shared_ptr<ConstraintSet>& end = c->Remember<std::shared_ptr<ConstraintSet>>(
            [&] { return motionScene->GetConstraintSetInstance(transition ? transition->GetEndConstraintSetId() : "end"); }, rt::Keys(motionScene, transition));
        if (start == nullptr || end == nullptr) return;
        MotionLayoutCore(start, end, transition, progress, options.debugFlags, options.optimizationLevel, modifier, contentState, options.invalidationStrategy,
                         ContentDelegate(contentState, options.invalidationStrategy, std::move(content)), key);
    }

    void MotionLayout(std::shared_ptr<MotionScene> motionScene, std::optional<std::string> constraintSetName, anim::SpecPtr animationSpec,
                      const Modifier& modifier, kiln::Function<void(MotionLayoutScope&)> content, kiln::Function<void()> finishedAnimationListener,
                      const MotionLayoutOptions& options, rt::Key key) {
        auto* c = rt::Composition::Current();
        std::shared_ptr<ContentState> contentState = RememberContentState(c);
        const std::shared_ptr<Transition>& transition =
            c->Remember<std::shared_ptr<Transition>>([&] { return motionScene->GetTransitionInstance("default"); }, rt::Keys(motionScene));
        const std::shared_ptr<ConstraintSet>& initialStart = c->Remember<std::shared_ptr<ConstraintSet>>(
            [&] { return motionScene->GetConstraintSetInstance(transition ? transition->GetStartConstraintSetId() : "start"); }, rt::Keys(motionScene));
        const std::shared_ptr<ConstraintSet>& initialEnd = c->Remember<std::shared_ptr<ConstraintSet>>(
            [&] { return motionScene->GetConstraintSetInstance(transition ? transition->GetEndConstraintSetId() : "end"); }, rt::Keys(motionScene));
        if (initialStart == nullptr || initialEnd == nullptr) return;

        struct Animated {
            rt::State<std::shared_ptr<ConstraintSet>> start, end;
            rt::State<bool> animateToEnd{ true };
            Animated(std::shared_ptr<ConstraintSet> s, std::shared_ptr<ConstraintSet> e) : start(std::move(s)), end(std::move(e)) {}
        };
        auto& animatedHolder = c->Remember<std::shared_ptr<Animated>>([&] { return std::make_shared<Animated>(initialStart, initialEnd); }, rt::Keys(motionScene));
        std::shared_ptr<Animated> animated = animatedHolder;
        const std::shared_ptr<ConstraintSet>& targetConstraintSet = c->Remember<std::shared_ptr<ConstraintSet>>(
            [&] { return constraintSetName ? motionScene->GetConstraintSetInstance(*constraintSetName) : nullptr; },
            rt::Keys(motionScene, constraintSetName.value_or(std::string("\x01"))));
        auto& progressHolder = c->Remember<std::shared_ptr<anim::Animatable>>([] { return std::make_shared<anim::Animatable>(0.0f); });
        std::shared_ptr<anim::Animatable> progress = progressHolder;
        auto& channelHolder = c->Remember<std::shared_ptr<rt::Channel<std::shared_ptr<ConstraintSet>>>>(
            [] { return std::shared_ptr<rt::Channel<std::shared_ptr<ConstraintSet>>>(rt::Channel<std::shared_ptr<ConstraintSet>>::Conflated()); });
        std::shared_ptr<rt::Channel<std::shared_ptr<ConstraintSet>>> channel = channelHolder;
        if (targetConstraintSet != nullptr) {
            std::shared_ptr<ConstraintSet> target = targetConstraintSet;
            c->SideEffect([channel, target] { channel->TrySend(target); });
            auto listener = std::make_shared<kiln::Function<void()>>(std::move(finishedAnimationListener));
            c->LaunchedEffect(rt::Keys(motionScene, channel), [channel, animated, progress, animationSpec, listener]() -> rt::Task<> {
                while (std::optional<std::shared_ptr<ConstraintSet>> constraints = co_await channel->Next()) {
                    std::shared_ptr<ConstraintSet> newConstraintSet = channel->TryReceive().value_or(*constraints);
                    const bool toEnd = animated->animateToEnd.Get();
                    const float animTargetValue = toEnd ? 1.0f : 0.0f;
                    const std::shared_ptr<ConstraintSet>& currentSet = toEnd ? animated->start.Get() : animated->end.Get();
                    if (!SameSet(newConstraintSet, currentSet)) {
                        if (toEnd) animated->end.Set(newConstraintSet);
                        else animated->start.Set(newConstraintSet);
                        co_await progress->AnimateTo(animTargetValue, animationSpec);
                        animated->animateToEnd.Set(!toEnd);
                        if (*listener) (*listener)();
                    }
                }
            });
        }
        MotionLayoutCore(animated->start.Get(), animated->end.Get(), transition, progress->Float(), options.debugFlags, options.optimizationLevel, modifier,
                         contentState, options.invalidationStrategy, ContentDelegate(contentState, options.invalidationStrategy, std::move(content)), key);
    }

}
