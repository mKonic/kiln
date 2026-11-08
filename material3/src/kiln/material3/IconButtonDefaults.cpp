#include "kiln/material3/IconButton.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/FilledIconButtonTokens.h"
#include "kiln/material3/tokens/FilledTonalIconButtonTokens.h"
#include "kiln/material3/tokens/IconButtonTokens.h"
#include "kiln/material3/tokens/OutlinedIconButtonTokens.h"
#include "kiln/material3/tokens/XSmallIconButtonTokens.h"
#include "kiln/material3/tokens/SmallIconButtonTokens.h"
#include "kiln/material3/tokens/MediumIconButtonTokens.h"
#include "kiln/material3/tokens/LargeIconButtonTokens.h"
#include "kiln/material3/tokens/XLargeIconButtonTokens.h"

namespace kiln::material3::IconButtonDefaults {

    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using foundation::ShapePtr;

    namespace {
        Color LocalColor() { return rt::Composition::Current()->Read(LocalContentColor); }
        IconButtonColors Transparent(Color content, Color disabled, float alpha) {
            return { Color::Transparent(), content, Color::Transparent(), disabled.WithAlpha(alpha) };
        }
        IconToggleButtonColors TransparentToggle(Color content, Color disabled, float alpha, Color checkedContainer, Color checkedContent) {
            return { Color::Transparent(), content, Color::Transparent(), disabled.WithAlpha(alpha), checkedContainer, checkedContent };
        }
        foundation::BorderStroke Border(bool enabled, Color color) {
            return { SmallIconButtonTokens::OutlinedOutlineWidth, enabled ? color : color.WithAlpha(OutlinedIconButtonTokens::DisabledOpacity) };
        }
        ui::DpSize Container(IconButtonWidthOption width, Dp height, Dp icon, Dp narrow, Dp uniform, Dp wide) {
            Dp space;
            switch (width) {
                case IconButtonWidthOption::Narrow: space = narrow; break;
                case IconButtonWidthOption::Uniform: space = uniform; break;
                case IconButtonWidthOption::Wide: space = wide; break;
            }
            return { icon + space, height };
        }
    }

    IconButtonColors iconButtonColors() {
        const Color content = LocalColor();
        return Transparent(content, content, StandardIconButtonTokens::DisabledOpacity);
    }
    IconButtonColors iconButtonVibrantColors() {
        return Transparent(Value(StandardIconButtonTokens::Color), Value(StandardIconButtonTokens::DisabledColor), StandardIconButtonTokens::DisabledOpacity);
    }
    IconToggleButtonColors iconToggleButtonColors() {
        const Color content = LocalColor();
        return TransparentToggle(content, content, StandardIconButtonTokens::DisabledOpacity, Color::Transparent(), Value(StandardIconButtonTokens::SelectedColor));
    }
    IconToggleButtonColors iconToggleButtonVibrantColors() {
        return TransparentToggle(Value(StandardIconButtonTokens::UnselectedColor), Value(StandardIconButtonTokens::DisabledColor), StandardIconButtonTokens::DisabledOpacity,
                                  Color::Transparent(), Value(StandardIconButtonTokens::SelectedColor));
    }
    IconButtonColors filledIconButtonColors() {
        return { Value(FilledIconButtonTokens::ContainerColor), Value(FilledIconButtonTokens::Color),
                 Value(FilledIconButtonTokens::DisabledContainerColor).WithAlpha(FilledIconButtonTokens::DisabledContainerOpacity),
                 Value(FilledIconButtonTokens::DisabledColor).WithAlpha(FilledIconButtonTokens::DisabledOpacity) };
    }
    IconToggleButtonColors filledIconToggleButtonColors() {
        return { Value(FilledIconButtonTokens::UnselectedContainerColor), Value(FilledIconButtonTokens::UnselectedColor),
                 Value(FilledIconButtonTokens::DisabledContainerColor).WithAlpha(FilledIconButtonTokens::DisabledContainerOpacity),
                 Value(FilledIconButtonTokens::DisabledColor).WithAlpha(FilledIconButtonTokens::DisabledOpacity),
                 Value(FilledIconButtonTokens::SelectedContainerColor), Value(FilledIconButtonTokens::SelectedColor) };
    }
    IconButtonColors filledTonalIconButtonColors() {
        return { Value(FilledTonalIconButtonTokens::ContainerColor), Value(FilledTonalIconButtonTokens::Color),
                 Value(FilledTonalIconButtonTokens::DisabledContainerColor).WithAlpha(FilledTonalIconButtonTokens::DisabledContainerOpacity),
                 Value(FilledTonalIconButtonTokens::DisabledColor).WithAlpha(FilledTonalIconButtonTokens::DisabledOpacity) };
    }
    IconToggleButtonColors filledTonalIconToggleButtonColors() {
        return { Value(FilledTonalIconButtonTokens::UnselectedContainerColor), Value(FilledTonalIconButtonTokens::UnselectedColor),
                 Value(FilledTonalIconButtonTokens::DisabledContainerColor).WithAlpha(FilledTonalIconButtonTokens::DisabledContainerOpacity),
                 Value(FilledTonalIconButtonTokens::DisabledColor).WithAlpha(FilledTonalIconButtonTokens::DisabledOpacity),
                 Value(FilledTonalIconButtonTokens::SelectedContainerColor), Value(FilledTonalIconButtonTokens::SelectedColor) };
    }
    IconButtonColors outlinedIconButtonColors() {
        const Color content = LocalColor();
        return Transparent(content, content, OutlinedIconButtonTokens::DisabledOpacity);
    }
    IconButtonColors outlinedIconButtonVibrantColors() {
        return Transparent(Value(OutlinedIconButtonTokens::Color), Value(OutlinedIconButtonTokens::DisabledColor), OutlinedIconButtonTokens::DisabledOpacity);
    }
    IconToggleButtonColors outlinedIconToggleButtonColors() {
        const Color content = LocalColor(), checked = Value(OutlinedIconButtonTokens::SelectedContainerColor);
        return TransparentToggle(content, content, OutlinedIconButtonTokens::DisabledOpacity, checked, ContentColorFor(checked));
    }
    IconToggleButtonColors outlinedIconToggleButtonVibrantColors() {
        return TransparentToggle(Value(OutlinedIconButtonTokens::UnselectedColor), Value(OutlinedIconButtonTokens::DisabledColor),
                                  OutlinedIconButtonTokens::DisabledOpacity, Value(OutlinedIconButtonTokens::SelectedContainerColor),
                                  Value(OutlinedIconButtonTokens::SelectedColor));
    }

