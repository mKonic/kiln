#include "kiln/material3/SegmentedButton.h"

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/OutlinedSegmentedButtonTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    SegmentedButtonColors SegmentedButtonColors::Copy(const SegmentedButtonColors& o) const {
        auto result = *this;
#define COLOR(field) if (o.field.Specified()) result.field = o.field;
        COLOR(activeContainerColor) COLOR(activeContentColor) COLOR(activeBorderColor)
        COLOR(inactiveContainerColor) COLOR(inactiveContentColor) COLOR(inactiveBorderColor)
        COLOR(disabledActiveContainerColor) COLOR(disabledActiveContentColor) COLOR(disabledActiveBorderColor)
        COLOR(disabledInactiveContainerColor) COLOR(disabledInactiveContentColor) COLOR(disabledInactiveBorderColor)
#undef COLOR
        return result;
    }
    Color SegmentedButtonColors::ContainerColor(bool enabled, bool active) const {
        return enabled ? (active ? activeContainerColor : inactiveContainerColor) : (active ? disabledActiveContainerColor : disabledInactiveContainerColor);
    }
    Color SegmentedButtonColors::ContentColor(bool enabled, bool active) const {
        return enabled ? (active ? activeContentColor : inactiveContentColor) : (active ? disabledActiveContentColor : disabledInactiveContentColor);
    }
    Color SegmentedButtonColors::BorderColor(bool enabled, bool active) const {
        return enabled ? (active ? activeBorderColor : inactiveBorderColor) : (active ? disabledActiveBorderColor : disabledInactiveBorderColor);
    }

    namespace SegmentedButtonDefaults {
        const PaddingValues ContentPadding{ Dp(12), Dp(8), Dp(12), Dp(8) };
        SegmentedButtonColors colors() {
            using namespace OutlinedSegmentedButtonTokens;
            const auto container = Value(SelectedContainerColor), content = Value(SelectedLabelTextColor), outline = Value(OutlineColor);
            const auto disabledContent = Value(DisabledLabelTextColor).WithAlpha(DisabledLabelTextOpacity);
            const auto disabledBorder = outline.WithAlpha(DisabledOutlineOpacity);
            return { container, content, outline, Color::Transparent(), Value(UnselectedLabelTextColor), outline,
                     container, disabledContent, disabledBorder, Color::Transparent(), disabledContent, disabledBorder };
        }
        SegmentedButtonColors colors(const SegmentedButtonColors& overrides) { return colors().Copy(overrides); }
        std::shared_ptr<const CornerBasedShape> baseShape() {
            return std::dynamic_pointer_cast<const CornerBasedShape>(Value(OutlinedSegmentedButtonTokens::Shape));
        }
        ShapePtr itemShape(int index, int count, std::shared_ptr<const CornerBasedShape> base) {
            if (!base) base = baseShape();
            if (count == 1) return base;
            if (index == 0) return base->Copy(base->topStart, {}, {}, base->bottomStart);
            if (index == count - 1) return base->Copy({}, base->topEnd, base->bottomEnd, {});
            return RectangleShape();
        }
        BorderStroke borderStroke(Color color, Dp width) { return { width, color }; }
        void ActiveIcon() { material3::Icon(internal::Icons::Filled::Check(), {}, SizeMod({}, IconSize, IconSize)); }
        void Icon(bool active, std::function<void()> activeContent, std::function<void()> inactiveContent) {
            const auto effects = Value(MotionSchemeKeyTokens::DefaultEffects);
            if (!inactiveContent) {
                AnimatedVisibility(active, {}, std::move(activeContent),
                                   FadeIn(effects) + ScaleIn(Value(MotionSchemeKeyTokens::FastSpatial), 0, 0, 1), {});
            } else {
                Crossfade(rt::Key(active ? 1 : 0), {}, [=](rt::Key target) { if (target == rt::Key(1)) activeContent(); else inactiveContent(); }, effects);
            }
        }
    }
}
