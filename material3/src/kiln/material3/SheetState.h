#pragma once

#include "kiln/Internal.h"
// SheetDefaults.kt: SheetState (where a bottom sheet rests: hidden, partially expanded or
// expanded, over an anchored draggable), its defaults, the drag handle, and the pieces the sheets
// share (the nested scroll that hands drags to the sheet, the overscroll stretch, the predictive
// back scaling).

#include "kiln/animation/Animatable.h"
#include <kiln/Function.h>
#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"
#include "kiln/ui/NestedScroll.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class SheetValue : std::uint8_t { Hidden, Expanded, PartiallyExpanded };

    // The values a sheet may rest at: Expanded always, PartiallyExpanded and Hidden optionally.
    struct SheetValues {
        bool hidden = true, partiallyExpanded = true;
        bool Contains(SheetValue v) const { return v == SheetValue::Expanded || (v == SheetValue::Hidden ? hidden : partiallyExpanded); }
        bool operator==(const SheetValues&) const = default;
    };

    class SheetState {
    public:
        // Throws std::invalid_argument when initialValue is not one of the enabled values.
        SheetState(SheetValues enabledValues, kiln::Function<float()> positionalThreshold, kiln::Function<float()> velocityThreshold,
                   SheetValue initialValue = SheetValue::Hidden, kiln::Function<bool(SheetValue)> confirmValueChange = {},
                   std::optional<bool> partiallyExpandedDeterministic = std::nullopt);   // unset: the flag

        // Reading these subscribes.
        SheetValue CurrentValue() const { return m_Anchored.SettledValue(); }
        SheetValue TargetValue() const { return m_Anchored.TargetValue(); }
        bool IsVisible() const { return m_Anchored.CurrentValue() != SheetValue::Hidden; }
        bool IsAnimationRunning() const { return m_Anchored.IsAnimationRunning(); }
        float RequireOffset() const { return m_Anchored.RequireOffset(); }
        bool HasExpandedState() const { return m_Anchored.Anchors().HasPositionFor(SheetValue::Expanded); }
        bool HasPartiallyExpandedState() const { return m_Anchored.Anchors().HasPositionFor(SheetValue::PartiallyExpanded); }

        // Animations (each only if confirmValueChange agrees). PartialExpand and Hide throw
        // std::logic_error when that value is not enabled.
        rt::Task<> Expand();
        rt::Task<> PartialExpand();
        // Partially expanded when the sheet can be, else expanded.
        rt::Task<> Show();
        rt::Task<> Hide();
        rt::Task<> SnapTo(SheetValue target);

        bool SkipPartiallyExpanded() const { return !m_Enabled.partiallyExpanded; }
        bool SkipHiddenState() const { return !m_Enabled.hidden; }

        // Internal.
        bool Confirm(SheetValue v) const { return !m_Confirm || m_Confirm(v); }
        rt::Task<> AnimateTo(SheetValue target, anim::SpecPtr spec);
        KILN_INTERNAL foundation::AnchoredDraggableState<SheetValue>& Anchored() { return m_Anchored; }
        KILN_INTERNAL const foundation::AnchoredDraggableState<SheetValue>& Anchored() const { return m_Anchored; }
        float PositionalThresholdPx() const { return m_PositionalThreshold ? m_PositionalThreshold() : 0.0f; }
        float VelocityThresholdPx() const { return m_VelocityThreshold ? m_VelocityThreshold() : 0.0f; }
        bool PartiallyExpandedDeterministic() const { return m_Deterministic; }
        const SheetValues& EnabledValues() const { return m_Enabled; }
        // Drags the sheet by `pixels`, clamped to its anchors (newOffsetForDelta); returns what moved.
        float DragBy(float pixels);
        anim::SpecPtr showMotionSpec, hideMotionSpec, anchoredDraggableMotionSpec;

    private:
        SheetValues m_Enabled;
        kiln::Function<float()> m_PositionalThreshold, m_VelocityThreshold;
        kiln::Function<bool(SheetValue)> m_Confirm;
        bool m_Deterministic;
        foundation::AnchoredDraggableState<SheetValue> m_Anchored;
    };

    // rememberBottomSheetState: the value survives re-creation (SheetState.Saver).
    std::shared_ptr<SheetState> RememberBottomSheetState(SheetValue initialValue, SheetValues enabledValues = {},
                                                         kiln::Function<bool(SheetValue)> confirmValueChange = {}, rt::Key key = {});

    namespace BottomSheetDefaults {
        foundation::ShapePtr HiddenShape();
        foundation::ShapePtr ExpandedShape();
        ui::Color ContainerColor();
        extern const ui::Dp Elevation;
        ui::Color ScrimColor();
        inline constexpr ui::Dp SheetPeekHeight{ 56 }, SheetMaxWidth{ 640 };
        foundation::WindowInsets standardWindowInsets();   // safe drawing, bottom
        foundation::WindowInsets modalWindowInsets();      // safe drawing, top and bottom
        inline constexpr ui::Dp PositionalThreshold{ 56 }, VelocityThreshold{ 125 }, BoundaryDampeningZone{ 125 };
        // A pill to drag the sheet by, described for accessibility.
        struct DragHandleOptions {
            std::optional<ui::Dp> width, height;   // unset: the tokens'
            foundation::ShapePtr shape;            // null: shapes.extraLarge
            std::optional<ui::Color> color;
        };
        void DragHandle(const ui::Modifier& modifier = {}, const DragHandleOptions& options = {});
    }

    namespace internal {
        // rememberSheetState
        std::shared_ptr<SheetState> RememberSheetState(SheetValues enabledValues = {}, kiln::Function<bool(SheetValue)> confirmValueChange = {},
                                                       SheetValue initialValue = SheetValue::Hidden,
                                                       ui::Dp positionalThreshold = BottomSheetDefaults::PositionalThreshold,
                                                       ui::Dp velocityThreshold = BottomSheetDefaults::VelocityThreshold,
                                                       std::optional<bool> partiallyExpandedDeterministic = std::nullopt,   // unset: the flag
                                                       rt::Key key = {});
        // DragHandleWithTooltip: the handle centred across the sheet, with its description as a tooltip.
        void DragHandleWithTooltip(const ui::Modifier& modifier, kiln::Function<void()> content);
        // ConsumeSwipeWithinBottomSheetBoundsNestedScrollConnection: content scrolled up drags the
        // sheet up first; what the content leaves drags it either way; flings settle it.
        std::shared_ptr<ui::NestedScrollConnection> ConsumeSwipeWithinBottomSheetBounds(std::shared_ptr<SheetState> state,
                                                                                       kiln::Function<rt::Task<float>(float velocity)> fling);
        // The sheet's release velocity (BottomSheetImpl's fling): damped near the hidden anchor,
        // kept at the velocity threshold when it started past it. The velocity trackers already
        // cap it at the system's maximum.
        float SheetFlingVelocity(const SheetState& state, float velocity, const ui::Density& density);
        // anchoredDrag(flingBehavior, velocity): the sheet's fling from `velocity`; returns the
        // velocity consumed.
        rt::Task<float> SheetFling(std::shared_ptr<SheetState> state, float velocity, ui::Density density, anim::SpecPtr spec);
        // verticalScaleUp / verticalScaleDown: the sheet stretches as it is pulled past its
        // highest anchor; its content keeps its size.
        ui::Modifier VerticalScaleUp(ui::Modifier m, std::shared_ptr<SheetState> state);
        ui::Modifier VerticalScaleDown(ui::Modifier m, std::shared_ptr<SheetState> state);
        // Predictive back: the sheet shrinks (48 dp across, 24 dp down at most) towards its
        // bottom edge; the content keeps its aspect.
        float SheetPredictiveBackScaleX(float progress, float width, const ui::Density& d);
        float SheetPredictiveBackScaleY(float progress, float height, const ui::Density& d);
        ui::Modifier SheetPredictiveBackScaling(ui::Modifier m, std::shared_ptr<SheetState> state, kiln::Function<float()> progress);
        ui::Modifier ContentPredictiveBackScaling(ui::Modifier m, kiln::Function<float()> progress);
        bool IsPartiallyExpandedAnchorAvailable(const SheetState& state, float fullHeight, float sheetHeight);
        float CalculatePartiallyExpandedOffset(const SheetState& state, float fullHeight, float sheetHeight);
        // BottomSheetAnimationSpec: tween(300, FastOutSlowIn).
        anim::SpecPtr BottomSheetAnimationSpec();
    }

}
