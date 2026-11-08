#include "kiln/material3/Divider.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/DividerTokens.h"

namespace kiln::material3 {

    ui::Color DividerDefaults::color() { return Value(tokens::DividerTokens::Color); }

    // drawLine with the thickness as stroke width (butt caps): a rect centred on the line.
    void HorizontalDivider(const ui::Modifier& modifier, ui::Dp thickness, std::optional<ui::Color> color) {
        const ui::Color c = color ? *color : DividerDefaults::color();
        foundation::Canvas(foundation::Height(foundation::FillMax(modifier, foundation::Axis::Horizontal), thickness), [c, thickness](ui::DrawScope& s) {
            const float t = s.ToPx(thickness);
            const float stroke = t == 0 ? 1.0f : t;  // Dp.Hairline: one physical pixel, centred on y = 0.
            s.DrawRect(c, { 0, (t - stroke) / 2 }, { s.GetSize().width, stroke });
        });
    }

    void VerticalDivider(const ui::Modifier& modifier, ui::Dp thickness, std::optional<ui::Color> color) {
        const ui::Color c = color ? *color : DividerDefaults::color();
        foundation::Canvas(foundation::Width(foundation::FillMax(modifier, foundation::Axis::Vertical), thickness), [c, thickness](ui::DrawScope& s) {
            const float t = s.ToPx(thickness);
            const float stroke = t == 0 ? 1.0f : t;
            s.DrawRect(c, { (t - stroke) / 2, 0 }, { stroke, s.GetSize().height });
        });
    }

}
