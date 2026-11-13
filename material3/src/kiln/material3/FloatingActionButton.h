#pragma once

// FloatingActionButton.kt: icon FABs, extended FABs (text-only or expandable icon + text),
// tonal/shadow elevation, and the show/hide modifier.

#include "kiln/animation/Spec.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"

#include <optional>

namespace kiln::material3 {

    struct FloatingActionButtonElevation {
        ui::Dp defaultElevation, pressedElevation, focusedElevation, hoveredElevation;
        ui::Dp TonalElevation() const { return defaultElevation; }
        bool operator==(const FloatingActionButtonElevation&) const = default;
    };
    ui::Dp ShadowElevation(const FloatingActionButtonElevation& elevation, const std::shared_ptr<foundation::MutableInteractionSource>& source);

    struct FloatingActionButtonOptions {
        foundation::ShapePtr shape;
        std::optional<ui::Color> containerColor, contentColor;
        std::optional<FloatingActionButtonElevation> elevation;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void FloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                              kiln::Function<void()> content);
    void SmallFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                   kiln::Function<void()> content);
    void MediumFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                    kiln::Function<void()> content);
    void LargeFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                   kiln::Function<void()> content);

    void ExtendedFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                      kiln::Function<void()> content);
    void SmallExtendedFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                           kiln::Function<void()> content);
    void MediumExtendedFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                            kiln::Function<void()> content);
    void LargeExtendedFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                           kiln::Function<void()> content);

    // The label has cleared semantics: the icon supplies the accessible name, including collapsed.
    void ExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> onClick,
                                      const ui::Modifier& modifier = {}, bool expanded = true, const FloatingActionButtonOptions& options = {});
    void SmallExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> onClick,
                                           const ui::Modifier& modifier = {}, bool expanded = true, const FloatingActionButtonOptions& options = {});
    void MediumExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> onClick,
                                            const ui::Modifier& modifier = {}, bool expanded = true, const FloatingActionButtonOptions& options = {});
    void LargeExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> onClick,
                                           const ui::Modifier& modifier = {}, bool expanded = true, const FloatingActionButtonOptions& options = {});

    namespace FloatingActionButtonDefaults {
        inline constexpr float ShowHideTargetScale = 0.2f;
        extern const ui::Dp MediumIconSize;
        inline constexpr ui::Dp LargeIconSize{ 36 };  // upstream corrects the generated token
        foundation::ShapePtr shape();
        foundation::ShapePtr smallShape();
        foundation::ShapePtr mediumShape();
        foundation::ShapePtr largeShape();
        foundation::ShapePtr extendedFabShape();
        foundation::ShapePtr smallExtendedFabShape();
        foundation::ShapePtr mediumExtendedFabShape();
        foundation::ShapePtr largeExtendedFabShape();
        ui::Color containerColor();
        FloatingActionButtonElevation elevation();
        FloatingActionButtonElevation loweredElevation();
        FloatingActionButtonElevation bottomAppBarFabElevation();
    }

    ui::Modifier AnimateFloatingActionButton(ui::Modifier modifier, bool visible, ui::Alignment alignment,
                                             float targetScale = FloatingActionButtonDefaults::ShowHideTargetScale,
                                             anim::SpecPtr scaleAnimationSpec = nullptr, anim::SpecPtr alphaAnimationSpec = nullptr);

}
