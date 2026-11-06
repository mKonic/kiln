#pragma once

#include "kiln/graphics/Brush.h"

// Foundation layouts and layout modifiers (spec 06 §13).

#include "kiln/foundation/Arrangement.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    using namespace kiln::ui;

    // --- measure policies ---------------------------------------------------------------------

    class RowColumnPolicy final : public MeasurePolicy {
    public:
        RowColumnPolicy(bool row, Arrangement main, AlignmentV rowAlign, AlignmentH columnAlign)
            : m_Row(row), m_Main(main), m_RowAlign(rowAlign), m_ColumnAlign(columnAlign) {}
        MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override;
        int MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) override;
        int MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) override;
        int MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) override;
        int MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) override;
        bool operator==(const RowColumnPolicy& o) const {
            return m_Row == o.m_Row && m_Main == o.m_Main && m_RowAlign == o.m_RowAlign && m_ColumnAlign == o.m_ColumnAlign;
        }
    private:
        int Intrinsic(MeasureScope& s, std::span<Measurable* const> ch, int cross, bool mainAxisQuery, bool max);
        bool m_Row;
        Arrangement m_Main;
        AlignmentV m_RowAlign;
        AlignmentH m_ColumnAlign;
    };

    class BoxPolicy final : public MeasurePolicy {
    public:
        BoxPolicy(Alignment a, bool propagateMin) : m_Align(a), m_PropagateMin(propagateMin) {}
        MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override;
        bool operator==(const BoxPolicy& o) const { return m_Align == o.m_Align && m_PropagateMin == o.m_PropagateMin; }
    private:
        Alignment m_Align;
        bool m_PropagateMin;
    };

    // --- modifiers ----------------------------------------------------------------------------

    struct PaddingValues {
        Dp start{ 0 }, top{ 0 }, end{ 0 }, bottom{ 0 };
        bool rtlAware = true;
        bool operator==(const PaddingValues&) const = default;
    };
    Modifier Padding(Modifier m, PaddingValues p);
    inline Modifier Padding(Modifier m, Dp all) { return Padding(std::move(m), { all, all, all, all }); }
    inline Modifier Padding(Modifier m, Dp horizontal, Dp vertical) { return Padding(std::move(m), { horizontal, vertical, horizontal, vertical }); }

    // Size family (Size.kt SizeNode): enforceIncoming=true for size/width/height/sizeIn,
    // false for required*.
    Modifier SizeIn(Modifier m, Dp minW, Dp minH, Dp maxW, Dp maxH, bool enforceIncoming = true);
    inline Modifier Width(Modifier m, Dp w) { return SizeIn(std::move(m), w, Dp::Unspecified(), w, Dp::Unspecified()); }
    inline Modifier Height(Modifier m, Dp h) { return SizeIn(std::move(m), Dp::Unspecified(), h, Dp::Unspecified(), h); }
    inline Modifier SizeMod(Modifier m, Dp w, Dp h) { return SizeIn(std::move(m), w, h, w, h); }
    inline Modifier RequiredSize(Modifier m, Dp w, Dp h) { return SizeIn(std::move(m), w, h, w, h, false); }
    Modifier DefaultMinSize(Modifier m, Dp minW, Dp minH);
    enum class Axis : std::uint8_t { Horizontal, Vertical, Both };
    Modifier FillMax(Modifier m, Axis axis, float fraction = 1.0f);
    Modifier WrapContent(Modifier m, Axis axis, Alignment align = Alignment::Center(), bool unbounded = false);
    Modifier OffsetMod(Modifier m, Dp x, Dp y, bool rtlAware = true);
    Modifier AspectRatio(Modifier m, float ratio, bool matchHeightConstraintsFirst = false);

    // Parent data
    Modifier Weight(Modifier m, float weight, bool fill = true);
    Modifier AlignInRow(Modifier m, AlignmentV a);
    Modifier AlignInColumn(Modifier m, AlignmentH a);
    Modifier AlignInBox(Modifier m, Alignment a);
    Modifier MatchParentSize(Modifier m);

    // Drawing
    struct CornerShape { Dp topStart{ 0 }, topEnd{ 0 }, bottomEnd{ 0 }, bottomStart{ 0 }; float percent = -1;
        static CornerShape Rounded(Dp r) { return { r, r, r, r }; }
        static CornerShape Pill() { CornerShape s; s.percent = 50; return s; }
        bool operator==(const CornerShape&) const = default; };
    Modifier Background(Modifier m, Color color, CornerShape shape = {});
    // background(brush, shape, alpha)
    Modifier Background(Modifier m, gfx::Brush brush, CornerShape shape = {}, float alpha = 1);

    // graphicsLayer (draw-time part): alpha (modulated per draw), scale about transformOrigin
    // (fractions of the size), translation, and an optional clip to the shape.
    struct GraphicsLayerParams {
        float alpha = 1, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
        float originX = 0.5f, originY = 0.5f;
        bool clip = false;
        CornerShape shape;
        bool operator==(const GraphicsLayerParams&) const = default;
    };
    Modifier GraphicsLayer(Modifier m, GraphicsLayerParams p);
    inline Modifier Alpha(Modifier m, float a) { GraphicsLayerParams p; p.alpha = a; return GraphicsLayer(std::move(m), p); }
    inline Modifier ClipToBounds(Modifier m) { GraphicsLayerParams p; p.clip = true; return GraphicsLayer(std::move(m), p); }
    inline Modifier Clip(Modifier m, CornerShape shape) { GraphicsLayerParams p; p.clip = true; p.shape = shape; return GraphicsLayer(std::move(m), p); }

}
