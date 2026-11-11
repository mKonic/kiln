#pragma once

#include <optional>
#include <vector>
#include <algorithm>

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Shape.h"

// Foundation layouts and layout modifiers (spec 06 §13).

#include "kiln/foundation/Arrangement.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    // Gives `n` the LocalLayoutDirection provided where it is composed, if any (layouts call
    // this when they create or update their node; without one it follows the window).
    void TakeLocalLayoutDirection(ui::LayoutNode& n);


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
    // zIndex: draw order among siblings (higher on top; ties keep placement order). Adds to
    // the zIndex the parent places with.
    Modifier ZIndex(Modifier m, float zIndex);
    enum class Axis : std::uint8_t { Horizontal, Vertical, Both };
    Modifier FillMax(Modifier m, Axis axis, float fraction = 1.0f);
    Modifier WrapContent(Modifier m, Axis axis, Alignment align = Alignment::Center(), bool unbounded = false);
    Modifier OffsetMod(Modifier m, Dp x, Dp y, bool rtlAware = true);
    // offset { IntOffset }: evaluated at placement, so state it reads re-places without
    // recomposing (the form for offsets that change every frame).
    Modifier OffsetMod(Modifier m, std::function<IntOffset(const Density&)> offset, bool rtlAware = true);
    // Modifier.layout { measurable, constraints -> }: measures and places what it wraps itself.
    // State read in the block remeasures it.
    using LayoutModifierMeasure = std::function<MeasureResult(MeasureScope&, Measurable&, const Constraints&)>;
    Modifier LayoutMod(Modifier m, LayoutModifierMeasure measure);
    // onSizeChanged: called after measuring when the size differs from the last one (and the first time).
    Modifier OnSizeChanged(Modifier m, std::function<void(IntSize)> onSizeChanged);
    // LayoutCoordinates as onGloballyPositioned hands them over: a live view of the space the
    // modifier sits in (read when asked, so a stored copy follows later moves). Popups share the
    // window's root, so root positions stand in for positionOnScreen.
    class LayoutCoordinates {
    public:
        LayoutCoordinates() = default;
        LayoutCoordinates(const ui::LayoutNode* node, const ui::ModifierNode* at, std::weak_ptr<const char> alive)
            : m_Node(node), m_At(at), m_Alive(std::move(alive)) {}
        bool IsAttached() const;
        IntSize GetSize() const;
        Offset PositionInRoot() const;
        Rect BoundsInRoot() const;
        // positionInParent / parentLayoutCoordinates.size: in the parent layout's content space
        // (where it places its children; the root's own position and the window without one).
        Offset PositionInParent() const;
        IntSize ParentSize() const;
        bool operator==(const LayoutCoordinates& o) const { return m_Node == o.m_Node && m_At == o.m_At; }

    private:
        const ui::LayoutNode* m_Node = nullptr;
        const ui::ModifierNode* m_At = nullptr;
        std::weak_ptr<const char> m_Alive;
    };
    // onGloballyPositioned: called after a layout pass when the space it sits in moved, changed
    // its transform or its size (and the first time).
    Modifier OnGloballyPositioned(Modifier m, std::function<void(const LayoutCoordinates&)> onGloballyPositioned);
    Modifier AspectRatio(Modifier m, float ratio, bool matchHeightConstraintsFirst = false);

    // Parent data
    Modifier Weight(Modifier m, float weight, bool fill = true);
    // FlowRowScope.fillMaxRowHeight / FlowColumnScope.fillMaxColumnWidth: `fraction` of the
    // line's cross size (its tallest item's).
    Modifier FillCrossAxisOfLine(Modifier m, float fraction);
    inline Modifier FillMaxRowHeight(Modifier m, float fraction = 1) { return FillCrossAxisOfLine(std::move(m), fraction); }
    inline Modifier FillMaxColumnWidth(Modifier m, float fraction = 1) { return FillCrossAxisOfLine(std::move(m), fraction); }
    Modifier AlignInRow(Modifier m, AlignmentV a);
    Modifier AlignInColumn(Modifier m, AlignmentH a);
    Modifier AlignInBox(Modifier m, Alignment a);
    // layoutId: tags a child for a custom layout's measure policy, which finds it with LayoutIdOf
    // (Compose's Modifier.layoutId / Measurable.layoutId).
    Modifier LayoutId(Modifier m, std::string id);
    // The child's layoutId, or null.
    const std::string* LayoutIdOf(const Measurable& m);
    // Row/Column child: align on an alignment line shared with the siblings that use it
    // (alignBy / alignByBaseline).
    Modifier AlignBy(Modifier m, const AlignmentLine& line);
    // alignBy { measured -> position }: align on a position the block reads from the measured
    // child (its size or lines).
    Modifier AlignBy(Modifier m, std::function<int(ui::Placeable&)> block);
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

    // A render effect on a graphics layer's image (RenderEffect): a Gaussian blur (radii in
    // pixels, which HWUI turns into sigmas; `edgeTreatment` is how pixels outside the image read,
    // Clamp or Decal) or an offset, applied after its `input` effect.
    struct RenderEffect {
        enum class Kind : std::uint8_t { Blur, Offset };
        Kind kind = Kind::Blur;
        float x = 0, y = 0;   // blur radii or offset
        gfx::TileMode edgeTreatment = gfx::TileMode::Clamp;
        bool inDp = false;    // Modifier.blur: radii in dp, converted with the draw's density
        std::shared_ptr<const RenderEffect> input;
        static RenderEffect Blur(float radiusX, float radiusY, gfx::TileMode edgeTreatment = gfx::TileMode::Clamp,
                                 std::shared_ptr<const RenderEffect> input = {}) {
            return { Kind::Blur, radiusX, radiusY, edgeTreatment, false, std::move(input) };
        }
        static RenderEffect Offset(float x, float y, std::shared_ptr<const RenderEffect> input = {}) {
            return { Kind::Offset, x, y, gfx::TileMode::Clamp, false, std::move(input) };
        }
        bool operator==(const RenderEffect& o) const {
            return kind == o.kind && x == o.x && y == o.y && edgeTreatment == o.edgeTreatment && inDp == o.inDp &&
                   (input == o.input || (input && o.input && *input == *o.input));
        }
    };

    // BlurRadiusSpec (ui-graphics ProgressiveBlur.kt): a blur radius that varies across the layer.
    // Stops give the radius at fractions along a line (Linear: start to end, the whole height or
    // width by default) or out from a centre (Radial: to fallOff, half the smaller side by
    // default). Uniform is the ordinary blur.
    struct BlurStop {
        float fraction = 0;
        Dp radius;
        bool operator==(const BlurStop&) const = default;
    };
    struct BlurRadiusSpec {
        enum class Kind : std::uint8_t { Uniform, Vertical, Horizontal, Linear, Radial };
        Kind kind = Kind::Uniform;
        std::vector<BlurStop> stops;                     // sorted; Uniform: one
        std::optional<Offset> start, end;                // Linear, in dp
        std::optional<Offset> center;                    // Radial, in dp; nullopt: the centre
        std::optional<Dp> fallOff;                       // Radial; nullopt: half the smaller side
        bool operator==(const BlurRadiusSpec&) const = default;

        static BlurRadiusSpec Uniform(Dp radius) { return { Kind::Uniform, { { 0, radius } } }; }
        static BlurRadiusSpec VerticalGradient(Dp startRadius, Dp endRadius) { return { Kind::Vertical, { { 0, startRadius }, { 1, endRadius } } }; }
        static BlurRadiusSpec VerticalGradient(std::vector<BlurStop> stops) { return { Kind::Vertical, Sorted(std::move(stops)) }; }
        static BlurRadiusSpec HorizontalGradient(Dp startRadius, Dp endRadius) { return { Kind::Horizontal, { { 0, startRadius }, { 1, endRadius } } }; }
        static BlurRadiusSpec HorizontalGradient(std::vector<BlurStop> stops) { return { Kind::Horizontal, Sorted(std::move(stops)) }; }
        static BlurRadiusSpec LinearGradient(Offset startDp, Offset endDp, Dp startRadius, Dp endRadius) {
            return { Kind::Linear, { { 0, startRadius }, { 1, endRadius } }, startDp, endDp };
        }
        static BlurRadiusSpec LinearGradient(Offset startDp, Offset endDp, std::vector<BlurStop> stops) {
            return { Kind::Linear, Sorted(std::move(stops)), startDp, endDp };
        }
        static BlurRadiusSpec RadialGradient(Dp startRadius, Dp endRadius, std::optional<Offset> centerDp = std::nullopt,
                                             std::optional<Dp> fallOff = std::nullopt) {
            return { Kind::Radial, { { 0, startRadius }, { 1, endRadius } }, std::nullopt, std::nullopt, centerDp, fallOff };
        }
        static BlurRadiusSpec RadialGradient(std::vector<BlurStop> stops, std::optional<Offset> centerDp = std::nullopt,
                                             std::optional<Dp> fallOff = std::nullopt) {
            return { Kind::Radial, Sorted(std::move(stops)), std::nullopt, std::nullopt, centerDp, fallOff };
        }
    private:
        static std::vector<BlurStop> Sorted(std::vector<BlurStop> s) {
            std::stable_sort(s.begin(), s.end(), [](const BlurStop& a, const BlurStop& b) { return a.fraction < b.fraction; });
            if (s.size() > 16) s.resize(16);   // BlurStop.MaxStops
            return s;
        }
    };

    struct GraphicsLayerParams {
        float alpha = 1, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
        float rotationZ = 0;                  // degrees, clockwise
        // Degrees about the horizontal / vertical axis through the pivot, seen by a camera
        // `cameraDistance` away (RenderNode units: 72 px each, so 8 = 576 px). The layer renders
        // offscreen and is composited in perspective.
        float rotationX = 0, rotationY = 0, cameraDistance = 8;
        CompositingStrategy compositingStrategy = CompositingStrategy::Auto;
        std::optional<RenderEffect> renderEffect;   // renders offscreen and composites through it
        // A progressive blur (Modifier.blur(BlurRadiusSpec)); with `progressiveBlurDecal` the
        // edges fade (Unbounded) instead of clamping.
        std::optional<BlurRadiusSpec> progressiveBlur;
        bool progressiveBlurDecal = false;
        // How the layer composites onto what is below, and a colour filter on its image; either
        // other than the default makes the layer offscreen (GraphicsLayer requiresCompositingLayer).
        gfx::BlendMode blendMode = gfx::BlendMode::SrcOver;
        std::optional<gfx::ColorFilter> colorFilter;
        // Elevation shadow (px) under the window's light, its outline the layer's shape, tinted
        // by the ambient and spot colours (DefaultShadowColor: black).
        float shadowElevation = 0;
        Color ambientShadowColor = Color(0xFF000000u), spotShadowColor = Color(0xFF000000u);
        float originX = 0.5f, originY = 0.5f; // transform origin (pivot) as a fraction of the size
        bool clip = false;
        CornerShape shape;
        std::shared_ptr<const gfx::Shape> clipShape;   // overrides `shape` when set
        bool operator==(const GraphicsLayerParams&) const = default;
    };
    Modifier GraphicsLayer(Modifier m, GraphicsLayerParams p);
    // graphicsLayer { }: the block sets the properties; state it reads is observed, so an
    // animated value redraws the layer without recomposing the caller.
    Modifier GraphicsLayer(Modifier m, std::function<void(GraphicsLayerParams&)> block);
    // The layer's full matrix (row-major 3x3, projective with rotationX/Y) for a layer of `size`.
    void GraphicsLayerMatrix(const GraphicsLayerParams& p, Size size, float out[9]);
    // Modifier.shadow(elevation, shape = RectangleShape, clip = elevation > 0, ambientColor,
    // spotColor): a graphics layer casting an elevation shadow of `shape` (and clipping to it).
    Modifier ShadowMod(Modifier m, Dp elevation, std::shared_ptr<const gfx::Shape> shape = nullptr, std::optional<bool> clip = std::nullopt,
                    Color ambientColor = Color(0xFF000000u), Color spotColor = Color(0xFF000000u));
    inline Modifier Rotate(Modifier m, float degrees) { GraphicsLayerParams p; p.rotationZ = degrees; return GraphicsLayer(std::move(m), p); }
    inline Modifier Alpha(Modifier m, float a) { GraphicsLayerParams p; p.alpha = a; return GraphicsLayer(std::move(m), p); }
    // Modifier.blur's edge treatment: Rectangle clips the blurred image to the bounds, a shape to
    // that shape (both read clamped edges); Unbounded lets the blur spread past the bounds.
    struct BlurredEdgeTreatment {
        bool clip = true;
        std::shared_ptr<const gfx::Shape> shape;   // null with clip: the bounds
        static BlurredEdgeTreatment Rectangle() { return {}; }
        static BlurredEdgeTreatment Unbounded() { return { false, nullptr }; }
        static BlurredEdgeTreatment Of(std::shared_ptr<const gfx::Shape> shape) { return { true, std::move(shape) }; }
    };
    Modifier Blur(Modifier m, Dp radiusX, Dp radiusY, BlurredEdgeTreatment edgeTreatment = BlurredEdgeTreatment::Rectangle());
    // Modifier.blur(blurRadiusSpec, edgeTreatment, alpha).
    Modifier Blur(Modifier m, BlurRadiusSpec spec, BlurredEdgeTreatment edgeTreatment = BlurredEdgeTreatment::Rectangle(), float alpha = 1);
    inline Modifier Blur(Modifier m, Dp radius, BlurredEdgeTreatment edgeTreatment = BlurredEdgeTreatment::Rectangle()) {
        return Blur(std::move(m), radius, radius, std::move(edgeTreatment));
    }
    inline Modifier ClipToBounds(Modifier m) { GraphicsLayerParams p; p.clip = true; return GraphicsLayer(std::move(m), p); }
    inline Modifier Clip(Modifier m, std::shared_ptr<const gfx::Shape> shape) { GraphicsLayerParams p; p.clip = true; p.clipShape = std::move(shape); return GraphicsLayer(std::move(m), p); }
    inline Modifier Clip(Modifier m, CornerShape shape) { GraphicsLayerParams p; p.clip = true; p.shape = shape; return GraphicsLayer(std::move(m), p); }

}
