#pragma once

// Lighting (ui lighting/: Light.kt, LightAnchorModifier.kt, IlluminationModifier.kt,
// SurfaceMaterial.kt, SurfaceGeometrySpec.kt, LightShader.android.kt): lights shine on a node's
// surface, drawn over its content. The surface is a rectangle or a rounded rectangle with
// circular corners, flat or with a convex or concave bevel; the material reflects the lights
// diffusely, specularly (Blinn-Phong) and at grazing angles (Schlick Fresnel). Up to four lights:
// directional, point and spot, placed in the node, in the window, or at another node (an anchor).

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/State.h"

#include <limits>
#include <optional>
#include <vector>

namespace kiln::foundation {

    // LightAnchorState: the place a lightAnchor node is, for lights anchored to it.
    class LightAnchorState {
    public:
        const std::optional<ui::LayoutCoordinates>& Coordinates() const { return m_Coordinates.Get(); }
        // internal
        void SetCoordinates(std::optional<ui::LayoutCoordinates> c) { m_Coordinates.Set(std::move(c)); }
        const std::optional<ui::LayoutCoordinates>& PeekCoordinates() const { return m_Coordinates.Peek(); }

    private:
        rt::State<std::optional<ui::LayoutCoordinates>, rt::NeverEqual> m_Coordinates{ std::nullopt };
    };

    // LightPosition: in the illuminated node (Local), in the window (Global), or at an anchor.
    // An unspecified (NaN) offset leaves the light unplaced (it gives no light).
    struct LightPosition {
        enum class Kind : std::uint8_t { Local, Global, Anchored };
        Kind kind = Kind::Local;
        ui::DpOffset offset{ Dp(std::numeric_limits<float>::quiet_NaN()), Dp(std::numeric_limits<float>::quiet_NaN()) };
        LightAnchorState* state = nullptr;
        static LightPosition Local(ui::DpOffset offset) { return { Kind::Local, offset, nullptr }; }
        static LightPosition Global(ui::DpOffset offset) { return { Kind::Global, offset, nullptr }; }
        static LightPosition Anchored(LightAnchorState& state, ui::DpOffset offset = {}) { return { Kind::Anchored, offset, &state }; }
        bool IsSpecified() const { return offset.x.value == offset.x.value && offset.y.value == offset.y.value; }
        bool operator==(const LightPosition& o) const {
            auto same = [](Dp a, Dp b) { return a.value == b.value || (a.value != a.value && b.value != b.value); };
            return kind == o.kind && state == o.state && same(offset.x, o.offset.x) && same(offset.y, o.offset.y);
        }
    };

    // Light: one of DirectionalLight, PointLight, SpotLight.
    struct Light {
        enum class Type : std::uint8_t { Directional, Point, Spot };
        Type type = Type::Directional;
        ui::Color color;
        float intensity = 1;
        LightPosition position;           // point and spot
        float directionX = 0, directionY = 0, directionZ = 1;   // directional: towards the light
        Dp elevation{ 0 }, falloffRadius{ 0 };                  // point and spot
        ui::DpOffset targetOffset;                              // spot: where its axis points, from the light
        float coneAngleDegrees = 0;                              // spot
        bool operator==(const Light& o) const {
            return type == o.type && color == o.color && intensity == o.intensity && position == o.position && directionX == o.directionX &&
                   directionY == o.directionY && directionZ == o.directionZ && elevation.value == o.elevation.value &&
                   falloffRadius.value == o.falloffRadius.value && targetOffset.x.value == o.targetOffset.x.value &&
                   targetOffset.y.value == o.targetOffset.y.value && coneAngleDegrees == o.coneAngleDegrees;
        }
    };
    // DirectionalLight: light from a direction (x, y in the surface's plane, z towards the viewer).
    Light DirectionalLight(ui::Color color, float directionX, float directionY, float directionZ, float intensity);
    // DirectionalLight.fromAngles: from the angle in the plane (degrees clockwise from +x) and
    // the elevation above it (degrees).
    Light DirectionalLightFromAngles(ui::Color color, float planeAngleDegrees, float elevationDegrees, float intensity);
    // PointLight: light from a point `elevation` above `position`, fading to nothing at `falloffRadius`.
    Light PointLight(ui::Color color, LightPosition position, Dp elevation, Dp falloffRadius, float intensity);
    // SpotLight: a point light lighting only the cone of `coneAngleDegrees` around its axis to
    // `targetOffset` (on the surface, from the light).
    Light SpotLight(ui::Color color, LightPosition position, ui::DpOffset targetOffset, Dp elevation, Dp falloffRadius, float coneAngleDegrees,
                    float intensity);
    // SpotLight.fromAngles: the axis from the plane angle and the elevation angle (0, 90].
    Light SpotLightFromAngles(ui::Color color, LightPosition position, float planeAngleDegrees, float elevationDegrees, Dp elevation, Dp falloffRadius,
                              float coneAngleDegrees, float intensity);

