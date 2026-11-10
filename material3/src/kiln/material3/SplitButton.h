#pragma once

// SplitButton.kt: a leading action button and a trailing (usually menu) button laid out as one
// control. The inner corners morph on press, and a checked trailing button becomes a circle.

#include "kiln/material3/Button.h"

namespace kiln::material3 {

    // SplitButtonShapes: null pressed / checked shapes fall back to `shape`.
    struct SplitButtonShapes {
        foundation::ShapePtr shape, pressedShape, checkedShape;
    };

    struct SplitButtonOptions {
        bool enabled = true;
        std::optional<SplitButtonShapes> shapes;                         // unset: the small size's
        std::optional<ButtonColors> colors;                              // unset: the variant's
        std::optional<std::optional<ButtonElevation>> elevation;         // unset: the variant's; nullopt inside: none
        std::optional<std::optional<foundation::BorderStroke>> border;   // unset: the variant's
        std::optional<PaddingValues> contentPadding;                     // unset: the small size's
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    namespace SplitButtonDefaults {
        extern const Dp LeadingIconSize, TrailingIconSize, Spacing;
        extern const foundation::CornerSize ExtraSmallInnerCornerSize, SmallInnerCornerSize, MediumInnerCornerSize, LargeInnerCornerSize,
            ExtraLargeInnerCornerSize;
        extern const foundation::CornerSize ExtraSmallInnerCornerSizePressed, SmallInnerCornerSizePressed, MediumInnerCornerSizePressed,
            LargeInnerCornerSizePressed, ExtraLargeInnerCornerSizePressed;
        foundation::CornerSize OuterCornerSize();
        extern const PaddingValues ExtraSmallLeadingButtonContentPadding, SmallLeadingButtonContentPadding, MediumLeadingButtonContentPadding,
            LargeLeadingButtonContentPadding, ExtraLargeLeadingButtonContentPadding;
        extern const PaddingValues ExtraSmallTrailingButtonContentPadding, SmallTrailingButtonContentPadding, MediumTrailingButtonContentPadding,
            LargeTrailingButtonContentPadding, ExtraLargeTrailingButtonContentPadding;
        extern const Dp ExtraSmallTrailingButtonIconSize, SmallTrailingButtonIconSize, MediumTrailingButtonIconSize, LargeTrailingButtonIconSize,
            ExtraLargeTrailingButtonIconSize;
        extern const Dp ExtraSmallContainerHeight, SmallContainerHeight, MediumContainerHeight, LargeContainerHeight, ExtraLargeContainerHeight;

        SplitButtonShapes leadingButtonShapesFor(Dp buttonHeight);
        SplitButtonShapes trailingButtonShapesFor(Dp buttonHeight);
        PaddingValues leadingButtonContentPaddingFor(Dp buttonHeight);
        PaddingValues trailingButtonContentPaddingFor(Dp buttonHeight);
        Dp leadingButtonIconSizeFor(Dp buttonHeight);
        Dp trailingButtonIconSizeFor(Dp buttonHeight);

        using Content = std::function<void()>;
        void LeadingButton(Content onClick, const ui::Modifier& modifier, const SplitButtonOptions& options, Content content);
        void TrailingButton(Content onClick, const ui::Modifier& modifier, const SplitButtonOptions& options, Content content);
        void TrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier, const SplitButtonOptions& options,
                            Content content);
        void TonalLeadingButton(Content onClick, const ui::Modifier& modifier, const SplitButtonOptions& options, Content content);
        void TonalTrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                 const SplitButtonOptions& options, Content content);
        void OutlinedLeadingButton(Content onClick, const ui::Modifier& modifier, const SplitButtonOptions& options, Content content);
        void OutlinedTrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                    const SplitButtonOptions& options, Content content);
        void ElevatedLeadingButton(Content onClick, const ui::Modifier& modifier, const SplitButtonOptions& options, Content content);
        void ElevatedTrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                    const SplitButtonOptions& options, Content content);
    }

    // The trailing button is measured first so it keeps its width; both get the taller one's
    // height. The minimum touch size applies to the whole layout, not to each button.
    void SplitButtonLayout(std::function<void()> leadingButton, std::function<void()> trailingButton, const ui::Modifier& modifier = {},
                           Dp spacing = SplitButtonDefaults::Spacing);

}
