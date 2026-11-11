#include "kiln/material3/SheetState.h"

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/ScrimTokens.h"
#include "kiln/material3/tokens/SheetBottomTokens.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    SheetState::SheetState(SheetValues enabledValues, std::function<float()> positionalThreshold, std::function<float()> velocityThreshold,
                           SheetValue initialValue, std::function<bool(SheetValue)> confirmValueChange, std::optional<bool> partiallyExpandedDeterministic)
        : showMotionSpec(anim::Snap()), hideMotionSpec(anim::Snap()), anchoredDraggableMotionSpec(internal::BottomSheetAnimationSpec()),
          m_Enabled(enabledValues), m_PositionalThreshold(std::move(positionalThreshold)), m_VelocityThreshold(std::move(velocityThreshold)),
          m_Confirm(std::move(confirmValueChange)), m_Deterministic(partiallyExpandedDeterministic.value_or(ComposeMaterial3Flags::isBottomSheetPartiallyExpandedDeterministicEnabled)),
          m_Anchored(initialValue, [this](const SheetValue& v) { return Confirm(v); }) {
        if (!enabledValues.Contains(initialValue))
            throw std::invalid_argument("The initial value must be one of the enabled values.");
    }

    rt::Task<> SheetState::Expand() {
        if (Confirm(SheetValue::Expanded)) co_await AnimateTo(SheetValue::Expanded, showMotionSpec);
    }

    rt::Task<> SheetState::PartialExpand() {
        if (SkipPartiallyExpanded())
            throw std::logic_error("Attempted to animate to partial expanded when skipPartiallyExpanded was enabled. Set "
                                   "skipPartiallyExpanded to false to use this function.");
        if (Confirm(SheetValue::PartiallyExpanded)) co_await AnimateTo(SheetValue::PartiallyExpanded, hideMotionSpec);
    }

    rt::Task<> SheetState::Show() {
        const SheetValue target = HasPartiallyExpandedState() ? SheetValue::PartiallyExpanded : SheetValue::Expanded;
        if (Confirm(target)) co_await AnimateTo(target, showMotionSpec);
    }

    rt::Task<> SheetState::Hide() {
        if (SkipHiddenState())
            throw std::logic_error("Attempted to animate to hidden when skipHiddenState was enabled. Set skipHiddenState to false to "
                                   "use this function.");
        if (Confirm(SheetValue::Hidden)) co_await AnimateTo(SheetValue::Hidden, hideMotionSpec);
    }

    // anchoredDrag(targetValue) follows the latest target while the anchors change; when that
    // differs from `target` (removed meanwhile), the sheet snaps to it.
    rt::Task<> SheetState::AnimateTo(SheetValue target, anim::SpecPtr spec) {
        const std::optional<SheetValue> resolved = co_await m_Anchored.AnimateToResolving(target, std::move(spec));
        if (resolved && *resolved != target && m_Anchored.Anchors().HasPositionFor(*resolved)) co_await m_Anchored.SnapTo(*resolved);
    }

    rt::Task<> SheetState::SnapTo(SheetValue target) { co_await m_Anchored.SnapTo(target); }

    float SheetState::DragBy(float pixels) { return m_Anchored.DispatchRawDelta(pixels); }

    std::shared_ptr<SheetState> RememberBottomSheetState(SheetValue initialValue, SheetValues enabledValues,
                                                         std::function<bool(SheetValue)> confirmValueChange, rt::Key key) {
        return internal::RememberSheetState(enabledValues, std::move(confirmValueChange), initialValue, BottomSheetDefaults::PositionalThreshold,
                                            BottomSheetDefaults::VelocityThreshold, std::nullopt, key);
    }

    namespace BottomSheetDefaults {
        ShapePtr HiddenShape() { return Value(SheetBottomTokens::DockedMinimizedContainerShape); }
        ShapePtr ExpandedShape() { return Value(SheetBottomTokens::DockedContainerShape); }
        Color ContainerColor() { return Value(SheetBottomTokens::DockedContainerColor); }
        const Dp Elevation = SheetBottomTokens::DockedModalContainerElevation;
        Color ScrimColor() { return Value(ScrimTokens::ContainerColor).WithAlpha(ScrimTokens::ContainerOpacity); }
        WindowInsets standardWindowInsets() { return WindowInsets::SafeDrawing().Only(WindowInsetsSides::Bottom()); }
        WindowInsets modalWindowInsets() { return WindowInsets::SafeDrawing().Only(WindowInsetsSides::Vertical()); }

        void DragHandle(const ui::Modifier& modifier, const DragHandleOptions& o) {
            const std::string description = GetString(Strings::BottomSheetDragHandleDescription);
            SurfaceOptions surface;
            surface.color = o.color.value_or(Value(SheetBottomTokens::DockedDragHandleColor));
            surface.shape = o.shape ? o.shape : MaterialTheme::shapes().extraLarge;
            ui::Modifier m = Padding(modifier, Dp(0), Dp(22));
            m = Semantics(std::move(m), [description](ui::SemanticsConfiguration& s) { s.contentDescription = { description }; });
            const Dp w = o.width.value_or(SheetBottomTokens::DockedDragHandleWidth), h = o.height.value_or(SheetBottomTokens::DockedDragHandleHeight);
            Surface(m, surface, [w, h] { Box(SizeMod(ui::Modifier{}, w, h)); });
        }
    }

    namespace internal {

        std::shared_ptr<SheetState> RememberSheetState(SheetValues enabledValues, std::function<bool(SheetValue)> confirmValueChange,
                                                       SheetValue initialValue, Dp positionalThreshold, Dp velocityThreshold,
                                                       std::optional<bool> partiallyExpandedDeterministic, rt::Key key) {
            auto& c = *rt::Composition::Current();
            const ui::Density density = c.Read(ui::LocalDensity);
            const bool deterministic =
                partiallyExpandedDeterministic.value_or(ComposeMaterial3Flags::isBottomSheetPartiallyExpandedDeterministicEnabled);
            std::function<float()> positional = [density, positionalThreshold] { return density.ToPx(positionalThreshold); };
            std::function<float()> velocity = [density, velocityThreshold] { return density.ToPx(velocityThreshold); };
            // SheetState.Saver keeps the current value.
            rt::Saver<std::shared_ptr<SheetState>> saver{
                [](const std::shared_ptr<SheetState>& s) { return rt::SavedValue(double(int(s->CurrentValue()))); },
                [=](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<SheetState>> {
                    if (auto n = rt::SavedNumber(saved))
                        return std::make_shared<SheetState>(enabledValues, positional, velocity, SheetValue(int(*n)), confirmValueChange, deterministic);
                    return std::nullopt;
                }
            };
            const int enabledKey = (enabledValues.hidden ? 1 : 0) | (enabledValues.partiallyExpanded ? 2 : 0);
            return rt::RememberSaveable<std::shared_ptr<SheetState>>(
                [&] { return std::make_shared<SheetState>(enabledValues, positional, velocity, initialValue, confirmValueChange, deterministic); },
                saver, rt::Keys(enabledKey, deterministic), key);
        }

        void DragHandleWithTooltip(const ui::Modifier& modifier, std::function<void()> content) {
            const std::string description = GetString(Strings::BottomSheetDragHandleDescription);
            Box(FillMax(ui::Modifier{}, Axis::Horizontal), ui::Alignment::Center(), false, [=] {
                const auto provider = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above);
                const TooltipState state = RememberTooltipState();
                TooltipBox(provider, [description](const TooltipScope& scope) {
                    PlainTooltipInternal(scope, description, {}, {}, [description] { Text(description); });
                }, state, modifier, {}, content);
            });
        }

        namespace {
            struct ConsumeSwipeConnection final : ui::NestedScrollConnection {
                std::shared_ptr<SheetState> state;
                std::function<rt::Task<float>(float)> fling;

                ui::Offset OnPreScroll(ui::Offset available, ui::NestedScrollSource source) override {
                    const float delta = available.y;
                    if (delta < 0 && source == ui::NestedScrollSource::UserInput) return { 0, state->Anchored().DispatchRawDelta(delta) };
                    return {};
                }
                ui::Offset OnPostScroll(ui::Offset, ui::Offset available, ui::NestedScrollSource source) override {
                    const float delta = available.y;
                    if (source == ui::NestedScrollSource::UserInput && delta != 0) return { 0, state->Anchored().DispatchRawDelta(delta) };
                    return {};
                }
                rt::Task<ui::Offset> OnPreFling(ui::Offset available) override {
                    const float toFling = available.y;
                    const float offset = state->RequireOffset();
                    const float minAnchor = state->Anchored().Anchors().MinPosition();
                    if (toFling < 0 && offset > minAnchor) {
                        // Settling to the anchor takes it all.
                        auto keep = state;
                        co_await fling(toFling);
                        co_return available;
                    }
                    co_return ui::Offset{};
                }
                rt::Task<ui::Offset> OnPostFling(ui::Offset consumed, ui::Offset available) override {
                    auto keep = state;
                    const float used = co_await fling(available.y);
                    co_return ui::Offset{ consumed.x, used };
                }
            };
        }

        std::shared_ptr<ui::NestedScrollConnection> ConsumeSwipeWithinBottomSheetBounds(std::shared_ptr<SheetState> state,
                                                                                       std::function<rt::Task<float>(float velocity)> fling) {
            auto c = std::make_shared<ConsumeSwipeConnection>();
            c->state = std::move(state);
            c->fling = std::move(fling);
            return c;
        }

        float SheetFlingVelocity(const SheetState& state, float velocity, const ui::Density& density) {
            const float initial = velocity;
            float v = velocity;
            const auto& anchors = state.Anchored().Anchors();
            if (v > 0 && anchors.HasPositionFor(SheetValue::Hidden)) {
                const float floor = anchors.PositionOf(SheetValue::Hidden);
                const float offset = state.Anchored().Offset();
                const float distanceToFloor = std::max(0.0f, floor - (std::isnan(offset) ? 0.0f : offset));
                const float zone = density.ToPx(BottomSheetDefaults::BoundaryDampeningZone);
                if (distanceToFloor < zone) {
                    const float f = distanceToFloor / zone;
                    v *= f * f;
                    // A fling that was past the threshold stays a fling.
                    const float threshold = density.ToPx(BottomSheetDefaults::VelocityThreshold);
                    if (initial >= threshold) v = std::max(v, threshold);
                }
            }
            return v;
        }

        rt::Task<float> SheetFling(std::shared_ptr<SheetState> state, float velocity, ui::Density density, anim::SpecPtr spec) {
            const float v = SheetFlingVelocity(*state, velocity, density);
            const float threshold = density.ToPx(Dp(anchored_draggable::MinFlingVelocityDp));
            const auto positional = [s = state.get()](float) { return s->PositionalThresholdPx(); };
            const float left = co_await state->Anchored().Fling(v, threshold, positional, std::move(spec));
            co_return velocity - left;
        }

        ui::Modifier VerticalScaleUp(ui::Modifier m, std::shared_ptr<SheetState> state) {
            return GraphicsLayer(std::move(m), [state](GraphicsLayerParams& l) {
                const float offset = state->Anchored().Offset();
                const float anchor = state->Anchored().Anchors().MinPosition();
                const float overflow = offset < anchor ? anchor - offset : 0.0f;
                const float h = l.size.height;
                l.scaleY = overflow > 0 && h > 0 ? (h + overflow) / h : 1.0f;
                l.originX = 0.5f;
                l.originY = 0;
            });
        }

        ui::Modifier VerticalScaleDown(ui::Modifier m, std::shared_ptr<SheetState> state) {
            return GraphicsLayer(std::move(m), [state](GraphicsLayerParams& l) {
                const float offset = state->Anchored().Offset();
                const float anchor = state->Anchored().Anchors().MinPosition();
                const float overflow = offset < anchor ? anchor - offset : 0.0f;
                const float h = l.size.height;
                l.scaleY = overflow > 0 && h > 0 ? 1.0f / ((h + overflow) / h) : 1.0f;
                l.originX = 0.5f;
                l.originY = 0;
            });
        }

        namespace {
            constexpr Dp PredictiveBackMaxScaleXDistance{ 48 }, PredictiveBackMaxScaleYDistance{ 24 };
        }

        float SheetPredictiveBackScaleX(float progress, float width, const ui::Density& d) {
            if (std::isnan(width) || width == 0) return 1;
            return 1 - std::min(d.ToPx(PredictiveBackMaxScaleXDistance), width) * progress / width;
        }

        float SheetPredictiveBackScaleY(float progress, float height, const ui::Density& d) {
            if (std::isnan(height) || height == 0) return 1;
            return 1 - std::min(d.ToPx(PredictiveBackMaxScaleYDistance), height) * progress / height;
        }

        ui::Modifier SheetPredictiveBackScaling(ui::Modifier m, std::shared_ptr<SheetState> state, std::function<float()> progress) {
            const ui::Density d = rt::Composition::Current()->Read(ui::LocalDensity);
            return GraphicsLayer(std::move(m), [state, progress, d](GraphicsLayerParams& l) {
                const float offset = state->Anchored().Offset();
                const float h = l.size.height;
                if (std::isnan(offset) || std::isnan(h) || h == 0) return;
                const float p = progress();
                l.scaleX = SheetPredictiveBackScaleX(p, l.size.width, d);
                l.scaleY = SheetPredictiveBackScaleY(p, h, d);
                l.originX = 0.5f;
                l.originY = (offset + h) / h;
            });
        }

        ui::Modifier ContentPredictiveBackScaling(ui::Modifier m, std::function<float()> progress) {
            const ui::Density d = rt::Composition::Current()->Read(ui::LocalDensity);
            return GraphicsLayer(std::move(m), [progress, d](GraphicsLayerParams& l) {
                const float p = progress();
                const float sx = SheetPredictiveBackScaleX(p, l.size.width, d), sy = SheetPredictiveBackScaleY(p, l.size.height, d);
                l.scaleY = sy != 0 ? sx / sy : 1.0f;
                l.originX = 0.5f;
                l.originY = 0;
            });
        }

        bool IsPartiallyExpandedAnchorAvailable(const SheetState& state, float fullHeight, float sheetHeight) {
            return !state.SkipPartiallyExpanded() && sheetHeight != 0 && (state.PartiallyExpandedDeterministic() || sheetHeight > fullHeight / 2);
        }

        float CalculatePartiallyExpandedOffset(const SheetState& state, float fullHeight, float sheetHeight) {
            return fullHeight - (state.PartiallyExpandedDeterministic() ? std::min(fullHeight / 2, sheetHeight / 2) : fullHeight / 2);
        }

        anim::SpecPtr BottomSheetAnimationSpec() { return anim::Tween(300, 0, anim::FastOutSlowIn()); }
    }

}
