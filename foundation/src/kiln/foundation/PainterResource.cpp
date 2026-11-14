#include "kiln/foundation/PainterResource.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace kiln::foundation {

    namespace {
        using namespace gfx;
        using namespace gfx::vector;

        float Float(const ui::VectorElement& e, std::string_view name, float def) {
            const auto v = e.Attr(name);
            if (!v || v->empty()) return def;
            return std::strtof(std::string(*v).c_str(), nullptr);
        }
        int Int(const ui::VectorElement& e, std::string_view name, int def) {
            const auto v = e.Attr(name);
            if (!v || v->empty()) return def;
            return std::atoi(std::string(*v).c_str());
        }
        std::string Str(const ui::VectorElement& e, std::string_view name) {
            const auto v = e.Attr(name);
            return v ? std::string(*v) : std::string();
        }
        // A dimension in dp: "24dp" / "24dip" as given, "<n>px" through the density.
        ui::Dp DimensionDp(const ui::VectorElement& e, std::string_view name, float density) {
            const auto v = e.Attr(name);
            if (!v) return ui::Dp(0);
            const std::string s(*v);
            const float n = std::strtof(s.c_str(), nullptr);
            if (s.ends_with("px")) return ui::Dp(n / density);
            if (const auto px = ui::ParseResourceDimension(s, density, 1)) return ui::Dp(*px / density);
            return ui::Dp(n);
        }
        // getStrokeLineCap / getStrokeLineJoin: enum values by index or by name.
        StrokeCap Cap(const ui::VectorElement& e) {
            const std::string v = Str(e, "strokeLineCap");
            if (v == "round" || v == "1") return StrokeCap::Round;
            if (v == "square" || v == "2") return StrokeCap::Square;
            return StrokeCap::Butt;
        }
        StrokeJoin Join(const ui::VectorElement& e) {
            const std::string v = Str(e, "strokeLineJoin");
            if (v == "round" || v == "1") return StrokeJoin::Round;
            if (v == "bevel" || v == "2") return StrokeJoin::Bevel;
            return StrokeJoin::Miter;
        }
        TileMode Tile(std::string_view v) {
            if (v == "repeat" || v == "1") return TileMode::Repeated;
            if (v == "mirror" || v == "2") return TileMode::Mirror;
            return TileMode::Clamp;
        }
        std::vector<std::string_view> Split(std::string_view s, char sep) {
            std::vector<std::string_view> out;
            std::size_t at = 0;
            while (true) {
                const std::size_t end = s.find(sep, at);
                out.push_back(s.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
                if (end == std::string_view::npos) break;
                at = end + 1;
            }
            return out;
        }
        float F(std::string_view s) { return std::strtof(std::string(s).c_str(), nullptr); }

        // obtainBrushFromComplexColor: a solid colour, a gradient, or none (transparent / absent).
        std::optional<Brush> ComplexBrush(const ui::VectorElement& e, std::string_view name) {
            const auto v = e.Attr(name);
            if (!v || v->empty()) return std::nullopt;
            if (v->starts_with("gradient:")) {
                const auto f = Split(v->substr(9), ';');
                if (f.size() < 10) return std::nullopt;
                std::vector<ui::Color> colors;
                std::vector<float> stops;
                for (std::string_view stop : Split(f[9], ',')) {
                    const std::size_t at = stop.find('@');
                    const auto c = ui::ParseResourceColor(stop.substr(0, at));
                    colors.push_back(c.value_or(ui::Color::Transparent()));
                    stops.push_back(at == std::string_view::npos ? 0 : F(stop.substr(at + 1)));
                }
                if (colors.size() < 2) return std::nullopt;
                const TileMode tile = Tile(f[8]);
                if (f[0] == "radial" || f[0] == "1") return Brush::RadialGradient(colors, Offset{ F(f[5]), F(f[6]) }, F(f[7]), tile, stops);
                if (f[0] == "sweep" || f[0] == "2") return Brush::SweepGradient(colors, Offset{ F(f[5]), F(f[6]) }, stops);
                return Brush::LinearGradient(colors, Offset{ F(f[1]), F(f[2]) }, Offset{ F(f[3]), F(f[4]) }, tile, stops);
            }
            const auto c = ui::ParseResourceColor(*v);
            if (!c || c->Alpha() == 0) return std::nullopt;   // ComplexColorCompat.willDraw
            return Brush::SolidColor(*c);
        }
        BlendMode TintMode(const ui::VectorElement& e) {
            const std::string v = Str(e, "tintMode");
            if (v == "src_over" || v == "3") return BlendMode::SrcOver;
            if (v == "src_atop" || v == "9") return BlendMode::SrcAtop;
            if (v == "multiply" || v == "14") return BlendMode::Modulate;
            if (v == "screen" || v == "15") return BlendMode::Screen;
            if (v == "add" || v == "16") return BlendMode::Plus;
            return BlendMode::SrcIn;
        }
    }

    ImageVectorPtr BuildVectorResource(const std::vector<ui::VectorElement>& elements, float density) {
        if (elements.empty() || elements.front().tag != "vector")
            throw std::invalid_argument("Only VectorDrawables and rasterized asset types are supported ex. PNG, JPG, WEBP");
        const ui::VectorElement& v = elements.front();
        const float vw = Float(v, "viewportWidth", 0), vh = Float(v, "viewportHeight", 0);
        if (vw <= 0) throw std::invalid_argument("<VectorGraphic> tag requires viewportWidth > 0");
        if (vh <= 0) throw std::invalid_argument("<VectorGraphic> tag requires viewportHeight > 0");
        ui::Color tint = ui::Color::Unspecified();
        if (const auto t = v.Attr("tint")) tint = ui::ParseResourceColor(*t).value_or(ui::Color::Unspecified());
        ImageVector::Builder b(Str(v, "name"), DimensionDp(v, "width", density), DimensionDp(v, "height", density), vw, vh, tint, TintMode(v),
                               Str(v, "autoMirrored") == "true");
        // parseCurrentVectorNode: a clip path opens a group closed with its enclosing group.
        std::vector<int> stack;
        int nested = 0;
        for (std::size_t i = 1; i < elements.size(); ++i) {
            const ui::VectorElement& e = elements[i];
            if (e.end) {
                if (e.tag == "group") {
                    for (int k = 0; k < nested + 1; ++k) b.ClearGroup();
                    nested = stack.empty() ? 0 : stack.back();
                    if (!stack.empty()) stack.pop_back();
                }
                continue;
            }
            if (e.tag == "path") {
                if (!e.Attr("pathData")) throw std::invalid_argument("No path data available");
                VectorPath p;
                p.name = Str(e, "name");
                p.pathData = ParsePathString(Str(e, "pathData"));
                p.fill = ComplexBrush(e, "fillColor");
                p.fillAlpha = Float(e, "fillAlpha", 1);
                p.stroke = ComplexBrush(e, "strokeColor");
                p.strokeAlpha = Float(e, "strokeAlpha", 1);
                p.strokeLineWidth = Float(e, "strokeWidth", 1);
                p.strokeLineCap = Cap(e);
                p.strokeLineJoin = Join(e);
                p.strokeLineMiter = Float(e, "strokeMiterLimit", 4);
                p.trimPathStart = Float(e, "trimPathStart", 0);
                p.trimPathEnd = Float(e, "trimPathEnd", 1);
                p.trimPathOffset = Float(e, "trimPathOffset", 0);
                const std::string fill = Str(e, "fillType");
                p.pathFillType = (fill == "evenOdd" || fill == "1") ? FillType::EvenOdd : FillType::NonZero;
                b.AddPath(std::move(p));
            } else if (e.tag == "clip-path") {
                b.AddGroup(Str(e, "name"), DefaultRotation, DefaultPivotX, DefaultPivotY, DefaultScaleX, DefaultScaleY, DefaultTranslationX,
                           DefaultTranslationY, ParsePathString(Str(e, "pathData")));
                ++nested;
            } else if (e.tag == "group") {
                b.AddGroup(Str(e, "name"), Float(e, "rotation", DefaultRotation), Float(e, "pivotX", DefaultPivotX), Float(e, "pivotY", DefaultPivotY),
                           Float(e, "scaleX", DefaultScaleX), Float(e, "scaleY", DefaultScaleY), Float(e, "translateX", DefaultTranslationX),
                           Float(e, "translateY", DefaultTranslationY));
                stack.push_back(nested);
                nested = 0;
            }
        }
        (void)Int;
        return b.Build();
    }

    namespace {
        // The remember key: the drawable and what picks its variant.
        std::string ResourceKey(const char* name) {
            auto* c = rt::Composition::Current();
            const ui::Configuration& config = c->Read(ui::LocalConfiguration);
            const ui::Density d = c->Read(ui::LocalDensity);
            return (std::string(name) + "|" + (config.IsNightModeActive() ? "n" : "d") + "|" + std::to_string(d.density) + "|" +
                           (config.locales.empty() ? std::string() : config.locales.front()));
        }
    }

    ImageVectorPtr VectorResource(ui::DrawableRes id) {
        auto* c = rt::Composition::Current();
        const std::string key = ResourceKey(id.name);
        const float density = c->Read(ui::LocalDensity).density;
        return c->Remember<ImageVectorPtr>([&] { return BuildVectorResource(ui::ReadDrawableResource(id).vector, density); }, rt::Keys(key));
    }

    gfx::ImageBitmap ImageResource(ui::DrawableRes id) {
        auto* c = rt::Composition::Current();
        const std::string key = ResourceKey(id.name);
        return c->Remember<gfx::ImageBitmap>([&] {
            const ui::DrawableResource d = ui::ReadDrawableResource(id);
            if (d.IsVector()) throw std::invalid_argument(std::string("kiln: drawable '") + id.name + "' is a vector (use VectorResource)");
            return gfx::ImageBitmap::Decode(d.bitmap);
        }, rt::Keys(key));
    }

    std::shared_ptr<Painter> PainterResource(ui::DrawableRes id) {
        auto* c = rt::Composition::Current();
        const std::string key = ResourceKey(id.name);
        // Read once per key: a vector or a bitmap.
        auto& resource = c->Remember<std::shared_ptr<const ui::DrawableResource>>(
            [&] { return std::make_shared<const ui::DrawableResource>(ui::ReadDrawableResource(id)); }, rt::Keys(key));
        if (resource->IsVector()) {
            const float density = c->Read(ui::LocalDensity).density;
            auto& image = c->Remember<ImageVectorPtr>([&] { return BuildVectorResource(resource->vector, density); }, rt::Keys(key));
            return RememberVectorPainter(image);
        }
        auto& painter = c->Remember<std::shared_ptr<Painter>>(
            [&] { return std::make_shared<BitmapPainter>(gfx::ImageBitmap::Decode(resource->bitmap)); }, rt::Keys(key));
        return painter;
    }

}
