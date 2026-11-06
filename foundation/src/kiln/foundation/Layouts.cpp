#include "kiln/graphics/Brush.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"

#include "kiln/ui/Draw.h"

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
        for (std::size_t i = 0; i < n; ++i) {
            const AlignmentLine* line = children[i]->GetParentData().alignBy;
            if (!line) continue;
            anyAlignBy = true;
            const int v = placeables[i]->Get(*line);
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
                if (pd.alignBy) {
                    const int v = p.Get(*pd.alignBy);
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
        using SizeFn = std::function<int(Measurable&, int)>;

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
        };

        // FillNode (Size.kt:697-750).
        struct FillParams { Axis axis; float fraction; bool operator==(const FillParams&) const = default; };
        struct FillNode final : ModifierNode, LayoutModifier {
            FillParams params;
            explicit FillNode(FillParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
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
        struct ParentParams { int kind; float weight; bool fill; float bias, bias2; const AlignmentLine* line = nullptr;
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
                    case 2: d.alignH = AlignmentH{ params.bias }; break;
                    case 3: d.boxAlign = Alignment{ params.bias, params.bias2 }; break;
                    case 4: d.matchParentSize = true; break;
                    case 5: d.alignBy = params.line; break;
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
        struct LayerNode final : ModifierNode, DrawModifier {
            GraphicsLayerParams params;
            explicit LayerNode(GraphicsLayerParams p) : params(p) {}
            void OnParamsChanged() { InvalidateDraw(); InvalidateSemantics(); }
            bool ClipsChildren() const override { return params.clip; }
            // translate(t) * translate(pivot) * rotate * scale * translate(-pivot).
            bool LayerTransform(Size size, gfx::Matrix& out) const override {
                if (params.translationX == 0 && params.translationY == 0 && params.rotationZ == 0 && params.scaleX == 1 && params.scaleY == 1)
                    return false;
                const float px = size.width * params.originX, py = size.height * params.originY;
                const float r = params.rotationZ * 3.14159265358979f / 180.0f, c = std::cos(r), sn = std::sin(r);
                // Linear part R*S, then the pivot and translation offsets.
                const float a = c * params.scaleX, b = sn * params.scaleX, cc = -sn * params.scaleY, d = c * params.scaleY;
                out = { a, b, cc, d, params.translationX + px - (a * px + cc * py), params.translationY + py - (b * px + d * py) };
                return true;
            }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                const Size size = s.GetSize();
                s.Save();
                // RenderNode order: translation, then rotation and scale about the pivot (scale first).
                s.Translate({ params.translationX, params.translationY });
                const Offset pivot{ size.width * params.originX, size.height * params.originY };
                if (params.rotationZ != 0) s.Rotate(params.rotationZ, pivot);
                if (params.scaleX != 1 || params.scaleY != 1) s.Scale(params.scaleX, params.scaleY, pivot);
                // CompositingStrategy.Auto: alpha < 1 renders offscreen and composites once, so
                // overlapping content fades as one image (RenderNode with overlapping rendering).
                if (params.alpha <= 0) { s.Restore(); return; }
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
                const bool offscreen = params.alpha < 1 || clipPath;
                if (offscreen) s.BeginLayer({ 0, 0, size.width, size.height }, params.alpha, clipPath ? &*clipPath : nullptr);
                if (params.clip && !clipPath) {
                    if (outline) {
                        if (auto* r = std::get_if<Rect>(&outline->value)) s.ClipRect(*r);
                        else { const auto& rr = std::get<gfx::RoundRect>(outline->value); s.ClipRect(rr.rect, rr.topLeft.x); }
                    } else {
                        s.ClipRect({ 0, 0, size.width, size.height }, CornerRadiusPx(params.shape, size, s));
                    }
                }
                s.DrawContent();
                if (offscreen) s.EndLayer();
                s.Restore();
            }
        };
    }

    Modifier GraphicsLayer(Modifier m, GraphicsLayerParams p) { return m.With<LayerNode>(p); }

    Modifier Padding(Modifier m, PaddingValues p) { return m.With<PaddingNode>(p); }
    Modifier SizeIn(Modifier m, Dp minW, Dp minH, Dp maxW, Dp maxH, bool enforce) { return m.With<SizeNode>(SizeParams{ minW, minH, maxW, maxH, enforce }); }
    Modifier DefaultMinSize(Modifier m, Dp minW, Dp minH) { return m.With<MinSizeNode>(MinSizeParams{ minW, minH }); }
    Modifier FillMax(Modifier m, Axis axis, float fraction) { return m.With<FillNode>(FillParams{ axis, fraction }); }
    Modifier WrapContent(Modifier m, Axis axis, Alignment a, bool unbounded) { return m.With<WrapNode>(WrapParams{ axis, a, unbounded }); }
    Modifier OffsetMod(Modifier m, Dp x, Dp y, bool rtlAware) { return m.With<OffsetNode>(OffsetParams{ x, y, rtlAware }); }
    Modifier AspectRatio(Modifier m, float ratio, bool heightFirst) { return m.With<AspectNode>(AspectParams{ ratio, heightFirst }); }
    Modifier Weight(Modifier m, float w, bool fill) { return m.With<ParentDataNode>(ParentParams{ 0, w, fill, 0, 0 }); }
    Modifier AlignInRow(Modifier m, AlignmentV a) { return m.With<ParentDataNode>(ParentParams{ 1, 0, true, a.bias, 0 }); }
    Modifier AlignInColumn(Modifier m, AlignmentH a) { return m.With<ParentDataNode>(ParentParams{ 2, 0, true, a.bias, 0 }); }
    Modifier AlignInBox(Modifier m, Alignment a) { return m.With<ParentDataNode>(ParentParams{ 3, 0, true, a.horizontal, a.vertical }); }
    Modifier MatchParentSize(Modifier m) { return m.With<ParentDataNode>(ParentParams{ 4, 0, true, 0, 0 }); }
    Modifier AlignBy(Modifier m, const AlignmentLine& line) { return m.With<ParentDataNode>(ParentParams{ 5, 0, true, 0, 0, &line }); }

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
