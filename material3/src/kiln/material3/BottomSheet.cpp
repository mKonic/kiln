#include "kiln/material3/BottomSheet.h"
#include <kiln/Function.h>

#include "kiln/animation/Easing.h"
#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/DraggableAnchors.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        // invokeOnCompletion { if (!state.isVisible) onDismissRequest() }: also when cancelled.
        struct DismissIfHidden {
            std::shared_ptr<SheetState> state;
            kiln::Function<void()> onDismiss;
            ~DismissIfHidden() { if (onDismiss && !state->IsVisible()) onDismiss(); }
        };

        rt::Task<> HideThenDismiss(std::shared_ptr<SheetState> state, kiln::Function<void()> onDismiss) {
            DismissIfHidden guard{ state, std::move(onDismiss) };
            co_await state->Hide();
        }
        rt::Task<> ExpandSheet(std::shared_ptr<SheetState> state) { co_await state->Expand(); }
        rt::Task<> ShowSheet(std::shared_ptr<SheetState> state) { co_await state->Show(); }
        rt::Task<> PartialExpandSheet(std::shared_ptr<SheetState> state) { co_await state->PartialExpand(); }
        rt::Task<> AnimateToZero(std::shared_ptr<anim::Animatable> a) { co_await a->AnimateTo(0.0f); }

        // The sheet's fling (modalBottomSheetFlingBehavior), from the nested scroll.
        rt::Task<float> FlingThenDismiss(std::shared_ptr<SheetState> state, float velocity, ui::Density density, anim::SpecPtr spec,
                                         kiln::Function<void()> onDismiss) {
            DismissIfHidden guard{ state, std::move(onDismiss) };
            co_return co_await internal::SheetFling(state, velocity, density, std::move(spec));
        }

        // settleToDismiss: an expanded sheet that can rest partially expanded goes there; any
        // other hides, then reports the dismissal.
        void SettleToDismiss(const std::shared_ptr<SheetState>& state, const std::shared_ptr<internal::CoroutineScope>& scope,
                             const std::shared_ptr<anim::Animatable>& progress, const kiln::Function<void()>& onDismiss) {
            if (state->CurrentValue() == SheetValue::Expanded && state->HasPartiallyExpandedState()) {
                scope->Launch(PartialExpandSheet(state));
                scope->Launch(AnimateToZero(progress));
            } else {
                scope->Launch(HideThenDismiss(state, onDismiss));
            }
        }

        rt::Task<> SheetPredictiveBack(BackProgress& events, std::shared_ptr<anim::Animatable> progress, std::shared_ptr<internal::CoroutineScope> scope,
                                       std::shared_ptr<SheetState> state, kiln::Function<void()> onDismiss) {
            struct OnCancel {
                std::shared_ptr<internal::CoroutineScope> scope;
                std::shared_ptr<anim::Animatable> progress;
                bool completed = false;
                ~OnCancel() { if (!completed) scope->Launch(AnimateToZero(progress)); }
            } onCancel{ scope, progress };
            static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);   // PredictiveBack.transform
            while (auto e = co_await events.Next()) progress->SnapTo(easing.Transform(e->progress));
            onCancel.completed = true;
            SettleToDismiss(state, scope, progress, onDismiss);
        }
    }

    void BottomSheet(const ui::Modifier& modifier, const BottomSheetOptions& o, kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto state = o.state ? o.state : RememberBottomSheetState(SheetValue::Hidden);
        const anim::SpecPtr showMotion = Value(MotionSchemeKeyTokens::DefaultSpatial);
        const anim::SpecPtr hideMotion = Value(MotionSchemeKeyTokens::FastEffects);
        c.SideEffect([state, showMotion, hideMotion] {
            state->showMotionSpec = showMotion;
            state->hideMotionSpec = hideMotion;
            state->anchoredDraggableMotionSpec = showMotion;
        });

        const auto progress = c.Remember<std::shared_ptr<anim::Animatable>>([] { return std::make_shared<anim::Animatable>(0.0f); });
        const auto scope = internal::RememberCoroutineScope();
        const auto onDismiss = o.onDismissRequest;
        PredictiveBackHandler(o.backHandlerEnabled && state->IsVisible(), [progress, scope, state, onDismiss](BackProgress& events) {
            return SheetPredictiveBack(events, progress, scope, state, onDismiss);
        });
        internal::BottomSheetImpl([progress] { return progress->Float(); }, modifier, state, o, std::move(content));
    }

    namespace internal {

        void BottomSheetImpl(kiln::Function<float()> predictiveBackProgress, const ui::Modifier& modifier, std::shared_ptr<SheetState> state,
                             const BottomSheetOptions& o, kiln::Function<void()> content) {
            auto& c = *rt::Composition::Current();
            const std::string paneTitle = GetString(Strings::BottomSheetPaneTitle);
            const ui::Density density = c.Read(ui::LocalDensity);
            const anim::SpecPtr spatialFlingSpec = Value(MotionSchemeKeyTokens::DefaultSpatial);
            const auto onDismiss = o.onDismissRequest;
            const auto scope = RememberCoroutineScope();
            const auto animateToDismiss = [state, scope, onDismiss] {
                if (state->Confirm(SheetValue::Hidden)) scope->Launch(HideThenDismiss(state, onDismiss));
            };

            ui::Modifier m = SizeIn(modifier, Dp::Unspecified(), Dp::Unspecified(), o.maxWidth, Dp::Unspecified());
            m = FillMax(std::move(m), Axis::Horizontal);
            if (o.gesturesEnabled) {
                // remember(state): the connection reads the latest fling settings through this holder.
                struct Holder {
                    ui::Density density;
                    anim::SpecPtr spec;
                    kiln::Function<void()> onDismiss;
                };
                const auto holder = c.Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); });
                holder->density = density;
                holder->spec = spatialFlingSpec;
                holder->onDismiss = onDismiss;
                const auto connection = c.Remember<std::shared_ptr<ui::NestedScrollConnection>>(
                    [&] {
                        return ConsumeSwipeWithinBottomSheetBounds(state, [state, holder](float v) {
                            return FlingThenDismiss(state, v, holder->density, holder->spec, holder->onDismiss);
                        });
                    },
                    rt::Keys(state.get()));
                m = NestedScroll(std::move(m), connection);
            }
            const auto anchors = c.Remember<std::shared_ptr<const AnchorsCalculation<SheetValue>>>(
                [&] {
                    return std::make_shared<const AnchorsCalculation<SheetValue>>([state](ui::IntSize sheetSize, const ui::Constraints& constraints) {
                        const float fullHeight = float(constraints.maxHeight);
                        const float sheetHeight = float(sheetSize.height);
                        foundation::DraggableAnchors<SheetValue> a;
                        a.At(SheetValue::Hidden, fullHeight);
                        if (IsPartiallyExpandedAnchorAvailable(*state, fullHeight, sheetHeight))
                            a.At(SheetValue::PartiallyExpanded, CalculatePartiallyExpandedOffset(*state, fullHeight, sheetHeight));
                        if (sheetHeight != 0) a.At(SheetValue::Expanded, std::max(0.0f, fullHeight - sheetHeight));
                        SheetValue target = SheetValue::Hidden;
                        switch (state->Anchored().TargetValue()) {
                            case SheetValue::Hidden: target = SheetValue::Hidden; break;
                            case SheetValue::PartiallyExpanded:
                                target = a.HasPositionFor(SheetValue::PartiallyExpanded) ? SheetValue::PartiallyExpanded
                                       : a.HasPositionFor(SheetValue::Expanded)          ? SheetValue::Expanded
                                                                                         : SheetValue::Hidden;
                                break;
                            case SheetValue::Expanded: target = a.HasPositionFor(SheetValue::Expanded) ? SheetValue::Expanded : SheetValue::Hidden; break;
                        }
                        return std::pair{ std::move(a), target };
                    });
                },
                rt::Keys(state.get()));
            m = DraggableAnchors(std::move(m), state->Anchored(), Orientation::Vertical, anchors);
            AnchoredDraggableOptions drag;
            drag.enabled = o.gesturesEnabled && state->CurrentValue() != SheetValue::Hidden;
            drag.positionalThreshold = [s = state.get()](float) { return s->PositionalThresholdPx(); };
            drag.snapAnimationSpec = spatialFlingSpec;
            drag.transformVelocity = [s = state.get(), density](float v) { return SheetFlingVelocity(*s, v, density); };
            drag.afterFling = [state, onDismiss]() -> rt::Task<> {
                if (onDismiss && !state->IsVisible()) onDismiss();
                co_return;
            };
            m = AnchoredDraggable(std::move(m), state->Anchored(), Orientation::Vertical, drag);
            m = Semantics(std::move(m), [paneTitle](ui::SemanticsConfiguration& s) {
                s.paneTitle = paneTitle;
                s.traversalIndex = 0.0f;
            });
            m = SheetPredictiveBackScaling(std::move(m), state, predictiveBackProgress);
            // The sheet stretches when a bouncy motion carries it past its highest anchor, so no
            // gap shows below it; its content is scaled back to keep its aspect.
            m = VerticalScaleUp(std::move(m), state);

            SurfaceOptions surface;
            surface.shape = o.shape ? o.shape : BottomSheetDefaults::ExpandedShape();
            const Color container = o.containerColor.value_or(BottomSheetDefaults::ContainerColor());
            surface.color = container;
            surface.contentColor = o.contentColor.value_or(ContentColorFor(container));
            surface.tonalElevation = o.tonalElevation.value_or(BottomSheetDefaults::Elevation);
            surface.shadowElevation = o.shadowElevation;
            const auto insets = o.contentWindowInsets;
            const auto dragHandle = o.dragHandle;
            const bool gestures = o.gesturesEnabled;
            Surface(m, surface, [=] {
                ui::Modifier col = FillMax(ui::Modifier{}, Axis::Horizontal);
                col = WindowInsetsPadding(std::move(col), insets ? insets() : BottomSheetDefaults::standardWindowInsets());
                col = ContentPredictiveBackScaling(std::move(col), predictiveBackProgress);
                col = VerticalScaleDown(std::move(col), state);
                Column(col, Arrangement::Top(), ui::AlignmentH::Start(), [=] {
                    if (dragHandle) {
                        const std::string collapseLabel = GetString(Strings::BottomSheetPartialExpandDescription);
                        const std::string dismissLabel = GetString(Strings::BottomSheetDismissDescription);
                        const std::string expandLabel = GetString(Strings::BottomSheetExpandDescription);
                        const SheetValue current = state->CurrentValue();
                        const bool hasPartial = state->HasPartiallyExpandedState();
                        ui::Modifier handle = Clickable(ui::Modifier{}, [state, scope, animateToDismiss] {
                            switch (state->CurrentValue()) {
                                case SheetValue::Expanded: animateToDismiss(); break;
                                case SheetValue::PartiallyExpanded: scope->Launch(ExpandSheet(state)); break;
                                default: scope->Launch(ShowSheet(state)); break;
                            }
                        });
                        handle = Semantics(std::move(handle), [=](ui::SemanticsConfiguration& s) {
                            // Actions for the sheet's current value.
                            if (!gestures) return;
                            s.dismiss = [animateToDismiss] { animateToDismiss(); return true; };
                            s.dismissLabel = dismissLabel;
                            if (current == SheetValue::PartiallyExpanded) {
                                s.expand = [state, scope] {
                                    if (state->Confirm(SheetValue::Expanded)) scope->Launch(ExpandSheet(state));
                                    return true;
                                };
                                s.expandLabel = expandLabel;
                            } else if (hasPartial) {
                                s.collapse = [state, scope] {
                                    if (state->Confirm(SheetValue::PartiallyExpanded)) scope->Launch(PartialExpandSheet(state));
                                    return true;
                                };
                                s.collapseLabel = collapseLabel;
                            }
                        }, true);
                        DragHandleWithTooltip(handle, dragHandle);
                    }
                    content();
                });
            });
        }
    }

}
