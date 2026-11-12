#include "kiln/foundation/Lighting.h"

#include "kiln/graphics/ColorSpace.h"

#include <cmath>
#include <memory>

namespace kiln::foundation {

    namespace {
        constexpr float kPi = 3.14159265358979f;
        float Radians(float degrees) { return degrees * (kPi / 180.0f); }
    }

    Light DirectionalLight(ui::Color color, float x, float y, float z, float intensity) {
        Light l;
        l.type = Light::Type::Directional;
        l.color = color;
        l.directionX = x; l.directionY = y; l.directionZ = z;
        l.intensity = std::max(intensity, 0.0f);
        return l;
    }

    Light DirectionalLightFromAngles(ui::Color color, float planeAngleDegrees, float elevationDegrees, float intensity) {
        const float e = Radians(elevationDegrees), p = Radians(planeAngleDegrees);
        return DirectionalLight(color, std::cos(e) * std::cos(p), std::cos(e) * std::sin(p), std::sin(e), intensity);
    }

    Light PointLight(ui::Color color, LightPosition position, Dp elevation, Dp falloffRadius, float intensity) {
        Light l;
        l.type = Light::Type::Point;
        l.color = color;
        l.position = position;
        l.elevation = elevation;
        l.falloffRadius = falloffRadius;
        l.intensity = std::max(intensity, 0.0f);
        return l;
    }

    Light SpotLight(ui::Color color, LightPosition position, ui::DpOffset targetOffset, Dp elevation, Dp falloffRadius, float coneAngleDegrees,
                    float intensity) {
        Light l = PointLight(color, position, elevation, falloffRadius, intensity);
        l.type = Light::Type::Spot;
        l.targetOffset = targetOffset;
        l.coneAngleDegrees = std::clamp(coneAngleDegrees, 0.0f, 180.0f);
        return l;
    }

    Light SpotLightFromAngles(ui::Color color, LightPosition position, float planeAngleDegrees, float elevationDegrees, Dp elevation, Dp falloffRadius,
                              float coneAngleDegrees, float intensity) {
        // calculateTargetOffsetFromAngles: the axis reaches the surface this far from the light.
        const float e = Radians(std::clamp(elevationDegrees, 1e-3f, 90.0f)), p = Radians(planeAngleDegrees);
        const float reach = elevation.value * (std::cos(e) / std::sin(e));
        return SpotLight(color, position, { Dp(reach * -std::cos(p)), Dp(reach * -std::sin(p)) }, elevation, falloffRadius, coneAngleDegrees, intensity);
    }

    namespace {
        struct IlluminateParams {
            std::vector<Light> lights;
            SurfaceMaterial material;
            SurfaceGeometrySpec geometry;
            std::shared_ptr<const std::function<void(IlluminationScope&)>> block;
            gfx::BlendMode blend = gfx::BlendMode::SrcOver;
            bool operator==(const IlluminateParams& o) const {
                return lights == o.lights && material == o.material && geometry == o.geometry && block == o.block && blend == o.blend;
            }
        };

        constexpr std::size_t kRow = 24;

        // Light.inLocalSpace: the light's place in the node (unplaced when it cannot be resolved).
        std::optional<Offset> ResolvePosition(const LightPosition& pos, const ui::LayoutCoordinates& here, const ui::Density& d) {
            if (!pos.IsSpecified()) return std::nullopt;
            const Offset px{ d.ToPx(pos.offset.x), d.ToPx(pos.offset.y) };
            switch (pos.kind) {
                case LightPosition::Kind::Local: return px;
                case LightPosition::Kind::Global: {
                    if (!here.IsAttached()) return std::nullopt;
                    const ui::LayoutCoordinates root = here.FindRootCoordinates();
                    if (!root.IsAttached()) return std::nullopt;
                    return here.LocalPositionOf(root, px);
                }
                case LightPosition::Kind::Anchored: {
                    if (!pos.state) return std::nullopt;
                    const auto& anchor = pos.state->Coordinates();
                    if (!anchor || !anchor->IsAttached() || !here.IsAttached() || !(anchor->FindRootCoordinates() == here.FindRootCoordinates()))
                        return std::nullopt;
                    return here.LocalPositionOf(*anchor, px);
                }
            }
            return std::nullopt;
        }

