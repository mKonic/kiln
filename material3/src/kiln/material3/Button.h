#pragma once

// material3 Button.kt: Button, ElevatedButton, FilledTonalButton, OutlinedButton and TextButton
// on a clickable Surface with labelLarge content in a centred Row (at least 58 x 40 dp). The
// Expressive forms take ButtonShapes and morph to the pressed shape while pressed. Shadow
// elevation animates with the interactions (internal animateElevation).

#include "kiln/animation/Spec.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/text/TextStyle.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    using foundation::PaddingValues;
    using ui::Color;
    using ui::Dp;

    // ButtonColors; Copy keeps this colour where the argument is Unspecified.
    struct ButtonColors {
        Color containerColor, contentColor, disabledContainerColor, disabledContentColor;
        ButtonColors Copy(Color container = Color::Unspecified(), Color content = Color::Unspecified(),
                          Color disabledContainer = Color::Unspecified(), Color disabledContent = Color::Unspecified()) const;
        Color ContainerColor(bool enabled) const { return enabled ? containerColor : disabledContainerColor; }
        Color ContentColor(bool enabled) const { return enabled ? contentColor : disabledContentColor; }
        bool operator==(const ButtonColors&) const = default;
    };

    // ButtonElevation: the shadow for each interaction, animated by internal animateElevation.
    struct ButtonElevation {
        Dp defaultElevation, pressedElevation, focusedElevation, hoveredElevation, disabledElevation;
        bool operator==(const ButtonElevation&) const = default;
    };
    // ButtonElevation.shadowElevation(enabled, interactionSource), read in composition.
    Dp ShadowElevation(const ButtonElevation& elevation, bool enabled, const std::shared_ptr<foundation::MutableInteractionSource>& source);

    // ButtonShapes: the shape at rest and while pressed.
    struct ButtonShapes {
        foundation::ShapePtr shape, pressedShape;
        ButtonShapes Copy(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr) const {
            return { shape ? shape : this->shape, pressedShape ? pressedShape : this->pressedShape };
        }
    };

    struct ButtonOptions {
        bool enabled = true;
        foundation::ShapePtr shape;                     // the plain form; null: the variant's default
        std::optional<ButtonShapes> shapes;             // the Expressive form (morphs while pressed)
        std::optional<ButtonColors> colors;             // unset: the variant's colours
        std::optional<std::optional<ButtonElevation>> elevation;   // unset: the variant's; nullopt inside: none
        std::optional<std::optional<foundation::BorderStroke>> border;   // unset: the variant's
        std::optional<PaddingValues> contentPadding;    // unset: ContentPadding (contentPaddingFor(MinHeight) with shapes)
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void Button(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& options, std::function<void()> content);
    void ElevatedButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& options, std::function<void()> content);
    void FilledTonalButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& options, std::function<void()> content);
    void OutlinedButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& options, std::function<void()> content);
    void TextButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& options, std::function<void()> content);

    namespace ButtonDefaults {
        extern const PaddingValues ContentPadding, ButtonWithIconContentPadding, TextButtonContentPadding, TextButtonWithIconContentPadding;
        PaddingValues SmallContentPadding();
        PaddingValues ExtraSmallContentPadding();
        PaddingValues MediumContentPadding();
        PaddingValues LargeContentPadding();
        PaddingValues ExtraLargeContentPadding();
        inline constexpr Dp MinWidth{ 58 };
        Dp MinHeight();   // 40 dp (36 dp with precision pointer sizing)
        extern const Dp ExtraSmallContainerHeight, MediumContainerHeight, LargeContainerHeight, ExtraLargeContainerHeight;
        inline constexpr Dp IconSize{ 18 };
        extern const Dp ExtraSmallIconSize, SmallIconSize, MediumIconSize, LargeIconSize, ExtraLargeIconSize;
        extern const Dp IconSpacing, ExtraSmallIconSpacing, MediumIconSpacing, LargeIconSpacing, ExtraLargeIconSpacing;

        foundation::ShapePtr squareShape();
        foundation::ShapePtr pressedShape();
        foundation::ShapePtr extraSmallPressedShape();
        foundation::ShapePtr mediumPressedShape();
        foundation::ShapePtr largePressedShape();
        foundation::ShapePtr extraLargePressedShape();
        foundation::ShapePtr shape();
        foundation::ShapePtr elevatedShape();
        foundation::ShapePtr filledTonalShape();
        foundation::ShapePtr outlinedShape();
        foundation::ShapePtr textShape();
        ButtonShapes shapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr);

        ButtonColors buttonColors();
        ButtonColors elevatedButtonColors();
        ButtonColors filledTonalButtonColors();
        ButtonColors outlinedButtonColors();
        ButtonColors textButtonColors();

        ButtonElevation buttonElevation();
        ButtonElevation elevatedButtonElevation();
        ButtonElevation filledTonalButtonElevation();
        foundation::BorderStroke outlinedButtonBorder(bool enabled = true);

        // The Expressive sizes: shapes, padding, icon size, icon spacing and text style for a
        // container height.
        ButtonShapes shapesFor(Dp buttonHeight);
        PaddingValues contentPaddingFor(Dp buttonHeight, bool hasStartIcon = false, bool hasEndIcon = false);
        Dp iconSizeFor(Dp buttonHeight);
        Dp iconSpacingFor(Dp buttonHeight);
        text::TextStyle textStyleFor(Dp buttonHeight);
    }

    namespace internal {
        // shapeByInteraction: the pressed or resting shape, morphing when both are corner based.
        foundation::ShapePtr ShapeByInteraction(const ButtonShapes& shapes, bool pressed, anim::SpecPtr spec);
    }

}
