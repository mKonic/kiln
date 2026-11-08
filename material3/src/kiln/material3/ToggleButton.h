#pragma once

// ToggleButton.kt: four toggleable text-button styles. Size selects the minimum height,
// padding, typography and optional leading icon; press and selection morph the shape.

#include "kiln/material3/Button.h"

namespace kiln::material3 {

    struct ToggleButtonSize {
        ui::Dp height;
        constexpr explicit ToggleButtonSize(ui::Dp height) : height(height) {}
        static const ToggleButtonSize ExtraSmall, Small, Medium, Large, ExtraLarge;
        bool operator==(const ToggleButtonSize&) const = default;
    };
    using ToggleButtonElevation = ButtonElevation;  // identical interaction/elevation state machine

    struct ToggleButtonColors {
        ui::Color containerColor, contentColor, disabledContainerColor, disabledContentColor, checkedContainerColor, checkedContentColor;
        ToggleButtonColors Copy(ui::Color container = ui::Color::Unspecified(), ui::Color content = ui::Color::Unspecified(),
                                ui::Color disabledContainer = ui::Color::Unspecified(), ui::Color disabledContent = ui::Color::Unspecified(),
                                ui::Color checkedContainer = ui::Color::Unspecified(), ui::Color checkedContent = ui::Color::Unspecified()) const;
        ui::Color ContainerColor(bool enabled, bool checked) const {
            return !enabled ? disabledContainerColor : checked ? checkedContainerColor : containerColor;
        }
        ui::Color ContentColor(bool enabled, bool checked) const {
            return !enabled ? disabledContentColor : checked ? checkedContentColor : contentColor;
        }
        bool operator==(const ToggleButtonColors&) const = default;
    };
    struct ToggleButtonShapes {
        foundation::ShapePtr shape, pressedShape, checkedShape;
        ToggleButtonShapes Copy(foundation::ShapePtr rest = nullptr, foundation::ShapePtr pressed = nullptr,
                                foundation::ShapePtr checked = nullptr) const {
            return { rest ? rest : shape, pressed ? pressed : pressedShape, checked ? checked : checkedShape };
        }
    };
    struct ToggleButtonOptions {
        ToggleButtonSize buttonSize = ToggleButtonSize::Small;
        bool enabled = true;
        std::function<void()> icon;
        std::optional<ToggleButtonShapes> shapes;
        std::optional<ToggleButtonColors> colors;
        std::optional<std::optional<ToggleButtonElevation>> elevation;
        std::optional<std::optional<foundation::BorderStroke>> border;
        std::optional<foundation::PaddingValues> contentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void ToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                      const ToggleButtonOptions& options, std::function<void()> content);
    void ElevatedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                              const ToggleButtonOptions& options, std::function<void()> content);
    void FilledTonalToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                 const ToggleButtonOptions& options, std::function<void()> content);
    void OutlinedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                               const ToggleButtonOptions& options, std::function<void()> content);

    namespace ToggleButtonDefaults {
        inline constexpr ToggleButtonSize size{ ui::Dp(40) };
        extern const ui::Dp MinHeight, IconSize, IconSpacing;
        foundation::ShapePtr shape();
        foundation::ShapePtr pressedShape();
        foundation::ShapePtr checkedShape();
        ToggleButtonShapes shapesFor(ui::Dp height);
        inline ToggleButtonShapes shapesFor(ToggleButtonSize size) { return shapesFor(size.height); }
        foundation::PaddingValues contentPaddingFor(ui::Dp height, bool hasStartIcon = false, bool hasEndIcon = false);
        inline foundation::PaddingValues contentPaddingFor(ToggleButtonSize size, bool start = false, bool end = false) {
            return contentPaddingFor(size.height, start, end);
        }
        ToggleButtonColors colors();
        ToggleButtonElevation elevation();
    }
    namespace ElevatedToggleButtonDefaults {
        using ToggleButtonDefaults::size, ToggleButtonDefaults::MinHeight, ToggleButtonDefaults::IconSize, ToggleButtonDefaults::IconSpacing;
        using ToggleButtonDefaults::shape, ToggleButtonDefaults::pressedShape, ToggleButtonDefaults::checkedShape;
        using ToggleButtonDefaults::shapesFor, ToggleButtonDefaults::contentPaddingFor;
        ToggleButtonColors colors();
        ToggleButtonElevation elevation();
    }
    namespace FilledTonalToggleButtonDefaults {
        using ToggleButtonDefaults::size, ToggleButtonDefaults::MinHeight, ToggleButtonDefaults::IconSize, ToggleButtonDefaults::IconSpacing;
        using ToggleButtonDefaults::shape, ToggleButtonDefaults::pressedShape, ToggleButtonDefaults::checkedShape;
        using ToggleButtonDefaults::shapesFor, ToggleButtonDefaults::contentPaddingFor;
        ToggleButtonColors colors();
        ToggleButtonElevation elevation();
    }
    namespace OutlinedToggleButtonDefaults {
        using ToggleButtonDefaults::size, ToggleButtonDefaults::MinHeight, ToggleButtonDefaults::IconSize, ToggleButtonDefaults::IconSpacing;
        using ToggleButtonDefaults::shape, ToggleButtonDefaults::pressedShape, ToggleButtonDefaults::checkedShape;
        using ToggleButtonDefaults::shapesFor, ToggleButtonDefaults::contentPaddingFor;
        ToggleButtonColors colors();
        std::optional<foundation::BorderStroke> border(bool enabled, bool checked);
    }
    namespace internal {
        std::optional<foundation::BorderStroke> AnimateBorderStrokeAsState(const std::optional<foundation::BorderStroke>& border);
    }

}
