#include "kiln/material3/Slider.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Hover.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/SliderTokens.h"

namespace kiln::material3 {
    using namespace ui;
    using namespace foundation;
    SliderColors SliderColors::Copy(const SliderColors& o) const {
        const auto use = [](Color override, Color base) { return override.Specified() ? override : base; };
        return { use(o.thumbColor, thumbColor), use(o.activeTrackColor, activeTrackColor), use(o.activeTickColor, activeTickColor),
            use(o.inactiveTrackColor, inactiveTrackColor), use(o.inactiveTickColor, inactiveTickColor), use(o.disabledThumbColor, disabledThumbColor),
            use(o.disabledActiveTrackColor, disabledActiveTrackColor), use(o.disabledActiveTickColor, disabledActiveTickColor),
            use(o.disabledInactiveTrackColor, disabledInactiveTrackColor), use(o.disabledInactiveTickColor, disabledInactiveTickColor) };
    }
    Color SliderColors::ThumbColor(bool enabled) const { return enabled ? thumbColor : disabledThumbColor; }
    Color SliderColors::TrackColor(bool enabled, bool active) const { return enabled ? (active ? activeTrackColor : inactiveTrackColor) : (active ? disabledActiveTrackColor : disabledInactiveTrackColor); }
    Color SliderColors::TickColor(bool enabled, bool active) const { return enabled ? (active ? activeTickColor : inactiveTickColor) : (active ? disabledActiveTickColor : disabledInactiveTickColor); }

    namespace SliderDefaults {
        SliderColors colors() {
            using namespace tokens::SliderTokens;
            const auto active = Value(ActiveTrackColor), inactive = Value(InactiveTrackColor);
            const auto disabledActive = Value(DisabledActiveTrackColor).WithAlpha(DisabledActiveTrackOpacity);
            const auto disabledInactive = Value(DisabledInactiveTrackColor).WithAlpha(DisabledInactiveTrackOpacity);
            return { Value(HandleColor), active, inactive, inactive, active,
                CompositeOver(Value(DisabledHandleColor).WithAlpha(DisabledHandleOpacity), MaterialTheme::colorScheme().surface),
                disabledActive, disabledInactive, disabledInactive, disabledActive };
        }
        SliderColors colors(const SliderColors& overrides) { return colors().Copy(overrides); }
        void Thumb(std::shared_ptr<MutableInteractionSource> source, const Modifier& modifier, const SliderThumbOptions& o) {
            const auto palette = o.colors.value_or(colors());
            const DpSize original = o.thumbSize.value_or(o.isVertical ? DpSize{ ThumbHeight, ThumbWidth } : DpSize{ ThumbWidth, ThumbHeight });
            auto active = original;
            if (source && (source->IsPressed() || source->IsFocused() || source->IsDragged())) {
                if (o.isVertical) active.height = Dp(active.height.value / 2);
                else active.width = Dp(active.width.value / 2);
            }
            auto m = SizeMod(modifier, original.width, original.height);
            if (source) m = Hoverable(m, source);
            m = PointerHoverIcon(m, PointerIcon::Hand);
            Box(m, Alignment::Center(), false, [=] {
                Spacer(Background(SizeMod({}, active.width, active.height), palette.ThumbColor(o.enabled), Value(tokens::SliderTokens::HandleShape)));
            });
        }
        void DrawStopIndicator(DrawScope& s, Offset offset, Dp size, Color color) {
            const float radius = s.ToPx(size) / 2;
            gfx::Path dot;
            dot.AddOval({ offset.x - radius, offset.y - radius, offset.x + radius, offset.y + radius });
            s.DrawPath(dot, color);
        }
    }
}
