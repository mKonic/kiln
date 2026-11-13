#pragma once

// Tab.kt: a tab of a tab row. Text and/or an icon (placed on their baselines, 48 dp tall, 72 dp
// with both), a leading icon before the text, or any content; its colour fades between the
// selected and unselected content colours.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct TabOptions {
        bool enabled = true;
        std::optional<ui::Color> selectedContentColor;     // unset: LocalContentColor (also the ripple's colour)
        std::optional<ui::Color> unselectedContentColor;   // unset: the selected content colour
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    // A tab with a text label and/or an icon (an icon-only tab passes nullptr for the text).
    void Tab(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, kiln::Function<void()> text, kiln::Function<void()> icon,
             const TabOptions& options = {});
    // A tab with an icon (24 dp) before its text label.
    void LeadingIconTab(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> text, kiln::Function<void()> icon,
                        const ui::Modifier& modifier, const TabOptions& options = {});
    // A tab with any content, centred in a column the width of the tab.
    void Tab(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, const TabOptions& options, kiln::Function<void()> content);

    namespace internal {
        inline constexpr ui::Dp HorizontalTextPadding{ 16 };
    }

}
