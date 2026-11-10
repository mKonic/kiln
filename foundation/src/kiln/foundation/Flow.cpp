#include "kiln/foundation/Flow.h"
#include "kiln/foundation/Layouts.h"

#include <cmath>
#include <optional>

namespace kiln::foundation {

    using namespace ui;

    MeasureResult FlowPolicy::Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
        const bool row = m_Row;
        const int mainMin = row ? c.minWidth : c.minHeight, mainMax = row ? c.maxWidth : c.maxHeight;
        const int crossMin = row ? c.minHeight : c.minWidth, crossMax = row ? c.maxHeight : c.maxWidth;
        auto make = [&](int mMin, int mMax, int xMin, int xMax) {
            return row ? Constraints{ mMin, mMax, xMin, xMax } : Constraints{ xMin, xMax, mMin, mMax };
        };
        auto mainOf = [row](const Placeable& p) { return row ? p.Width() : p.Height(); };
        auto crossOf = [row](const Placeable& p) { return row ? p.Height() : p.Width(); };   // copied into the placement
        // FlowLayout spacing is ceil(toPx) (breakDownItems).
        const int spacing = int(std::ceil(s.ToPx(m_Main.Spacing())));
        const int crossSpacing = int(std::ceil(s.ToPx(m_Cross.Spacing())));

        // Measure every item once (measureAndCache); weighted and line-filling items count with
        // their minimum intrinsic sizes and are measured with their line.
        const std::size_t n = children.size();
        std::vector<Placeable*> placeables(n, nullptr);
        std::vector<int> mains(n), crosses(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (children[i]->GetParentData().weight > 0 || children[i]->GetParentData().fillCrossAxisFraction) {
                mains[i] = row ? children[i]->MinIntrinsicWidth(Constraints::Infinity) : children[i]->MinIntrinsicHeight(Constraints::Infinity);
                crosses[i] = row ? children[i]->MinIntrinsicHeight(mains[i]) : children[i]->MinIntrinsicWidth(mains[i]);
                continue;
            }
            Placeable& p = children[i]->Measure(make(0, mainMax, 0, crossMax));
            placeables[i] = &p;
            mains[i] = mainOf(p);
            crosses[i] = crossOf(p);
        }

