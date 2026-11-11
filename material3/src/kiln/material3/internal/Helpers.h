#pragma once

// material3 internal helpers shared by components:
//   Elevation.kt              animateElevation (shadow elevation following interactions)
//   AnimatedShape.kt          AnimatedShapeState / rememberAnimatedShape (corner morphs)
//   HorizontalCenterOptically.kt  horizontalCenterOptically (optical centring in rounded shapes)
//   MenuPosition.kt (+ Menu.kt's MenuAnchorPosition, calculateTransformOrigin)

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/Transition.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Popup.h"

#include <functional>
#include <optional>

namespace kiln::material3::internal {

    using ui::Dp;
    using ui::IntRect;
    using ui::IntSize;
    using ui::LayoutDirection;

    // animateElevation: towards `target` with the incoming spec of `to` (press, drag, hover,
    // focus: 120 ms FastOutSlowIn), else the outgoing spec of `from` (150 ms, hover 120 ms,
    // CubicBezier(0.4, 0, 0.6, 1)), else a snap. The animatable holds dp.
    rt::Task<> AnimateElevation(anim::Animatable& animatable, Dp target, std::optional<foundation::Interaction::Kind> from = std::nullopt,
                                std::optional<foundation::Interaction::Kind> to = std::nullopt);

    // ShapeWithHorizontalCenterOptically: a shape that says how far to shift its content.
    class ShapeWithHorizontalCenterOptically : public gfx::Shape {
    public:
        virtual float Offset(ui::Size size, const ui::Density& density) const = 0;
    };
    inline constexpr float CenterOpticallyCoefficient = 0.11f;

    // horizontalCenterOptically: the content placed by 0.11 of the difference between the
    // start and end corners' mean radii, within [-maxStartOffset, maxEndOffset].
    ui::Modifier HorizontalCenterOptically(ui::Modifier m, std::shared_ptr<const foundation::CornerBasedShape> shape, Dp maxStartOffset = Dp(0),
                                           Dp maxEndOffset = Dp(0));
    ui::Modifier HorizontalCenterOptically(ui::Modifier m, std::shared_ptr<const ShapeWithHorizontalCenterOptically> shape,
                                           Dp maxStartOffset = Dp(0), Dp maxEndOffset = Dp(0));

    // AnimatedShapeState: morphs between corner-based shapes with a progress 0..1. Going back to
    // the shape it came from reverses in place, keeping the (negated) velocity.
    class AnimatedShapeState {
    public:
        AnimatedShapeState(std::shared_ptr<const foundation::CornerBasedShape> initialShape, anim::SpecPtr spec);
        std::shared_ptr<const foundation::CornerBasedShape> MorphedShape();
        rt::Task<> AnimateToShape(std::shared_ptr<const foundation::CornerBasedShape> newTarget);
        anim::Animatable progress{ 1.0f };
        std::shared_ptr<const foundation::CornerBasedShape> startShape, targetShape;
        anim::SpecPtr spec;
    private:
        float m_CachedProgress = -1;
        std::shared_ptr<const foundation::CornerBasedShape> m_Cached;
    };
    // rememberAnimatedShape(state): a shape whose outline is the state's current morph.
    std::shared_ptr<const ShapeWithHorizontalCenterOptically> RememberAnimatedShape(const std::shared_ptr<AnimatedShapeState>& state);
    // rememberAnimatedShape(currentShape, spec): animates to each new shape.
    // `key` starts over with a new state when it changes (key(...) around the call).
    std::shared_ptr<const ShapeWithHorizontalCenterOptically> RememberAnimatedShape(std::shared_ptr<const foundation::CornerBasedShape> currentShape,
                                                                                    anim::SpecPtr spec, std::size_t key = 0);

    // MenuPosition: candidate positions for a menu against its anchor and the window.
    namespace MenuPosition {
        using Vertical = std::function<int(IntRect anchorBounds, IntSize windowSize, int menuHeight)>;
        using Horizontal = std::function<int(IntRect anchorBounds, IntSize windowSize, int menuWidth, LayoutDirection dir)>;
        extern const Horizontal startToAnchorStart, startToAnchorEnd, endToAnchorEnd, endToAnchorStart, leftToWindowLeft, rightToWindowRight;
        extern const Vertical topToAnchorBottom, topToAnchorTop, bottomToAnchorTop, bottomToAnchorBottom, centerToAnchorTop, topToWindowTop,
            bottomToWindowBottom;
    }

    // MenuPositionScope
    struct MenuPositionScope {
        IntRect anchorBounds;
        IntSize windowSize;
        IntSize menuSize;
        LayoutDirection layoutDirection = LayoutDirection::Ltr;
    };

    // MenuAnchorPosition: the x and y candidates, tried in order.
    struct MenuAnchorPosition {
        std::function<std::vector<int>(const MenuPositionScope&)> xCandidates, yCandidates;
        static const MenuAnchorPosition& Above();
        static const MenuAnchorPosition& Below();
        static const MenuAnchorPosition& Left();
        static const MenuAnchorPosition& Right();
        static const MenuAnchorPosition& Start();
        static const MenuAnchorPosition& End();
    };

    // TransformOrigin as fractions of the menu's size.
    struct TransformOrigin {
        float pivotFractionX = 0.5f, pivotFractionY = 0.5f;
        bool operator==(const TransformOrigin&) const = default;
    };
    // calculateTransformOrigin: where the menu meets its anchor.
    TransformOrigin CalculateTransformOrigin(IntRect anchorBounds, IntRect menuBounds);

    inline constexpr Dp MenuVerticalMargin{ 48 }, MenuHorizontalMargin{ 8 };

    // DropdownMenuContent: the menu's surface, scaling and fading in from `transformOrigin`
    // with its expanded state, its content in a scrolling column.
    void DropdownMenuContent(const ui::Modifier& modifier, foundation::MutableTransitionState<bool>& expandedState,
                             std::function<TransformOrigin()> transformOrigin, foundation::ScrollState& scrollState, foundation::ShapePtr shape,
                             ui::Color containerColor, ui::Dp tonalElevation, ui::Dp shadowElevation, std::optional<foundation::BorderStroke> border,
                             std::function<void()> content);

    // DropdownMenuPositionProvider: the first candidate that fits within the margins; failing
    // all, centred (when wider than the window less margins) or clamped. Remembers the
    // transform origin for the menu's scale animation.
    class DropdownMenuPositionProvider final : public ui::PopupPositionProvider {
    public:
        DropdownMenuPositionProvider(ui::DpOffset contentOffset, const ui::Density& density, MenuAnchorPosition anchorPosition = MenuAnchorPosition::Below(),
                                     std::function<void(IntRect anchorBounds, IntRect menuBounds)> onPositionCalculated = {});
        ui::IntOffset CalculatePosition(IntRect anchorBounds, IntSize windowSize, LayoutDirection layoutDirection, IntSize popupContentSize) const override;
        // Observable: reading it follows each new position.
        TransformOrigin GetTransformOrigin() const { return m_TransformOrigin.Get(); }
        int verticalMargin, horizontalMargin;
    private:
        ui::DpOffset m_ContentOffset;
        ui::Density m_Density;
        MenuAnchorPosition m_AnchorPosition;
        std::function<void(IntRect, IntRect)> m_OnPositionCalculated;
        mutable rt::State<TransformOrigin> m_TransformOrigin{ TransformOrigin{} };
    };

}
