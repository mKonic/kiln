#include "kiln/material3/internal/ListItemLayout.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/ListTokens.h"

namespace kiln::material3::internal {
    using namespace foundation;
    using namespace ui;

    namespace {
        enum Slot { Headline, Overline, Supporting, Leading, Trailing, Count };
        int Subtract(int value, int amount) { return value == Constraints::Infinity ? value : std::max(0, value - amount); }
        int Type(bool overline, bool supporting, bool multiline) { return (overline && supporting) || multiline ? 3 : overline || supporting ? 2 : 1; }
        Dp TokenHeight(int type) {
            return type == 1 ? tokens::ListTokens::ItemOneLineContainerHeight : type == 2 ? tokens::ListTokens::ItemTwoLineContainerHeight : tokens::ListTokens::ItemThreeLineContainerHeight;
        }
        int Width(Placeable* p) { return p ? p->Width() : 0; }
        int Height(Placeable* p) { return p ? p->Height() : 0; }
        struct Slots { std::array<LayoutNode*, Count> nodes{}; };

        class FirstChildPolicy final : public MeasurePolicy {
        public:
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override {
                if (children.empty()) return s.Layout(c.minWidth, c.minHeight);
                auto* p = &children.front()->Measure(c);
                return s.Layout(p->Width(), p->Height(), [p](PlacementScope& scope) { scope.Place(p[0], 0, 0); });
            }
            int MinIntrinsicWidth(MeasureScope&, std::span<Measurable* const> c, int h) override { return c.empty() ? 0 : c.front()->MinIntrinsicWidth(h); }
            int MaxIntrinsicWidth(MeasureScope&, std::span<Measurable* const> c, int h) override { return c.empty() ? 0 : c.front()->MaxIntrinsicWidth(h); }
            int MinIntrinsicHeight(MeasureScope&, std::span<Measurable* const> c, int w) override { return c.empty() ? 0 : c.front()->MinIntrinsicHeight(w); }
            int MaxIntrinsicHeight(MeasureScope&, std::span<Measurable* const> c, int w) override { return c.empty() ? 0 : c.front()->MaxIntrinsicHeight(w); }
        };