    foundation::BorderStroke outlinedIconButtonBorder(bool enabled) { return Border(enabled, LocalColor()); }
    foundation::BorderStroke outlinedIconButtonVibrantBorder(bool enabled) { return Border(enabled, Value(OutlinedIconButtonTokens::OutlineColor)); }
    std::optional<foundation::BorderStroke> outlinedIconToggleButtonBorder(bool enabled, bool checked) {
        return checked ? std::nullopt : std::optional(outlinedIconButtonBorder(enabled));
    }
    std::optional<foundation::BorderStroke> outlinedIconToggleButtonVibrantBorder(bool enabled, bool checked) {
        return checked ? std::nullopt : std::optional(outlinedIconButtonVibrantBorder(enabled));
    }

    ShapePtr standardShape() { return smallRoundShape(); }
    ShapePtr filledShape() { return smallRoundShape(); }
    ShapePtr outlinedShape() { return smallRoundShape(); }
    ShapePtr extraSmallRoundShape() { return Value(XSmallIconButtonTokens::ContainerShapeRound); }
    ShapePtr extraSmallSquareShape() { return Value(XSmallIconButtonTokens::ContainerShapeSquare); }
    ShapePtr extraSmallPressedShape() { return Value(XSmallIconButtonTokens::PressedContainerShape); }
    ShapePtr extraSmallSelectedRoundShape() { return Value(XSmallIconButtonTokens::SelectedContainerShapeRound); }
    ShapePtr extraSmallSelectedSquareShape() { return Value(XSmallIconButtonTokens::SelectedContainerShapeSquare); }
    ShapePtr smallRoundShape() { return Value(SmallIconButtonTokens::ContainerShapeRound); }
    ShapePtr smallSquareShape() { return Value(SmallIconButtonTokens::ContainerShapeSquare); }
    ShapePtr smallPressedShape() { return Value(SmallIconButtonTokens::PressedContainerShape); }
    ShapePtr smallSelectedRoundShape() { return Value(SmallIconButtonTokens::SelectedContainerShapeRound); }
    ShapePtr smallSelectedSquareShape() { return Value(SmallIconButtonTokens::SelectedContainerShapeSquare); }
    ShapePtr mediumRoundShape() { return Value(MediumIconButtonTokens::ContainerShapeRound); }
    ShapePtr mediumSquareShape() { return Value(MediumIconButtonTokens::ContainerShapeSquare); }
    ShapePtr mediumPressedShape() { return Value(MediumIconButtonTokens::PressedContainerShape); }
    ShapePtr mediumSelectedRoundShape() { return Value(MediumIconButtonTokens::SelectedContainerShapeRound); }
    ShapePtr mediumSelectedSquareShape() { return Value(MediumIconButtonTokens::SelectedContainerShapeSquare); }
    ShapePtr largeRoundShape() { return Value(LargeIconButtonTokens::ContainerShapeRound); }
    ShapePtr largeSquareShape() { return Value(LargeIconButtonTokens::ContainerShapeSquare); }
    ShapePtr largePressedShape() { return Value(LargeIconButtonTokens::PressedContainerShape); }
    ShapePtr largeSelectedRoundShape() { return Value(LargeIconButtonTokens::SelectedContainerShapeRound); }
    ShapePtr largeSelectedSquareShape() { return Value(LargeIconButtonTokens::SelectedContainerShapeSquare); }
    ShapePtr extraLargeRoundShape() { return Value(XLargeIconButtonTokens::ContainerShapeRound); }
    ShapePtr extraLargeSquareShape() { return Value(XLargeIconButtonTokens::ContainerShapeSquare); }
    ShapePtr extraLargePressedShape() { return Value(XLargeIconButtonTokens::PressedContainerShape); }
    ShapePtr extraLargeSelectedRoundShape() { return Value(XLargeIconButtonTokens::SelectedContainerShapeRound); }
    ShapePtr extraLargeSelectedSquareShape() { return Value(XLargeIconButtonTokens::SelectedContainerShapeSquare); }
    IconButtonShapes shapes(ShapePtr shape, ShapePtr pressedShape) {
        return IconButtonShapes{ smallRoundShape(), smallPressedShape() }.Copy(std::move(shape), std::move(pressedShape));
    }
    IconToggleButtonShapes toggleableShapes(ShapePtr shape, ShapePtr pressedShape, ShapePtr checkedShape) {
        return IconToggleButtonShapes{ smallRoundShape(), smallPressedShape(), smallSelectedRoundShape() }
            .Copy(std::move(shape), std::move(pressedShape), std::move(checkedShape));
    }

