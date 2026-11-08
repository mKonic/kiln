#include "kiln/graphics/shadow/ShadowContext.h"

#include "kiln/graphics/Rasterizer.h"
#include "SkMaskBlur.h"

#include <limits>

namespace kiln::gfx::shadow {
    namespace {
        using ui::Offset;
        using ui::Rect;
        using ui::Size;

        int Dimension(float value) {
            const double n = std::ceil(double(value));
            return std::isfinite(n) && n > 0 && n <= std::numeric_limits<int>::max() ? int(n) : 0;
        }
        double Sigma(float radius) { return radius > 0 ? std::min(double(0.57735f * radius + 0.5f), 135.0) : 0; }
        unsigned Mul(unsigned a, unsigned b) { return (a * b + 127) / 255; }

        // Canvas(ImageBitmap(Alpha8)) operations used by Compose's shadow painters. Paths are
        // rasterised before the blur, retaining off-bitmap source pixels that can blur into it.
        struct Canvas {
            int width, height;
            std::vector<std::uint8_t> alpha;
            Canvas(int w, int h) : width(w), height(h), alpha(std::size_t(w) * h) {}
            Rect Bounds(int pad = 0) const { return { float(-pad), float(-pad), float(width) + pad, float(height) + pad }; }
            void DrawMask(const Mask& m, BlendMode blend = BlendMode::SrcOver) {
                const int x0 = std::max(0, m.x), y0 = std::max(0, m.y);
                const int x1 = std::min(width, m.x + m.width), y1 = std::min(height, m.y + m.height);
                for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                    const unsigned s = m.alpha[std::size_t(y - m.y) * m.width + x - m.x];
                    auto& d = alpha[std::size_t(y) * width + x];
                    if (blend == BlendMode::Clear) d = std::uint8_t(Mul(d, 255 - s));
                    else d = std::uint8_t(s + Mul(d, 255 - s));
                }
            }
            void BlurAndDraw(Mask m, float radius) {
                if (m.alpha.empty()) return;
                const double sigma = Sigma(radius);
                const int pad = skia_mask::Border(sigma);
                if (pad) {
                    m.alpha = skia_mask::Blur(m.alpha, m.width, m.height, sigma);
                    m.width += 2 * pad; m.height += 2 * pad; m.x -= pad; m.y -= pad;
                }
                DrawMask(m);
            }
            void DrawPath(const Path& path, float radius = 0, const Stroke* stroke = nullptr, BlendMode blend = BlendMode::SrcOver) {
                const auto polygons = stroke ? StrokeToPolygons(path, *stroke, {}) : path.Flatten({});
                const int pad = skia_mask::Border(Sigma(radius));
                Mask mask = Rasterize(polygons, stroke ? FillType::NonZero : path.fillType, Bounds(pad));
                if (blend == BlendMode::Clear) DrawMask(mask, blend);
                else BlurAndDraw(std::move(mask), radius);
            }
            void DrawImage(const Canvas& source, Offset offset, float radius) {
                const int pad = skia_mask::Border(Sigma(radius));
                const Rect b = Bounds(pad).Intersect({ std::floor(offset.x), std::floor(offset.y),
                                                      std::ceil(offset.x + source.width), std::ceil(offset.y + source.height) });
                if (b.Empty()) return;
                Mask m{ int(b.left), int(b.top), int(b.Width()), int(b.Height()), {} };
                m.alpha.resize(std::size_t(m.width) * m.height);
                auto at = [&](int x, int y) -> float {
                    return x >= 0 && y >= 0 && x < source.width && y < source.height ? source.alpha[std::size_t(y) * source.width + x] : 0;
                };
                for (int y = 0; y < m.height; ++y) for (int x = 0; x < m.width; ++x) {
                    const float sx = x + m.x - offset.x, sy = y + m.y - offset.y;
                    const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    const float fx = sx - ix, fy = sy - iy;
                    const float a = std::lerp(at(ix, iy), at(ix + 1, iy), fx);
                    const float c = std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), fx);
                    m.alpha[std::size_t(y) * m.width + x] = std::uint8_t(ui::RoundJava(std::lerp(a, c, fy)));
                }
                BlurAndDraw(std::move(m), radius);
            }
            void Invert() { for (auto& a : alpha) a = 255 - a; }   // opaque rect XOR the A8 mask
            ImageBitmap Image(const Mask* coverage = nullptr) const {
                const int w = coverage ? coverage->width : width, h = coverage ? coverage->height : height;
                const int ox = coverage ? coverage->x : 0, oy = coverage ? coverage->y : 0;
                if (!w || !h) return {};
                std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                    const std::size_t i = std::size_t(y) * w + x;
                    // ImageShader's default Clamp sampling, including paths outside the size.
                    rgba[i * 4] = alpha[std::size_t(std::clamp(y + oy, 0, height - 1)) * width + std::clamp(x + ox, 0, width - 1)];
                    rgba[i * 4 + 1] = coverage ? coverage->alpha[i] : 255;
                    rgba[i * 4 + 3] = 255;
                }
                return ImageBitmap::FromPixels(w, h, rgba, true);
            }
        };

        Path RoundPath(Rect r, Offset radius) {
            Path p;
            if (!r.Empty()) p.AddRoundRect(RoundRect{ r, radius, radius, radius, radius });
            return p;
        }

        // ShadowRenderer.updateParamsFromOutline: simple outlines consume the draw size, whereas
        // nonuniform corners and Generic outlines consume the path itself.
        std::shared_ptr<const ShadowMask> Build(const Outline& outline, Size size, float radius, float spread, Offset offset, bool inner) {
            const auto* round = std::get_if<RoundRect>(&outline.value);
            const bool simple = std::holds_alternative<Rect>(outline.value) ||
                (round && round->topLeft == round->topRight && round->topLeft == round->bottomRight && round->topLeft == round->bottomLeft);
            const Offset corner = simple && round ? round->topLeft : Offset{};
            const int width = Dimension(inner ? std::ceil(size.width) + (simple ? 0 : 2 * std::ceil(radius)) : size.width + 2 * radius + 2 * spread);
            const int height = Dimension(inner ? std::ceil(size.height) + (simple ? 0 : 2 * std::ceil(radius)) : size.height + 2 * radius + 2 * spread);
            // All bitmap byte counts must fit Android's signed byte-count representation.
            if (!width || !height || std::uint64_t(width) * height > std::numeric_limits<int>::max() / 4) return {};
            const Path original = outline.ToPath();
            if (!std::all_of(original.Points().begin(), original.Points().end(), [](Offset p) { return std::isfinite(p.x) && std::isfinite(p.y); }) ||
                !std::all_of(original.ConicWeights().begin(), original.ConicWeights().end(), [](float w) { return std::isfinite(w); })) return {};
            const Rect pathBounds = original.Bounds();
            const int pathWidth = Dimension(pathBounds.Width()), pathHeight = Dimension(pathBounds.Height());
            if (!simple && (!pathWidth || !pathHeight || std::uint64_t(pathWidth) * pathHeight > std::numeric_limits<int>::max() / 4 ||
                std::fabs(pathBounds.left) >= float(std::numeric_limits<int>::max() / 2) ||
                std::fabs(pathBounds.top) >= float(std::numeric_limits<int>::max() / 2) ||
                std::fabs(pathBounds.right) >= float(std::numeric_limits<int>::max() / 2) ||
                std::fabs(pathBounds.bottom) >= float(std::numeric_limits<int>::max() / 2))) return {};
            Canvas bitmap(width, height);
            if (!inner) {
                if (simple) {
                    bitmap.DrawPath(RoundPath({ radius, radius, size.width + radius + 2 * spread, size.height + radius + 2 * spread }, corner), radius);
                } else {
                    Path p = original;
                    const float translation = spread > 0 ? radius + spread : radius;
                    p.Transform({ 1, 0, 0, 1, translation, translation });
                    bitmap.DrawPath(p, radius);
                    if (spread > 0) {
                        Stroke stroke; stroke.width = 2 * spread;
                        // Compose draws the fill and blurred stroke separately with SrcOver.
                        bitmap.DrawPath(p, radius, &stroke);
                    }
                }
            } else {
                if (simple) {
                    const float left = offset.x + spread, top = offset.y + spread;
                    bitmap.DrawPath(RoundPath({ left, top, std::max(left, offset.x + size.width - spread),
                                               std::max(top, offset.y + size.height - spread) }, corner), radius);
                } else if (spread > 0) {
                    const Rect bounds = original.Bounds();
                    const int pw = Dimension(bounds.Width()), ph = Dimension(bounds.Height());
                    if (!pw || !ph || std::uint64_t(pw) * ph > std::numeric_limits<int>::max() / 4) return {};
                    Canvas pathBitmap(pw, ph);
                    pathBitmap.DrawPath(original);
                    Stroke stroke; stroke.width = 2 * spread;
                    pathBitmap.DrawPath(original, 0, &stroke, BlendMode::Clear);
                    bitmap.DrawImage(pathBitmap, offset, radius);
                } else {
                    Path p = original;
                    p.Transform({ 1, 0, 0, 1, offset.x, offset.y });
                    bitmap.DrawPath(p, radius);
                }
                bitmap.Invert();
            }
            auto result = std::make_shared<ShadowMask>();
            if (inner) {
                const Path clip = simple ? RoundPath({ 0, 0, size.width, size.height }, corner) : original;
                const Mask coverage = Rasterize(clip.Flatten({}), clip.fillType, clip.Bounds());
                result->image = bitmap.Image(&coverage);
                result->origin = { float(coverage.x), float(coverage.y) };
            } else result->image = bitmap.Image();
            return result;
        }
    }

    std::shared_ptr<const ShadowMask> ShadowContext::Obtain(std::shared_ptr<const Shape> shape, ui::Size size,
                                                           ui::LayoutDirection direction, const ui::Density& density,
                                                           const Shadow& shadow, bool inner) {
        const float radius = density.ToPx(shadow.radius), spread = density.ToPx(shadow.spread);
        const Offset rawOffset{ density.ToPx(shadow.offset.x), density.ToPx(shadow.offset.y) };
        const Offset offset = inner ? rawOffset : Offset{};
        if (!shape || !std::isfinite(density.density) || density.density <= 0 ||
            !std::isfinite(size.width) || !std::isfinite(size.height) || size.width <= 0 || size.height <= 0 ||
            !std::isfinite(radius) || !std::isfinite(spread) || !std::isfinite(rawOffset.x) || !std::isfinite(rawOffset.y)) return {};
        std::erase_if(m_Cache, [](const Entry& e) { return e.mask.expired(); });
        for (const auto& e : m_Cache) {
            if (e.size == size && e.direction == direction && e.density == density.density && e.radius == radius &&
                e.spread == spread && e.offset == offset && e.inner == inner && e.shape->Equals(*shape)) return e.mask.lock();
        }
        auto mask = Build(shape->CreateOutline(size, direction, density), size, radius, spread, offset, inner);
        if (mask) m_Cache.push_back({ std::move(shape), size, direction, density.density, radius, spread, offset, inner, mask });
        return mask;
    }
}
