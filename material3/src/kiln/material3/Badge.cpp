#include "kiln/material3/Badge.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/tokens/BadgeTokens.h"

#include <cmath>

namespace kiln::material3 {

    using namespace tokens;

    ui::Color BadgeDefaults::containerColor() { return Value(BadgeTokens::Color); }

    namespace internal {
        const ui::HorizontalRuler& BadgeTopRuler() { static const ui::HorizontalRuler r; return r; }
        const ui::VerticalRuler& BadgeEndRuler() { static const ui::VerticalRuler r; return r; }

        namespace {
            struct BoundsParams { bool operator==(const BoundsParams&) const = default; };
            struct BoundsNode final : ui::ModifierNode, ui::LayoutModifier {
                BoundsParams params;
                explicit BoundsNode(BoundsParams p) : params(p) {}
                ui::LayoutModifier* AsLayout() override { return this; }
                void OnParamsChanged() {}
                ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                    ui::Placeable& p = m.Measure(c);
                    const int w = p.Width();
                    return s.Layout(p.Width(), p.Height(), {}, [w](ui::RulerScope& r) {
                        r.Provides(BadgeEndRuler(), float(w));
                        r.Provides(BadgeTopRuler(), 0);
                    }, [&p](ui::PlacementScope& ps) { ps.Place(p, 0, 0); });
                }
            };
        }
        ui::Modifier BadgeBounds(ui::Modifier m) { return m.With<BoundsNode>(BoundsParams{}); }
    }

    namespace {
        class BadgedBoxPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                // children: the anchor, then the badge (layoutId "anchor" / "badge").
                ui::Constraints badgeConstraints = c;
                badgeConstraints.minHeight = 0;
                ui::Placeable& badge = children[1]->Measure(badgeConstraints);
                ui::Placeable& anchor = children[0]->Measure(c);
                const int firstBaseline = anchor.Get(ui::FirstBaseline), lastBaseline = anchor.Get(ui::LastBaseline);
                const int w = anchor.Width(), h = anchor.Height();
                const bool hasContent = badge.Width() > s.RoundToPx(BadgeTokens::Size);
                const int hOffset = s.RoundToPx(hasContent ? internal::BadgeWithContentHorizontalOffset : internal::BadgeOffset);
                const int vOffset = s.RoundToPx(hasContent ? internal::BadgeWithContentVerticalOffset : internal::BadgeOffset);
                return s.Layout(w, h, [&anchor, &badge, w, hOffset, vOffset](ui::PlacementScope& p) {
                    p.PlaceRelative(anchor, 0, 0);
                    const float end = p.Current(internal::BadgeEndRuler(), INFINITY);
                    const float top = p.Current(internal::BadgeTopRuler(), -INFINITY);
                    const int badgeX = std::isinf(end) ? w - hOffset : std::min(w - hOffset, int(end) - badge.Width());
                    const int badgeY = std::isinf(top) ? -badge.Height() + vOffset : std::max(-badge.Height() + vOffset, int(top));
                    p.PlaceRelative(badge, badgeX, badgeY);
                }, { { &ui::FirstBaseline, firstBaseline }, { &ui::LastBaseline, lastBaseline } });
            }
        };
    }

    void BadgedBox(std::function<void()> badge, const ui::Modifier& modifier, std::function<void()> content) {
        static const auto policy = std::make_shared<BadgedBoxPolicy>();
        foundation::Layout(modifier, policy, [badge = std::move(badge), content = std::move(content)] {
            foundation::Box({}, foundation::Alignment::Center(), false, content);
            foundation::Box({}, foundation::Alignment::TopStart(), false, badge);
        });
    }

    void Badge(const ui::Modifier& modifier, std::optional<ui::Color> containerColor, std::optional<ui::Color> contentColor, std::function<void()> content) {
        const ui::Color container = containerColor ? *containerColor : BadgeDefaults::containerColor();
        const ui::Color contentC = contentColor ? *contentColor : ContentColorFor(container);
        const bool has = bool(content);
        const ui::Dp size = has ? BadgeTokens::LargeSize : BadgeTokens::Size;
        ui::Modifier m = foundation::Background(foundation::DefaultMinSize(modifier, size, size), container,
                                                Value(has ? BadgeTokens::LargeShape : BadgeTokens::Shape));
        if (has) m = foundation::Padding(std::move(m), internal::BadgeWithContentHorizontalPadding, ui::Dp(0));
        foundation::Row(m, foundation::Arrangement::Center(), foundation::AlignmentV::CenterVertically(), [has, contentC, content] {
            if (has) internal::ProvideContentColorTextStyle(contentC, Value(BadgeTokens::LargeLabelTextFont), content);
        });
    }

}
