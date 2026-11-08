#pragma once

// material3 Divider.kt: a 1 dp outlineVariant line across the width (HorizontalDivider) or the
// height (VerticalDivider).

#include "kiln/ui/LayoutNode.h"

#include <optional>

namespace kiln::material3 {

    namespace DividerDefaults {
        inline constexpr ui::Dp Thickness{ 1 };   // DividerTokens.Thickness
        ui::Color color();                        // DividerTokens.Color (outlineVariant)
    }

    void HorizontalDivider(const ui::Modifier& modifier = {}, ui::Dp thickness = DividerDefaults::Thickness,
                           std::optional<ui::Color> color = std::nullopt);
    void VerticalDivider(const ui::Modifier& modifier = {}, ui::Dp thickness = DividerDefaults::Thickness,
                         std::optional<ui::Color> color = std::nullopt);

}
