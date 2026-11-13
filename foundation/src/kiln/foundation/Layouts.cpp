#include "kiln/graphics/Brush.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"

#include "kiln/ui/Draw.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kiln::foundation {

    // --- Row / Column (RowColumnMeasurePolicy.kt:77-279, Row.kt placeHelper) -----------------

    MeasureResult RowColumnPolicy::Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
        const bool row = m_Row;
        const int mainMin = row ? c.minWidth : c.minHeight, mainMax = row ? c.maxWidth : c.maxHeight;
        const int crossMin = row ? c.minHeight : c.minWidth, crossMax = row ? c.maxHeight : c.maxWidth;
        auto make = [&](int mMin, int mMax, int xMin, int xMax) {
            return row ? Constraints{ mMin, mMax, xMin, xMax } : Constraints{ xMin, xMax, mMin, mMax };
        };
        auto mainOf = [&](const Placeable& p) { return row ? p.Width() : p.Height(); };
        auto crossOf = [&](const Placeable& p) { return row ? p.Height() : p.Width(); };

        const int spacing = s.RoundToPx(m_Main.Spacing());
        const std::size_t n = children.size();
        std::vector<Placeable*> placeables(n, nullptr);
        std::vector<int> mainSizes(n, 0);
        float totalWeight = 0;
        int fixedSpace = 0, crossSpace = 0, weightCount = 0, spaceAfterLastNoWeight = 0;

        for (std::size_t i = 0; i < n; ++i) {
            const ParentData& pd = children[i]->GetParentData();
            if (pd.weight > 0) { totalWeight += pd.weight; ++weightCount; continue; }
            const int remaining = mainMax - fixedSpace;
            Placeable& p = children[i]->Measure(make(0, mainMax == Constraints::Infinity ? Constraints::Infinity : std::max(0, remaining), 0, crossMax));
            mainSizes[i] = mainOf(p);
            spaceAfterLastNoWeight = std::min(spacing, std::max(0, remaining - mainSizes[i]));
            fixedSpace += mainSizes[i] + spaceAfterLastNoWeight;
            crossSpace = std::max(crossSpace, crossOf(p));
            placeables[i] = &p;
        }

        int weightedSpace = 0;
        if (weightCount == 0) {
            fixedSpace -= spaceAfterLastNoWeight;
        } else {
            const int target = mainMax != Constraints::Infinity ? mainMax : mainMin;
            const long long spacingTotal = (long long)spacing * (weightCount - 1);
            const long long remainingToTarget = std::max<long long>(0, target - fixedSpace - spacingTotal);
            const float unit = float(remainingToTarget) / totalWeight;
            long long remainder = remainingToTarget;
            for (std::size_t i = 0; i < n; ++i)
                if (children[i]->GetParentData().weight > 0)
                    remainder -= RoundJava(unit * children[i]->GetParentData().weight);
            for (std::size_t i = 0; i < n; ++i) {
                if (placeables[i]) continue;
                const ParentData& pd = children[i]->GetParentData();
                // The rounding remainder goes one pixel at a time to the first weighted children.
                const int unitSign = remainder > 0 ? 1 : remainder < 0 ? -1 : 0;
                remainder -= unitSign;
                const int childMain = std::max(0, RoundJava(unit * pd.weight) + unitSign);
                Placeable& p = children[i]->Measure(make(pd.fill && childMain != Constraints::Infinity ? childMain : 0, childMain, 0, crossMax));
                mainSizes[i] = mainOf(p);
                weightedSpace += mainSizes[i];
                crossSpace = std::max(crossSpace, crossOf(p));
                placeables[i] = &p;
            }
            weightedSpace = int(std::clamp<long long>(weightedSpace + spacingTotal, 0, (long long)mainMax - fixedSpace));
        }

        // alignBy children (RowColumnMeasurePolicy.kt:222-258): the cross size fits the largest
        // extent before and after the shared line.
        int beforeLine = 0, afterLine = 0;
        bool anyAlignBy = false;
        // The alignment position of an alignBy child (a line, or a block's own position).
        auto alignPos = [](const ParentData& pd, Placeable& p) -> int {
            if (pd.alignByBlock) return (*pd.alignByBlock)(p);
            return p.Get(*pd.alignBy);
        };
        for (std::size_t i = 0; i < n; ++i) {
            const ParentData& apd = children[i]->GetParentData();
            if (!apd.alignBy && !apd.alignByBlock) continue;
            anyAlignBy = true;
            const int v = alignPos(apd, *placeables[i]);
            const int at = v != AlignmentLine::Unspecified ? v : 0;
            beforeLine = std::max(beforeLine, at);
            afterLine = std::max(afterLine, crossOf(*placeables[i]) - at);
        }
        const int mainSize = std::max(std::max(0, fixedSpace + weightedSpace), mainMin);
        const int crossSize = std::max({ crossSpace, crossMin, anyAlignBy ? beforeLine + afterLine : 0 });
        std::vector<int> positions(n, 0);
        m_Main.Arrange(s, mainSize, mainSizes, row ? s.Direction() : LayoutDirection::Ltr, positions);

        const LayoutDirection dir = s.Direction();
        auto layoutW = row ? mainSize : crossSize, layoutH = row ? crossSize : mainSize;
        return s.Layout(layoutW, layoutH, [=, this](PlacementScope& place) {
            for (std::size_t i = 0; i < n; ++i) {
                Placeable& p = *placeables[i];
                const ParentData& pd = children[i]->GetParentData();
                if (pd.alignBy || pd.alignByBlock) {
                    const int v = alignPos(pd, p);
                    int cross = v != AlignmentLine::Unspecified ? beforeLine - v : 0;
                    if (!row && dir == LayoutDirection::Rtl) cross = crossSize - p.Width() - cross;
                    if (row) place.Place(p, positions[i], cross);
                    else place.Place(p, cross, positions[i]);
                    continue;
                }
                if (row) {
                    const AlignmentV a = pd.alignV.value_or(m_RowAlign);
                    place.Place(p, positions[i], a.Align(p.Height(), crossSize));
                } else {
                    const AlignmentH a = pd.alignH.value_or(m_ColumnAlign);
                    place.Place(p, a.Align(p.Width(), crossSize, dir), positions[i]);
                }
            }
        });
    }

    namespace {
        using SizeFn = kiln::Function<int(Measurable&, int)>;

        int IntrinsicMain(std::span<Measurable* const> ch, const SizeFn& mainSize, int crossAvail, int spacing) {
            if (ch.empty()) return 0;
            int unitSpace = 0, fixed = 0;
            float total = 0;
            for (Measurable* m : ch) {
                const float w = m->GetParentData().weight;
                const int size = mainSize(*m, crossAvail);
                if (w == 0) fixed += size;
                else if (w > 0) { total += w; unitSpace = std::max(unitSpace, RoundJava(size / w)); }
            }
            return RoundJava(unitSpace * total) + fixed + (int(ch.size()) - 1) * spacing;
        }

        int IntrinsicCross(std::span<Measurable* const> ch, const SizeFn& mainSize, const SizeFn& crossSize,
                           int mainAvail, int spacing) {
            if (ch.empty()) return 0;
            int fixed = std::min((int(ch.size()) - 1) * spacing, mainAvail);
            int crossMax = 0;
            float total = 0;
            for (Measurable* m : ch) {
                const float w = m->GetParentData().weight;
                if (w == 0) {
                    const int remaining = mainAvail == Constraints::Infinity ? Constraints::Infinity : mainAvail - fixed;
                    const int space = std::min(mainSize(*m, Constraints::Infinity), remaining);
                    fixed += space;
                    crossMax = std::max(crossMax, crossSize(*m, space));
                } else if (w > 0) total += w;
            }
            const int unit = total == 0 ? 0 : mainAvail == Constraints::Infinity ? Constraints::Infinity
                                                                                 : RoundJava(std::max(mainAvail - fixed, 0) / total);
            for (Measurable* m : ch) {
                const float w = m->GetParentData().weight;
                if (w > 0) crossMax = std::max(crossMax, crossSize(*m, unit != Constraints::Infinity ? RoundJava(unit * w) : Constraints::Infinity));
            }
            return crossMax;
        }

        const SizeFn kMinW = [](Measurable& m, int h) { return m.MinIntrinsicWidth(h); };
        const SizeFn kMaxW = [](Measurable& m, int h) { return m.MaxIntrinsicWidth(h); };
        const SizeFn kMinH = [](Measurable& m, int w) { return m.MinIntrinsicHeight(w); };
        const SizeFn kMaxH = [](Measurable& m, int w) { return m.MaxIntrinsicHeight(w); };
    }

    // IntrinsicMeasureBlocks (RowColumnImpl.kt:261-): main-axis queries sum, cross-axis queries
    // measure each child's cross size at the main size it would get.
    int RowColumnPolicy::MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) {
        const int sp = s.RoundToPx(m_Main.Spacing());
        return m_Row ? IntrinsicMain(ch, kMinW, h, sp) : IntrinsicCross(ch, kMaxH, kMinW, h, sp);
    }
    int RowColumnPolicy::MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) {
        const int sp = s.RoundToPx(m_Main.Spacing());
        return m_Row ? IntrinsicMain(ch, kMaxW, h, sp) : IntrinsicCross(ch, kMaxH, kMaxW, h, sp);
    }
    int RowColumnPolicy::MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) {
        const int sp = s.RoundToPx(m_Main.Spacing());
        return m_Row ? IntrinsicCross(ch, kMaxW, kMinH, w, sp) : IntrinsicMain(ch, kMinH, w, sp);
    }
    int RowColumnPolicy::MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) {
        const int sp = s.RoundToPx(m_Main.Spacing());
        return m_Row ? IntrinsicCross(ch, kMaxW, kMaxH, w, sp) : IntrinsicMain(ch, kMaxH, w, sp);
    }

    // --- Box (Box.kt:101-200) -------------------------------------------------------------------

    MeasureResult BoxPolicy::Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
        if (children.empty()) return s.Layout(c.minWidth, c.minHeight);
        const Constraints content = m_PropagateMin ? c : c.CopyMaxDimensions();
        std::vector<Placeable*> placeables(children.size(), nullptr);
        int boxW = c.minWidth, boxH = c.minHeight;
        bool anyMatch = false;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i]->GetParentData().matchParentSize) { anyMatch = true; continue; }
            Placeable& p = children[i]->Measure(content);
            placeables[i] = &p;
            boxW = std::max(boxW, p.Width());
            boxH = std::max(boxH, p.Height());
        }
        if (anyMatch) {
            const Constraints match = Constraints::Fixed(boxW == Constraints::Infinity ? 0 : boxW, boxH == Constraints::Infinity ? 0 : boxH);
            for (std::size_t i = 0; i < children.size(); ++i)
                if (!placeables[i]) placeables[i] = &children[i]->Measure(match);
        }
        const Alignment align = m_Align;
        const LayoutDirection dir = s.Direction();
        return s.Layout(boxW, boxH, [=](PlacementScope& place) {
            for (std::size_t i = 0; i < children.size(); ++i) {
                Placeable& p = *placeables[i];
                const Alignment a = children[i]->GetParentData().boxAlign.value_or(align);
                const IntOffset o = a.Align({ p.Width(), p.Height() }, { boxW, boxH }, dir);
                place.Place(p, o.x, o.y);
            }
        });
    }

    // --- layout modifiers ---------------------------------------------------------------------

    namespace {

        struct PaddingNode final : ModifierNode, LayoutModifier {
            PaddingValues params;
            explicit PaddingNode(PaddingValues p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const int start = s.RoundToPx(params.start), top = s.RoundToPx(params.top);
                const int h = start + s.RoundToPx(params.end), v = top + s.RoundToPx(params.bottom);
                Placeable& p = m.Measure(c.Offset(-h, -v));
                const bool rtlAware = params.rtlAware;
                return s.Layout(c.ConstrainWidth(p.Width() + h), c.ConstrainHeight(p.Height() + v),
                                [&p, start, top, rtlAware](PlacementScope& place) {
                                    if (rtlAware) place.PlaceRelative(p, start, top); else place.Place(p, start, top);
                                });
            }
            int MinIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override {
                const int h = s.RoundToPx(params.start) + s.RoundToPx(params.end), v = s.RoundToPx(params.top) + s.RoundToPx(params.bottom);
                return m.MinIntrinsicWidth(height == Constraints::Infinity ? height : std::max(0, height - v)) + h;
            }
            int MaxIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override {
                const int h = s.RoundToPx(params.start) + s.RoundToPx(params.end), v = s.RoundToPx(params.top) + s.RoundToPx(params.bottom);
                return m.MaxIntrinsicWidth(height == Constraints::Infinity ? height : std::max(0, height - v)) + h;
            }
            int MinIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                const int h = s.RoundToPx(params.start) + s.RoundToPx(params.end), v = s.RoundToPx(params.top) + s.RoundToPx(params.bottom);
                return m.MinIntrinsicHeight(width == Constraints::Infinity ? width : std::max(0, width - h)) + v;
            }
            int MaxIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                const int h = s.RoundToPx(params.start) + s.RoundToPx(params.end), v = s.RoundToPx(params.top) + s.RoundToPx(params.bottom);
                return m.MaxIntrinsicHeight(width == Constraints::Infinity ? width : std::max(0, width - h)) + v;
            }
        };

        // SizeNode (Size.kt:841-928).
        struct SizeParams {
            Dp minW, minH, maxW, maxH; bool enforceIncoming;
            bool operator==(const SizeParams& o) const {
                auto eq = [](Dp a, Dp b) { return (!a.Specified() && !b.Specified()) || a == b; };
                return eq(minW, o.minW) && eq(minH, o.minH) && eq(maxW, o.maxW) && eq(maxH, o.maxH) && enforceIncoming == o.enforceIncoming;
            }
        };
        struct SizeNode final : ModifierNode, LayoutModifier {
            SizeParams params;
            explicit SizeNode(SizeParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            Constraints Target(const Density& d) const {
                const int maxW = params.maxW.Specified() ? std::max(0, d.RoundToPx(params.maxW)) : Constraints::Infinity;
                const int maxH = params.maxH.Specified() ? std::max(0, d.RoundToPx(params.maxH)) : Constraints::Infinity;
                int minW = 0, minH = 0;
                if (params.minW.Specified()) { minW = std::clamp(d.RoundToPx(params.minW), 0, maxW); if (minW == Constraints::Infinity) minW = 0; }
                if (params.minH.Specified()) { minH = std::clamp(d.RoundToPx(params.minH), 0, maxH); if (minH == Constraints::Infinity) minH = 0; }
                return { minW, maxW, minH, maxH };
            }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const Constraints t = Target(s);
                Constraints wrapped;
                if (params.enforceIncoming) {
                    wrapped = c.Constrain(t);
                } else {
                    wrapped.minWidth = params.minW.Specified() ? t.minWidth : std::min(c.minWidth, t.maxWidth);
                    wrapped.maxWidth = params.maxW.Specified() ? t.maxWidth : std::max(c.maxWidth, t.minWidth);
                    wrapped.minHeight = params.minH.Specified() ? t.minHeight : std::min(c.minHeight, t.maxHeight);
                    wrapped.maxHeight = params.maxH.Specified() ? t.maxHeight : std::max(c.maxHeight, t.minHeight);
                }
                Placeable& p = m.Measure(wrapped);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
            // Size.kt:930-977.
            int MinIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override {
                const Constraints t = Target(s);
                if (t.HasFixedWidth()) return t.maxWidth;
                return t.ConstrainWidth(m.MinIntrinsicWidth(params.enforceIncoming ? height : t.ConstrainHeight(height)));
            }
            int MaxIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override {
                const Constraints t = Target(s);
                if (t.HasFixedWidth()) return t.maxWidth;
                return t.ConstrainWidth(m.MaxIntrinsicWidth(params.enforceIncoming ? height : t.ConstrainHeight(height)));
            }
            int MinIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                const Constraints t = Target(s);
                if (t.HasFixedHeight()) return t.maxHeight;
                return t.ConstrainHeight(m.MinIntrinsicHeight(params.enforceIncoming ? width : t.ConstrainWidth(width)));
            }
            int MaxIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                const Constraints t = Target(s);
                if (t.HasFixedHeight()) return t.maxHeight;
                return t.ConstrainHeight(m.MaxIntrinsicHeight(params.enforceIncoming ? width : t.ConstrainWidth(width)));
            }
        };

        // UnspecifiedConstraintsNode (defaultMinSize): min applies only where the incoming min is 0.
        struct MinSizeParams { Dp minW, minH; bool operator==(const MinSizeParams& o) const {
            return (minW == o.minW || (!minW.Specified() && !o.minW.Specified())) && (minH == o.minH || (!minH.Specified() && !o.minH.Specified())); } };
        struct MinSizeNode final : ModifierNode, LayoutModifier {
            MinSizeParams params;
            explicit MinSizeNode(MinSizeParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Constraints w = c;
                if (params.minW.Specified() && c.minWidth == 0) w.minWidth = std::min(std::max(0, s.RoundToPx(params.minW)), c.maxWidth);
                if (params.minH.Specified() && c.minHeight == 0) w.minHeight = std::min(std::max(0, s.RoundToPx(params.minH)), c.maxHeight);
                Placeable& p = m.Measure(w);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
            // Size.kt:1191-1221: intrinsics are coerced to the minimum as well.
            int MinW(MeasureScope& s) const { return params.minW.Specified() ? s.RoundToPx(params.minW) : 0; }
            int MinH(MeasureScope& s) const { return params.minH.Specified() ? s.RoundToPx(params.minH) : 0; }
            int MinIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override { return std::max(m.MinIntrinsicWidth(height), MinW(s)); }
            int MaxIntrinsicWidth(MeasureScope& s, Measurable& m, int height) override { return std::max(m.MaxIntrinsicWidth(height), MinW(s)); }
            int MinIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override { return std::max(m.MinIntrinsicHeight(width), MinH(s)); }
            int MaxIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override { return std::max(m.MaxIntrinsicHeight(width), MinH(s)); }
        };

        // ZIndexNode (ZIndexModifier.kt): places its content with a zIndex, which adds to the
        // layout's draw order among its siblings.
        struct ZIndexParams { float z; bool operator==(const ZIndexParams&) const = default; };
        struct ZIndexNode final : ModifierNode, LayoutModifier {
            ZIndexParams params;
            explicit ZIndexNode(ZIndexParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const float z = params.z;
                return s.Layout(p.Width(), p.Height(), [&p, z](PlacementScope& place) { place.Place(p, 0, 0, z); });
            }
        };

        // FillNode (Size.kt:697-750).
        struct FillParams { Axis axis; float fraction; bool operator==(const FillParams&) const = default; };
        struct FillNode final : ModifierNode, LayoutModifier, ParentDataModifier {
            FillParams params;
            explicit FillNode(FillParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override {
                if (params.axis != Axis::Vertical) d.fillHorizontalFraction = params.fraction;
                if (params.axis != Axis::Horizontal) d.fillVerticalFraction = params.fraction;
            }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Constraints w = c;
                if (c.HasBoundedWidth() && params.axis != Axis::Vertical) {
                    const int width = std::clamp(RoundJava(c.maxWidth * params.fraction), c.minWidth, c.maxWidth);
                    w.minWidth = w.maxWidth = width;
                }
                if (c.HasBoundedHeight() && params.axis != Axis::Horizontal) {
                    const int height = std::clamp(RoundJava(c.maxHeight * params.fraction), c.minHeight, c.maxHeight);
                    w.minHeight = w.maxHeight = height;
                }
                Placeable& p = m.Measure(w);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
        };

        // WrapContentNode (Size.kt:1064-1099).
        struct WrapParams { Axis axis; Alignment align; bool unbounded; bool operator==(const WrapParams&) const = default; };
        struct WrapNode final : ModifierNode, LayoutModifier {
            WrapParams params;
            explicit WrapNode(WrapParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const bool h = params.axis != Axis::Vertical, v = params.axis != Axis::Horizontal;
                Constraints w{ h ? 0 : c.minWidth, h && params.unbounded ? Constraints::Infinity : c.maxWidth,
                               v ? 0 : c.minHeight, v && params.unbounded ? Constraints::Infinity : c.maxHeight };
                Placeable& p = m.Measure(w);
                const int ww = std::clamp(p.Width(), c.minWidth, c.maxWidth), wh = std::clamp(p.Height(), c.minHeight, c.maxHeight);
                const Alignment a = params.align;
                const LayoutDirection dir = s.Direction();
                return s.Layout(ww, wh, [&p, ww, wh, a, dir](PlacementScope& place) {
                    const IntOffset o = a.Align({ p.Width(), p.Height() }, { ww, wh }, dir);
                    place.Place(p, o.x, o.y);
                });
            }
        };

        struct OffsetParams { Dp x, y; bool rtlAware; bool operator==(const OffsetParams&) const = default; };
        struct OffsetNode final : ModifierNode, LayoutModifier {
            OffsetParams params;
            explicit OffsetNode(OffsetParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const int x = s.RoundToPx(params.x), y = s.RoundToPx(params.y);
                const bool rtlAware = params.rtlAware;
                return s.Layout(p.Width(), p.Height(), [&p, x, y, rtlAware](PlacementScope& place) {
                    if (rtlAware) place.PlaceRelative(p, x, y); else place.Place(p, x, y);
                });
            }
        };

        // OffsetPxNode: the lambda runs in placement (its reads re-place the node).
        struct OffsetPxParams {
            kiln::Function<IntOffset(const Density&)> offset;
            bool rtlAware;
            bool operator==(const OffsetPxParams&) const { return false; }   // lambdas never compare equal
        };
        struct OffsetPxNode final : ModifierNode, LayoutModifier {
            OffsetPxParams params;
            explicit OffsetPxNode(OffsetPxParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const Density d = s;
                return s.Layout(p.Width(), p.Height(), [this, &p, d](PlacementScope& place) {
                    const IntOffset o = params.offset(d);
                    if (params.rtlAware) place.PlaceRelative(p, o.x, o.y); else place.Place(p, o.x, o.y);
                });
            }
        };

        struct LayoutBlockParams {
            LayoutModifierMeasure measure;
            bool operator==(const LayoutBlockParams&) const { return false; }   // lambdas never compare equal
        };
        struct LayoutBlockNode final : ModifierNode, LayoutModifier {
            LayoutBlockParams params;
            explicit LayoutBlockNode(LayoutBlockParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override { return params.measure(s, m, c); }
        };

        struct ApproachParams {
            kiln::Function<bool(IntSize)> measuring;
            kiln::Function<bool(PlacementScope&, const ui::LayoutCoordinates&)> placing;
            ApproachMeasure measure;
            bool operator==(const ApproachParams&) const { return false; }
        };
        struct ApproachNode final : ModifierNode, ui::ApproachLayoutModifier {
            ApproachParams params;
            explicit ApproachNode(ApproachParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            bool IsMeasurementApproachInProgress(IntSize lookaheadSize) override { return params.measuring(lookaheadSize); }
            bool IsPlacementApproachInProgress(PlacementScope& s, const ui::LayoutCoordinates& c) override {
                return params.placing && params.placing(s, c);
            }
            MeasureResult ApproachMeasure(ui::ApproachMeasureScope& s, Measurable& m, const Constraints& c) override { return params.measure(s, m, c); }
        };

        struct SizeChangedParams {
            kiln::Function<void(IntSize)> callback;
            bool operator==(const SizeChangedParams&) const { return false; }
        };
        struct SizeChangedNode final : ModifierNode, LayoutModifier {
            SizeChangedParams params;
            std::optional<IntSize> last;
            explicit SizeChangedNode(SizeChangedParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const IntSize size{ p.Width(), p.Height() };
                // onRemeasured: the main pass's size only.
                if (!s.IsLookingAhead() && (!last || *last != size)) {
                    last = size;
                    if (params.callback) params.callback(size);
                }
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
        };

        struct AspectParams { float ratio; bool heightFirst; bool operator==(const AspectParams&) const = default; };
        struct AspectNode final : ModifierNode, LayoutModifier {
            AspectParams params;
            explicit AspectNode(AspectParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            // AspectRatio.kt: try max width, max height, min width, min height (strict, then not).
            std::optional<IntSize> TryW(const Constraints& c, bool strict, bool useMax) const {
                const int w = useMax ? c.maxWidth : c.minWidth;
                if (w == Constraints::Infinity) return std::nullopt;
                const int h = RoundJava(w / params.ratio);
                if (h <= 0) return std::nullopt;
                IntSize sz{ w, h };
                if (!strict || c.Constrain(sz) == sz) return sz;
                return std::nullopt;
            }
            std::optional<IntSize> TryH(const Constraints& c, bool strict, bool useMax) const {
                const int h = useMax ? c.maxHeight : c.minHeight;
                if (h == Constraints::Infinity) return std::nullopt;
                const int w = RoundJava(h * params.ratio);
                if (w <= 0) return std::nullopt;
                IntSize sz{ w, h };
                if (!strict || c.Constrain(sz) == sz) return sz;
                return std::nullopt;
            }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                std::optional<IntSize> size;
                for (bool strict : { true, false }) {
                    if (size) break;
                    if (!params.heightFirst) size = TryW(c, strict, true).or_else([&] { return TryH(c, strict, true); })
                        .or_else([&] { return TryW(c, strict, false); }).or_else([&] { return TryH(c, strict, false); });
                    else size = TryH(c, strict, true).or_else([&] { return TryW(c, strict, true); })
                        .or_else([&] { return TryH(c, strict, false); }).or_else([&] { return TryW(c, strict, false); });
                }
                Placeable& p = m.Measure(size ? Constraints::Fixed(size->width, size->height) : c);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
        };

        // Parent data
        struct ParentParams { int kind; float weight; bool fill; float bias, bias2; const AlignmentLine* line = nullptr; bool absolute = false;
            bool operator==(const ParentParams&) const = default; };
        struct ParentDataNode final : ModifierNode, ParentDataModifier {
            ParentParams params;
            explicit ParentDataNode(ParentParams p) : params(p) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override {
                switch (params.kind) {
                    case 0: d.weight = params.weight; d.fill = params.fill; break;
                    case 1: d.alignV = AlignmentV{ params.bias }; break;
                    case 2: d.alignH = AlignmentH{ params.bias, params.absolute }; break;
                    case 3: d.boxAlign = Alignment{ params.bias, params.bias2, params.absolute }; break;
                    case 4: d.matchParentSize = true; break;
                    case 5: d.alignBy = params.line; d.alignByBlock.reset(); break;
                    case 6: d.fillCrossAxisFraction = params.weight; break;
                }
            }
        };

        struct BackgroundParams { Color color; CornerShape shape; bool operator==(const BackgroundParams&) const = default; };
        struct BackgroundNode final : ModifierNode, DrawModifier {
            BackgroundParams params;
            explicit BackgroundNode(BackgroundParams p) : params(p) {}
            void OnParamsChanged() { InvalidateDraw(); }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                const Size size = s.GetSize();
                const CornerShape& sh = params.shape;
                float r = sh.percent >= 0 ? size.MinDimension() * sh.percent / 100.0f : s.ToPx(sh.topStart);
                // Corner-sum clamp (CornerBasedShape.kt:43-77) for the uniform case.
                r = std::min(r, size.MinDimension() / 2);
                if (r > 0) s.DrawRoundRect(params.color, {}, size, r);
                else s.DrawRect(params.color, {}, size);
                s.DrawContent();
            }
        };
    }

    namespace {
        struct BrushBackgroundParams { gfx::Brush brush; CornerShape shape; float alpha; bool operator==(const BrushBackgroundParams&) const = default; };
        struct BrushBackgroundNode final : ModifierNode, DrawModifier {
            BrushBackgroundParams params;
            explicit BrushBackgroundNode(BrushBackgroundParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                const Size size = s.GetSize();
                const CornerShape& sh = params.shape;
                float r = sh.percent >= 0 ? size.MinDimension() * sh.percent / 100.0f : s.ToPx(sh.topStart);
                r = std::min(r, size.MinDimension() / 2);
                s.DrawRoundRect(params.brush, {}, size, r, params.alpha);
                s.DrawContent();
            }
        };
    }

    namespace {
        float CornerRadiusPx(const CornerShape& sh, Size size, const Density& d) {
            float r = sh.percent >= 0 ? size.MinDimension() * sh.percent / 100.0f : d.ToPx(sh.topStart);
            return std::min(r, size.MinDimension() / 2);
        }
        // RenderProperties::updateMatrix with rotationX/Y: translate(pivot + t) * camera * scale *
        // translate(-pivot), the camera being Sk3DView's (rotateX, rotateY, rotateZ(-rz)) seen from
        // z = -72 * cameraDistance through SkCamera3D::patchToMatrix. Row-major 3x3.
        void LayerMatrix3D(const GraphicsLayerParams& p, Size size, float out[9], std::array<float, 3>* zPlane = nullptr, float elevation = 0) {
            const double px = size.width * p.originX, py = size.height * p.originY;
            auto rot = [](double ax, double ay, double az, double deg, double r[9]) {
                const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a), t = 1 - c;
                const double m[9] = { t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay,
                                      t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax,
                                      t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c };
                std::copy(m, m + 9, r);
            };
            auto mul = [](const double* x, const double* y, double* o) {
                double r[9];
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r[i * 3 + j] = x[i * 3] * y[j] + x[i * 3 + 1] * y[3 + j] + x[i * 3 + 2] * y[6 + j];
                std::copy(r, r + 9, o);
            };
            double rx[9], ry[9], rz[9], m[9];
            rot(1, 0, 0, p.rotationX, rx);
            rot(0, -1, 0, p.rotationY, ry);
            rot(0, 0, 1, -p.rotationZ, rz);
            mul(rx, ry, m);
            mul(m, rz, m);
            // The patch's u = M (1, 0, 0), v = M (0, -1, 0); the camera on the z axis at lz.
            const double ux = m[0], uy = m[3], uz = m[6];
            const double vx = -m[1], vy = -m[4], vz = -m[7];
            const double lz = -72.0 * std::max(0.0f, p.cameraDistance);
            const double cam[9] = { ux, vx, 0, -uy, -vy, 0, uz / -lz, vz / -lz, 1 };
            const double tr[9] = { 1, 0, px + p.translationX, 0, 1, py + p.translationY, 0, 0, 1 };
            const double sc[9] = { p.scaleX, 0, -px * p.scaleX, 0, p.scaleY, -py * p.scaleY, 0, 0, 1 };   // S * T(-pivot)
            double h[9];
            mul(tr, cam, h);
            mul(h, sc, h);
            for (int i = 0; i < 9; ++i) out[i] = float(h[i]);
            if (zPlane) {
                // RenderNode::applyViewPropertyTransforms(true): the full 3D matrix, not the
                // camera homography. HWUI passes its z row to SkShadowUtils for tilted casters.
                rot(0, 1, 0, p.rotationY, ry);
                rot(0, 0, 1, p.rotationZ, rz);
                mul(rx, ry, m);
                mul(m, rz, m);
                const float a = float(m[6] * p.scaleX), b = float(m[7] * p.scaleY);
                *zPlane = { a, b, elevation - a * float(px) - b * float(py) };
            }
        }

        // The layer's properties: fixed, or a block run at draw time whose reads are observed
        // (graphicsLayer { }): a change redraws the layer without recomposing.
        struct LayerSpec : GraphicsLayerParams {
            std::shared_ptr<const kiln::Function<void(GraphicsLayerParams&)>> block;
            // Modifier.shadow: the elevation in dp, made px with the layer's density at draw.
            float shadowElevationDp = 0;
            bool operator==(const LayerSpec& o) const {
                return block == o.block && shadowElevationDp == o.shadowElevationDp && (block || static_cast<const GraphicsLayerParams&>(*this) == o);
            }
        };
        struct LayerNode final : ModifierNode, DrawModifier {
            LayerSpec params;
            explicit LayerNode(LayerSpec p) : params(std::move(p)) { Resolve(); }
            void OnParamsChanged() { Resolve(); InvalidateDraw(); InvalidateSemantics(); }
            void OnDetach() override { m_Watch.Unsubscribe(); }
            struct Watch final : rt::Subscriber {
                LayerNode* node = nullptr;
                void OnStateChanged() override { node->Resolve(); node->InvalidateDraw(); node->InvalidateSemantics(); }
            };
            Watch m_Watch;
            void Resolve() {
                if (!params.block) return;
                m_Watch.node = this;
                GraphicsLayerParams p;
                p.size = m_Size;
                rt::Observation::Track(m_Watch, [&] { (*params.block)(p); });
                static_cast<GraphicsLayerParams&>(params) = p;
                params.size = m_Size;
            }
            // A block sees the size it is drawn at.
            void SizeIs(Size size) const {
                if (!params.block || size == m_Size) return;
                auto* self = const_cast<LayerNode*>(this);
                self->m_Size = size;
                self->Resolve();
            }
            Size m_Size;
            bool ClipsChildren() const override { return params.clip; }
            float LayerAlpha() const override { return params.alpha; }
            // translate(t) * translate(pivot) * rotate * scale * translate(-pivot).
            bool LayerTransform(Size size, gfx::Matrix& out) const override {
                SizeIs(size);
                if (params.translationX == 0 && params.translationY == 0 && params.rotationZ == 0 && params.scaleX == 1 && params.scaleY == 1)
                    return false;
                const float px = size.width * params.originX, py = size.height * params.originY;
                const float r = params.rotationZ * 3.14159265358979f / 180.0f, c = std::cos(r), sn = std::sin(r);
                // Linear part R*S, then the pivot and translation offsets.
                const float a = c * params.scaleX, b = sn * params.scaleX, cc = -sn * params.scaleY, d = c * params.scaleY;
                out = { a, b, cc, d, params.translationX + px - (a * px + cc * py), params.translationY + py - (b * px + d * py) };
                return true;
            }
            bool LayerClipContains(Size size, Offset p, LayoutDirection dir, const Density& d) const override {
                SizeIs(size);
                if (!params.clip) return true;
                if (params.clipShape) return params.clipShape->CreateOutline(size, dir, d).Contains(p);
                const float r = CornerRadiusPx(params.shape, size, d);
                return gfx::Outline{ gfx::RoundRect::Uniform({ 0, 0, size.width, size.height }, r) }.Contains(p);
            }
            bool LayerProjective(Size size, float out[9]) const override {
                SizeIs(size);
                if (params.rotationX == 0 && params.rotationY == 0) return false;
                LayerMatrix3D(params, size, out);
                return true;
            }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                const Size size = s.GetSize();
                SizeIs(size);
                if ((params.rotationX != 0 || params.rotationY != 0) && params.alpha > 0) { Draw3D(s, size); return; }
                s.Save();
                // RenderNode order: translation, then rotation and scale about the pivot (scale first).
                s.Translate({ params.translationX, params.translationY });
                const Offset pivot{ size.width * params.originX, size.height * params.originY };
                if (params.rotationZ != 0) s.Rotate(params.rotationZ, pivot);
                if (params.scaleX != 1 || params.scaleY != 1) s.Scale(params.scaleX, params.scaleY, pivot);
                // CompositingStrategy.Auto: alpha < 1 renders offscreen and composites once, so
                // overlapping content fades as one image (RenderNode with overlapping rendering).
                if (params.alpha <= 0) {
                    // Invisible, but its content still draws into a discarded recording, so layers
                    // recorded inside it (shared elements drawn in an overlay) stay current, as a
                    // RenderNode records content it does not show.
                    ui::DisplayList discarded;
                    s.BeginRecording(discarded);
                    s.DrawContent();
                    s.EndRecording();
                    s.Restore();
                    return;
                }
                // A clip shape whose outline is a path clips through a masked layer.
                std::optional<gfx::Path> clipPath;
                std::optional<gfx::Outline> outline;
                if (params.clip && params.clipShape) {
                    outline = params.clipShape->CreateOutline(size, s.Direction(), s);
                    if (auto* p = std::get_if<gfx::Path>(&outline->value)) clipPath = *p;
                    else if (auto* rr = std::get_if<gfx::RoundRect>(&outline->value);
                             rr && !(rr->topLeft == rr->topRight && rr->topLeft == rr->bottomLeft && rr->topLeft == rr->bottomRight &&
                                     rr->topLeft.x == rr->topLeft.y))
                        clipPath = outline->ToPath();   // non-uniform corners: no SDF fast path
                }
                if (ShadowElevationPx(s) > 0) DrawElevationShadow(s, size);
                if (params.renderEffect || params.progressiveBlur) {
                    DrawBlurred(s, size, clipPath ? &*clipPath : nullptr, outline ? &*outline : nullptr);
                    s.Restore();
                    return;
                }
                const bool offscreen = clipPath || params.compositingStrategy == CompositingStrategy::Offscreen ||
                                       params.blendMode != gfx::BlendMode::SrcOver || params.colorFilter ||
                                       (params.compositingStrategy == CompositingStrategy::Auto && params.alpha < 1);
                if (offscreen) s.BeginLayer(clipPath ? Rect{ 0, 0, size.width, size.height } : OutsetBounds(s, size), params.alpha, clipPath ? &*clipPath : nullptr,
                                            params.blendMode, Filter());
                else if (params.alpha < 1) s.MultiplyAlpha(params.alpha);   // ModulateAlpha
                if (params.clip && !clipPath) ClipToShape(s, size, outline ? &*outline : nullptr);
                s.DrawContent();
                if (offscreen) s.EndLayer();
                s.Restore();
            }
            const gfx::ColorFilter* Filter() const { return params.colorFilter ? &*params.colorFilter : nullptr; }
            // The layer's bounds grown by its outsets (the offscreen image's extent).
            Rect OutsetBounds(const ContentDrawScope& s, Size size) const {
                const LayerOutsets& o = params.outsets;
                const auto& px = params.outsetsPx;
                return { -s.ToPx(o.left) - float(px[0]), -s.ToPx(o.top) - float(px[1]), size.width + s.ToPx(o.right) + float(px[2]),
                         size.height + s.ToPx(o.bottom) + float(px[3]) };
            }
            // The RenderNode's shadow (HWUI draws it before the node's content): the outline
            // GraphicsLayer.configureOutlineAndClip gives it, a round rect on whole pixels or a
            // path, with the layer's alpha as both node and outline alpha.
            void DrawElevationShadow(ContentDrawScope& s, Size size, const gfx::ShadowProjection* projection = nullptr) {
                if (params.scaleX == 0 || params.scaleY == 0) return;
                gfx::Outline o;
                if (params.clipShape) o = params.clipShape->CreateOutline(size, s.Direction(), s);
                else o.value = gfx::RoundRect::Uniform({ 0, 0, size.width, size.height }, CornerRadiusPx(params.shape, size, s));
                auto whole = [](Rect r) {
                    auto round = [](float v) { return std::floor(v + 0.5f); };   // fastRoundToInt
                    return Rect{ round(r.left), round(r.top), round(r.right), round(r.bottom) };
                };
                if (auto* r = std::get_if<Rect>(&o.value)) {
                    o.value = whole(*r);
                } else if (auto* rr = std::get_if<gfx::RoundRect>(&o.value)) {
                    const bool simple = rr->topLeft == rr->topRight && rr->topLeft == rr->bottomRight && rr->topLeft == rr->bottomLeft &&
                                        rr->topLeft.x == rr->topLeft.y;
                    if (simple) o.value = gfx::RoundRect::Uniform(whole(rr->rect), rr->topLeft.x);   // setRoundRect
                    else o.value = o.ToPath();                                                    // setPath
                }
                s.DrawShadow(o, ShadowElevationPx(s), params.ambientShadowColor, params.spotShadowColor, params.alpha * params.alpha, projection);
            }
            float ShadowElevationPx(const Density& d) const {
                return params.shadowElevationDp > 0 ? d.ToPx(Dp(params.shadowElevationDp)) : params.shadowElevation;
            }
            void ClipToShape(ContentDrawScope& s, Size size, const gfx::Outline* outline) {
                if (outline) {
                    if (auto* r = std::get_if<Rect>(&outline->value)) s.ClipRect(*r);
                    else { const auto& rr = std::get<gfx::RoundRect>(outline->value); s.ClipRect(rr.rect, rr.topLeft.x); }
                } else {
                    s.ClipRect({ 0, 0, size.width, size.height }, CornerRadiusPx(params.shape, size, s));
                }
            }
            // A render effect works on the layer's image; the clip shape then clips the result
            // (HWUI clips the layer draw to the outline), so it wraps the effect.
            void DrawBlurred(ContentDrawScope& s, Size size, const gfx::Path* clipPath, const gfx::Outline* outline) {
                if (params.progressiveBlur && params.progressiveBlur->kind != BlurRadiusSpec::Kind::Uniform) {
                    DrawProgressive(s, size, clipPath, outline);
                    return;
                }
                // The chain, input first: blurs in order and the summed offset.
                std::vector<const RenderEffect*> chain;
                for (const RenderEffect* e = &*params.renderEffect; e; e = e->input.get()) chain.push_back(e);
                std::vector<DrawScope::LayerBlur> blurs;
                Offset offset;
                float marginX = 0, marginY = 0;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    const RenderEffect& e = **it;
                    const float k = e.inDp ? s.ToPx(Dp(1)) : 1.0f;
                    if (e.kind == RenderEffect::Kind::Offset) { offset = offset + Offset{ e.x * k, e.y * k }; continue; }
                    // HWUI Blur::convertRadiusToSigma.
                    auto sigma = [](float r) { return r > 0 ? 0.57735f * r + 0.5f : 0.0f; };
                    const DrawScope::LayerBlur b{ sigma(e.x * k), sigma(e.y * k), e.edgeTreatment == gfx::TileMode::Decal };
                    // Decal lets the blur spread 3 sigma past the image (SkBlurImageFilter bounds).
                    if (b.decal) { marginX += 3 * b.sigmaX; marginY += 3 * b.sigmaY; }
                    blurs.push_back(b);
                }
                const Rect bounds{ 0, 0, size.width, size.height };
                float alpha = params.alpha;
                if (clipPath) { s.BeginLayer(bounds, alpha, clipPath, params.blendMode, Filter()); alpha = 1; }
                else if (params.clip) ClipToShape(s, size, outline);
                const Rect out = OutsetBounds(s, size);
                const Rect layer{ out.left - marginX, out.top - marginY, out.right + marginX, out.bottom + marginY };
                if (clipPath) s.BeginLayer(layer, alpha);
                else s.BeginLayer(layer, alpha, nullptr, params.blendMode, Filter());
                s.DrawContent();
                s.EndLayerEffects(blurs, offset);
                if (clipPath) s.EndLayer();
            }
            // The intensity of a progressive blur as a gradient whose alpha is the stop's radius over
            // the largest one (ProgressiveBlurEffectV33 fill: normalised intensities).
            void DrawProgressive(ContentDrawScope& s, Size size, const gfx::Path* clipPath, const gfx::Outline* outline) {
                const BlurRadiusSpec& spec = *params.progressiveBlur;
                float maxPx = 0;
                for (const BlurStop& st : spec.stops) maxPx = std::max(maxPx, s.ToPx(st.radius));
                const Rect bounds{ 0, 0, size.width, size.height };
                float alpha = params.alpha;
                if (clipPath) { s.BeginLayer(bounds, alpha, clipPath, params.blendMode, Filter()); alpha = 1; }
                else if (params.clip) ClipToShape(s, size, outline);
                if (clipPath) s.BeginLayer(bounds, alpha);
                else s.BeginLayer(OutsetBounds(s, size), alpha, nullptr, params.blendMode, Filter());
                s.DrawContent();
                if (maxPx <= 0) {
                    s.EndLayer();
                } else {
                    std::vector<Color> colors;
                    std::vector<float> stops;
                    for (const BlurStop& st : spec.stops) {
                        const float k = std::clamp(s.ToPx(st.radius) / maxPx, 0.0f, 1.0f);
                        colors.push_back(Color(std::uint32_t(std::lround(k * 255)) << 24));
                        stops.push_back(st.fraction);
                    }
                    auto px = [&](Offset dp) { return Offset{ s.ToPx(Dp(dp.x)), s.ToPx(Dp(dp.y)) }; };
                    gfx::Brush intensity;
                    switch (spec.kind) {
                        case BlurRadiusSpec::Kind::Shader:
                            if (spec.shader && *spec.shader) intensity = (*spec.shader)(static_cast<const Density&>(s), size);
                            break;
                        case BlurRadiusSpec::Kind::Vertical: intensity = gfx::Brush::LinearGradient(colors, { 0, 0 }, { 0, size.height }, gfx::TileMode::Clamp, stops); break;
                        case BlurRadiusSpec::Kind::Horizontal: intensity = gfx::Brush::LinearGradient(colors, { 0, 0 }, { size.width, 0 }, gfx::TileMode::Clamp, stops); break;
                        case BlurRadiusSpec::Kind::Linear:
                            intensity = gfx::Brush::LinearGradient(colors, px(spec.start.value_or(Offset{})), px(spec.end.value_or(Offset{})), gfx::TileMode::Clamp, stops);
                            break;
                        default: {
                            const Offset c = spec.center ? px(*spec.center) : Offset{ size.width / 2, size.height / 2 };
                            const float r = spec.fallOff && std::isfinite(spec.fallOff->value) ? s.ToPx(*spec.fallOff) : size.MinDimension() / 2;
                            intensity = gfx::Brush::RadialGradient(colors, c, r, gfx::TileMode::Clamp, stops);
                            break;
                        }
                    }
                    s.EndLayerProgressiveBlur(std::min(maxPx, 150.0f), intensity, params.progressiveBlurDecal);
                }
                if (clipPath) s.EndLayer();
            }
            // rotationX / rotationY: the content renders flat (clipped as usual) into a layer that
            // is composited through the perspective matrix (it is planar, so this is the same image).
            void Draw3D(ContentDrawScope& s, Size size) {
                s.Save();
                gfx::ShadowProjection projection;
                LayerMatrix3D(params, size, projection.matrix.data(), &projection.zPlane, ShadowElevationPx(s));
                if (ShadowElevationPx(s) > 0) DrawElevationShadow(s, size, &projection);
                // Without a clip, content overflowing the bounds is drawn too (a RenderNode projects
                // its whole display list): the layer then covers everything visible.
                constexpr float kAll = 1e6f;
                const Rect captured = params.clip ? Rect{ 0, 0, size.width, size.height } : Rect{ -kAll, -kAll, kAll, kAll };
                s.BeginLayer(captured, params.alpha, nullptr, params.blendMode, Filter());
                std::optional<gfx::Path> clipPath;
                if (params.clip) {
                    if (params.clipShape) {
                        auto outline = params.clipShape->CreateOutline(size, s.Direction(), s);
                        if (auto* r = std::get_if<Rect>(&outline.value)) s.ClipRect(*r);
                        else clipPath = outline.ToPath();
                    } else {
                        s.ClipRect({ 0, 0, size.width, size.height }, CornerRadiusPx(params.shape, size, s));
                    }
                }
                if (clipPath) s.BeginLayer({ 0, 0, size.width, size.height }, 1, &*clipPath);
                s.DrawContent();
                if (clipPath) s.EndLayer();
                s.EndLayerProjective(projection.matrix.data());
                s.Restore();
            }
        };
    }

    Modifier GraphicsLayer(Modifier m, GraphicsLayerParams p) {
        LayerSpec spec;
        static_cast<GraphicsLayerParams&>(spec) = p;
        return m.With<LayerNode>(spec);
    }

    Modifier GraphicsLayer(Modifier m, kiln::Function<void(GraphicsLayerParams&)> block) {
        LayerSpec spec;
        spec.block = std::make_shared<const kiln::Function<void(GraphicsLayerParams&)>>(std::move(block));
        return m.With<LayerNode>(spec);
    }

    bool IsGraphicsLayerElement(const ui::ModifierElement& e) { return e.Type() == typeid(LayerNode); }
    void GraphicsLayerMatrix(const GraphicsLayerParams& p, Size size, float out[9]) { LayerMatrix3D(p, size, out); }

    void detail::DrawWithLayerParams(ui::ContentDrawScope& s, const GraphicsLayerParams& p, kiln::Function<void()> content) {
        LayerSpec spec;
        static_cast<GraphicsLayerParams&>(spec) = p;
        LayerNode node(std::move(spec));
        kiln::Function<void()> outer = s.SwapContent(std::move(content));
        node.Draw(s);
        s.SwapContent(std::move(outer));
    }

    Modifier ShadowMod(Modifier m, Dp elevation, std::shared_ptr<const gfx::Shape> shape, std::optional<bool> clip, Color ambientColor, Color spotColor) {
        const bool doClip = clip.value_or(elevation.value > 0);
        if (!(elevation.value > 0 || doClip)) return m;
        // ShadowGraphicsLayerElement
        LayerSpec spec;
        spec.shadowElevationDp = std::max(elevation.value, 0.0f);
        spec.clipShape = shape ? std::move(shape) : RectangleShape();
        spec.clip = doClip;
        spec.ambientShadowColor = ambientColor;
        spec.spotShadowColor = spotColor;
        return m.With<LayerNode>(spec);
    }
    Modifier Blur(Modifier m, Dp radiusX, Dp radiusY, BlurredEdgeTreatment e) {
        // Modifier.blur: zero radii still clip with a bounded treatment.
        if (!((radiusX.value > 0 && radiusY.value > 0) || e.clip)) return m;
        GraphicsLayerParams p;
        if (radiusX.value > 0 && radiusY.value > 0)
            p.renderEffect = RenderEffect{ RenderEffect::Kind::Blur, radiusX.value, radiusY.value,
                                           e.clip ? gfx::TileMode::Clamp : gfx::TileMode::Decal, true, nullptr };
        p.clip = e.clip;
        p.clipShape = std::move(e.shape);
        return GraphicsLayer(std::move(m), p);
    }

    Modifier Padding(Modifier m, PaddingValues p) { return m.With<PaddingNode>(p); }
    Modifier SizeIn(Modifier m, Dp minW, Dp minH, Dp maxW, Dp maxH, bool enforce) { return m.With<SizeNode>(SizeParams{ minW, minH, maxW, maxH, enforce }); }
    Modifier ZIndex(Modifier m, float zIndex) { return m.With<ZIndexNode>(ZIndexParams{ zIndex }); }
    Modifier DefaultMinSize(Modifier m, Dp minW, Dp minH) { return m.With<MinSizeNode>(MinSizeParams{ minW, minH }); }
    Modifier FillMax(Modifier m, Axis axis, float fraction) { return m.With<FillNode>(FillParams{ axis, fraction }); }
    Modifier WrapContent(Modifier m, Axis axis, Alignment a, bool unbounded) { return m.With<WrapNode>(WrapParams{ axis, a, unbounded }); }
    Modifier OffsetMod(Modifier m, Dp x, Dp y, bool rtlAware) { return m.With<OffsetNode>(OffsetParams{ x, y, rtlAware }); }
    Modifier OffsetMod(Modifier m, kiln::Function<IntOffset(const Density&)> offset, bool rtlAware) {
        return m.With<OffsetPxNode>(OffsetPxParams{ std::move(offset), rtlAware });
    }
    Modifier LayoutMod(Modifier m, LayoutModifierMeasure measure) { return m.With<LayoutBlockNode>(LayoutBlockParams{ std::move(measure) }); }

    // RulerAlignment.kt.
    Modifier FitInside(Modifier m, ui::RectRulers rulers) {
        return LayoutMod(std::move(m), [rulers](MeasureScope& s, Measurable& measurable, const Constraints& c) {
            auto side = [&rulers](ui::PlacementScope& p, int width, int height) {
                return std::array<int, 4>{ std::clamp(RoundJava(p.Current(rulers.left, 0)), 0, width), std::clamp(RoundJava(p.Current(rulers.top, 0)), 0, height),
                                           std::clamp(RoundJava(p.Current(rulers.right, float(width))), 0, width),
                                           std::clamp(RoundJava(p.Current(rulers.bottom, float(height))), 0, height) };
            };
            if (c.HasBoundedWidth() && c.HasBoundedHeight()) {
                const int width = c.maxWidth, height = c.maxHeight;
                return s.Layout(width, height, [&measurable, side, width, height](ui::PlacementScope& p) {
                    const auto [l, t, r, b] = side(p, width, height);
                    Placeable& placeable = measurable.Measure(Constraints::Fixed(r - l, b - t));
                    p.Place(placeable, l, t);
                });
            }
            Placeable& placeable = measurable.Measure(c);
            const int width = placeable.Width(), height = placeable.Height();
            return s.Layout(width, height, [&placeable, side, width, height](ui::PlacementScope& p) {
                const auto [l, t, r, b] = side(p, width, height);
                p.Place(placeable, (l + r - width) / 2, (t + b - height) / 2);
            });
        });
    }

    Modifier FitOutside(Modifier m, ui::RectRulers rulers) {
        return LayoutMod(std::move(m), [rulers](MeasureScope& s, Measurable& measurable, const Constraints& c) {
            if (!c.HasBoundedWidth() || !c.HasBoundedHeight())
                return s.Layout(0, 0, [&measurable](ui::PlacementScope& p) { p.Place(measurable.Measure(Constraints::Fixed(0, 0)), 0, 0); });
            const int width = c.maxWidth, height = c.maxHeight;
            return s.Layout(width, height, [&measurable, rulers, width, height](ui::PlacementScope& p) {
                const int left = RoundJava(p.Current(rulers.left, 0)), top = RoundJava(p.Current(rulers.top, 0));
                const int right = RoundJava(p.Current(rulers.right, float(width))), bottom = RoundJava(p.Current(rulers.bottom, float(height)));
                int childWidth = width, childHeight = height, placeLeft = 0, placeTop = 0;
                if (left > 0) childWidth = left;
                else if (top > 0) childHeight = top;
                else if (right < width) { placeLeft = right; childWidth = width - right; }
                else if (bottom < height) { placeTop = bottom; childHeight = height - bottom; }
                else { childWidth = 0; childHeight = 0; }
                p.Place(measurable.Measure(Constraints::Fixed(childWidth, childHeight)), placeLeft, placeTop);
            });
        });
    }
    Modifier ApproachLayout(Modifier m, kiln::Function<bool(IntSize)> measuring, kiln::Function<bool(PlacementScope&, const ui::LayoutCoordinates&)> placing,
                            ApproachMeasure measure) {
        return m.With<ApproachNode>(ApproachParams{ std::move(measuring), std::move(placing), std::move(measure) });
    }
    Modifier OnSizeChanged(Modifier m, kiln::Function<void(IntSize)> f) { return m.With<SizeChangedNode>(SizeChangedParams{ std::move(f) }); }

    namespace {
        struct GloballyPositionedParams {
            kiln::Function<void(const LayoutCoordinates&)> callback;
            bool operator==(const GloballyPositionedParams&) const { return false; }
        };
        struct GloballyPositionedNode final : ModifierNode, ui::GlobalPositionAwareModifier {
            GloballyPositionedParams params;
            explicit GloballyPositionedNode(GloballyPositionedParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnGloballyPositioned() override {
                if (Owner() && params.callback) params.callback(LayoutCoordinates(Owner(), this, AliveToken()));
            }
        };
    }
    Modifier OnGloballyPositioned(Modifier m, kiln::Function<void(const LayoutCoordinates&)> f) {
        return m.With<GloballyPositionedNode>(GloballyPositionedParams{ std::move(f) });
    }
    namespace {
        struct PlacedParams {
            kiln::Function<void(const LayoutCoordinates&)> callback;
            bool operator==(const PlacedParams&) const { return false; }
        };
        struct PlacedNode final : ModifierNode, ui::LayoutAwareModifier {
            PlacedParams params;
            explicit PlacedNode(PlacedParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }
            void OnPlaced(const LayoutCoordinates& c) override { if (params.callback) params.callback(c); }
        };
        struct LocalConsumerParams {
            kiln::Function<void(const ModifierNode&)> consumer;
            bool operator==(const LocalConsumerParams&) const { return false; }
        };
        struct LocalConsumerNode final : ModifierNode {
            LocalConsumerParams params;
            explicit LocalConsumerNode(LocalConsumerParams p) : params(std::move(p)) {}
            void OnAttach() override { Consume(); }
            void OnParamsChanged() { if (IsAttached()) Consume(); }
            void OnObservedReadsChanged() override { Consume(); }
            void Consume() { ObserveReads([this] { if (params.consumer) params.consumer(*this); }); }
        };
    }
    namespace {
        struct RectChangedParams {
            kiln::Function<void(const ui::RelativeLayoutBounds&)> callback;
            std::int64_t throttle = 0, debounce = 64;
            bool operator==(const RectChangedParams&) const { return false; }
        };
        struct RectChangedNode final : ModifierNode {
            RectChangedParams params;
            ui::RegistrationHandle handle;
            explicit RectChangedNode(RectChangedParams p) : params(std::move(p)) {}
            void Register() {
                handle.Unregister();
                auto alive = AliveToken();
                handle = ui::RegisterOnLayoutRectChanged(*this, params.throttle, params.debounce, [this, alive](const ui::RelativeLayoutBounds& b) {
                    if (!alive.expired() && params.callback) params.callback(b);
                });
            }
            // The entry calls through the node, so a new callback needs no new registration
            // (Compose re-registers when the element changes; its lambdas are usually the same).
            std::int64_t registeredThrottle = -1, registeredDebounce = -1;
            void OnAttach() override { Register(); registeredThrottle = params.throttle; registeredDebounce = params.debounce; }
            void OnParamsChanged() {
                if (!IsAttached() || (params.throttle == registeredThrottle && params.debounce == registeredDebounce)) return;
                OnAttach();
            }
            void OnDetach() override { handle.Unregister(); }
        };

        struct BoundsParams {
            LayoutBoundsHolder* holder = nullptr;
            bool operator==(const BoundsParams&) const = default;
        };
        struct BoundsNode final : ModifierNode {
            BoundsParams params;
            ui::RegistrationHandle handle;
            std::optional<ui::RelativeLayoutBounds> last;
            explicit BoundsNode(BoundsParams p) : params(p) {}
            void OnAttach() override {
                handle.Unregister();
                auto alive = AliveToken();
                handle = ui::RegisterOnLayoutRectChanged(*this, 0, 0, [this, alive](const ui::RelativeLayoutBounds& b) {
                    if (alive.expired()) return;
                    last = b;
                    params.holder->SetBounds(b);
                });
            }
            void OnParamsChanged() { params.holder->SetBounds(last); }
            void OnDetach() override { handle.Unregister(); params.holder->SetBounds(std::nullopt); }
        };

        struct VisibilityParams {
            kiln::Function<void(bool)> callback;
            std::int64_t minDurationMs = 0;
            float minFractionVisible = 1;
            LayoutBoundsHolder* viewport = nullptr;
            bool operator==(const VisibilityParams&) const { return false; }
        };
        // OnVisibilityChangedNode.
        struct VisibilityNode final : ModifierNode, ui::LayoutAwareModifier {
            VisibilityParams params;
            ui::RegistrationHandle handle;
            rt::Job job;
            bool lastResult = false, lastReported = false;
            std::optional<ui::RelativeLayoutBounds> lastBounds, lastViewport;
            explicit VisibilityNode(VisibilityParams p) : params(std::move(p)) {}
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }
            void Check(const ui::RelativeLayoutBounds& bounds) {
                lastBounds = bounds;
                if (!lastViewport && params.viewport) return;
                const float f = lastViewport ? bounds.FractionVisibleIn(*lastViewport) : bounds.FractionVisibleInWindow();
                const bool now = f > params.minFractionVisible || f == 1.0f;
                if (now == lastResult) return;
                lastResult = now;
                job.Cancel();
                if (now == lastReported) return;
                if (now && params.minDurationMs > 0) {
                    auto alive = AliveToken();
                    job = rt::Launch(rt::Dispatcher::Main(), [](VisibilityNode* self, std::weak_ptr<const char> alive, std::int64_t ms) -> rt::Task<> {
                        co_await rt::Delay(ms);
                        if (!alive.expired()) self->Trigger();
                    }(this, alive, params.minDurationMs));
                } else {
                    Trigger();
                }
            }
            void Trigger() {
                job.Cancel();
                lastReported = lastResult;
                if (params.callback) params.callback(lastResult);
            }
            void ForceUpdate() { if (lastBounds) Check(*lastBounds); }
            void FireExitIfNeeded() {
                job.Cancel();
                lastResult = false;
                if (lastReported) Trigger();
            }
            void UpdateViewport() {
                if (!params.viewport) {
                    if (lastViewport) { lastViewport.reset(); ForceUpdate(); }
                    return;
                }
                ObserveReads([this] {
                    const auto v = params.viewport->Bounds();
                    if (!(lastViewport == v)) { lastViewport = v; ForceUpdate(); }
                });
            }
            void OnAttach() override {
                handle.Unregister();
                auto alive = AliveToken();
                handle = ui::RegisterOnLayoutRectChanged(*this, 0, 0, [this, alive](const ui::RelativeLayoutBounds& b) {
                    if (alive.expired()) return;
                    if (params.viewport) lastViewport = params.viewport->Bounds();
                    Check(b);
                });
                UpdateViewport();
            }
            void OnParamsChanged() { if (IsAttached()) { UpdateViewport(); ForceUpdate(); } }
            void OnObservedReadsChanged() override { UpdateViewport(); }
            void OnDetach() override { handle.Unregister(); FireExitIfNeeded(); }
            void OnUnplaced() override { FireExitIfNeeded(); }
        };
    }
    Modifier OnLayoutRectChanged(Modifier m, kiln::Function<void(const ui::RelativeLayoutBounds&)> f, std::int64_t throttle, std::int64_t debounce) {
        return m.With<RectChangedNode>(RectChangedParams{ std::move(f), throttle, debounce });
    }
    Modifier LayoutBounds(Modifier m, LayoutBoundsHolder& holder) { return m.With<BoundsNode>(BoundsParams{ &holder }); }
    Modifier OnVisibilityChanged(Modifier m, kiln::Function<void(bool)> f, std::int64_t minDurationMs, float minFraction, LayoutBoundsHolder* viewport) {
        return m.With<VisibilityNode>(VisibilityParams{ std::move(f), minDurationMs, minFraction, viewport });
    }
    Modifier OnPlaced(Modifier m, kiln::Function<void(const LayoutCoordinates&)> f) { return m.With<PlacedNode>(PlacedParams{ std::move(f) }); }
    Modifier ModifierLocalConsumer(Modifier m, kiln::Function<void(const ModifierNode&)> f) {
        return m.With<LocalConsumerNode>(LocalConsumerParams{ std::move(f) });
    }
    Modifier AspectRatio(Modifier m, float ratio, bool heightFirst) { return m.With<AspectNode>(AspectParams{ ratio, heightFirst }); }
    Modifier Weight(Modifier m, float w, bool fill) { return m.With<ParentDataNode>(ParentParams{ 0, w, fill, 0, 0 }); }
    Modifier AlignInRow(Modifier m, AlignmentV a) { return m.With<ParentDataNode>(ParentParams{ 1, 0, true, a.bias, 0 }); }
    Modifier AlignInColumn(Modifier m, AlignmentH a) { return m.With<ParentDataNode>(ParentParams{ 2, 0, true, a.bias, 0, nullptr, a.absolute }); }
    namespace {
        // LayoutIdElement / LayoutIdModifier.
        struct LayoutIdParams { std::string id; bool operator==(const LayoutIdParams&) const = default; };
        struct LayoutIdNode final : ModifierNode, ParentDataModifier {
            LayoutIdParams params;
            explicit LayoutIdNode(LayoutIdParams p) : params(std::move(p)) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override { d.layoutId = params.id; }
        };
    }
    Modifier LayoutId(Modifier m, std::string id) { return m.With<LayoutIdNode>(LayoutIdParams{ std::move(id) }); }
    const std::string* LayoutIdOf(const Measurable& m) {
        const auto& id = m.GetParentData().layoutId;
        return id ? &*id : nullptr;
    }
    Modifier AlignInBox(Modifier m, Alignment a) { return m.With<ParentDataNode>(ParentParams{ 3, 0, true, a.horizontal, a.vertical, nullptr, a.absolute }); }
    Modifier MatchParentSize(Modifier m) { return m.With<ParentDataNode>(ParentParams{ 4, 0, true, 0, 0 }); }
    Modifier AlignBy(Modifier m, const AlignmentLine& line) { return m.With<ParentDataNode>(ParentParams{ 5, 0, true, 0, 0, &line }); }
    namespace {
        struct AlignBlockParams { std::shared_ptr<const kiln::Function<int(Placeable&)>> block; bool operator==(const AlignBlockParams&) const = default; };
        struct AlignBlockNode final : ModifierNode, ParentDataModifier {
            AlignBlockParams params;
            explicit AlignBlockNode(AlignBlockParams p) : params(std::move(p)) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override { d.alignByBlock = params.block; d.alignBy = nullptr; }
        };
    }
    Modifier AlignBy(Modifier m, kiln::Function<int(Placeable&)> block) {
        return m.With<AlignBlockNode>(AlignBlockParams{ std::make_shared<const kiln::Function<int(Placeable&)>>(std::move(block)) });
    }

    namespace {
        // ProgressiveBlurElement's layer block: the scope's spec, edges and alpha onto the layer.
        void ApplyBlur(GraphicsLayerParams& p, BlurRadiusSpec spec, BlurredEdgeTreatment e, float alpha) {
            p.alpha = alpha;
            p.clip = e.clip;
            p.clipShape = std::move(e.shape);
            p.renderEffect.reset();
            p.progressiveBlur.reset();
            if (spec.kind == BlurRadiusSpec::Kind::Uniform) {
                const Dp r = spec.stops.empty() ? Dp(0) : spec.stops[0].radius;
                if (r.value > 0)
                    p.renderEffect = RenderEffect{ RenderEffect::Kind::Blur, r.value, r.value, e.clip ? gfx::TileMode::Clamp : gfx::TileMode::Decal, true, nullptr };
            } else {
                p.progressiveBlur = std::move(spec);
                p.progressiveBlurDecal = !e.clip;
            }
        }
    }

    Modifier Blur(Modifier m, kiln::Function<void(BlurScope&)> block) {
        rt::Composition* c = rt::Composition::Current();
        const ui::Density density = c ? c->Read(ui::LocalDensity) : ui::Density{};
        return GraphicsLayer(std::move(m), [block = std::move(block), density](GraphicsLayerParams& p) {
            BlurScope scope;
            static_cast<ui::Density&>(scope) = density;
            scope.size = { Dp(p.size.width / density.density), Dp(p.size.height / density.density) };
            if (block) block(scope);
            ApplyBlur(p, std::move(scope.blurRadiusSpec), std::move(scope.edgeTreatment), scope.alpha);
        });
    }

    Modifier Blur(Modifier m, BlurRadiusSpec spec, BlurredEdgeTreatment e, float alpha) {
        GraphicsLayerParams p;
        ApplyBlur(p, std::move(spec), std::move(e), alpha);
        return GraphicsLayer(std::move(m), p);
    }


    Modifier FillCrossAxisOfLine(Modifier m, float fraction) {
        return m.With<ParentDataNode>(ParentParams{ 6, std::clamp(fraction, 0.0f, 1.0f), true, 0, 0 });
    }

    namespace {
        // AlignmentLineOffsetDpNode (AlignmentLine.kt:307-362).
        struct LineOffsetParams { const AlignmentLine* line; Dp before, after; bool operator==(const LineOffsetParams&) const = default; };
        struct LineOffsetNode final : ModifierNode, LayoutModifier {
            LineOffsetParams params;
            explicit LineOffsetNode(LineOffsetParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const bool horizontal = params.line->horizontal;
                Constraints loose = c;
                if (horizontal) loose.minHeight = 0; else loose.minWidth = 0;
                Placeable& p = m.Measure(loose);
                const int v = p.Get(*params.line);
                const int linePos = v != AlignmentLine::Unspecified ? v : 0;
                const int axis = horizontal ? p.Height() : p.Width();
                const int axisMax = horizontal ? c.maxHeight : c.maxWidth;
                const int room = axisMax == Constraints::Infinity ? Constraints::Infinity : axisMax - axis;
                const int before = params.before.Specified() ? s.RoundToPx(params.before) : 0;
                const int after = params.after.Specified() ? s.RoundToPx(params.after) : 0;
                const int padBefore = std::clamp(before - linePos, 0, std::max(0, room));
                const int padAfter = std::clamp(after - axis + linePos, 0, std::max(0, room == Constraints::Infinity ? room : room - padBefore));
                const int w = horizontal ? p.Width() : std::max(padBefore + p.Width() + padAfter, c.minWidth);
                const int h = horizontal ? std::max(padBefore + p.Height() + padAfter, c.minHeight) : p.Height();
                const bool hasBefore = params.before.Specified();
                return s.Layout(w, h, [&p, horizontal, hasBefore, padBefore, padAfter, w, h](PlacementScope& place) {
                    const int x = horizontal ? 0 : (hasBefore ? padBefore : w - padAfter - p.Width());
                    const int y = !horizontal ? 0 : (hasBefore ? padBefore : h - padAfter - p.Height());
                    place.PlaceRelative(p, x, y);
                });
            }
        };
    }

    namespace {
        // IntrinsicWidthNode / IntrinsicHeightNode (Intrinsic.kt).
        struct IntrinsicParams { bool width; IntrinsicSize size; bool enforce; bool operator==(const IntrinsicParams&) const = default; };
        struct IntrinsicNode final : ModifierNode, LayoutModifier {
            IntrinsicParams params;
            explicit IntrinsicNode(IntrinsicParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            Constraints Content(Measurable& m, const Constraints& c) const {
                if (params.width) {
                    if (params.enforce && c.HasFixedWidth()) return c;
                    const int w = params.size == IntrinsicSize::Min ? m.MinIntrinsicWidth(c.maxHeight) : m.MaxIntrinsicWidth(c.maxHeight);
                    return Constraints::FixedWidth(std::max(0, w));
                }
                if (params.enforce && c.HasFixedHeight()) return c;
                const int h = params.size == IntrinsicSize::Min ? m.MinIntrinsicHeight(c.maxWidth) : m.MaxIntrinsicHeight(c.maxWidth);
                return Constraints::FixedHeight(std::max(0, h));
            }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const Constraints content = Content(m, c);
                Placeable& p = m.Measure(params.enforce ? c.Constrain(content) : content);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
            int Pick(Measurable& m, int other, bool widthQuery) const {
                if (widthQuery) return params.size == IntrinsicSize::Min ? m.MinIntrinsicWidth(other) : m.MaxIntrinsicWidth(other);
                return params.size == IntrinsicSize::Min ? m.MinIntrinsicHeight(other) : m.MaxIntrinsicHeight(other);
            }
            int MinIntrinsicWidth(MeasureScope&, Measurable& m, int h) override { return params.width ? Pick(m, h, true) : m.MinIntrinsicWidth(h); }
            int MaxIntrinsicWidth(MeasureScope&, Measurable& m, int h) override { return params.width ? Pick(m, h, true) : m.MaxIntrinsicWidth(h); }
            int MinIntrinsicHeight(MeasureScope&, Measurable& m, int w) override { return params.width ? m.MinIntrinsicHeight(w) : Pick(m, w, false); }
            int MaxIntrinsicHeight(MeasureScope&, Measurable& m, int w) override { return params.width ? m.MaxIntrinsicHeight(w) : Pick(m, w, false); }
        };
    }

    Modifier Width(Modifier m, IntrinsicSize s) { return m.With<IntrinsicNode>(IntrinsicParams{ true, s, true }); }
    Modifier Height(Modifier m, IntrinsicSize s) { return m.With<IntrinsicNode>(IntrinsicParams{ false, s, true }); }
    Modifier RequiredWidth(Modifier m, IntrinsicSize s) { return m.With<IntrinsicNode>(IntrinsicParams{ true, s, false }); }
    Modifier RequiredHeight(Modifier m, IntrinsicSize s) { return m.With<IntrinsicNode>(IntrinsicParams{ false, s, false }); }

    Modifier PaddingFrom(Modifier m, const AlignmentLine& line, Dp before, Dp after) {
        return m.With<LineOffsetNode>(LineOffsetParams{ &line, before, after });
    }
    Modifier PaddingFromBaseline(Modifier m, Dp top, Dp bottom) {
        if (top.Specified()) m = PaddingFrom(std::move(m), FirstBaseline, top, Dp::Unspecified());
        if (bottom.Specified()) m = PaddingFrom(std::move(m), LastBaseline, Dp::Unspecified(), bottom);
        return m;
    }
    Modifier Background(Modifier m, Color c, CornerShape s) { return m.With<BackgroundNode>(BackgroundParams{ c, s }); }
    namespace {
        // background(color/brush, shape) (Background.kt): the shape's outline at the node's size.
        struct ShapeBackgroundParams {
            std::optional<Color> color; std::optional<gfx::Brush> brush; std::shared_ptr<const gfx::Shape> shape; float alpha;
            bool operator==(const ShapeBackgroundParams& o) const {
                return color == o.color && brush == o.brush && alpha == o.alpha &&
                       (shape == o.shape || (shape && o.shape && shape->Equals(*o.shape)));
            }
        };
        struct ShapeBackgroundNode final : ModifierNode, DrawModifier {
            ShapeBackgroundParams params;
            explicit ShapeBackgroundNode(ShapeBackgroundParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                if (params.shape) {
                    const gfx::Outline o = params.shape->CreateOutline(s.GetSize(), s.Direction(), s);
                    if (params.color) DrawOutline(s, o, *params.color);
                    else if (params.brush) DrawOutline(s, o, *params.brush, params.alpha);
                }
                s.DrawContent();
            }
        };
    }

    Modifier Background(Modifier m, Color c, std::shared_ptr<const gfx::Shape> shape) {
        return m.With<ShapeBackgroundNode>(ShapeBackgroundParams{ c, std::nullopt, std::move(shape), 1 });
    }
    Modifier Background(Modifier m, gfx::Brush brush, std::shared_ptr<const gfx::Shape> shape, float alpha) {
        return m.With<ShapeBackgroundNode>(ShapeBackgroundParams{ std::nullopt, std::move(brush), std::move(shape), alpha });
    }

    Modifier Background(Modifier m, gfx::Brush brush, CornerShape s, float alpha) {
        return m.With<BrushBackgroundNode>(BrushBackgroundParams{ std::move(brush), s, alpha });
    }

}