        // Break into lines (getWrapInfo with Clip overflow).
        struct Line { std::size_t start, end; int cross; };
        std::vector<Line> lines;
        int leftOver = mainMax, leftOverCross = crossMax, totalCross = 0, lineCross = 0, longest = 0, lineMain = 0;
        std::size_t start = 0;
        std::size_t shown = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const int indexInLine = int(i - start);
            const int need = mains[i] + (indexInLine > 0 ? spacing : 0);
            bool wrap = indexInLine > 0 && (indexInLine >= m_Options.maxItemsInEachLine ||
                                            (mainMax != Constraints::Infinity && leftOver - need < 0));
            if (wrap) {
                lines.push_back({ start, i, lineCross });
                longest = std::max(longest, lineMain);
                totalCross += lineCross + crossSpacing;
                if (crossMax != Constraints::Infinity) leftOverCross = crossMax - totalCross;
                start = i;
                leftOver = mainMax;
                lineCross = lineMain = 0;
            }
            const int lineIndex = int(lines.size());
            // The item would start a line past maxLines or past the cross-axis room: stop.
            if (lineIndex >= m_Options.maxLines) break;
            if (crossMax != Constraints::Infinity && leftOverCross - crosses[i] < 0 && int(i - start) == 0 && !lines.empty()) break;
            const int add = mains[i] + (i > start ? spacing : 0);
            if (mainMax != Constraints::Infinity) leftOver -= add;
            lineMain += add;
            lineCross = std::max(lineCross, crosses[i]);
            shown = i + 1;
        }
        if (shown > start) {
            lines.push_back({ start, shown, lineCross });
            longest = std::max(longest, lineMain);
        }

        // Lay each line out like a Row of at least the longest line's length.
        const int lineMainSize = std::clamp(std::max(longest, mainMin), 0, mainMax == Constraints::Infinity ? INT_MAX : mainMax);
        struct LinePlacement { std::size_t start, end; int cross; std::vector<int> positions; };
        std::vector<LinePlacement> laid;
        std::vector<int> lineCrosses;
        int crossSoFar = 0;
        for (const Line& l : lines) {
            // A line with a filling item is as tall as its tallest item (crossAxisFillEnabledLines);
            // the filling items take their fraction of that.
            bool fills = false;
            for (std::size_t i = l.start; i < l.end; ++i) fills = fills || children[i]->GetParentData().fillCrossAxisFraction.has_value();
            const int lineCrossMax = fills ? l.cross : crossMax == Constraints::Infinity ? Constraints::Infinity : std::max(0, crossMax - crossSoFar);
            auto desired = [&](const ParentData& pd) -> std::optional<int> {
                if (lineCrossMax == Constraints::Infinity || !pd.fillCrossAxisFraction) return std::nullopt;
                return RoundJava(*pd.fillCrossAxisFraction * float(lineCrossMax));
            };
            {
                int fixedSpace = 0;
                for (std::size_t i = l.start; i < l.end; ++i) {
                    const ParentData& pd = children[i]->GetParentData();
                    if (pd.weight > 0) continue;
                    if (auto d = desired(pd)) {
                        const int remaining = mainMax == Constraints::Infinity ? Constraints::Infinity : std::max(0, mainMax - fixedSpace);
                        Placeable& p = children[i]->Measure(make(0, remaining, *d, *d));
                        placeables[i] = &p;
                        mains[i] = mainOf(p);
                        crosses[i] = crossOf(p);
                    } else if (!placeables[i]) {
                        Placeable& p = children[i]->Measure(make(0, mainMax, 0, lineCrossMax));
                        placeables[i] = &p;
                        mains[i] = mainOf(p);
                        crosses[i] = crossOf(p);
                    }
                    fixedSpace += mains[i] + (i > l.start ? spacing : 0);
                }
            }
            // Weighted items share what the fixed ones leave.
            float totalWeight = 0;
            int fixed = 0;
            for (std::size_t i = l.start; i < l.end; ++i) {
                const float w = children[i]->GetParentData().weight;
                if (w > 0) totalWeight += w; else fixed += mains[i];
            }
            fixed += spacing * int(l.end - l.start - 1);
            if (totalWeight > 0) {
                const int remaining = std::max(0, lineMainSize - fixed);
                for (std::size_t i = l.start; i < l.end; ++i) {
                    const ParentData& pd = children[i]->GetParentData();
                    if (pd.weight <= 0) continue;
                    const int size = RoundJava(float(remaining) * pd.weight / totalWeight);
                    const auto d = desired(pd);
                    Placeable& p = children[i]->Measure(make(pd.fill ? size : 0, size, d.value_or(0), d.value_or(lineCrossMax)));
                    placeables[i] = &p;
                    mains[i] = mainOf(p);
                    crosses[i] = crossOf(p);
                }
            }
            int cross = 0;
            std::vector<int> sizes;
            for (std::size_t i = l.start; i < l.end; ++i) { sizes.push_back(mains[i]); cross = std::max(cross, crosses[i]); }
            std::vector<int> positions(sizes.size());
            m_Main.Arrange(s, lineMainSize, sizes, row ? s.Direction() : LayoutDirection::Ltr, positions);
            laid.push_back({ l.start, l.end, cross, std::move(positions) });
            lineCrosses.push_back(cross);
            crossSoFar += cross + crossSpacing;
        }

        // Arrange the lines along the cross axis (placeHelper).
        int totalCrossSize = 0;
        for (int v : lineCrosses) totalCrossSize += v;
        if (!laid.empty()) totalCrossSize += crossSpacing * int(laid.size() - 1);
        totalCrossSize = std::clamp(totalCrossSize, crossMin, crossMax == Constraints::Infinity ? INT_MAX : crossMax);
        std::vector<int> lineOffsets(laid.size());
        m_Cross.Arrange(s, totalCrossSize, lineCrosses, row ? LayoutDirection::Ltr : s.Direction(), lineOffsets);
        const int mainSize = laid.empty() ? std::clamp(0, mainMin, mainMax) : lineMainSize;

        const float bias = m_ItemBias;
        const LayoutDirection dir = s.Direction();
        const int crossLayout = laid.empty() ? std::clamp(0, crossMin, crossMax == Constraints::Infinity ? INT_MAX : crossMax) : totalCrossSize;
        return s.Layout(row ? mainSize : crossLayout, row ? crossLayout : mainSize,
                        [laid, lineOffsets, placeables, row, bias, dir, crossOf](PlacementScope& place) {
            for (std::size_t li = 0; li < laid.size(); ++li) {
                const LinePlacement& l = laid[li];
                for (std::size_t i = l.start; i < l.end; ++i) {
                    Placeable& p = *placeables[i];
                    const int main = l.positions[i - l.start];
                    // Item alignment within the line's cross size.
                    const float h = row ? bias : (dir == LayoutDirection::Ltr ? bias : -bias);
                    const int cross = lineOffsets[li] + RoundJava(float(l.cross - crossOf(p)) / 2.0f * (1 + h));
                    if (row) place.Place(p, main, cross);
                    else place.Place(p, cross, main);
                }
            }
        });
    }

    void FlowRow(const Modifier& modifier, Arrangement horizontal, Arrangement vertical, AlignmentV itemAlignment,
                 std::function<void()> content, FlowOptions options, rt::Key key) {
        auto* c = rt::Composition::Current();
        FlowPolicy next(true, horizontal, vertical, itemAlignment.bias, options);
        c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                            [&](LayoutNode& n) {
                                n.SetModifier(modifier);
                                TakeLocalLayoutDirection(n);
                                if (auto* cur = dynamic_cast<FlowPolicy*>(n.Policy().get()); !cur || !(*cur == next))
                                    n.SetMeasurePolicy(std::make_shared<FlowPolicy>(next));
                            }, std::move(content), key);
    }

    void FlowColumn(const Modifier& modifier, Arrangement vertical, Arrangement horizontal, AlignmentH itemAlignment,
                    std::function<void()> content, FlowOptions options, rt::Key key) {
        auto* c = rt::Composition::Current();
        FlowPolicy next(false, vertical, horizontal, itemAlignment.bias, options);
        c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                            [&](LayoutNode& n) {
                                n.SetModifier(modifier);
                                TakeLocalLayoutDirection(n);
                                if (auto* cur = dynamic_cast<FlowPolicy*>(n.Policy().get()); !cur || !(*cur == next))
                                    n.SetMeasurePolicy(std::make_shared<FlowPolicy>(next));
                            }, std::move(content), key);
    }

}
