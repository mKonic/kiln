#include "kiln/material3/BottomSheetScaffold.h"

#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
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

    std::shared_ptr<BottomSheetScaffoldState> RememberBottomSheetScaffoldState(std::shared_ptr<SheetState> bottomSheetState,
                                                                               std::shared_ptr<SnackbarHostState> snackbarHostState, rt::Key key) {
        auto& c = *rt::Composition::Current();
        if (!bottomSheetState)
            bottomSheetState = RememberBottomSheetState(SheetValue::PartiallyExpanded, SheetValues{ false, true }, {}, key);
        const auto ownHost = c.Remember<std::shared_ptr<SnackbarHostState>>([] { return std::make_shared<SnackbarHostState>(); }, {}, key);
        if (!snackbarHostState) snackbarHostState = ownHost;
        return c.Remember<std::shared_ptr<BottomSheetScaffoldState>>(
            [&] { return std::make_shared<BottomSheetScaffoldState>(BottomSheetScaffoldState{ bottomSheetState, snackbarHostState }); },
            rt::Keys(bottomSheetState.get(), snackbarHostState.get()), key);
    }

    std::shared_ptr<SheetState> RememberStandardBottomSheetState(SheetValue initialValue, std::function<bool(SheetValue)> confirmValueChange,
                                                                 bool skipHiddenState, rt::Key key) {
        return internal::RememberSheetState(SheetValues{ !skipHiddenState, true }, std::move(confirmValueChange), initialValue,
                                            BottomSheetDefaults::PositionalThreshold, BottomSheetDefaults::VelocityThreshold, false, key);
    }

    namespace {
        rt::Task<> ExpandSheet(std::shared_ptr<SheetState> state) { co_await state->Expand(); }
        rt::Task<> ShowSheet(std::shared_ptr<SheetState> state) { co_await state->Show(); }
        rt::Task<> PartialExpandSheet(std::shared_ptr<SheetState> state) { co_await state->PartialExpand(); }
        rt::Task<> HideSheet(std::shared_ptr<SheetState> state) { co_await state->Hide(); }

        // AnchoredDraggableDefaults.flingBehavior: settles by the state's positional threshold
        // (no damping near the bottom, unlike the modal sheet's fling).
        rt::Task<float> ScaffoldSheetFling(std::shared_ptr<SheetState> state, float velocity, ui::Density density, anim::SpecPtr spec) {
            const float threshold = density.ToPx(Dp(anchored_draggable::MinFlingVelocityDp));
            const auto positional = [s = state.get()](float) { return s->PositionalThresholdPx(); };
            const float left = co_await state->Anchored().Fling(velocity, threshold, positional, std::move(spec));
            co_return velocity - left;
        }

        struct StandardSheetParams {
            std::shared_ptr<SheetState> state;
            Dp peekHeight, maxWidth;
            bool swipeEnabled;
            ShapePtr shape;
            Color containerColor, contentColor;
            Dp tonalElevation, shadowElevation;
            std::function<void()> dragHandle, content;
        };

        void StandardBottomSheet(const StandardSheetParams& p) {
            auto& c = *rt::Composition::Current();
            const auto state = p.state;
            const anim::SpecPtr showMotion = Value(MotionSchemeKeyTokens::DefaultSpatial);
            const anim::SpecPtr hideMotion = Value(MotionSchemeKeyTokens::FastEffects);
            const anim::SpecPtr spatialFlingSpec = showMotion;
            c.SideEffect([state, showMotion, hideMotion, spatialFlingSpec] {
                state->showMotionSpec = showMotion;
                state->hideMotionSpec = hideMotion;
                state->anchoredDraggableMotionSpec = spatialFlingSpec;
            });

            const auto scope = internal::RememberCoroutineScope();
            const ui::Density density = c.Read(ui::LocalDensity);
            const float peekHeightPx = density.ToPx(p.peekHeight);

            ui::Modifier m = SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), p.maxWidth, Dp::Unspecified());
            m = FillMax(std::move(m), Axis::Horizontal);
            m = SizeIn(std::move(m), Dp::Unspecified(), p.peekHeight, Dp::Unspecified(), Dp::Unspecified(), false);
            if (p.swipeEnabled) {
                // remember(state): the connection reads the latest fling settings through this holder.
                struct Holder {
                    ui::Density density;
                    anim::SpecPtr spec;
                };
                const auto holder = c.Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); });
                holder->density = density;
                holder->spec = spatialFlingSpec;
                const auto connection = c.Remember<std::shared_ptr<ui::NestedScrollConnection>>(
                    [&] {
                        return internal::ConsumeSwipeWithinBottomSheetBounds(state, [state, holder](float v) {
                            return ScaffoldSheetFling(state, v, holder->density, holder->spec);
                        });
                    },
                    rt::Keys(state.get()));
                m = NestedScroll(std::move(m), connection);
            }
            // The peek height feeds the anchors; a holder keeps one calculation per state.
            const auto peek = c.Remember<std::shared_ptr<float>>([] { return std::make_shared<float>(0.0f); });
            *peek = peekHeightPx;
            const auto anchors = c.Remember<std::shared_ptr<const internal::AnchorsCalculation<SheetValue>>>(
                [&] {
                    return std::make_shared<const internal::AnchorsCalculation<SheetValue>>([state, peek](ui::IntSize sheetSize, const ui::Constraints& constraints) {
                        const float layoutHeight = float(constraints.maxHeight);
                        const float sheetHeight = float(sheetSize.height);
                        const float peekPx = *peek;
                        const auto& current = state->Anchored().Anchors();
                        const bool hiddenAvailable = sheetHeight == 0 || peekPx == 0 || !state->SkipHiddenState();
                        // Ambiguous anchors are kept for the first pass (a peek height that starts
                        // at 0 dp before it is known) and while resting there, so the sheet settles
                        // partially expanded, the anchor callers reach through the peek height.
                        const bool initialLayout = current.Size() == 0;
                        const bool stableAtPartial = state->CurrentValue() == SheetValue::PartiallyExpanded && !state->IsAnimationRunning();
                        const bool ambiguousPartialAllowed = peekPx == 0 && (initialLayout || stableAtPartial);
                        const bool partialAvailable = !state->SkipPartiallyExpanded() && (peekPx > 0 || ambiguousPartialAllowed) && peekPx != sheetHeight;
                        const bool expandedAvailable = sheetHeight > 0;
                        foundation::DraggableAnchors<SheetValue> a;
                        if (partialAvailable) a.At(SheetValue::PartiallyExpanded, layoutHeight - peekPx);
                        if (hiddenAvailable) a.At(SheetValue::Hidden, layoutHeight);
                        if (expandedAvailable) a.At(SheetValue::Expanded, layoutHeight - sheetHeight);
                        const SheetValue old = state->TargetValue();
                        SheetValue target = old;
                        switch (old) {
                            case SheetValue::Hidden: target = a.HasPositionFor(SheetValue::Hidden) ? SheetValue::Hidden : old; break;
                            case SheetValue::PartiallyExpanded:
                                target = a.HasPositionFor(SheetValue::PartiallyExpanded) ? SheetValue::PartiallyExpanded
                                       : a.HasPositionFor(SheetValue::Expanded)          ? SheetValue::Expanded
                                       : a.HasPositionFor(SheetValue::Hidden)            ? SheetValue::Hidden
                                                                                         : old;
                                break;
                            case SheetValue::Expanded: target = a.HasPositionFor(SheetValue::Expanded) ? SheetValue::Expanded : SheetValue::Hidden; break;
                        }
                        return std::pair{ std::move(a), target };
                    });
                },
                rt::Keys(state.get()));
            m = internal::DraggableAnchors(std::move(m), state->Anchored(), Orientation::Vertical, anchors);
            AnchoredDraggableOptions drag;
            drag.enabled = p.swipeEnabled;
            drag.positionalThreshold = [s = state.get()](float) { return s->PositionalThresholdPx(); };
            drag.snapAnimationSpec = spatialFlingSpec;
            m = AnchoredDraggable(std::move(m), state->Anchored(), Orientation::Vertical, drag);
            // The sheet stretches when a bouncy motion carries it past its highest anchor, so no
            // gap shows below it; its content is scaled back to keep its aspect.
            m = internal::VerticalScaleUp(std::move(m), state);

            SurfaceOptions surface;
            surface.shape = p.shape;
            surface.color = p.containerColor;
            surface.contentColor = p.contentColor;
            surface.tonalElevation = p.tonalElevation;
            surface.shadowElevation = p.shadowElevation;
            const auto dragHandle = p.dragHandle;
            const auto content = p.content;
            const bool swipe = p.swipeEnabled;
            Surface(m, surface, [=] {
                Column(internal::VerticalScaleDown(FillMax({}, Axis::Horizontal), state), Arrangement::Top(), ui::AlignmentH::Start(), [=] {
                    if (dragHandle) {
                        const std::string collapseLabel = GetString(Strings::BottomSheetPartialExpandDescription);
                        const std::string dismissLabel = GetString(Strings::BottomSheetDismissDescription);
                        const std::string expandLabel = GetString(Strings::BottomSheetExpandDescription);
                        const SheetValue current = state->CurrentValue();
                        const bool several = state->Anchored().Anchors().Size() > 1;
                        ui::Modifier handle = Clickable(ui::Modifier{}, [state, scope] {
                            switch (state->CurrentValue()) {
                                case SheetValue::Expanded:
                                    if (!state->SkipHiddenState()) scope->Launch(HideSheet(state));
                                    else scope->Launch(PartialExpandSheet(state));
                                    break;
                                case SheetValue::PartiallyExpanded: scope->Launch(ExpandSheet(state)); break;
                                default: scope->Launch(ShowSheet(state)); break;
                            }
                        });
                        handle = Semantics(std::move(handle), [=](ui::SemanticsConfiguration& s) {
                            // Only when there is somewhere to swipe to and swiping is on.
                            if (!several || !swipe) return;
                            if (current == SheetValue::PartiallyExpanded) {
                                s.expand = [state, scope] {
                                    const bool can = state->Confirm(SheetValue::Expanded);
                                    if (can) scope->Launch(ExpandSheet(state));
                                    return can;
                                };
                                s.expandLabel = expandLabel;
                            } else {
                                s.collapse = [state, scope] {
                                    const bool can = state->Confirm(SheetValue::PartiallyExpanded);
                                    scope->Launch(PartialExpandSheet(state));
                                    return can;
                                };
                                s.collapseLabel = collapseLabel;
                            }
                            if (!state->SkipHiddenState()) {
                                s.dismiss = [state, scope] {
                                    const bool can = state->Confirm(SheetValue::Hidden);
                                    scope->Launch(HideSheet(state));
                                    return can;
                                };
                                s.dismissLabel = dismissLabel;
                            }
                        }, true);
                        internal::DragHandleWithTooltip(handle, dragHandle);
                    }
                    content();
                });
            });
        }

        // BottomSheetScaffoldLayout: the top bar over the body, the sheet centred across the
        // width, and the snackbar host just above the sheet while it peeks (at the bottom else).
        struct ScaffoldPolicy final : MultiContentMeasurePolicy {
            std::shared_ptr<SheetState> state;
            explicit ScaffoldPolicy(std::shared_ptr<SheetState> s) : state(std::move(s)) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, const std::vector<std::vector<ui::Measurable*>>& contents, const ui::Constraints& c) override {
                const int layoutWidth = c.maxWidth, layoutHeight = c.maxHeight;
                ui::Constraints loose = c;
                loose.minWidth = loose.minHeight = 0;
                const auto measureAll = [](const std::vector<ui::Measurable*>& group, const ui::Constraints& k) {
                    std::vector<ui::Placeable*> out;
                    for (ui::Measurable* m : group) out.push_back(&m->Measure(k));
                    return out;
                };
                const auto maxOf = [](const std::vector<ui::Placeable*>& ps, bool width) {
                    int v = 0;
                    for (const ui::Placeable* p : ps) v = std::max(v, width ? p->Width() : p->Height());
                    return v;
                };
                const auto sheets = measureAll(contents[2], loose);
                const auto topBars = measureAll(contents[0], loose);
                const int topBarHeight = maxOf(topBars, false);
                ui::Constraints body = loose;
                body.maxHeight = layoutHeight - topBarHeight;
                const auto bodies = measureAll(contents[1], body);
                const auto snackbars = measureAll(contents[3], loose);
                const auto st = state;
                return s.Layout(layoutWidth, layoutHeight, [=](ui::PlacementScope& place) {
                    const int sheetWidth = maxOf(sheets, true);
                    const int sheetOffsetX = std::max(0, (layoutWidth - sheetWidth) / 2);
                    const int snackbarWidth = maxOf(snackbars, true);
                    const int snackbarHeight = maxOf(snackbars, false);
                    const int snackbarOffsetX = (layoutWidth - snackbarWidth) / 2;
                    const int snackbarOffsetY = st->CurrentValue() == SheetValue::PartiallyExpanded
                                                    ? int(std::lround(st->RequireOffset())) - snackbarHeight
                                                    : layoutHeight - snackbarHeight;
                    // Placement order is the elevation order.
                    for (ui::Placeable* p : bodies) place.PlaceRelative(*p, 0, topBarHeight);
                    for (ui::Placeable* p : topBars) place.PlaceRelative(*p, 0, 0);
                    for (ui::Placeable* p : sheets) place.PlaceRelative(*p, sheetOffsetX, 0);
                    for (ui::Placeable* p : snackbars) place.PlaceRelative(*p, snackbarOffsetX, snackbarOffsetY);
                });
            }
        };
    }

    void BottomSheetScaffold(std::function<void()> sheetContent, const ui::Modifier& modifier, const BottomSheetScaffoldOptions& o,
                             std::function<void(const PaddingValues&)> content) {
        auto& c = *rt::Composition::Current();
        const auto scaffold = o.scaffoldState ? o.scaffoldState : RememberBottomSheetScaffoldState();
        const Color container = o.containerColor.value_or(MaterialTheme::colorScheme().surface);
        const Color contentColor = o.contentColor.value_or(ContentColorFor(container));
        const Color sheetContainer = o.sheetContainerColor.value_or(BottomSheetDefaults::ContainerColor());
        StandardSheetParams sheet;
        sheet.state = scaffold->bottomSheetState;
        sheet.peekHeight = o.sheetPeekHeight;
        sheet.maxWidth = o.sheetMaxWidth;
        sheet.swipeEnabled = o.sheetSwipeEnabled;
        sheet.shape = o.sheetShape ? o.sheetShape : BottomSheetDefaults::ExpandedShape();
        sheet.containerColor = sheetContainer;
        sheet.contentColor = o.sheetContentColor.value_or(ContentColorFor(sheetContainer));
        sheet.tonalElevation = o.sheetTonalElevation;
        sheet.shadowElevation = o.sheetShadowElevation.value_or(BottomSheetDefaults::Elevation);
        sheet.dragHandle = o.sheetDragHandle;
        sheet.content = std::move(sheetContent);
        const auto policy = c.Remember<std::shared_ptr<ScaffoldPolicy>>([&] { return std::make_shared<ScaffoldPolicy>(sheet.state); },
                                                                        rt::Keys(sheet.state.get()));
        const Dp peek = o.sheetPeekHeight;
        const auto topBar = o.topBar;
        const auto snackbarHost = o.snackbarHost;
        const auto hostState = scaffold->snackbarHostState;
        Box(Background(FillMax(modifier, Axis::Both), container), ui::Alignment::TopStart(), false, [=] {
            // LocalContentColor rather than a Surface: a Surface clips, which takes touches in tests.
            rt::Composition::Current()->Provide(LocalContentColor, contentColor, [=] {
                Layout({},
                       { topBar ? topBar : [] {},
                         [content, peek] { content(PaddingValues{ Dp(0), Dp(0), Dp(0), peek }); },
                         [sheet] { StandardBottomSheet(sheet); },
                         [snackbarHost, hostState] {
                             if (snackbarHost) snackbarHost(*hostState);
                             else SnackbarHost(*hostState);
                         } },
                       policy);
            });
        });
    }

}