        void PutColor(float* out, ui::Color c) {
            const auto v = gfx::colorspace::ConvertColor(c, gfx::colorspace::ColorSpaces::LinearExtendedSrgb());
            out[0] = v[0]; out[1] = v[1]; out[2] = v[2]; out[3] = v[3];
        }

        // BaseLightShader's uniforms as quad_body.glsl's lightColor reads them; empty when the
        // surface's shape is not a rectangle or a rounded rectangle with circular corners.
        std::vector<float> Uniforms(const IlluminationScope& s, Size size, const ui::LayoutCoordinates& here, const ui::Density& d, ui::LayoutDirection dir) {
            const SurfaceGeometrySpec& g = s.surfaceGeometrySpec;
            if (!g.shape) return {};
            const gfx::Outline outline = g.shape->CreateOutline(size, dir, d);
            ui::Rect bounds;
            float radii[4] = { 0, 0, 0, 0 };
            if (const auto* r = std::get_if<ui::Rect>(&outline.value)) {
                bounds = *r;
            } else if (const auto* rr = std::get_if<gfx::RoundRect>(&outline.value)) {
                if (rr->topLeft.x != rr->topLeft.y || rr->topRight.x != rr->topRight.y || rr->bottomRight.x != rr->bottomRight.y ||
                    rr->bottomLeft.x != rr->bottomLeft.y)
                    return {};
                bounds = rr->rect;
                radii[0] = rr->topLeft.x; radii[1] = rr->topRight.x; radii[2] = rr->bottomRight.x; radii[3] = rr->bottomLeft.x;
            } else {
                return {};
            }
            const std::size_t count = std::min<std::size_t>(s.lights.size(), std::size_t(IlluminationScope::MaxLightsAllowed()));
            std::vector<float> u((2 + count) * kRow, 0.0f);
            const float bevel = g.kind == SurfaceGeometrySpec::Kind::Flat ? 0.0f : std::clamp(d.ToPx(g.bevelWidth), 0.0f, std::max(0.0f, std::min(size.width, size.height) * 0.5f));
            const float tilt = g.kind == SurfaceGeometrySpec::Kind::Convex ? 1.0f : g.kind == SurfaceGeometrySpec::Kind::Concave ? -1.0f : 0.0f;
            const float row0[16] = { bounds.left, bounds.right, bounds.top, bounds.bottom, radii[0], radii[1], radii[2], radii[3],
                                     bevel, tilt, d.ToPx(g.thickness), float(count),
                                     s.surfaceMaterial.IsDiffuseEnabled() ? 1.0f : 0.0f, s.surfaceMaterial.IsSpecularEnabled() ? 1.0f : 0.0f,
                                     s.surfaceMaterial.IsFresnelEnabled() ? 1.0f : 0.0f, s.surfaceMaterial.specularShininess };
            std::copy(std::begin(row0), std::end(row0), u.begin());
            if (s.surfaceMaterial.IsDiffuseEnabled()) PutColor(&u[16], s.surfaceMaterial.diffuseColor);
            if (s.surfaceMaterial.IsSpecularEnabled()) PutColor(&u[20], s.surfaceMaterial.specularColor);
            u[kRow + 0] = s.surfaceMaterial.fresnelReflectivity;
            u[kRow + 1] = s.surfaceMaterial.fresnelIntensity;
            u[kRow + 2] = 1;   // alpha, filled when drawn
            for (std::size_t i = 0; i < count; ++i) {
                const Light& l = s.lights[i];
                float* r = &u[(2 + i) * kRow];
                r[0] = float(int(l.type));
                r[3] = l.intensity;
                PutColor(&r[4], l.color);
                if (l.type == Light::Type::Directional) {
                    r[12] = l.directionX; r[13] = l.directionY; r[14] = l.directionZ;
                    continue;
                }
                r[1] = d.ToPx(l.falloffRadius);
                const float elevation = d.ToPx(l.elevation);
                if (l.type == Light::Type::Spot) {
                    r[2] = std::cos(Radians(l.coneAngleDegrees));
                    r[12] = d.ToPx(l.targetOffset.x); r[13] = d.ToPx(l.targetOffset.y); r[14] = -elevation;
                }
                if (const auto p = ResolvePosition(l.position, here, d)) {
                    r[8] = p->x; r[9] = p->y; r[10] = elevation;
                } else {
                    r[3] = 0;   // unplaced: no light
                }
            }
            return u;
        }

