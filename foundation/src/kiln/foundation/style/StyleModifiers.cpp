#include "kiln/foundation/style/StyleModifiers.h"

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Shadow.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Semantics.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    namespace {

        using ui::Constraints;

        int AddMaxWithMinimum(int max, int value) { return max == Constraints::Infinity ? max : std::max(max + value, 0); }

        // --- Placement.kt ---------------------------------------------------------------------

        struct PlacementParams {
            std::shared_ptr<const std::function<void(PlacementModifierScope&)>> block;
            bool operator==(const PlacementParams&) const = default;
        };
        struct PlacementNode final : ui::ModifierNode, ui::LayoutModifier {
            PlacementParams params;
            explicit PlacementNode(PlacementParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& measurable, const Constraints& constraints) override {
                // Reads in the block are observed by the measure.
                PlacementModifierScope scope;
                if (params.block && *params.block) (*params.block)(scope);
                auto px = [&](ui::Dp d) { return d.Specified() ? s.ToPx(d) : 0.0f; };
                const float start = px(scope.left), end = px(scope.right), top = px(scope.top), bottom = px(scope.bottom);
                const int horizontal = ui::RoundJava(start + end), vertical = ui::RoundJava(top + bottom);
                int minWidth = std::max(constraints.minWidth - horizontal, 0);
                int maxWidth = AddMaxWithMinimum(constraints.maxWidth, horizontal);
                int minHeight = std::max(constraints.minHeight - vertical, 0);
                int maxHeight = AddMaxWithMinimum(constraints.maxHeight, vertical);

                auto axis = [&](ui::Dp minD, ui::Dp maxD, const Breadth& size, int& lo, int& hi, bool bounded, bool bothSides) {
                    int styleMin = 0, styleMax = Constraints::Infinity;
                    if (maxD.Specified()) styleMax = std::max(ui::RoundJava(s.ToPx(maxD)), 0);
                    if (minD.Specified()) styleMin = std::clamp(ui::RoundJava(s.ToPx(minD)), 0, styleMax);
                    if (size.IsDistance()) {
                        styleMin = std::clamp(ui::RoundJava(s.ToPx(size.distance)), styleMin, styleMax);
                        styleMax = styleMin;
                    }
                    if (styleMin != 0) lo = std::clamp(styleMin, lo, hi);
                    if (styleMax != Constraints::Infinity) hi = std::clamp(styleMax, lo, hi);
                    // A fraction of what is left, after the incoming constraints.
                    if (size.IsFraction()) {
                        if (bounded) {
                            const int v = std::clamp(ui::RoundJava(float(hi) * size.fraction), lo, hi);
                            lo = hi = v;
                        } else if (bothSides) {
                            lo = hi;
                        }
                    }
                };
                axis(scope.minWidth, scope.maxWidth, scope.width, minWidth, maxWidth, constraints.HasBoundedWidth(),
                     scope.left.Specified() && scope.right.Specified());
                axis(scope.minHeight, scope.maxHeight, scope.height, minHeight, maxHeight, constraints.HasBoundedHeight(),
                     scope.top.Specified() && scope.bottom.Specified());

                ui::Placeable& p = measurable.Measure(Constraints{ minWidth, maxWidth, minHeight, maxHeight });
                // Anchored to the right / bottom when only that side is given.
                const int x = !scope.left.Specified() && scope.right.Specified() ? constraints.maxWidth - p.Width() - ui::RoundJava(end)
                                                                                 : ui::RoundJava(start);
                const int y = scope.bottom.Specified() && !scope.top.Specified() ? constraints.maxHeight - p.Height() - ui::RoundJava(bottom)
                                                                                 : ui::RoundJava(top);
                return s.Layout(p.Width() + horizontal, p.Height() + vertical, [&p, x, y](ui::PlacementScope& place) { place.Place(p, x, y); });
            }
        };

        // --- Appearance.kt --------------------------------------------------------------------

        struct AppearanceParams {
            std::shared_ptr<const std::function<void(AppearanceScope&)>> block;
            bool operator==(const AppearanceParams&) const = default;
        };
        struct AppearanceNode final : ui::ModifierNode, ui::DrawModifier, ui::SemanticsModifier {
            AppearanceParams params;
            explicit AppearanceNode(AppearanceParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            ui::DrawModifier* AsDraw() override { return this; }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void ApplySemantics(ui::SemanticsConfiguration& c) override {
                if (m_LastShape) c.shape = m_LastShape;
            }

            void Draw(ui::ContentDrawScope& s) override {
                // Reads in the block are observed by the draw.
                AppearanceScope scope;
                if (params.block && *params.block) (*params.block)(scope);
                const ShapePtr shape = scope.shape ? scope.shape : RectangleShape();
                if (scope.dropShadow.kind != Shadows::Kind::None) DrawShadows(s, scope.dropShadow, shape, false);

                const gfx::Outline outline = Outline(s, shape);
                const bool hasBorder = scope.borderWidth.Specified() && s.RoundToPx(scope.borderWidth / 2) > 0;
                auto fill = [&](const Fill& f) {
                    if (f.kind == Fill::Kind::Brush) DrawOutline(s, outline, f.brush);
                    else if (f.kind == Fill::Kind::Color && f.color.Specified()) DrawOutline(s, outline, f.color);
                };
                fill(scope.background);
                s.DrawContent();
                fill(scope.foreground);
                if (hasBorder) {
                    // Black when the border has no fill.
                    const gfx::Brush brush = scope.border.kind == Fill::Kind::Brush   ? scope.border.brush
                                             : scope.border.kind == Fill::Kind::Color ? gfx::Brush::SolidColor(scope.border.color)
                                                                                      : gfx::Brush::SolidColor(ui::Color(0xFF000000u));
                    m_Border.DrawBorder(s, scope.borderWidth, brush, outline);
                }
                if (scope.innerShadow.kind != Shadows::Kind::None) DrawShadows(s, scope.innerShadow, shape, true);
                if (m_LastShape != shape) {
                    m_LastShape = shape;
                    InvalidateSemantics();
                }
            }

        private:
            gfx::Outline Outline(ui::ContentDrawScope& s, const ShapePtr& shape) {
                const ui::Size size = s.GetSize();
                if (!(m_OutlineSize == size && m_OutlineDirection == s.Direction() && m_OutlineShape == shape)) {
                    m_Outline = shape->CreateOutline(size, s.Direction(), s);
                    m_OutlineSize = size;
                    m_OutlineDirection = s.Direction();
                    m_OutlineShape = shape;
                }
                return m_Outline;
            }
            // Each shadow's painter is kept while the shadow and shape stay the same.
            void DrawShadows(ui::ContentDrawScope& s, const Shadows& shadows, const ShapePtr& shape, bool inner) {
                auto& cache = inner ? m_Inner : m_Drop;
                if (cache.shape != shape) cache = {};
                cache.shape = shape;
                cache.shadows.resize(shadows.shadows.size());
                cache.painters.resize(shadows.shadows.size());
                for (std::size_t i = 0; i < shadows.shadows.size(); ++i) {
                    const auto& shadow = shadows.shadows[i];
                    if (!cache.painters[i] || !cache.shadows[i] || !(*cache.shadows[i] == shadow)) {
                        if (inner) cache.painters[i] = std::make_shared<InnerShadowPainter>(shape, shadow);
                        else cache.painters[i] = std::make_shared<DropShadowPainter>(shape, shadow);
                        cache.shadows[i] = shadow;
                    }
                    cache.painters[i]->Draw(s, s.GetSize());
                }
            }
            struct ShadowCache {
                ShapePtr shape;
                std::vector<std::optional<gfx::shadow::Shadow>> shadows;
                std::vector<std::shared_ptr<PaintedShadowPainter>> painters;
            };
            BorderRenderer m_Border;
            ShadowCache m_Drop, m_Inner;
            ShapePtr m_LastShape;
            gfx::Outline m_Outline;
            ui::Size m_OutlineSize{ -1, -1 };
            ui::LayoutDirection m_OutlineDirection = ui::LayoutDirection::Ltr;
            ShapePtr m_OutlineShape;
        };

        // --- StylePadding.kt ------------------------------------------------------------------

        enum class PaddingKind : std::uint8_t { Content, External, Border };
        struct StylePaddingParams {
            std::shared_ptr<StyleResolver> resolver;
            PaddingKind kind;
            bool operator==(const StylePaddingParams&) const = default;
        };
        struct StylePaddingNode final : ui::ModifierNode, ui::LayoutModifier {
            StylePaddingParams params;
            explicit StylePaddingNode(StylePaddingParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }

            // left, top, right, bottom in pixels for the direction (calculateLeftPadding ...).
            std::array<int, 4> Sides(const ui::MeasureScope& s) const {
                StyleResolver& r = *params.resolver;
                const bool ltr = s.Direction() == ui::LayoutDirection::Ltr;
                if (params.kind == PaddingKind::Border) {
                    const ui::Dp w = r.Resolve([&](const StyleResolverScope& sc) {
                        auto width = sc.GetOrNull(style::BorderWidth());
                        if (!width) return ui::Dp(0);
                        // Border rounds up where padding rounds: force the ceiling (Hairline: 1 px).
                        const float px = width->value == 0 ? 1.0f : std::ceil(sc.ToPx(*width));
                        return sc.ToDp(px);
                    });
                    const int v = s.RoundToPx(w);
                    return { v, v, v, v };
                }
                const bool content = params.kind == PaddingKind::Content;
                const auto& startP = content ? style::ContentPaddingStart() : style::ExternalPaddingStart();
                const auto& endP = content ? style::ContentPaddingEnd() : style::ExternalPaddingEnd();
                const auto& topP = content ? style::ContentPaddingTop() : style::ExternalPaddingTop();
                const auto& bottomP = content ? style::ContentPaddingBottom() : style::ExternalPaddingBottom();
                return r.Resolve([&](const StyleResolverScope& sc) {
                    const ui::Dp start = sc.Value(startP), end = sc.Value(endP);
                    return std::array<int, 4>{ s.RoundToPx(ltr ? start : end), s.RoundToPx(sc.Value(topP)), s.RoundToPx(ltr ? end : start),
                                               s.RoundToPx(sc.Value(bottomP)) };
                });
            }

            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const Constraints& c) override {
                const auto [left, top, right, bottom] = Sides(s);
                const int h = left + right, v = top + bottom;
                ui::Placeable& p = m.Measure(c.Offset(-h, -v));
                return s.Layout(c.ConstrainWidth(p.Width() + h), c.ConstrainHeight(p.Height() + v),
                                [&p, left, top](ui::PlacementScope& place) { place.Place(p, left, top); });
            }
            int MinIntrinsicWidth(ui::MeasureScope& s, ui::Measurable& m, int height) override {
                const auto d = Sides(s);
                return m.MinIntrinsicWidth(height == Constraints::Infinity ? height : std::max(0, height - d[1] - d[3])) + d[0] + d[2];
            }
            int MaxIntrinsicWidth(ui::MeasureScope& s, ui::Measurable& m, int height) override {
                const auto d = Sides(s);
                return m.MaxIntrinsicWidth(height == Constraints::Infinity ? height : std::max(0, height - d[1] - d[3])) + d[0] + d[2];
            }
            int MinIntrinsicHeight(ui::MeasureScope& s, ui::Measurable& m, int width) override {
                const auto d = Sides(s);
                return m.MinIntrinsicHeight(width == Constraints::Infinity ? width : std::max(0, width - d[0] - d[2])) + d[1] + d[3];
            }
            int MaxIntrinsicHeight(ui::MeasureScope& s, ui::Measurable& m, int width) override {
                const auto d = Sides(s);
                return m.MaxIntrinsicHeight(width == Constraints::Infinity ? width : std::max(0, width - d[0] - d[2])) + d[1] + d[3];
            }
        };

    }

    Modifier Placement(Modifier m, std::function<void(PlacementModifierScope&)> block) {
        return m.With<PlacementNode>(PlacementParams{ std::make_shared<const std::function<void(PlacementModifierScope&)>>(std::move(block)) });
    }

    Modifier Appearance(Modifier m, std::function<void(AppearanceScope&)> block) {
        return m.With<AppearanceNode>(AppearanceParams{ std::make_shared<const std::function<void(AppearanceScope&)>>(std::move(block)) });
    }

    Modifier GraphicsLayer(Modifier m, std::function<bool()> layerRequired, std::function<void(GraphicsLayerParams&)> block) {
        return GraphicsLayer(std::move(m), [layerRequired = std::move(layerRequired), block = std::move(block)](GraphicsLayerParams& p) {
            if (!layerRequired || layerRequired()) block(p);
        });
    }

    Modifier StylePlacement(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return Placement(std::move(m), [resolver](PlacementModifierScope& p) {
            resolver->Resolve([&](const StyleResolverScope& s) {
                IfSet(s, style::Top(), [&](ui::Dp v) { p.top = v; });
                IfSet(s, style::Bottom(), [&](ui::Dp v) { p.bottom = v; });
                IfSet(s, style::Left(), [&](ui::Dp v) { p.left = v; });
                IfSet(s, style::Right(), [&](ui::Dp v) { p.right = v; });
                IfSet(s, style::MinHeight(), [&](ui::Dp v) { p.minHeight = v; });
                IfSet(s, style::MaxHeight(), [&](ui::Dp v) { p.maxHeight = v; });
                IfSet(s, style::MinWidth(), [&](ui::Dp v) { p.minWidth = v; });
                IfSet(s, style::MaxWidth(), [&](ui::Dp v) { p.maxWidth = v; });
                IfSet(s, style::Width(), [&](const Breadth& v) { p.width = v; });
                IfSet(s, style::Height(), [&](const Breadth& v) { p.height = v; });
            });
        });
    }

    Modifier StyleAppearance(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return Appearance(std::move(m), [resolver](AppearanceScope& a) {
            resolver->Resolve([&](const StyleResolverScope& s) {
                IfSet(s, style::Shape(), [&](const ShapePtr& v) { a.shape = v; });
                IfSet(s, style::Background(), [&](const Fill& v) { a.background = v; });
                IfSet(s, style::Foreground(), [&](const Fill& v) { a.foreground = v; });
                IfSet(s, style::BorderFill(), [&](const Fill& v) { a.border = v; });
                IfSet(s, style::BorderWidth(), [&](ui::Dp v) { a.borderWidth = v; });
                IfSet(s, style::DropShadow(), [&](const Shadows& v) { a.dropShadow = v; });
                IfSet(s, style::InnerShadow(), [&](const Shadows& v) { a.innerShadow = v; });
            });
        });
    }

    Modifier StyleGraphicsLayer(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        auto required = [resolver] {
            static const std::vector<const StylePropertyBase*> layerProperties{
                style::Alpha().get(), style::TranslationX().get(), style::TranslationY().get(), style::TransformOriginX().get(),
                style::TransformOriginY().get(), style::RotationX().get(), style::RotationY().get(), style::RotationZ().get(),
                style::ScaleX().get(), style::ScaleY().get(), style::CameraDistance().get(), style::ColorFilter().get()
            };
            return resolver->Resolve([&](const StyleResolverScope& s) { return s.AnySet(layerProperties) || s.Value(style::Clip()); });
        };
        return GraphicsLayer(std::move(m), required, [resolver](GraphicsLayerParams& p) {
            resolver->Resolve([&](const StyleResolverScope& s) {
                p.clip = s.Value(style::Clip());
                p.clipShape = s.Value(style::Shape());
                p.alpha = s.Value(style::Alpha());
                p.translationX = s.Value(style::TranslationX());
                p.translationY = s.Value(style::TranslationY());
                p.originX = s.Value(style::TransformOriginX());
                p.originY = s.Value(style::TransformOriginY());
                p.rotationX = s.Value(style::RotationX());
                p.rotationY = s.Value(style::RotationY());
                p.rotationZ = s.Value(style::RotationZ());
                p.scaleX = s.Value(style::ScaleX());
                p.scaleY = s.Value(style::ScaleY());
                p.cameraDistance = s.Value(style::CameraDistance());
                p.colorFilter = s.Value(style::ColorFilter());
            });
        });
    }

    Modifier StyleContentPadding(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return m.With<StylePaddingNode>(StylePaddingParams{ std::move(resolver), PaddingKind::Content });
    }
    Modifier StyleExternalPadding(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return m.With<StylePaddingNode>(StylePaddingParams{ std::move(resolver), PaddingKind::External });
    }
    Modifier StyleBorderPadding(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return m.With<StylePaddingNode>(StylePaddingParams{ std::move(resolver), PaddingKind::Border });
    }

}
