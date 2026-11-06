#include "kiln/foundation/Compose.h"

#include "kiln/TextBackend.h"
#include "kiln/foundation/Semantics.h"

#include <cmath>
#include <map>
#include <optional>

namespace kiln::foundation {

    LayoutNode& Layout(const Modifier& modifier, std::shared_ptr<MeasurePolicy> policy, std::function<void()> content, Key key) {
        auto* c = rt::Composition::Current();
        return c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                   [&](LayoutNode& n) { n.SetModifier(modifier); n.SetMeasurePolicy(policy); },
                                   std::move(content), key);
    }

    namespace {
        // A layout whose policy is a value type: keep the node's current policy if equal.
        template<typename P, typename... A>
        void ValueLayout(const Modifier& modifier, std::function<void()> content, Key key, A&&... args) {
            auto* c = rt::Composition::Current();
            P next(std::forward<A>(args)...);
            c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                [&](LayoutNode& n) {
                                    n.SetModifier(modifier);
                                    if (auto* cur = dynamic_cast<P*>(n.Policy().get()); !cur || !(*cur == next))
                                        n.SetMeasurePolicy(std::make_shared<P>(next));
                                },
                                std::move(content), key);
        }
    }

    void Box(const Modifier& m, Alignment a, bool propagateMin, std::function<void()> content, Key key) {
        ValueLayout<BoxPolicy>(m, std::move(content), key, a, propagateMin);
    }
    void Row(const Modifier& m, Arrangement h, AlignmentV v, std::function<void()> content, Key key) {
        ValueLayout<RowColumnPolicy>(m, std::move(content), key, true, h, v, AlignmentH::Start());
    }
    void Column(const Modifier& m, Arrangement v, AlignmentH h, std::function<void()> content, Key key) {
        ValueLayout<RowColumnPolicy>(m, std::move(content), key, false, v, AlignmentV::Top(), h);
    }
    namespace {
        // SpacerMeasurePolicy (Spacer.kt:51-53): fixed constraints give the size, else 0.
        struct SpacerPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return s.Layout(c.HasFixedWidth() ? c.maxWidth : 0, c.HasFixedHeight() ? c.maxHeight : 0);
            }
            bool operator==(const SpacerPolicy&) const { return true; }
        };
    }

    void Spacer(const Modifier& m, Key key) {
        ValueLayout<SpacerPolicy>(m, {}, key);
    }

    // --- BasicText ----------------------------------------------------------------------------

    namespace {
        struct TextParams { std::string text; TextStyle style; bool operator==(const TextParams&) const = default; };

        struct TextNode final : ModifierNode, LayoutModifier, DrawModifier {
            TextParams params;
            explicit TextNode(TextParams p) : params(std::move(p)) {}
            void OnParamsChanged() { m_Cache.clear(); InvalidateMeasure(); InvalidateDraw(); }
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }

            const ParagraphData& Paragraph(int maxWidth) {
                auto it = m_Cache.find(maxWidth);
                if (it != m_Cache.end()) return it->second;
                ParagraphData data;
                if (Owner() && Owner()->GetOwner() && Owner()->GetOwner()->Text())
                    data = Owner()->GetOwner()->Text()->Layout(params.text, params.style.fontSizeSp,
                                                               maxWidth == Constraints::Infinity ? 0.0f : float(maxWidth));
                if (m_Cache.size() > 8) m_Cache.clear();
                return m_Cache.emplace(maxWidth, std::move(data)).first->second;
            }

            MeasureResult Measure(MeasureScope& s, Measurable&, const Constraints& c) override {
                const ParagraphData& p = Paragraph(c.maxWidth);
                m_DrawnKey = c.maxWidth;
                const int w = c.ConstrainWidth(int(std::ceil(p.width)));
                const int h = c.ConstrainHeight(int(std::ceil(p.height)));
                // TextLayoutResult.firstBaseline / lastBaseline, rounded (BasicText's measure).
                return s.Layout(w, h, {}, { { &FirstBaseline, RoundJava(p.firstBaseline) }, { &LastBaseline, RoundJava(p.lastBaseline) } });
            }
            // Min intrinsic width: the widest unbreakable run, approximated by breaking at 1px.
            int MinIntrinsicWidth(MeasureScope&, Measurable&, int) override { return int(std::ceil(Paragraph(1).width)); }
            int MaxIntrinsicWidth(MeasureScope&, Measurable&, int) override { return int(std::ceil(Paragraph(Constraints::Infinity).width)); }
            int MinIntrinsicHeight(MeasureScope&, Measurable&, int w) override { return int(std::ceil(Paragraph(w).height)); }
            int MaxIntrinsicHeight(MeasureScope&, Measurable&, int w) override { return int(std::ceil(Paragraph(w).height)); }

            void Draw(ContentDrawScope& s) override {
                if (m_DrawnKey) s.DrawParagraph(Paragraph(*m_DrawnKey), {}, params.style.color);
                s.DrawContent();
            }

            std::map<int, ParagraphData> m_Cache;
            std::optional<int> m_DrawnKey;   // the width the last measure laid out at
        };
    }

    void BasicText(const std::string& text, const Modifier& modifier, TextStyle style, Key key) {
        Modifier m = Semantics(modifier, [text](SemanticsConfiguration& c) { c.text.push_back(text); }).With<TextNode>(TextParams{ text, style });
        ValueLayout<BoxPolicy>(m, {}, key, Alignment::TopStart(), false);
    }

}