    const Dp ExtraSmallIconSize = XSmallIconButtonTokens::IconSize, SmallIconSize = SmallIconButtonTokens::IconSize,
             MediumIconSize = MediumIconButtonTokens::IconSize, LargeIconSize = LargeIconButtonTokens::IconSize,
             ExtraLargeIconSize = XLargeIconButtonTokens::IconSize;
    ui::DpSize extraSmallContainerSize(IconButtonWidthOption width) {
        return Container(width, XSmallIconButtonTokens::ContainerHeight, XSmallIconButtonTokens::IconSize,
                         XSmallIconButtonTokens::NarrowLeadingSpace + XSmallIconButtonTokens::NarrowTrailingSpace,
                         XSmallIconButtonTokens::DefaultLeadingSpace * 2,
                         XSmallIconButtonTokens::WideLeadingSpace + XSmallIconButtonTokens::WideTrailingSpace);
    }
    ui::DpSize smallContainerSize(IconButtonWidthOption width) {
        return Container(width, SmallIconButtonTokens::ContainerHeight, SmallIconButtonTokens::IconSize,
                         SmallIconButtonTokens::NarrowLeadingSpace + SmallIconButtonTokens::NarrowTrailingSpace,
                         SmallIconButtonTokens::DefaultLeadingSpace * 2,
                         SmallIconButtonTokens::WideLeadingSpace + SmallIconButtonTokens::WideTrailingSpace);
    }
    ui::DpSize mediumContainerSize(IconButtonWidthOption width) {
        return Container(width, MediumIconButtonTokens::ContainerHeight, MediumIconButtonTokens::IconSize,
                         MediumIconButtonTokens::NarrowLeadingSpace + MediumIconButtonTokens::NarrowTrailingSpace,
                         MediumIconButtonTokens::DefaultLeadingSpace * 2,
                         MediumIconButtonTokens::WideLeadingSpace + MediumIconButtonTokens::WideTrailingSpace);
    }
    ui::DpSize largeContainerSize(IconButtonWidthOption width) {
        return Container(width, LargeIconButtonTokens::ContainerHeight, LargeIconButtonTokens::IconSize,
                         LargeIconButtonTokens::NarrowLeadingSpace + LargeIconButtonTokens::NarrowTrailingSpace,
                         LargeIconButtonTokens::UniformLeadingSpace * 2,
                         LargeIconButtonTokens::WideLeadingSpace + LargeIconButtonTokens::WideTrailingSpace);
    }
    ui::DpSize extraLargeContainerSize(IconButtonWidthOption width) {
        return Container(width, XLargeIconButtonTokens::ContainerHeight, XLargeIconButtonTokens::IconSize,
                         XLargeIconButtonTokens::NarrowLeadingSpace + XLargeIconButtonTokens::NarrowTrailingSpace,
                         XLargeIconButtonTokens::DefaultLeadingSpace * 2,
                         XLargeIconButtonTokens::WideLeadingSpace + XLargeIconButtonTokens::WideTrailingSpace);
    }

}
