#pragma once

// material3 Badge.kt: Badge (a 6 dp error-coloured dot, or a 16 dp pill around short content in
// labelSmall) and BadgedBox (a badge over the anchor's top end corner, kept inside the bounds
// badgeBounds marks, as navigation items do).

#include "kiln/ui/LayoutNode.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    namespace BadgeDefaults {
        ui::Color containerColor();   // BadgeTokens.Color (error)
    }

    void BadgedBox(std::function<void()> badge, const ui::Modifier& modifier, std::function<void()> content);
    // content: shown in a Row, centred; null draws the small dot.
    void Badge(const ui::Modifier& modifier = {}, std::optional<ui::Color> containerColor = std::nullopt,
               std::optional<ui::Color> contentColor = std::nullopt, std::function<void()> content = {});

    namespace internal {
        // BadgeTopRuler / BadgeEndRuler, provided by badgeBounds: the top and end edge a badge
        // may reach.
        const ui::HorizontalRuler& BadgeTopRuler();
        const ui::VerticalRuler& BadgeEndRuler();
        ui::Modifier BadgeBounds(ui::Modifier m);
        inline constexpr ui::Dp BadgeWithContentHorizontalPadding{ 4 }, BadgeWithContentHorizontalOffset{ 12 },
            BadgeWithContentVerticalOffset{ 14 }, BadgeOffset{ 6 };
    }

}
