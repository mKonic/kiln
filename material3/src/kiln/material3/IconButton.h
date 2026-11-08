#pragma once

// IconButton.kt / IconButtonDefaults.kt: the four icon button styles and their toggleable
// forms, with 48 dp minimum targets, optional Expressive shape morphing and five sizes.

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    struct IconButtonColors {
        ui::Color containerColor, contentColor, disabledContainerColor, disabledContentColor;
        IconButtonColors Copy(ui::Color container = ui::Color::Unspecified(), ui::Color content = ui::Color::Unspecified(),
                              ui::Color disabledContainer = ui::Color::Unspecified(), ui::Color disabledContent = ui::Color::Unspecified()) const;
        ui::Color ContainerColor(bool enabled) const { return enabled ? containerColor : disabledContainerColor; }
        ui::Color ContentColor(bool enabled) const { return enabled ? contentColor : disabledContentColor; }
        bool operator==(const IconButtonColors&) const = default;
    };

    struct IconToggleButtonColors {
        ui::Color containerColor, contentColor, disabledContainerColor, disabledContentColor, checkedContainerColor, checkedContentColor;
        IconToggleButtonColors Copy(ui::Color container = ui::Color::Unspecified(), ui::Color content = ui::Color::Unspecified(),
                                    ui::Color disabledContainer = ui::Color::Unspecified(), ui::Color disabledContent = ui::Color::Unspecified(),
                                    ui::Color checkedContainer = ui::Color::Unspecified(), ui::Color checkedContent = ui::Color::Unspecified()) const;
        ui::Color ContainerColor(bool enabled, bool checked) const {
            return !enabled ? disabledContainerColor : checked ? checkedContainerColor : containerColor;
        }
        ui::Color ContentColor(bool enabled, bool checked) const {
            return !enabled ? disabledContentColor : checked ? checkedContentColor : contentColor;
        }
        bool operator==(const IconToggleButtonColors&) const = default;
    };

    struct IconButtonShapes {
        foundation::ShapePtr shape, pressedShape = shape;
        IconButtonShapes Copy(foundation::ShapePtr rest = nullptr, foundation::ShapePtr pressed = nullptr) const {
            return { rest ? rest : shape, pressed ? pressed : pressedShape };
        }
    };
    struct IconToggleButtonShapes {
        foundation::ShapePtr shape, pressedShape = shape, checkedShape = shape;
        IconToggleButtonShapes Copy(foundation::ShapePtr rest = nullptr, foundation::ShapePtr pressed = nullptr,
                                    foundation::ShapePtr checked = nullptr) const {
            return { rest ? rest : shape, pressed ? pressed : pressedShape, checked ? checked : checkedShape };
        }
    };

    struct IconButtonOptions {
        bool enabled = true;
        foundation::ShapePtr shape;
        std::optional<IconButtonShapes> shapes;
        std::optional<IconButtonColors> colors;
        // Unset: variant default. An empty inner optional removes the border.
        std::optional<std::optional<foundation::BorderStroke>> border;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    struct IconToggleButtonOptions {
        bool enabled = true;
        foundation::ShapePtr shape;
        std::optional<IconToggleButtonShapes> shapes;
        std::optional<IconToggleButtonColors> colors;
        std::optional<std::optional<foundation::BorderStroke>> border;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void IconButton(std::function<void()> onClick, const ui::Modifier& modifier, const IconButtonOptions& options, std::function<void()> content);
    void FilledIconButton(std::function<void()> onClick, const ui::Modifier& modifier, const IconButtonOptions& options, std::function<void()> content);
    void FilledTonalIconButton(std::function<void()> onClick, const ui::Modifier& modifier, const IconButtonOptions& options, std::function<void()> content);
    void OutlinedIconButton(std::function<void()> onClick, const ui::Modifier& modifier, const IconButtonOptions& options, std::function<void()> content);
    void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                          const IconToggleButtonOptions& options, std::function<void()> content);
    void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                const IconToggleButtonOptions& options, std::function<void()> content);
    void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                     const IconToggleButtonOptions& options, std::function<void()> content);
    void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                  const IconToggleButtonOptions& options, std::function<void()> content);

    enum class IconButtonWidthOption { Narrow, Uniform, Wide };

    namespace IconButtonDefaults {
        IconButtonColors iconButtonColors();
        IconButtonColors iconButtonVibrantColors();
        IconButtonColors filledIconButtonColors();
        IconButtonColors filledTonalIconButtonColors();
        IconButtonColors outlinedIconButtonColors();
        IconButtonColors outlinedIconButtonVibrantColors();
        IconToggleButtonColors iconToggleButtonColors();
        IconToggleButtonColors iconToggleButtonVibrantColors();
        IconToggleButtonColors filledIconToggleButtonColors();
        IconToggleButtonColors filledTonalIconToggleButtonColors();
        IconToggleButtonColors outlinedIconToggleButtonColors();
        IconToggleButtonColors outlinedIconToggleButtonVibrantColors();
        foundation::BorderStroke outlinedIconButtonBorder(bool enabled);
        foundation::BorderStroke outlinedIconButtonVibrantBorder(bool enabled);
        std::optional<foundation::BorderStroke> outlinedIconToggleButtonBorder(bool enabled, bool checked);
        std::optional<foundation::BorderStroke> outlinedIconToggleButtonVibrantBorder(bool enabled, bool checked);

        foundation::ShapePtr standardShape();
        foundation::ShapePtr filledShape();
        foundation::ShapePtr outlinedShape();
        foundation::ShapePtr extraSmallRoundShape();
        foundation::ShapePtr extraSmallSquareShape();
        foundation::ShapePtr extraSmallPressedShape();
        foundation::ShapePtr extraSmallSelectedRoundShape();
        foundation::ShapePtr extraSmallSelectedSquareShape();
        foundation::ShapePtr smallRoundShape();
        foundation::ShapePtr smallSquareShape();
        foundation::ShapePtr smallPressedShape();
        foundation::ShapePtr smallSelectedRoundShape();
        foundation::ShapePtr smallSelectedSquareShape();
        foundation::ShapePtr mediumRoundShape();
        foundation::ShapePtr mediumSquareShape();
        foundation::ShapePtr mediumPressedShape();
        foundation::ShapePtr mediumSelectedRoundShape();
        foundation::ShapePtr mediumSelectedSquareShape();
        foundation::ShapePtr largeRoundShape();
        foundation::ShapePtr largeSquareShape();
        foundation::ShapePtr largePressedShape();
        foundation::ShapePtr largeSelectedRoundShape();
        foundation::ShapePtr largeSelectedSquareShape();
        foundation::ShapePtr extraLargeRoundShape();
        foundation::ShapePtr extraLargeSquareShape();
        foundation::ShapePtr extraLargePressedShape();
        foundation::ShapePtr extraLargeSelectedRoundShape();
        foundation::ShapePtr extraLargeSelectedSquareShape();
        IconButtonShapes shapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr);
        IconToggleButtonShapes toggleableShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr,
                                                foundation::ShapePtr checkedShape = nullptr);

        extern const ui::Dp ExtraSmallIconSize, SmallIconSize, MediumIconSize, LargeIconSize, ExtraLargeIconSize;
        ui::DpSize extraSmallContainerSize(IconButtonWidthOption width = IconButtonWidthOption::Uniform);
        ui::DpSize smallContainerSize(IconButtonWidthOption width = IconButtonWidthOption::Uniform);
        ui::DpSize mediumContainerSize(IconButtonWidthOption width = IconButtonWidthOption::Uniform);
        ui::DpSize largeContainerSize(IconButtonWidthOption width = IconButtonWidthOption::Uniform);
        ui::DpSize extraLargeContainerSize(IconButtonWidthOption width = IconButtonWidthOption::Uniform);
    }

}
