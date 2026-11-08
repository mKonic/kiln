#pragma once

// material3 Ripple.kt: ripple(), the Indication MaterialTheme provides. Presses show Android's
// sparkle ripple; hover, focus and drag show a state layer (StateTokens opacities) of the
// content colour; focus can show an inset ring instead (RippleDefaults.InsetFocusRingThemeConfiguration).

#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    // material-ripple RippleAlpha: the state layer opacities.
    struct RippleAlpha {
        float draggedAlpha = 0, focusedAlpha = 0, hoveredAlpha = 0, pressedAlpha = 0;
        bool operator==(const RippleAlpha&) const = default;
    };

    // RippleThemeConfiguration: how focus shows, for every ripple in a subtree. Focus.Opacity is
    // a state layer; Focus.InsetRing two strokes inside the focus ring shape.
    struct RippleThemeConfiguration {
        struct InsetRing {
            ui::Dp outerStrokeInset, outerStrokeWidth, innerStrokeInset, innerStrokeWidth;
            bool operator==(const InsetRing&) const = default;
        };
        std::optional<InsetRing> focus;   // nullopt: Focus.Opacity
        bool operator==(const RippleThemeConfiguration&) const = default;
    };

    namespace RippleDefaults {
        // StateTokens: pressed 0.10, focused 0.10, dragged 0.16, hovered 0.08.
        RippleAlpha DefaultRippleAlpha();
        RippleThemeConfiguration OpacityFocusThemeConfiguration();
        // Outer stroke 2 dp at the edge, inner stroke 3 dp inset 1 dp.
        RippleThemeConfiguration InsetFocusRingThemeConfiguration();
        inline RippleThemeConfiguration ThemeConfiguration() { return OpacityFocusThemeConfiguration(); }
    }

    inline rt::Local<RippleThemeConfiguration> LocalRippleThemeConfiguration{ RippleThemeConfiguration{} };

    // RippleConfiguration: per-subtree colour (Unspecified: the content colour), inset ring
    // stroke colours (unset: secondary and onSecondary), and the deprecated alpha overrides.
    struct RippleConfiguration {
        struct InsetRingColors {
            ui::Color outerStrokeColor, innerStrokeColor;
            bool operator==(const InsetRingColors&) const = default;
        };
        ui::Color color = ui::Color::Unspecified();
        std::optional<InsetRingColors> focus;
        std::optional<RippleAlpha> rippleAlpha;
        bool operator==(const RippleConfiguration&) const = default;
    };
    // nullopt turns ripples off in the subtree.
    inline rt::Local<std::optional<RippleConfiguration>> LocalRippleConfiguration{ RippleConfiguration{} };

    // ripple(bounded, radius, color | colorProducer, focusRingShape, enable*Indication).
    struct RippleOptions {
        bool bounded = true;
        ui::Dp radius = ui::Dp::Unspecified();   // Unspecified: from the size
        ui::Color color = ui::Color::Unspecified();
        std::function<ui::Color()> colorProducer;   // read at draw time; overrides color
        foundation::ShapePtr focusRingShape;        // null: rounded by radius, else a rectangle
        bool enablePressIndication = true, enableFocusIndication = true, enableHoverIndication = true,
             enableDragIndication = true;
    };
    foundation::IndicationPtr Ripple(const RippleOptions& options = {});

}
