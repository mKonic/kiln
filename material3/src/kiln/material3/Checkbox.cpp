#include "kiln/material3/Checkbox.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/Transition.h"
#include "kiln/graphics/PathMeasure.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/tokens/CheckboxTokens.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

#include <cmath>

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Take(Color color, Color fallback) { return color.Specified() ? color : fallback; }
        struct CheckDrawingCache { gfx::Path check, segment; gfx::PathMeasure measure; };
        void DrawBox(ui::DrawScope& s, Color box, Color border, float radius, gfx::Stroke stroke) {
            const float size = s.GetSize().width, half = stroke.width / 2;
            if (box == border) s.DrawRoundRect(box, {}, { size, size }, radius);
            else {
                s.DrawRoundRect(box, { stroke.width, stroke.width }, { size - 2 * stroke.width, size - 2 * stroke.width }, std::max(0.0f, radius - stroke.width));
                gfx::Path outline;
                outline.AddRoundRect(gfx::RoundRect::Uniform({ half, half, size - half, size - half }, radius - half));
                stroke.join = gfx::StrokeJoin::Miter;  // drawRoundRect ignores StrokeJoin
                s.DrawPath(outline, border, &stroke);
            }
        }
    }

    CheckboxColors CheckboxColors::Copy(const CheckboxColors& o) const {
        return { Take(o.checkedCheckmarkColor, checkedCheckmarkColor), Take(o.uncheckedCheckmarkColor, uncheckedCheckmarkColor),
                 Take(o.checkedBoxColor, checkedBoxColor), Take(o.uncheckedBoxColor, uncheckedBoxColor),
                 Take(o.disabledCheckedBoxColor, disabledCheckedBoxColor), Take(o.disabledUncheckedBoxColor, disabledUncheckedBoxColor),
                 Take(o.disabledIndeterminateBoxColor, disabledIndeterminateBoxColor), Take(o.checkedBorderColor, checkedBorderColor),
                 Take(o.uncheckedBorderColor, uncheckedBorderColor), Take(o.disabledBorderColor, disabledBorderColor),
                 Take(o.disabledUncheckedBorderColor, disabledUncheckedBorderColor), Take(o.disabledIndeterminateBorderColor, disabledIndeterminateBorderColor),
                 Take(o.disabledCheckmarkColor, disabledCheckmarkColor) };
    }
    Color CheckboxColors::CheckmarkColor(bool enabled, ToggleableState state) const {
        return !enabled ? disabledCheckmarkColor : state == ToggleableState::Off ? uncheckedCheckmarkColor : checkedCheckmarkColor;
    }
    Color CheckboxColors::IndicatorColor(ToggleableState state) const { return state == ToggleableState::Off ? uncheckedBoxColor : checkedBoxColor; }
    Color CheckboxColors::BoxColor(bool enabled, ToggleableState state) const {
        if (enabled) return state == ToggleableState::Off ? uncheckedBoxColor : checkedBoxColor;
        return state == ToggleableState::On ? disabledCheckedBoxColor : state == ToggleableState::Off ? disabledUncheckedBoxColor : disabledIndeterminateBoxColor;
    }
    Color CheckboxColors::BorderColor(bool enabled, ToggleableState state) const {
        if (enabled) return state == ToggleableState::Off ? uncheckedBorderColor : checkedBorderColor;
        return state == ToggleableState::On ? disabledBorderColor : state == ToggleableState::Off ? disabledUncheckedBorderColor : disabledIndeterminateBorderColor;
    }
    namespace CheckboxDefaults {
        CheckboxColors colors() {
            const auto container = Value(CheckboxTokens::SelectedContainerColor);
            const auto disabled = Value(CheckboxTokens::SelectedDisabledContainerColor).WithAlpha(CheckboxTokens::SelectedDisabledContainerOpacity);
            return { Value(CheckboxTokens::SelectedIconColor), Color::Transparent(), container, Color::Transparent(), disabled, Color::Transparent(), disabled,
                     container, Value(CheckboxTokens::UnselectedOutlineColor), disabled,
                     Value(CheckboxTokens::UnselectedDisabledOutlineColor).WithAlpha(CheckboxTokens::UnselectedDisabledContainerOpacity), disabled,
                     Value(CheckboxTokens::SelectedDisabledIconColor) };
        }
        CheckboxColors colors(const CheckboxColors& overrides) { return colors().Copy(overrides); }
        CheckboxColors colors(Color checked, Color unchecked, Color checkmark, Color disabledChecked, Color disabledUnchecked, Color disabledIndeterminate) {
            return colors().Copy({ checkmark, Color::Transparent(), checked, Color::Transparent(), disabledChecked, Color::Transparent(), disabledIndeterminate,
                                   checked, unchecked, disabledChecked, disabledUnchecked, disabledIndeterminate, checkmark });
        }
    }

    void Checkbox(bool checked, std::function<void(bool)> change, const ui::Modifier& modifier, const CheckboxOptions& options) {
        std::function<void()> click;
        if (change) click = [change = std::move(change), checked] { change(!checked); };
        TriStateCheckbox(checked ? ToggleableState::On : ToggleableState::Off, std::move(click), modifier, options);
    }
    void TriStateCheckbox(ToggleableState state, std::function<void()> click, const ui::Modifier& modifier, const CheckboxOptions& options) {
        const bool fixed = ComposeMaterial3Flags::isCheckboxStylingFixEnabled;
        const auto colors = options.colors.value_or(CheckboxDefaults::colors());
        auto m = modifier;
        if (click) {
            ClickableOptions toggle;
            toggle.enabled = options.enabled;
            toggle.role = ui::Role::Checkbox;
            toggle.interactionSource = options.interactionSource;
            RippleOptions ripple;
            ripple.bounded = false;
            ripple.radius = Dp(CheckboxTokens::StateLayerSize.value / 2);
            ripple.color = fixed ? colors.IndicatorColor(state) : Color::Unspecified();
            ripple.focusRingShape = RoundedCornerPercent(25);
            toggle.indication = Ripple(ripple);
            toggle.useLocalIndication = false;
            m = TriStateToggleable(MinimumInteractiveComponentSize(m), int(state), std::move(click), toggle);
        }
        if (!fixed) m = Padding(m, Dp(2));
        const Dp size = fixed ? CheckboxTokens::ContainerSize : Dp(20);
        m = RequiredSize(WrapContent(m, Axis::Both), size, size);
        auto& transition = UpdateTransition<ToggleableState>(state);
        const auto spatial = Value(MotionSchemeKeyTokens::DefaultSpatial);
        const float draw = AnimateFloat<ToggleableState>(transition, [](auto value) { return value == ToggleableState::Off ? 0.0f : 1.0f; }, [spatial](const auto& segment) {
            return segment.initialState == ToggleableState::Off ? spatial : segment.targetState == ToggleableState::Off ? anim::Snap(100) : spatial;
        });
        const float shift = AnimateFloat<ToggleableState>(transition, [](auto value) { return value == ToggleableState::Indeterminate ? 1.0f : 0.0f; }, [spatial](const auto& segment) {
            return segment.initialState == ToggleableState::Off ? anim::Snap() : segment.targetState == ToggleableState::Off ? anim::Snap(100) : spatial;
        });
        auto& c = *rt::Composition::Current();
        const auto cache = c.Remember<std::shared_ptr<CheckDrawingCache>>([] { return std::make_shared<CheckDrawingCache>(); });
        const auto effect = Value(state == ToggleableState::Off ? MotionSchemeKeyTokens::FastEffects : MotionSchemeKeyTokens::DefaultEffects);
        const auto checkmark = AnimateColorAsState(colors.CheckmarkColor(fixed ? options.enabled : true, state), effect);
        const auto boxTarget = colors.BoxColor(options.enabled, state), borderTarget = colors.BorderColor(options.enabled, state);
        const auto box = options.enabled ? AnimateColorAsState(boxTarget, effect) : boxTarget;
        const auto border = options.enabled ? AnimateColorAsState(borderTarget, effect) : borderTarget;
        Canvas(m, [=](ui::DrawScope& s) {
            const float strokeWidth = std::floor(s.ToPx(CheckboxDefaults::StrokeWidth));
            gfx::Stroke outline = options.outlineStroke.value_or(gfx::Stroke{ strokeWidth });
            gfx::Stroke check = options.checkmarkStroke.value_or(gfx::Stroke{ strokeWidth, 4, gfx::StrokeCap::Square });
            DrawBox(s, box, border, s.ToPx(Dp(2)), outline);
            const float width = s.GetSize().width;
            const float crossX = 0.4f + 0.1f * shift;
            const float crossY = (fixed ? 0.65f : 0.7f) * (1 - shift) + 0.5f * shift;
            const float rightY = 0.3f + 0.2f * shift;
            cache->check.Reset(); cache->segment.Reset();
            cache->check.MoveTo(width * (fixed ? 0.25f : 0.2f), width * 0.5f);
            cache->check.LineTo(width * crossX, width * crossY);
            cache->check.LineTo(width * (fixed ? 0.75f : 0.8f), width * rightY);
            cache->measure.SetPath(&cache->check, false);
            cache->measure.GetSegment(0, cache->measure.Length() * draw, cache->segment, true);
            s.DrawPath(cache->segment, checkmark, &check);
        });
    }
}
