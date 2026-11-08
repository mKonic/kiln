#pragma once

// Vector graphics (ui-graphics vector/PathNode.kt, PathParser.kt, PathBuilder.kt and ui
// vector/ImageVector.kt): path commands, SVG path data parsing, and the immutable ImageVector
// tree of groups and paths that VectorPainter draws.

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Path.h"
#include "kiln/ui/Geometry.h"

#include <array>
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

}
