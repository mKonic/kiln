#include "kiln/material3/MaterialShapes.h"

#include "kiln/graphics/shapes/Shapes.h"

#include <cmath>
#include <numbers>

namespace kiln::material3::MaterialShapes {

    using gfx::shapes::CornerRounding;
    using gfx::shapes::Point;

    namespace {
        constexpr float kPi = std::numbers::pi_v<float>;
        const CornerRounding cornerRound15{ 0.15f }, cornerRound20{ 0.2f }, cornerRound30{ 0.3f }, cornerRound50{ 0.5f }, cornerRound100{ 1.0f };

        // Matrix.rotateZ / scale, applied as RoundedPolygon.transformed(matrix).
        RoundedPolygon Rotated(const RoundedPolygon& p, float degrees) {
            const float a = degrees / 180 * kPi, c = std::cos(a), s = std::sin(a);
            return p.Transformed([c, s](float x, float y) { return Point{ c * x - s * y, s * x + c * y }; });
        }
        RoundedPolygon Scaled(const RoundedPolygon& p, float sx, float sy) {
            return p.Transformed([sx, sy](float x, float y) { return Point{ x * sx, y * sy }; });
        }

        struct PointNRound { Point o; CornerRounding r{}; };

        float ToRadians(float degrees) { return degrees / 360.0f * 2 * kPi; }
        float AngleDegrees(Point p) { return std::atan2(p.y, p.x) * 180.0f / kPi; }
        Point RotateDegrees(Point p, float angle, Point center) {
            const float a = ToRadians(angle);
            const Point off = p - center;
            return Point{ off.x * std::cos(a) - off.y * std::sin(a), off.x * std::sin(a) + off.y * std::cos(a) } + center;
        }

        // doRepeat: `reps` copies around the centre; mirroring alternates copies reflected
        // across each section's edge (the first point of a reflected copy is skipped).
        std::vector<PointNRound> DoRepeat(const std::vector<PointNRound>& points, int reps, Point center, bool mirroring) {
            std::vector<PointNRound> out;
            if (mirroring) {
                std::vector<float> angles, distances;
                for (const auto& p : points) {
                    const Point d = p.o - center;
                    angles.push_back(AngleDegrees(d));
                    distances.push_back(std::hypot(d.x, d.y));
                }
                const int actualReps = reps * 2;
                const float sectionAngle = 360.0f / float(actualReps);
                const int last = int(points.size()) - 1;
                for (int it = 0; it < actualReps; ++it) {
                    for (int index = 0; index <= last; ++index) {
                        const int i = it % 2 == 0 ? index : last - index;
                        if (i > 0 || it % 2 == 0) {
                            const float a = ToRadians(sectionAngle * float(it) +
                                                      (it % 2 == 0 ? angles[std::size_t(i)] : sectionAngle - angles[std::size_t(i)] + 2 * angles[0]));
                            out.push_back({ Point{ std::cos(a), std::sin(a) } * distances[std::size_t(i)] + center, points[std::size_t(i)].r });
                        }
                    }
                }
            } else {
                const int np = int(points.size());
                for (int it = 0; it < np * reps; ++it)
                    out.push_back({ RotateDegrees(points[std::size_t(it % np)].o, float(it / np) * 360.0f / float(reps), center), points[std::size_t(it % np)].r });
            }
            return out;
        }

        RoundedPolygon CustomPolygon(const std::vector<PointNRound>& pnr, int reps, Point center = { 0.5f, 0.5f }, bool mirroring = false) {
            const auto points = DoRepeat(pnr, reps, center, mirroring);
            std::vector<float> vertices;
            std::vector<CornerRounding> rounding;
            for (const auto& p : points) {
                vertices.push_back(p.o.x);
                vertices.push_back(p.o.y);
                rounding.push_back(p.r);
            }
            return RoundedPolygon::FromVertices(vertices, {}, &rounding, center.x, center.y);
        }

        using P = PointNRound;
        Point O(float x, float y) { return { x, y }; }
        CornerRounding R(float radius, float smoothing = 0) { return { radius, smoothing }; }
    }

    // Each shape is built once and normalized, as the companion's lazy properties do.
#define KILN_SHAPE(Name, ...) \
    const RoundedPolygon& Name() { static const RoundedPolygon polygon = [] { return __VA_ARGS__; }().Normalized(); return polygon; }

