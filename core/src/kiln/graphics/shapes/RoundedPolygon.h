#pragma once

// androidx.graphics.shapes port, part 2: CornerRounding, Feature and RoundedPolygon
// (CornerRounding.kt, Features.kt, RoundedPolygon.kt).

#include "kiln/graphics/Path.h"
#include "kiln/graphics/shapes/Cubic.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace kiln::gfx::shapes {

    struct CornerRounding {
        float radius = 0;      // >= 0
        float smoothing = 0;   // 0..1
        static CornerRounding Unrounded() { return {}; }
        bool operator==(const CornerRounding&) const = default;
    };

    // A run of cubics that is either an edge (ignorable when morphing) or a convex/concave corner.
    struct Feature {
        enum class Kind : std::uint8_t { Edge, ConvexCorner, ConcaveCorner };
        Kind kind = Kind::Edge;
        std::vector<Cubic> cubics;

        bool IsIgnorableFeature() const { return kind == Kind::Edge; }
        bool IsEdge() const { return kind == Kind::Edge; }
        bool IsConvexCorner() const { return kind == Kind::ConvexCorner; }
        bool IsConcaveCorner() const { return kind == Kind::ConcaveCorner; }
        Feature Transformed(const PointTransformer& f) const;
        // Reversed cubics; a corner flips between convex and concave.
        Feature Reversed() const;
        bool operator==(const Feature&) const = default;

        static Feature Edge(std::vector<Cubic> c) { return { Kind::Edge, std::move(c) }; }
        static Feature Corner(std::vector<Cubic> c, bool convex) { return { convex ? Kind::ConvexCorner : Kind::ConcaveCorner, std::move(c) }; }

        // Feature.Factory: features for a custom RoundedPolygon. The cubics must be continuous
        // (each starts where the previous ends) and not empty.
        static Feature BuildIgnorableFeature(std::vector<Cubic> c) { return Validated(Edge(std::move(c))); }
        static Feature BuildEdge(const Cubic& c) { return Edge({ c }); }
        static Feature BuildConvexCorner(std::vector<Cubic> c) { return Validated(Corner(std::move(c), true)); }
        static Feature BuildConcaveCorner(std::vector<Cubic> c) { return Validated(Corner(std::move(c), false)); }

    private:
        static Feature Validated(Feature f) {
            assert(!f.cubics.empty() && "Features need at least one cubic.");
            for (std::size_t i = 1; i < f.cubics.size(); ++i)
                assert(std::abs(f.cubics[i].Anchor0X() - f.cubics[i - 1].Anchor1X()) <= DistanceEpsilon &&
                       std::abs(f.cubics[i].Anchor0Y() - f.cubics[i - 1].Anchor1Y()) <= DistanceEpsilon && "Feature must be continuous");
            return f;
        }
    };

    class RoundedPolygon {
    public:
        RoundedPolygon(std::vector<Feature> features, Point center);

        const std::vector<Feature>& Features() const { return m_Features; }
        const std::vector<Cubic>& Cubics() const { return m_Cubics; }
        float CenterX() const { return m_Center.x; }
        float CenterY() const { return m_Center.y; }
        Point Center() const { return m_Center; }

        RoundedPolygon Transformed(const PointTransformer& f) const;
        // Scaled and translated into the unit square, centred, aspect kept.
        RoundedPolygon Normalized() const;
        std::array<float, 4> CalculateMaxBounds() const;
        std::array<float, 4> CalculateBounds(bool approximate = true) const;
        bool operator==(const RoundedPolygon& o) const { return m_Features == o.m_Features; }

        // As a kiln path (cubics, closed).
        Path ToPath() const;

        // Factories (RoundedPolygon(...) constructors).
        static RoundedPolygon Regular(int numVertices, float radius = 1, float centerX = 0, float centerY = 0,
                                      CornerRounding rounding = {}, const std::vector<CornerRounding>* perVertex = nullptr);
        // Sentinel centre = the vertices' average (Float.MIN_VALUE in Kotlin).
        static constexpr float kAutoCenter = std::numeric_limits<float>::denorm_min();
        static RoundedPolygon FromVertices(const std::vector<float>& vertices, CornerRounding rounding = {},
                                           const std::vector<CornerRounding>* perVertex = nullptr,
                                           float centerX = kAutoCenter, float centerY = kAutoCenter);
        static RoundedPolygon FromFeatures(std::vector<Feature> features, float centerX = std::nanf(""), float centerY = std::nanf(""));

    private:
        std::vector<Feature> m_Features;
        Point m_Center;
        std::vector<Cubic> m_Cubics;
    };

    Point CalculateCenter(const std::vector<float>& vertices);

}