        class IlluminateNode final : public ModifierNode, public ui::DrawModifier, public ui::GlobalPositionAwareModifier {
        public:
            IlluminateParams params;
            explicit IlluminateNode(IlluminateParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            ui::DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }

            void Draw(ui::ContentDrawScope& s) override {
                IlluminationScope scope;
                if (params.block) {
                    if (*params.block) (*params.block)(scope);
                } else {
                    scope.lights = params.lights;
                    scope.surfaceMaterial = params.material;
                    scope.surfaceGeometrySpec = params.geometry;
                }
                m_HasPlacedLights = false;
                for (const Light& l : scope.lights) if (l.type != Light::Type::Directional && l.position.kind != LightPosition::Kind::Local) m_HasPlacedLights = true;
                s.DrawContent();
                const Size size = s.GetSize();
                if (scope.lights.empty() || !(size.width > 0) || !(size.height > 0) || !Owner()) return;
                std::vector<float> u = Uniforms(scope, size, ui::RequireLayoutCoordinates(*this), s, s.Direction());
                if (u.empty()) return;
                s.Save();
                s.SetBlendMode(params.blend);
                s.DrawLighting({ 0, 0, size.width, size.height }, std::move(u));
                s.Restore();
            }
            // Lights placed in the window or at an anchor move when this node does.
            void OnGloballyPositioned() override { if (m_HasPlacedLights) InvalidateDraw(); }

        private:
            bool m_HasPlacedLights = false;
        };

        struct AnchorParams {
            LightAnchorState* state;
            bool operator==(const AnchorParams&) const = default;
        };
        class LightAnchorNode final : public ModifierNode, public ui::GlobalPositionAwareModifier, public ui::LayoutAwareModifier {
        public:
            AnchorParams params;
            explicit LightAnchorNode(AnchorParams p) : params(p) {}
            void OnParamsChanged() {
                if (params.state == m_Published) return;
                const auto coords = m_Coords;
                Release();
                if (coords) Publish(*coords);
            }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }
            void OnGloballyPositioned() override { if (Owner()) Publish(ui::RequireLayoutCoordinates(*this)); }
            void OnUnplaced() override { Release(); }
            void OnDetach() override { Release(); }

        private:
            void Publish(const ui::LayoutCoordinates& c) {
                m_Coords = c;
                m_Published = params.state;
                if (params.state) params.state->SetCoordinates(c);
            }
            void Release() {
                if (m_Published && m_Coords && m_Published->PeekCoordinates() == m_Coords) m_Published->SetCoordinates(std::nullopt);
                m_Coords.reset();
                m_Published = nullptr;
            }
            std::optional<ui::LayoutCoordinates> m_Coords;
            LightAnchorState* m_Published = nullptr;
        };
    }

    Modifier Illuminate(Modifier m, std::vector<Light> lights, SurfaceMaterial material, SurfaceGeometrySpec geometry, gfx::BlendMode blendMode) {
        return m.With<IlluminateNode>(IlluminateParams{ std::move(lights), std::move(material), std::move(geometry), nullptr, blendMode });
    }

    Modifier Illuminate(Modifier m, std::function<void(IlluminationScope&)> block, gfx::BlendMode blendMode) {
        return m.With<IlluminateNode>(IlluminateParams{ {}, {}, {}, std::make_shared<const std::function<void(IlluminationScope&)>>(std::move(block)), blendMode });
    }

    Modifier LightAnchor(Modifier m, LightAnchorState& state) { return m.With<LightAnchorNode>(AnchorParams{ &state }); }

}
