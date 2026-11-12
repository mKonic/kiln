#pragma once

// Vector graphics (ui-graphics vector/PathNode.kt, PathParser.kt, PathBuilder.kt and ui
// vector/ImageVector.kt): path commands, SVG path data parsing, and the immutable ImageVector
// tree of groups and paths that VectorPainter draws.

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Path.h"
#include "kiln/ui/Geometry.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kiln::gfx::vector {

    // PathNode: one path command with its arguments (arcs keep their flags as 0 / 1).
    struct PathNode {
        enum class Kind : std::uint8_t {
            Close, RelativeMoveTo, MoveTo, RelativeLineTo, LineTo, RelativeHorizontalTo, HorizontalTo,
            RelativeVerticalTo, VerticalTo, RelativeCurveTo, CurveTo, RelativeReflectiveCurveTo, ReflectiveCurveTo,
            RelativeQuadTo, QuadTo, RelativeReflectiveQuadTo, ReflectiveQuadTo, RelativeArcTo, ArcTo,
        };
        Kind kind = Kind::Close;
        std::array<float, 7> v{};
        bool IsCurve() const {
            return kind == Kind::RelativeCurveTo || kind == Kind::CurveTo || kind == Kind::RelativeReflectiveCurveTo ||
                   kind == Kind::ReflectiveCurveTo;
        }
        bool IsQuad() const {
            return kind == Kind::RelativeQuadTo || kind == Kind::QuadTo || kind == Kind::RelativeReflectiveQuadTo ||
                   kind == Kind::ReflectiveQuadTo;
        }
        bool operator==(const PathNode&) const = default;
    };
    using PathData = std::vector<PathNode>;

    // PathParser.pathStringToNodes / addPathNodes: SVG path data to nodes. Malformed numbers end
    // a command's arguments; unknown commands are skipped.
    PathData ParsePathString(std::string_view pathData);
    // List<PathNode>.toPath: replaces `target`'s contours, keeping its fill type. Arcs become
    // cubic Béziers of at most 45 degrees each.
    void ToPath(const PathData& nodes, Path& target);

    // PathBuilder
    class PathBuilder {
    public:
        PathBuilder& Close() { return Add(PathNode::Kind::Close, {}); }
        PathBuilder& MoveTo(float x, float y) { return Add(PathNode::Kind::MoveTo, { x, y }); }
        PathBuilder& MoveToRelative(float dx, float dy) { return Add(PathNode::Kind::RelativeMoveTo, { dx, dy }); }
        PathBuilder& LineTo(float x, float y) { return Add(PathNode::Kind::LineTo, { x, y }); }
        PathBuilder& LineToRelative(float dx, float dy) { return Add(PathNode::Kind::RelativeLineTo, { dx, dy }); }
        PathBuilder& HorizontalLineTo(float x) { return Add(PathNode::Kind::HorizontalTo, { x }); }
        PathBuilder& HorizontalLineToRelative(float dx) { return Add(PathNode::Kind::RelativeHorizontalTo, { dx }); }
        PathBuilder& VerticalLineTo(float y) { return Add(PathNode::Kind::VerticalTo, { y }); }
        PathBuilder& VerticalLineToRelative(float dy) { return Add(PathNode::Kind::RelativeVerticalTo, { dy }); }
        PathBuilder& CurveTo(float x1, float y1, float x2, float y2, float x3, float y3) {
            return Add(PathNode::Kind::CurveTo, { x1, y1, x2, y2, x3, y3 });
        }
        PathBuilder& CurveToRelative(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
            return Add(PathNode::Kind::RelativeCurveTo, { dx1, dy1, dx2, dy2, dx3, dy3 });
        }
        PathBuilder& ReflectiveCurveTo(float x1, float y1, float x2, float y2) { return Add(PathNode::Kind::ReflectiveCurveTo, { x1, y1, x2, y2 }); }
        PathBuilder& ReflectiveCurveToRelative(float dx1, float dy1, float dx2, float dy2) {
            return Add(PathNode::Kind::RelativeReflectiveCurveTo, { dx1, dy1, dx2, dy2 });
        }
        PathBuilder& QuadTo(float x1, float y1, float x2, float y2) { return Add(PathNode::Kind::QuadTo, { x1, y1, x2, y2 }); }
        PathBuilder& QuadToRelative(float dx1, float dy1, float dx2, float dy2) { return Add(PathNode::Kind::RelativeQuadTo, { dx1, dy1, dx2, dy2 }); }
        PathBuilder& ReflectiveQuadTo(float x, float y) { return Add(PathNode::Kind::ReflectiveQuadTo, { x, y }); }
        PathBuilder& ReflectiveQuadToRelative(float dx, float dy) { return Add(PathNode::Kind::RelativeReflectiveQuadTo, { dx, dy }); }
        PathBuilder& ArcTo(float horizontalEllipseRadius, float verticalEllipseRadius, float theta, bool isMoreThanHalf, bool isPositiveArc,
                           float x1, float y1) {
            return Add(PathNode::Kind::ArcTo, { horizontalEllipseRadius, verticalEllipseRadius, theta, isMoreThanHalf ? 1.0f : 0.0f,
                                                isPositiveArc ? 1.0f : 0.0f, x1, y1 });
        }
        PathBuilder& ArcToRelative(float a, float b, float theta, bool isMoreThanHalf, bool isPositiveArc, float dx1, float dy1) {
            return Add(PathNode::Kind::RelativeArcTo, { a, b, theta, isMoreThanHalf ? 1.0f : 0.0f, isPositiveArc ? 1.0f : 0.0f, dx1, dy1 });
        }
        const PathData& Nodes() const { return m_Nodes; }

    private:
        PathBuilder& Add(PathNode::Kind k, std::initializer_list<float> args) {
            PathNode n;
            n.kind = k;
            std::size_t i = 0;
            for (float a : args) n.v[i++] = a;
            m_Nodes.push_back(n);
            return *this;
        }
        PathData m_Nodes;
    };

    // Vector.kt's defaults.
    inline constexpr std::string_view DefaultGroupName = "";
    inline constexpr float DefaultRotation = 0, DefaultPivotX = 0, DefaultPivotY = 0, DefaultScaleX = 1, DefaultScaleY = 1;
    inline constexpr float DefaultTranslationX = 0, DefaultTranslationY = 0;
    inline const PathData EmptyPath{};
    inline constexpr std::string_view DefaultPathName = "";
    inline constexpr float DefaultStrokeLineWidth = 0, DefaultStrokeLineMiter = 4;
    inline constexpr float DefaultTrimPathStart = 0, DefaultTrimPathEnd = 1, DefaultTrimPathOffset = 0;
    inline constexpr StrokeCap DefaultStrokeLineCap = StrokeCap::Butt;
    inline constexpr StrokeJoin DefaultStrokeLineJoin = StrokeJoin::Miter;
    inline constexpr BlendMode DefaultTintBlendMode = BlendMode::SrcIn;
    inline const ui::Color DefaultTintColor = ui::Color::Transparent();
    inline constexpr FillType DefaultFillType = FillType::NonZero;
    // VectorPainter.kt: the root group's name.
    inline constexpr std::string_view RootGroupName = "VectorRootGroup";

    // VectorPath
    struct VectorPath {
        std::string name;
        PathData pathData;
        FillType pathFillType = FillType::NonZero;
        std::optional<Brush> fill;
        float fillAlpha = 1;
        std::optional<Brush> stroke;
        float strokeAlpha = 1;
        float strokeLineWidth = 0;
        StrokeCap strokeLineCap = StrokeCap::Butt;
        StrokeJoin strokeLineJoin = StrokeJoin::Miter;
        float strokeLineMiter = 4;
        float trimPathStart = 0, trimPathEnd = 1, trimPathOffset = 0;
        bool operator==(const VectorPath&) const = default;
    };

    // VectorGroup: transformed about the pivot (translate, rotate, scale), clipped to its clip path.
    struct VectorGroup;
    using VectorNode = std::variant<VectorPath, std::shared_ptr<const VectorGroup>>;
    struct VectorGroup {
        std::string name;
        float rotation = 0, pivotX = 0, pivotY = 0, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
        PathData clipPathData;
        std::vector<VectorNode> children;
        // Structural equality: children compared by value, not by pointer.
        bool operator==(const VectorGroup& o) const;
    };

    // ImageVector
    class ImageVector {
    public:
        std::string name;
        ui::Dp defaultWidth, defaultHeight;
        float viewportWidth = 0, viewportHeight = 0;
        std::shared_ptr<const VectorGroup> root;
        ui::Color tintColor = ui::Color::Unspecified();
        BlendMode tintBlendMode = BlendMode::SrcIn;
        bool autoMirror = false;
        int genId = 0;   // generateImageVectorId

        class Builder {
        public:
            Builder(std::string name, ui::Dp defaultWidth, ui::Dp defaultHeight, float viewportWidth, float viewportHeight,
                    ui::Color tintColor = ui::Color::Unspecified(), BlendMode tintBlendMode = BlendMode::SrcIn, bool autoMirror = false);
            Builder& AddGroup(std::string name = {}, float rotate = 0, float pivotX = 0, float pivotY = 0, float scaleX = 1, float scaleY = 1,
                              float translationX = 0, float translationY = 0, PathData clipPathData = {});
            Builder& ClearGroup();
            Builder& AddPath(VectorPath path);
            // ImageVector.Builder.path { ... }: fill and the other path parameters, nodes from the builder.
            Builder& Path(PathData nodes, std::optional<Brush> fill = std::nullopt, float fillAlpha = 1, std::optional<Brush> stroke = std::nullopt,
                          float strokeAlpha = 1, float strokeLineWidth = 0, StrokeCap cap = StrokeCap::Butt, StrokeJoin join = StrokeJoin::Miter,
                          float miter = 4, FillType fillType = FillType::NonZero);
            std::shared_ptr<const ImageVector> Build();

        private:
            ImageVector* m_Image;
            std::shared_ptr<ImageVector> m_Result;
            std::vector<std::shared_ptr<VectorGroup>> m_Stack;
            bool m_Consumed = false;
        };
    };
    using ImageVectorPtr = std::shared_ptr<const ImageVector>;

    // VectorProperty: a property of a group or path a VectorConfig can override.
    template <typename T>
    struct VectorProperty {
        int id;
        bool operator==(const VectorProperty&) const = default;
    };
    namespace VectorProperties {
        inline constexpr VectorProperty<float> Rotation{ 0 }, PivotX{ 1 }, PivotY{ 2 }, ScaleX{ 3 }, ScaleY{ 4 }, TranslateX{ 5 }, TranslateY{ 6 };
        inline constexpr VectorProperty<PathData> PathData{ 7 };
        inline constexpr VectorProperty<std::optional<Brush>> Fill{ 8 }, Stroke{ 10 };
        inline constexpr VectorProperty<float> FillAlpha{ 9 }, StrokeLineWidth{ 11 }, StrokeAlpha{ 12 }, TrimPathStart{ 13 }, TrimPathEnd{ 14 },
            TrimPathOffset{ 15 };
    }

    // VectorConfig: values that replace a node's properties as RenderVectorGroup emits it
    // (getOrDefault: the default unless overridden).
    class VectorConfig {
    public:
        virtual ~VectorConfig() = default;
        virtual float GetOrDefault(VectorProperty<float>, float defaultValue) const { return defaultValue; }
        virtual PathData GetOrDefault(VectorProperty<PathData>, const PathData& defaultValue) const { return defaultValue; }
        virtual std::optional<Brush> GetOrDefault(VectorProperty<std::optional<Brush>>, const std::optional<Brush>& defaultValue) const {
            return defaultValue;
        }
    };

    // RenderVectorGroup(group, configs): emits the group's children into the vector being built,
    // each node's properties taken from the config under its name where there is one.
    void RenderVectorGroup(ImageVector::Builder& builder, const VectorGroup& group, const std::map<std::string, const VectorConfig*>& configs = {});

}
