// App resources (ui res/*Resources.android.kt): R:: names over the tables tools/gen-app-res
// writes from tests/res, as a desktop app reads them; the format and plural pieces they use.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/PainterResource.h"
#include "kiln/text/LocaleFormat.h"
#include "kiln/ui/AppResources.h"

#include "kiln_res.h"

#include <cmath>
#include <variant>

using namespace kiln;

namespace {
    // The resources read with this configuration (as content reads LocalConfiguration).
    template<typename F>
    auto WithConfig(ui::Configuration config, F&& read) {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::optional<std::invoke_result_t<F>> out;
        composition.SetContent([&] {
            rt::Composition::Current()->Provide(ui::LocalConfiguration, config, [&] { out.emplace(read()); });
        });
        rt::Composition::RecomposeAll();
        composition.SetContent([] {});
        return std::move(*out);
    }
    ui::Configuration Locale(const char* tag, bool night = false) {
        ui::Configuration c;
        c.locales = { tag };
        if (night) c.uiMode = ui::Configuration::kUiModeTypeNormal | ui::Configuration::kUiModeNightYes;
        return c;
    }
}

TEST(AppStringsResolveEscapesReferencesAndLocales) {
    CHECK(WithConfig(Locale("en-US"), [] { return ui::StringResource(R::string::app_name); }) == "Shelf");
    CHECK(WithConfig(Locale("de-DE"), [] { return ui::StringResource(R::string::app_name); }) == "Regal");
    // A locale without its own value falls back to the default resources.
    CHECK(WithConfig(Locale("fr-FR"), [] { return ui::StringResource(R::string::app_name); }) == "Shelf");
    CHECK(WithConfig(Locale("en"), [] { return ui::StringResource(R::string::quoted); }) == "It's here");
    CHECK(WithConfig(Locale("en"), [] { return ui::StringResource(R::string::alias); }) == "Shelf");
    CHECK(WithConfig(Locale("en"), [] { return ui::StringResource(R::string::greeting, "Ada", 3); }) == "Hello, Ada! 3 new");
    CHECK(WithConfig(Locale("en"), [] { return ui::StringResource(R::string::size, 1.25); }) == "1.3 MB");
    CHECK(WithConfig(Locale("de"), [] { return ui::StringResource(R::string::size, 1.25); }) == "1,3 MB");
}

TEST(AppPluralsFollowEachLanguagesRules) {
    auto chapters = [](const char* tag, int n) { return WithConfig(Locale(tag), [n] { return ui::PluralStringResource(R::plurals::chapters, n, n); }); };
    CHECK(chapters("en", 1) == "1 chapter");
    CHECK(chapters("en", 0) == "0 chapters");
    CHECK(chapters("de", 1) == "1 Kapitel");
    CHECK(chapters("de", 7) == "7 Kapitel insgesamt");
    CHECK(chapters("ru", 1) == "1 глава");
    CHECK(chapters("ru", 3) == "3 главы");
    CHECK(chapters("ru", 5) == "5 глав");
    CHECK(chapters("ru", 21) == "21 глава");
    CHECK(chapters("ru", 112) == "112 глав");
    // Japanese has only "other", which the default resources hold.
    CHECK(chapters("ja", 1) == "1 chapters");
    CHECK(ui::PluralCategoryOf("pl", 22) == ui::PluralCategory::Few);
    CHECK(ui::PluralCategoryOf("pl", 25) == ui::PluralCategory::Many);
    CHECK(ui::PluralCategoryOf("fr", 0) == ui::PluralCategory::One);
    CHECK(ui::PluralCategoryOf("ar", 100) == ui::PluralCategory::Other);
    CHECK(ui::PluralCategoryOf("ar", 11) == ui::PluralCategory::Many);
}