        class ListPolicy final : public MeasurePolicy {
        public:
            ListPolicy(std::shared_ptr<Slots> slots, bool legacy, Dp padding, ListItemVerticalAlignment alignment)
                : m_Slots(std::move(slots)), m_Legacy(legacy), m_Padding(padding), m_Alignment(std::move(alignment)),
                  m_HeightFix(ComposeMaterial3Flags::isExpressiveListItemHeightBasedOnTextLinesFixEnabled) {}

            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override {
                const auto m = Measurables(children);
                auto loose = c.CopyMaxDimensions();
                const int horizontal = m_Legacy ? s.RoundToPx(Dp(32)) : 0;
                if (m_Legacy) {
                    const int leadingWidth = m[Leading] ? m[Leading]->MinIntrinsicWidth(c.maxHeight) : 0;
                    const int trailingWidth = m[Trailing] ? m[Trailing]->MinIntrinsicWidth(c.maxHeight) : 0;
                    const int supportingHeight = m[Supporting] ? m[Supporting]->MinIntrinsicHeight(Subtract(c.maxWidth, leadingWidth + trailingWidth + horizontal)) : 0;
                    const int type = Type(m[Overline], m[Supporting], supportingHeight > RoundJava(s.SpToPx(30)));
                    loose = loose.Offset(-horizontal, -s.RoundToPx(Dp(type == 3 ? 24 : 16)));
                }
                std::array<Placeable*, Count> p{};
                int usedX = 0, usedY = 0;
                auto measure = [&](Slot slot, int x, int y) {
                    if (m[slot]) p[slot] = &m[slot]->Measure(loose.Offset(-x, -y));
                };
                measure(Leading, 0, 0);
                usedX += Width(p[Leading]);
                measure(Trailing, usedX, 0);
                usedX += Width(p[Trailing]);
                if (m_Legacy) {
                    measure(Headline, usedX, 0);
                    usedY += Height(p[Headline]);
                    measure(Supporting, usedX, usedY);
                    usedY += Height(p[Supporting]);
                    measure(Overline, usedX, usedY);
                } else {
                    measure(Overline, usedX, 0);
                    usedY += Height(p[Overline]);
                    measure(Headline, usedX, usedY);
                    usedY += Height(p[Headline]);
                    measure(Supporting, usedX, usedY);
                }
                const bool multiline = p[Supporting] && p[Supporting]->Get(FirstBaseline) != p[Supporting]->Get(LastBaseline);
                const int type = Type(p[Overline], p[Supporting], multiline);
                const int top = m_Legacy ? s.RoundToPx(Dp(type == 3 ? 12 : 8)) : 0;
                const int width = c.HasBoundedWidth() ? c.maxWidth : horizontal + usedX + std::max({ Width(p[Headline]), Width(p[Overline]), Width(p[Supporting]) });
                const int height = CalculateHeight(s, c, type, Height(p[Leading]), Height(p[Trailing]), Height(p[Overline]), Height(p[Headline]), Height(p[Supporting]));
                const int start = m_Legacy ? s.RoundToPx(Dp(16)) : 0;
                auto alignment = m_Alignment;
                const bool legacy = m_Legacy;
                return s.Layout(width, height, [=](PlacementScope& scope) {
                    auto align = [&](int size) { return legacy ? (type == 3 ? top : AlignmentV::CenterVertically().Align(size, height)) : alignment(size, height); };
                    if (p[Leading]) scope.PlaceRelative(*p[Leading], start, align(Height(p[Leading])));
                    int y = align(Height(p[Overline]) + Height(p[Headline]) + Height(p[Supporting]));
                    for (auto slot : { Overline, Headline, Supporting }) {
                        if (p[slot]) scope.PlaceRelative(*p[slot], start + Width(p[Leading]), y);
                        y += Height(p[slot]);
                    }
                    if (p[Trailing]) scope.PlaceRelative(*p[Trailing], width - start - Width(p[Trailing]), align(Height(p[Trailing])));
                });
            }
            int MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> children, int h) override { return IntrinsicWidth(s, children, h, false); }
            int MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> children, int h) override { return IntrinsicWidth(s, children, h, true); }
            int MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> children, int w) override { return IntrinsicHeight(s, children, w, false); }
            int MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> children, int w) override { return IntrinsicHeight(s, children, w, true); }
        private:
            std::array<Measurable*, Count> Measurables(std::span<Measurable* const> children) const {
                std::array<Measurable*, Count> result{};
                for (int i = 0; i < Count && i < int(children.size()); ++i)
                    if (m_Slots->nodes[i] && !m_Slots->nodes[i]->Children().empty()) result[i] = children[i];
                return result;
            }
            int CalculateHeight(MeasureScope& s, const Constraints& c, int type, int leading, int trailing, int overline, int headline, int supporting) const {
                int height = std::max({ leading, trailing, overline + headline + supporting });
                int minHeight;
                if (m_Legacy) {
                    height += s.RoundToPx(Dp(type == 3 ? 24 : 16));
                    minHeight = std::max(c.minHeight, s.RoundToPx(TokenHeight(type)));
                } else {
                    const auto noPadding = Dp(std::max(0.0f, (TokenHeight(m_HeightFix ? type : 1) - m_Padding).value));
                    minHeight = c.minHeight == 0 ? std::clamp(s.RoundToPx(noPadding), 0, c.maxHeight) : c.minHeight;
                }
                return std::min(c.maxHeight, std::max(minHeight, height));
            }
            int IntrinsicWidth(MeasureScope& s, std::span<Measurable* const> children, int height, bool max) const {
                auto m = Measurables(children);
                auto width = [&](Slot slot) { return !m[slot] ? 0 : max ? m[slot]->MaxIntrinsicWidth(height) : m[slot]->MinIntrinsicWidth(height); };
                return (m_Legacy ? s.RoundToPx(Dp(32)) : 0) + width(Leading) + width(Trailing) + std::max({ width(Headline), width(Overline), width(Supporting) });
            }
            int IntrinsicHeight(MeasureScope& s, std::span<Measurable* const> children, int width, bool max) const {
                auto m = Measurables(children);
                int remaining = Subtract(width, m_Legacy ? s.RoundToPx(Dp(32)) : 0);
                auto height = [&](Slot slot) { return !m[slot] ? 0 : max ? m[slot]->MaxIntrinsicHeight(remaining) : m[slot]->MinIntrinsicHeight(remaining); };
                const int leading = height(Leading);
                if (m[Leading]) remaining = Subtract(remaining, m[Leading]->MaxIntrinsicWidth(Constraints::Infinity));
                const int trailing = height(Trailing);
                if (m[Trailing]) remaining = Subtract(remaining, m[Trailing]->MaxIntrinsicWidth(Constraints::Infinity));
                const int overline = height(Overline), headline = height(Headline), supporting = height(Supporting);
                const int type = Type(overline > 0, supporting > 0, supporting > RoundJava(s.SpToPx(30)));
                return CalculateHeight(s, {}, type, leading, trailing, overline, headline, supporting);
            }
            std::shared_ptr<Slots> m_Slots;
            bool m_Legacy;
            Dp m_Padding;
            ListItemVerticalAlignment m_Alignment;
            bool m_HeightFix;
        };
    }

    void ListItemLayout(const Modifier& modifier, bool legacy, Dp verticalPadding, ListItemVerticalAlignment alignment, std::array<std::function<void()>, 5> content) {
        const auto slots = std::make_shared<Slots>();
        const auto firstChild = std::make_shared<FirstChildPolicy>();
        Layout(modifier, std::make_shared<ListPolicy>(slots, legacy, verticalPadding, std::move(alignment)), [=] {
            for (int i = 0; i < Count; ++i) {
                slots->nodes[i] = &Layout({}, firstChild, content[i], rt::Key(i));
            }
        });
    }
}
