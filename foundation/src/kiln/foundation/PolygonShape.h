#pragma once

// PolygonShape / MorphPolygonShape (foundation shape/PolygonShape.kt, MorphPolygonShape.kt):
// shapes made of RoundedPolygons sized to the layout. A builder returns the polygon's geometry
// for the current size; ready-made factories cover regular polygons, stars, pills, pill stars,
// circles and rectangles; corner-based shapes convert. An optional transform block rotates,
// scales, translates or fits the polygon into the size. MorphPolygonShape morphs between two of
// them by a progress read when the outline is made.

#include "kiln/foundation/Image.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/graphics/shapes/Morph.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::foundation {

    using ui::Offset;
    using ui::Dp;

    // foundation.shape.CornerRounding: a radius in dp, or a fraction of the shape's reference
    // size (the radius of a regular polygon, the vertex bounds of a vertex list, the size of a
    // rectangle), with smoothing 0..1.
    struct PolygonCornerRounding {
        enum class Unit : std::uint8_t { Dp, Fraction };
        float value = 0;
        Unit unit = Unit::Dp;
        float smoothing = 0;
        static PolygonCornerRounding Unrounded() { return {}; }
        static PolygonCornerRounding Of(Dp radius, float smoothing = 0) { return { radius.value, Unit::Dp, smoothing }; }
        static PolygonCornerRounding Fraction(float fraction, float smoothing = 0) { return { fraction, Unit::Fraction, smoothing }; }
        bool operator==(const PolygonCornerRounding&) const = default;
    };

    inline constexpr Offset kUnspecifiedOffset{ NAN, NAN };

    // PolygonShapeGeometry: what a builder returns. An unspecified centre is the vertices'
    // average.
    struct PolygonShapeGeometry {
        std::vector<Offset> vertices;
        Offset center = kUnspecifiedOffset;
        PolygonCornerRounding rounding;
        std::optional<std::vector<PolygonCornerRounding>> perVertexRounding;
        std::optional<Size> roundingReference;   // for fractions; default: the vertex bounds
        bool operator==(const PolygonShapeGeometry& o) const;
    };

    class PolygonShapeScope {
    public:
        Size size;
        ui::LayoutDirection layoutDirection = ui::LayoutDirection::Ltr;
        ui::Density density;
        // A regular polygon, its first vertex at angle 0; radius defaults to half the smaller
        // side, the centre to the size's centre. Fractions round against the radius.
        PolygonShapeGeometry Polygon(int numVertices, float radius = NAN, Offset center = kUnspecifiedOffset,
                                     PolygonCornerRounding rounding = {}) const;
        PolygonShapeGeometry Polygon(int numVertices, std::vector<PolygonCornerRounding> perVertexRounding,
                                     float radius = NAN, Offset center = kUnspecifiedOffset) const;
        PolygonShapeGeometry Polygon(std::vector<Offset> vertices, Offset center = kUnspecifiedOffset,
                                     PolygonCornerRounding rounding = {}) const;
        PolygonShapeGeometry Polygon(std::vector<Offset> vertices, std::vector<PolygonCornerRounding> perVertexRounding,
                                     Offset center = kUnspecifiedOffset) const;
    };

    // PolygonShapeTransformScope: each call applies after the ones before; pivots default to the
    // polygon's centre (which scaleToFit moves along).
    class PolygonShapeTransformScope {
    public:
        Size size;
        ui::LayoutDirection layoutDirection = ui::LayoutDirection::Ltr;
        ui::Density density;
        void Rotate(float degrees, Offset pivot = kUnspecifiedOffset);
        void Translate(float x = 0, float y = 0);
        void Scale(float scaleX, float scaleY = NAN, Offset pivot = kUnspecifiedOffset);
        // An affine matrix, applied about the centre.
        void Transform(const gfx::Matrix& m);
        // Scales and positions the (transformed) polygon's bounds into the size.
        void ScaleToFit(ContentScale contentScale = ContentScale::Fit(), Alignment alignment = Alignment::Center());

        // internal
        void Reset(const gfx::shapes::RoundedPolygon& base, Size size, ui::LayoutDirection dir, ui::Density density);
        gfx::Matrix matrix;
        bool hasMatrix = false;
    private:
        void Then(const gfx::Matrix& after) { matrix = Combine(after, matrix); hasMatrix = true; }
        static gfx::Matrix Combine(const gfx::Matrix& after, const gfx::Matrix& before);
        const gfx::shapes::RoundedPolygon* m_Base = nullptr;
        float m_CenterX = 0, m_CenterY = 0;
    };

    class PolygonShape : public gfx::Shape {
    public:
        using Builder = std::function<PolygonShapeGeometry(PolygonShapeScope&)>;
        using TransformBlock = std::function<void(PolygonShapeTransformScope&)>;

        gfx::Outline CreateOutline(Size size, ui::LayoutDirection dir, const ui::Density& density) const final;
        // transform { }: replaces the transform block.
        void Transform(TransformBlock block);
        // A new shape with the same polygon and transform (own caches).
        std::shared_ptr<PolygonShape> Copy() const;
        // The polygon for a size, transform applied (shared while nothing changes).
        std::shared_ptr<const gfx::shapes::RoundedPolygon> ResolvePolygon(Size size, ui::LayoutDirection dir, const ui::Density& density) const;

        // PolygonShape { }: from a builder.
        static std::shared_ptr<PolygonShape> FromBuilder(Builder builder);
        static std::shared_ptr<PolygonShape> RegularPolygon(int numVertices, PolygonCornerRounding rounding = {});
        static std::shared_ptr<PolygonShape> Star(int numPoints, float innerRadiusRatio = 0.5f, PolygonCornerRounding outerRounding = {},
                                                  std::optional<PolygonCornerRounding> innerRounding = std::nullopt);
        static std::shared_ptr<PolygonShape> Pill(float smoothing = 0);
        static std::shared_ptr<PolygonShape> PillStar(int numPoints, float innerRadiusRatio = 0.5f, float vertexSpacing = 0.5f,
                                                      float startLocation = 0, PolygonCornerRounding outerRounding = {},
                                                      std::optional<PolygonCornerRounding> innerRounding = std::nullopt);
        static std::shared_ptr<PolygonShape> Circle(int numVertices = 8);
        static std::shared_ptr<PolygonShape> Rectangle(PolygonCornerRounding rounding = {});
        // Start / end follow the layout direction.
        static std::shared_ptr<PolygonShape> Rectangle(PolygonCornerRounding topStart, PolygonCornerRounding topEnd,
                                                       PolygonCornerRounding bottomEnd, PolygonCornerRounding bottomStart);
        static std::shared_ptr<PolygonShape> AbsoluteRectangle(PolygonCornerRounding topLeft = {}, PolygonCornerRounding topRight = {},
                                                               PolygonCornerRounding bottomRight = {}, PolygonCornerRounding bottomLeft = {});

    protected:
        virtual gfx::shapes::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection dir, const ui::Density& density) const = 0;
        virtual std::shared_ptr<PolygonShape> CopyBase() const = 0;
        // Bumps when a builder's geometry changes for the same size.
        virtual int ContentVersion(Size, ui::LayoutDirection, const ui::Density&) const { return 0; }

    private:
        struct Transformation;
        std::shared_ptr<const gfx::shapes::RoundedPolygon> ResolveBase(Size size, ui::LayoutDirection dir, const ui::Density& density) const;
        mutable std::shared_ptr<const gfx::shapes::RoundedPolygon> m_Base;
        mutable Size m_BaseSize{ -1, -1 };
        mutable ui::LayoutDirection m_BaseDir = ui::LayoutDirection::Ltr;
        mutable ui::Density m_BaseDensity{ -1, -1 };
        mutable int m_BaseVersion = -1;
        std::shared_ptr<Transformation> m_Transform;
        mutable std::shared_ptr<const gfx::shapes::RoundedPolygon> m_OutlinePolygon;
        mutable std::optional<gfx::Outline> m_Outline;
    };

    // toPolygonShape: a rounded or cut corner shape as polygons (so it can morph).
    std::shared_ptr<PolygonShape> ToPolygonShape(const CornerBasedShape& shape);

    // MorphPolygonShape(start, end, progress): progress <= 0 is start, >= 1 is end.
    ShapePtr MorphPolygonShape(std::shared_ptr<PolygonShape> start, std::shared_ptr<PolygonShape> end, std::function<float()> progress);

}
