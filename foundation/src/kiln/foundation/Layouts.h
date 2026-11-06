#pragma once

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/Shape.h"

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
    // offset { IntOffset }: evaluated at placement, so state it reads re-places without
    // recomposing (the form for offsets that change every frame).
    Modifier OffsetMod(Modifier m, std::function<IntOffset(const Density&)> offset, bool rtlAware = true);
    // onSizeChanged: called after measuring when the size differs from the last one (and the first time).
    Modifier OnSizeChanged(Modifier m, std::function<void(IntSize)> onSizeChanged);
    Modifier AspectRatio(Modifier m, float ratio, bool matchHeightConstraintsFirst = false);

    // Parent data
    Modifier Weight(Modifier m, float weight, bool fill = true);
    Modifier AlignInRow(Modifier m, AlignmentV a);
    Modifier AlignInColumn(Modifier m, AlignmentH a);
    Modifier AlignInBox(Modifier m, Alignment a);
    // Row/Column child: align on an alignment line shared with the siblings that use it
    // (alignBy / alignByBaseline).
    Modifier AlignBy(Modifier m, const AlignmentLine& line);
    inline Modifier AlignByBaseline(Modifier m) { return AlignBy(std::move(m), FirstBaseline); }
    // paddingFrom / paddingFromBaseline: space so the line sits `before` from the top (or start)
    // and `after` from the bottom (or end).
    Modifier PaddingFrom(Modifier m, const AlignmentLine& line, Dp before = Dp::Unspecified(), Dp after = Dp::Unspecified());
    Modifier PaddingFromBaseline(Modifier m, Dp top = Dp::Unspecified(), Dp bottom = Dp::Unspecified());
    // width/height(IntrinsicSize.Min/Max): size to the content's intrinsic measurement;
    // the required forms ignore the incoming constraints.
    enum class IntrinsicSize : std::uint8_t { Min, Max };
    Modifier Width(Modifier m, IntrinsicSize s);
    Modifier Height(Modifier m, IntrinsicSize s);
    Modifier RequiredWidth(Modifier m, IntrinsicSize s);
    Modifier RequiredHeight(Modifier m, IntrinsicSize s);
    Modifier MatchParentSize(Modifier m);

    // Drawing
    struct CornerShape { Dp topStart{ 0 }, topEnd{ 0 }, bottomEnd{ 0 }, bottomStart{ 0 }; float percent = -1;
        static CornerShape Rounded(Dp r) { return { r, r, r, r }; }
        static CornerShape Pill() { CornerShape s; s.percent = 50; return s; }
        bool operator==(const CornerShape&) const = default; };
    Modifier Background(Modifier m, Color color, CornerShape shape = {});
    // background(brush, shape, alpha)
    Modifier Background(Modifier m, gfx::Brush brush, CornerShape shape = {}, float alpha = 1);
    // background(color/brush, shape) with any Shape (see Shapes.h).
    Modifier Background(Modifier m, Color color, std::shared_ptr<const gfx::Shape> shape);
    Modifier Background(Modifier m, gfx::Brush brush, std::shared_ptr<const gfx::Shape> shape, float alpha = 1);

    // graphicsLayer (draw-time part): alpha (modulated per draw), scale about transformOrigin
    // (fractions of the size), translation, and an optional clip to the shape.
    // CompositingStrategy: Auto renders offscreen only when alpha < 1 (so overlapping content
    // fades as one image); Offscreen always does; ModulateAlpha never does and multiplies each
    // draw's alpha instead (cheaper, but overlaps show through).
    enum class CompositingStrategy : std::uint8_t { Auto, Offscreen, ModulateAlpha };

    struct GraphicsLayerParams {
        float alpha = 1, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
        float rotationZ = 0;                  // degrees, clockwise
        // Degrees about the horizontal / vertical axis through the pivot, seen by a camera
        // `cameraDistance` away (RenderNode units: 72 px each, so 8 = 576 px). The layer renders
        // offscreen and is composited in perspective.
        float rotationX = 0, rotationY = 0, cameraDistance = 8;
        CompositingStrategy compositingStrategy = CompositingStrategy::Auto;
        float originX = 0.5f, originY = 0.5f; // transform origin (pivot) as a fraction of the size
        bool clip = false;
        CornerShape shape;
        std::shared_ptr<const gfx::Shape> clipShape;   // overrides `shape` when set
        bool operator==(const GraphicsLayerParams&) const = default;
    };
    Modifier GraphicsLayer(Modifier m, GraphicsLayerParams p);
    // The layer's full matrix (row-major 3x3, projective with rotationX/Y) for a layer of `size`.
    void GraphicsLayerMatrix(const GraphicsLayerParams& p, Size size, float out[9]);
    inline Modifier Rotate(Modifier m, float degrees) { GraphicsLayerParams p; p.rotationZ = degrees; return GraphicsLayer(std::move(m), p); }
    inline Modifier Alpha(Modifier m, float a) { GraphicsLayerParams p; p.alpha = a; return GraphicsLayer(std::move(m), p); }
    inline Modifier ClipToBounds(Modifier m) { GraphicsLayerParams p; p.clip = true; return GraphicsLayer(std::move(m), p); }
    inline Modifier Clip(Modifier m, std::shared_ptr<const gfx::Shape> shape) { GraphicsLayerParams p; p.clip = true; p.clipShape = std::move(shape); return GraphicsLayer(std::move(m), p); }
    inline Modifier Clip(Modifier m, CornerShape shape) { GraphicsLayerParams p; p.clip = true; p.shape = shape; return GraphicsLayer(std::move(m), p); }

}