    KILN_SHAPE(Circle, gfx::shapes::Circle(10))
    KILN_SHAPE(Square, gfx::shapes::Rectangle(1, 1, cornerRound30))
    KILN_SHAPE(Slanted, CustomPolygon({ P{ O(0.926f, 0.970f), R(0.189f, 0.811f) }, P{ O(-0.021f, 0.967f), R(0.187f, 0.057f) } }, 2))
    KILN_SHAPE(Arch, [] {
        const std::vector<CornerRounding> r{ cornerRound100, cornerRound100, cornerRound20, cornerRound20 };
        return Rotated(RoundedPolygon::Regular(4, 1, 0, 0, {}, &r), -135);
    }())
    KILN_SHAPE(Fan, CustomPolygon({ P{ O(1.004f, 1.000f), R(0.148f, 0.417f) }, P{ O(0.000f, 1.000f), R(0.151f) }, P{ O(0.000f, -0.003f), R(0.148f) },
                                    P{ O(0.978f, 0.020f), R(0.803f) } }, 1))
    KILN_SHAPE(Arrow, CustomPolygon({ P{ O(0.500f, 0.892f), R(0.313f) }, P{ O(-0.216f, 1.050f), R(0.207f) }, P{ O(0.499f, -0.160f), R(0.215f, 1.000f) },
                                      P{ O(1.225f, 1.060f), R(0.211f) } }, 1))
    KILN_SHAPE(SemiCircle, [] {
        const std::vector<CornerRounding> r{ cornerRound20, cornerRound20, cornerRound100, cornerRound100 };
        return gfx::shapes::Rectangle(1.6f, 1, {}, &r);
    }())
    KILN_SHAPE(Oval, Rotated(Scaled(gfx::shapes::Circle(), 1, 0.64f), -45))
    KILN_SHAPE(Pill, CustomPolygon({ P{ O(0.961f, 0.039f), R(0.426f) }, P{ O(1.001f, 0.428f) }, P{ O(1.000f, 0.609f), R(1.000f) } }, 2, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Triangle, Rotated(RoundedPolygon::Regular(3, 1, 0, 0, cornerRound20), -90))
    KILN_SHAPE(Diamond, CustomPolygon({ P{ O(0.500f, 1.096f), R(0.151f, 0.524f) }, P{ O(0.040f, 0.500f), R(0.159f) } }, 2))
    KILN_SHAPE(ClamShell, CustomPolygon({ P{ O(0.171f, 0.841f), R(0.159f) }, P{ O(-0.020f, 0.500f), R(0.140f) }, P{ O(0.170f, 0.159f), R(0.159f) } }, 2))
    KILN_SHAPE(Pentagon, CustomPolygon({ P{ O(0.500f, -0.009f), R(0.172f) }, P{ O(1.030f, 0.365f), R(0.164f) }, P{ O(0.828f, 0.970f), R(0.169f) } }, 1,
                                       { 0.5f, 0.5f }, true))
    KILN_SHAPE(Gem, CustomPolygon({ P{ O(0.499f, 1.023f), R(0.241f, 0.778f) }, P{ O(-0.005f, 0.792f), R(0.208f) }, P{ O(0.073f, 0.258f), R(0.228f) },
                                    P{ O(0.433f, -0.000f), R(0.491f) } }, 1, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Sunny, gfx::shapes::Star(8, 1, 0.8f, cornerRound15))
    KILN_SHAPE(VerySunny, CustomPolygon({ P{ O(0.500f, 1.080f), R(0.085f) }, P{ O(0.358f, 0.843f), R(0.085f) } }, 8))
    KILN_SHAPE(Cookie4Sided, CustomPolygon({ P{ O(1.237f, 1.236f), R(0.258f) }, P{ O(0.500f, 0.918f), R(0.233f) } }, 4))
    KILN_SHAPE(Cookie6Sided, CustomPolygon({ P{ O(0.723f, 0.884f), R(0.394f) }, P{ O(0.500f, 1.099f), R(0.398f) } }, 6))
    KILN_SHAPE(Cookie7Sided, Rotated(gfx::shapes::Star(7, 1, 0.75f, cornerRound50), -90))
    KILN_SHAPE(Cookie9Sided, Rotated(gfx::shapes::Star(9, 1, 0.8f, cornerRound50), -90))
    KILN_SHAPE(Cookie12Sided, Rotated(gfx::shapes::Star(12, 1, 0.8f, cornerRound50), -90))
    KILN_SHAPE(Ghostish, CustomPolygon({ P{ O(0.500f, 0), R(1.000f) }, P{ O(1, 0), R(1.000f) }, P{ O(1, 1.140f), R(0.254f, 0.106f) },
                                         P{ O(0.575f, 0.906f), R(0.253f) } }, 1, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Clover4Leaf, CustomPolygon({ P{ O(0.500f, 0.074f) }, P{ O(0.725f, -0.099f), R(0.476f) } }, 4, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Clover8Leaf, CustomPolygon({ P{ O(0.500f, 0.036f) }, P{ O(0.758f, -0.101f), R(0.209f) } }, 8))
    KILN_SHAPE(Burst, CustomPolygon({ P{ O(0.500f, -0.006f), R(0.006f) }, P{ O(0.592f, 0.158f), R(0.006f) } }, 12))
    KILN_SHAPE(SoftBurst, CustomPolygon({ P{ O(0.193f, 0.277f), R(0.053f) }, P{ O(0.176f, 0.055f), R(0.053f) } }, 10))
    KILN_SHAPE(Boom, CustomPolygon({ P{ O(0.457f, 0.296f), R(0.007f) }, P{ O(0.500f, -0.051f), R(0.007f) } }, 15))
    KILN_SHAPE(SoftBoom, CustomPolygon({ P{ O(0.733f, 0.454f) }, P{ O(0.839f, 0.437f), R(0.532f) }, P{ O(0.949f, 0.449f), R(0.439f, 1.000f) },
                                         P{ O(0.998f, 0.478f), R(0.174f) } }, 16, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Flower, CustomPolygon({ P{ O(0.370f, 0.187f) }, P{ O(0.416f, 0.049f), R(0.381f) }, P{ O(0.479f, 0.001f), R(0.095f) } }, 8, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Puffy, Scaled(CustomPolygon({ P{ O(0.500f, 0.053f) }, P{ O(0.545f, -0.040f), R(0.405f) }, P{ O(0.670f, -0.035f), R(0.426f) },
                                             P{ O(0.717f, 0.066f), R(0.574f) }, P{ O(0.722f, 0.128f) }, P{ O(0.777f, 0.002f), R(0.360f) },
                                             P{ O(0.914f, 0.149f), R(0.660f) }, P{ O(0.926f, 0.289f), R(0.660f) }, P{ O(0.881f, 0.346f) },
                                             P{ O(0.940f, 0.344f), R(0.126f) }, P{ O(1.003f, 0.437f), R(0.255f) } }, 2, { 0.5f, 0.5f }, true),
                             1, 0.742f))
    KILN_SHAPE(PuffyDiamond, CustomPolygon({ P{ O(0.870f, 0.130f), R(0.146f) }, P{ O(0.818f, 0.357f) }, P{ O(1.000f, 0.332f), R(0.853f) } }, 4,
                                           { 0.5f, 0.5f }, true))
    KILN_SHAPE(PixelCircle, CustomPolygon({ P{ O(0.500f, 0.000f) }, P{ O(0.704f, 0.000f) }, P{ O(0.704f, 0.065f) }, P{ O(0.843f, 0.065f) },
                                            P{ O(0.843f, 0.148f) }, P{ O(0.926f, 0.148f) }, P{ O(0.926f, 0.296f) }, P{ O(1.000f, 0.296f) } }, 2,
                                          { 0.5f, 0.5f }, true))
    KILN_SHAPE(PixelTriangle, CustomPolygon({ P{ O(0.110f, 0.500f) }, P{ O(0.113f, 0.000f) }, P{ O(0.287f, 0.000f) }, P{ O(0.287f, 0.087f) },
                                              P{ O(0.421f, 0.087f) }, P{ O(0.421f, 0.170f) }, P{ O(0.560f, 0.170f) }, P{ O(0.560f, 0.265f) },
                                              P{ O(0.674f, 0.265f) }, P{ O(0.675f, 0.344f) }, P{ O(0.789f, 0.344f) }, P{ O(0.789f, 0.439f) },
                                              P{ O(0.888f, 0.439f) } }, 1, { 0.5f, 0.5f }, true))
    KILN_SHAPE(Bun, CustomPolygon({ P{ O(0.796f, 0.500f) }, P{ O(0.853f, 0.518f), R(1) }, P{ O(0.992f, 0.631f), R(1) }, P{ O(0.968f, 1.000f), R(1) } }, 2,
                                  { 0.5f, 0.5f }, true))
    KILN_SHAPE(Heart, CustomPolygon({ P{ O(0.500f, 0.268f), R(0.016f) }, P{ O(0.792f, -0.066f), R(0.958f) }, P{ O(1.064f, 0.276f), R(1.000f) },
                                      P{ O(0.501f, 0.946f), R(0.129f) } }, 1, { 0.5f, 0.5f }, true))

#undef KILN_SHAPE

}
