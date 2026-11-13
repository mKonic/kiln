#include "kiln/material3/RadioButton.h"
#include <kiln/Function.h>

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/RadioButtonTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Dp;
    using ui::Color;

    RadioButtonColors RadioButtonColors::Copy(Color selected, Color unselected, Color disabledSelected, Color disabledUnselected) const {
        return { selected.Specified() ? selected : selectedColor, unselected.Specified() ? unselected : unselectedColor,
                 disabledSelected.Specified() ? disabledSelected : disabledSelectedColor, disabledUnselected.Specified() ? disabledUnselected : disabledUnselectedColor };
    }
    Color RadioButtonColors::RadioColor(bool enabled, bool selected) const {
        return enabled ? (selected ? selectedColor : unselectedColor) : (selected ? disabledSelectedColor : disabledUnselectedColor);
    }
    namespace RadioButtonDefaults {
        RadioButtonColors colors() {
            return { Value(RadioButtonTokens::SelectedIconColor), Value(RadioButtonTokens::UnselectedIconColor),
                     Value(RadioButtonTokens::DisabledSelectedIconColor).WithAlpha(RadioButtonTokens::DisabledSelectedIconOpacity),
                     Value(RadioButtonTokens::DisabledUnselectedIconColor).WithAlpha(RadioButtonTokens::DisabledUnselectedIconOpacity) };
        }
    }
    void RadioButton(bool selected, kiln::Function<void()> click, const ui::Modifier& modifier, const RadioButtonOptions& options) {
        const auto colors = options.colors.value_or(RadioButtonDefaults::colors());
        const auto radius = AnimateDpAsState(selected ? Dp(6) : Dp(0), Value(MotionSchemeKeyTokens::FastSpatial));
        const auto targetColor = colors.RadioColor(options.enabled, selected);
        const auto color = options.enabled ? AnimateColorAsState(targetColor, Value(MotionSchemeKeyTokens::DefaultEffects)) : targetColor;
        auto m = modifier;
        if (click) {
            ClickableOptions selectable;
            selectable.enabled = options.enabled;
            selectable.role = ui::Role::RadioButton;
            selectable.interactionSource = options.interactionSource;
            RippleOptions ripple;
            ripple.bounded = false;
            ripple.radius = Dp(RadioButtonTokens::StateLayerSize.value / 2);
            ripple.focusRingShape = CircleShape();
            selectable.indication = Ripple(ripple);
            selectable.useLocalIndication = false;
            m = Selectable(MinimumInteractiveComponentSize(m), selected, std::move(click), selectable);
        }
        m = RequiredSize(Padding(WrapContent(m, Axis::Both), Dp(2)), RadioButtonTokens::IconSize, RadioButtonTokens::IconSize);
        Canvas(m, [=](ui::DrawScope& s) {
            const auto size = s.GetSize();
            const float stroke = s.ToPx(Dp(2)), x = size.width / 2, y = size.height / 2;
            const float outer = s.ToPx(RadioButtonTokens::IconSize) / 2 - stroke / 2;
            gfx::Path ring;
            ring.AddOval({ x - outer, y - outer, x + outer, y + outer });
            const gfx::Stroke style{ stroke };
            s.DrawPath(ring, color, &style);
            if (radius.value > 0) {
                const float inner = s.ToPx(radius) - stroke / 2;
                if (inner > 0) {
                    gfx::Path dot;
                    dot.AddOval({ x - inner, y - inner, x + inner, y + inner });
                    s.DrawPath(dot, color);
                }
            }
        });
    }
}