TEST(AppValuesArraysColoursAndDimensions) {
    const auto sorts = WithConfig(Locale("en"), [] { return ui::StringArrayResource(R::array::sorts); });
    CHECK((sorts == std::vector<std::string>{ "Title", "Shelf" }));
    CHECK((WithConfig(Locale("en"), [] { return ui::IntegerArrayResource(R::array::steps); }) == std::vector<int>{ 1, 5 }));
    CHECK(WithConfig(Locale("en"), [] { return ui::ColorResource(R::color::accent); }) == ui::Color(0xFF6750A4u));
    CHECK(WithConfig(Locale("en", true), [] { return ui::ColorResource(R::color::accent); }) == ui::Color(0xFFD0BCFFu));
    CHECK(WithConfig(Locale("en"), [] { return ui::ColorResource(R::color::accent_alias); }) == ui::Color(0xFF6750A4u));
    CHECK(WithConfig(Locale("en"), [] { return ui::DimensionResource(R::dimen::gutter); }).value == 16.0f);
    CHECK(WithConfig(Locale("en"), [] { return ui::IntegerResource(R::integer::columns); }) == 3);
    CHECK(WithConfig(Locale("en"), [] { return ui::BooleanResource(R::bool_::tablet); }));
    bool threw = false;
    try { (void)WithConfig(Locale("en"), [] { return ui::StringResource(ui::StringRes{ "missing" }); }); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
}

TEST(AppVectorDrawableBuildsItsGroupsClipsPathsAndGradients) {
    const ui::DrawableResource d = WithConfig(Locale("en"), [] { return ui::ReadDrawableResource(R::drawable::ic_badge); });
    CHECK(d.IsVector());
    const auto image = foundation::BuildVectorResource(d.vector, 2.0f);
    CHECK(image->defaultWidth.value == 24.0f && image->defaultHeight.value == 20.0f);
    CHECK(image->viewportWidth == 24.0f && image->viewportHeight == 20.0f);
    CHECK(image->autoMirror);
    CHECK(image->tintColor == ui::Color::Unspecified());   // a theme attribute: Android only
    CHECK(image->root->children.size() == 2);
    const auto* tilted = std::get_if<std::shared_ptr<const gfx::vector::VectorGroup>>(&image->root->children[0]);
    CHECK(tilted && (*tilted)->name == "tilted" && (*tilted)->rotation == 45.0f);
    if (tilted && !(*tilted)->children.empty()) {
        // The clip path is a group of its own inside "tilted", holding the star.
        const auto* window = std::get_if<std::shared_ptr<const gfx::vector::VectorGroup>>(&(*tilted)->children[0]);
        CHECK(window && (*window)->name == "window" && !(*window)->clipPathData.empty());
        if (window && !(*window)->children.empty()) {
            const auto* star = std::get_if<gfx::vector::VectorPath>(&(*window)->children[0]);
            CHECK(star && star->name == "star");
            if (star) {
                CHECK(star->fill && star->fill->colors[0] == ui::Color(0xFF6750A4u));
                CHECK(star->stroke && star->strokeLineWidth == 2.0f);
                CHECK(star->strokeLineCap == gfx::StrokeCap::Round && star->strokeLineJoin == gfx::StrokeJoin::Bevel);
                CHECK(star->pathFillType == gfx::FillType::EvenOdd);
            }
        }
    }
    const auto* bar = std::get_if<gfx::vector::VectorPath>(&image->root->children[1]);
    CHECK(bar && bar->fill && bar->fill->kind == gfx::Brush::Kind::Linear);
    if (bar && bar->fill) {
        CHECK(bar->fill->colors.size() == 2 && bar->fill->colors[0] == ui::Color(0xFFFF0000u) && bar->fill->colors[1] == ui::Color(0xFF0000FFu));
        CHECK(bar->fill->end.x == 24.0f);
    }
    // A size in px goes through the density.
    std::vector<ui::VectorElement> px = d.vector;
    for (auto& [k, v] : px.front().attrs) if (k == "width") v = "48px";
    CHECK(foundation::BuildVectorResource(px, 2.0f)->defaultWidth.value == 24.0f);
}

TEST(AppPainterResourceDrawsVectorsAndBitmaps) {
    // The test process has no platform decoder: a stand-in reading a PNG's size.
    gfx::SetImageDecoder([](std::span<const std::uint8_t> b) -> std::optional<gfx::DecodedImage> {
        if (b.size() < 24) return std::nullopt;
        auto be = [&](std::size_t at) { return int(b[at]) << 24 | int(b[at + 1]) << 16 | int(b[at + 2]) << 8 | int(b[at + 3]); };
        gfx::DecodedImage d;
        d.width = be(16);
        d.height = be(20);
        d.rgba.assign(std::size_t(d.width * d.height * 4), 0xff);
        return d;
    });
    ui::Owner owner;
    rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
    std::shared_ptr<foundation::Painter> vector, bitmap;
    composition.SetContent([&] {
        vector = foundation::PainterResource(R::drawable::ic_badge);
        bitmap = foundation::PainterResource(R::drawable::cover);
    });
    rt::Composition::RecomposeAll();
    CHECK(vector && bitmap);
    if (vector) CHECK(vector->IntrinsicSize().has_value());
    if (bitmap) {
        const auto size = bitmap->IntrinsicSize();
        CHECK(size && size->width == 3 && size->height == 2);
    }
    composition.SetContent([] {});
    gfx::SetImageDecoder({});
}

TEST(FormatJavaCoversJavasConversions) {
    auto f = [](const char* pattern, std::initializer_list<text::FormatArg> args, const char* tag = "en-US") {
        return text::FormatJava(pattern, std::span<const text::FormatArg>(args.begin(), args.size()), tag);
    };
    CHECK(f("%d items", { 3 }) == "3 items");
    CHECK(f("%2$s %1$s", { "b", "a" }) == "a b");
    CHECK(f("%,d", { 1234567 }) == "1,234,567");
    CHECK(f("%,d", { 1234567 }, "de-DE") == "1.234.567");
    CHECK(f("%05d|%-4s|%4s", { 42, "ab", "cd" }) == "00042|ab  |  cd");
    CHECK(f("%+d %(d", { 5, -5 }) == "+5 (5)");
    CHECK(f("%x %X %#x %o", { 255, 255, 255, 8 }) == "ff FF 0xff 10");
    CHECK(f("%.2f %.0f %f", { 3.14159, 2.5, 1.0 }) == "3.14 3 1.000000");   // HALF_UP on the shortest digits
    CHECK(f("%.2f %.1f %.2f", { 2.675, 0.05, 0.004 }) == "2.68 0.1 0.00");
    CHECK(f("%.1f", { 1234.56 }, "fr-FR") == "1234,6");
    CHECK(f("%,.2f", { 1234.5 }, "en") == "1,234.50");
    CHECK(f("%.3e", { 12345.678 }) == "1.235e+04");
    CHECK(f("%b %c %%%n", { "true", 65 }) == "true A %\n");
    CHECK(f("%S %.3s", { "up", "truncate" }) == "UP tru");
    CHECK(f("%d", { -42 }) == "-42");
    CHECK(f("%s", { 1.5 }) == "1.5");
}

#include "kiln/kiln.h"
#include "kiln_res_c.h"

TEST(CApiAppResourcesByName) {
    struct Read { std::string greeting, plural, item; kiln_color accent = 0; float gutter = 0; int32_t columns = 0; bool tablet = false, missing = true;
                  size_t sorts = 0; bool vector = false; };
    const Read r = WithConfig(Locale("en"), [] {
        Read out;
        const kiln_format_arg args[] = { KILN_FORMAT_S("Ada"), KILN_FORMAT_I(3) };
        out.greeting = kiln_string_resource(R_string_greeting, args, 2);
        const kiln_format_arg n[] = { KILN_FORMAT_I(2) };
        out.plural = kiln_plural_string_resource(R_plurals_chapters, 2, n, 1);
        out.sorts = kiln_string_array_resource_size(R_array_sorts);
        out.item = kiln_string_array_resource(R_array_sorts, 1);
        kiln_color_resource(R_color_accent, &out.accent);
        kiln_dimension_resource(R_dimen_gutter, &out.gutter);
        kiln_integer_resource(R_integer_columns, &out.columns);
        kiln_bool_resource(R_bool_tablet, &out.tablet);
        out.missing = kiln_string_resource("missing", nullptr, 0) == nullptr;
        kiln_image_vector* v = kiln_vector_resource(R_drawable_ic_badge);
        out.vector = v != nullptr;
        kiln_image_vector_release(v);
        return out;
    });
    CHECK(r.greeting == "Hello, Ada! 3 new");
    CHECK(r.plural == "2 chapters");
    CHECK(r.sorts == 2 && r.item == "Shelf");
    CHECK(r.accent == KILN_ARGB(0xFF6750A4u));
    CHECK(r.gutter == 16.0f && r.columns == 3 && r.tablet);
    CHECK(r.missing);
    CHECK(r.vector);
}