    // SurfaceMaterial: how the surface reflects light; None reflects nothing.
    class SurfaceMaterial {
    public:
        static SurfaceMaterial None() { return {}; }
        SurfaceMaterial Diffuse(ui::Color color) const { SurfaceMaterial m = *this; m.diffuseColor = color; return m; }
        // shininess > 0: the specular exponent.
        SurfaceMaterial Specular(ui::Color color, float shininess) const { SurfaceMaterial m = *this; m.specularColor = color; m.specularShininess = std::max(shininess, 1e-6f); return m; }
        // reflectivity at normal incidence and the effect's strength, both in [0, 1].
        SurfaceMaterial Fresnel(float reflectivity, float intensity = 1) const {
            SurfaceMaterial m = *this; m.fresnelReflectivity = std::clamp(reflectivity, 0.0f, 1.0f); m.fresnelIntensity = std::clamp(intensity, 0.0f, 1.0f); return m;
        }
        bool IsDiffuseEnabled() const { return diffuseColor.Specified(); }
        bool IsSpecularEnabled() const { return specularColor.Specified(); }
        bool IsFresnelEnabled() const { return fresnelReflectivity > 0 && fresnelIntensity > 0; }
        bool operator==(const SurfaceMaterial&) const = default;

        ui::Color diffuseColor = ui::Color::Unspecified(), specularColor = ui::Color::Unspecified();
        float specularShininess = 1, fresnelReflectivity = 0, fresnelIntensity = 0;
    };

    // SurfaceGeometrySpec: the surface's shape (rectangles and rounded rectangles with circular
    // corners; others are not lit), its thickness (how far the surface stands above the plane
    // lights are placed over) and its bevel (convex: the edge curves away; concave: it dips).
    struct SurfaceGeometrySpec {
        enum class Kind : std::uint8_t { Flat, Convex, Concave };
        Kind kind = Kind::Flat;
        ShapePtr shape;
        Dp thickness{ 0 }, bevelWidth{ 0 };
        static SurfaceGeometrySpec Flat(ShapePtr shape, Dp thickness) { return { Kind::Flat, std::move(shape), thickness, Dp(0) }; }
        static SurfaceGeometrySpec Convex(ShapePtr shape, Dp thickness, Dp bevelWidth) { return { Kind::Convex, std::move(shape), thickness, bevelWidth }; }
        static SurfaceGeometrySpec Concave(ShapePtr shape, Dp thickness, Dp bevelWidth) { return { Kind::Concave, std::move(shape), thickness, bevelWidth }; }
        bool operator==(const SurfaceGeometrySpec& o) const {
            return kind == o.kind && shape == o.shape && thickness.value == o.thickness.value && bevelWidth.value == o.bevelWidth.value;
        }
    };

    // IlluminationScope: what an illuminate { } block sets.
    struct IlluminationScope {
        std::vector<Light> lights;
        SurfaceMaterial surfaceMaterial = SurfaceMaterial::None();
        SurfaceGeometrySpec surfaceGeometrySpec = SurfaceGeometrySpec::Flat(RectangleShape(), Dp(0));
        static int MaxLightsAllowed() { return 4; }
        static bool IsSupported() { return true; }
    };

    // Modifier.illuminate: the lights over the content, composited with `blendMode`.
    Modifier Illuminate(Modifier m, std::vector<Light> lights, SurfaceMaterial surfaceMaterial, SurfaceGeometrySpec surfaceGeometrySpec,
                        gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
    Modifier Illuminate(Modifier m, std::function<void(IlluminationScope&)> block, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
    // Modifier.lightAnchor: lights positioned Anchored(state) are placed relative to this node.
    Modifier LightAnchor(Modifier m, LightAnchorState& state);

}
