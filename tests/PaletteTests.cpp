// Palette (androidx palette's BucketTests, SwatchTests, MaxColorsTest, ConsistencyTest). The
// upstream photo is stood in for by a generated image with many hues and lightnesses.
#include "Check.h"

#include "kiln/graphics/Palette.h"

#include <cmath>

using namespace kiln;
using gfx::Palette;
namespace cu = gfx::color_utils;

namespace {
    std::vector<std::uint32_t> Fill(int w, int h, std::uint32_t argb) { return std::vector<std::uint32_t>(std::size_t(w) * std::size_t(h), argb); }
    // Hue across, lightness down, saturation in bands: a "photo" with many colours.
    std::vector<std::uint32_t> Photo(int w, int h) {
        std::vector<std::uint32_t> px;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float hue = 360.f * x / w, light = 0.1f + 0.8f * y / h, sat = (y / 20) % 2 ? 0.9f : 0.3f;
                px.push_back(cu::HslToColor({ std::fmod(hue, 360.f), sat, light }));
            }
        return px;
    }
    void CheckClose(std::uint32_t expected, std::uint32_t actual) {
        auto ch = [](std::uint32_t c, int s) { return int((c >> s) & 0xFF); };
        for (int s : { 16, 8, 0 }) CHECK(std::abs(ch(expected, s) - ch(actual, s)) <= 8);
    }
}

TEST(PaletteOfOneColourAndOfARegion) {
    // testBlueBitmapReturnsBlueSwatch / WithRegion.
    Palette p = Palette::FromPixels(Fill(300, 300, 0xFF0000FF), 300, 300).Generate();
    CHECK(p.Swatches().size() == 1);
    if (!p.Swatches().empty()) CheckClose(0xFF0000FF, p.Swatches()[0].Rgb());
    Palette r = Palette::FromPixels(Fill(300, 300, 0xFF0000FF), 300, 300).SetRegion(0, 150, 300, 300).Generate();
    CHECK(r.Swatches().size() == 1);
    // A region off the image throws; one-pixel-tall and -wide bitmaps generate.
    bool threw = false;
    try { Palette::FromPixels(Fill(10, 10, 0xFF0000FF), 10, 10).SetRegion(20, 20, 30, 30); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // From an ImageBitmap (premultiplied RGBA, read back straight).
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < 64 * 64; ++i) rgba.insert(rgba.end(), { 0x21, 0x96, 0xF3, 0xFF });
    Palette fromBitmap = Palette::From(gfx::ImageBitmap::FromPixels(64, 64, rgba)).Generate();
    CHECK(fromBitmap.Swatches().size() == 1);
    if (!fromBitmap.Swatches().empty()) CheckClose(0xFF2196F3, fromBitmap.Swatches()[0].Rgb());
    Palette::FromPixels(Fill(1000, 1, 0), 1000, 1).Generate();
    Palette::FromPixels(Fill(1, 1000, 0), 1, 1000).Generate();
}

TEST(PaletteDominantSwatchIsTheLargest) {
    // testDominantSwatch: blue, a green band (rows 0-9), a red band (rows 11-29).
    std::vector<std::uint32_t> px = Fill(100, 100, 0xFF0000FF);
    for (int y = 0; y < 10; ++y) for (int x = 0; x < 100; ++x) px[std::size_t(y * 100 + x)] = 0xFF00FF00;
    for (int y = 11; y < 30; ++y) for (int x = 0; x < 100; ++x) px[std::size_t(y * 100 + x)] = 0xFFFF0000;
    Palette p = Palette::FromPixels(px, 100, 100).Generate();
    CHECK(p.Swatches().size() == 3);
    CHECK(p.DominantSwatch() != nullptr);
    if (p.DominantSwatch()) CheckClose(0xFF0000FF, p.DominantSwatch()->Rgb());   // 5-bit buckets: 0xFF0000F8
}

TEST(PaletteOfAPhotoKeepsItsLimitsAndContrasts) {
    const auto photo = Photo(240, 180);
    for (int max : { 32, 1, 15 }) {   // testMaxColorCount
        Palette p = Palette::FromPixels(photo, 240, 180).MaximumColorCount(max).Generate();
        CHECK(int(p.Swatches().size()) <= max);
    }
    Palette p = Palette::FromPixels(photo, 240, 180).Generate();
    CHECK(!p.Swatches().empty() && p.VibrantSwatch() && p.MutedSwatch());
    for (const auto& s : p.Swatches()) {
        // testTextColorContrasts and testHslIsRgb.
        CHECK(cu::CalculateContrast(s.BodyTextColor(), s.Rgb()) >= 4.5);
        CHECK(cu::CalculateContrast(s.TitleTextColor(), s.Rgb()) >= 3.0);
        CHECK(cu::HslToColor(s.Hsl()) == s.Rgb());
    }
    // testConsistency: the same image gives the same palette.
    Palette again = Palette::FromPixels(photo, 240, 180).Generate();
    CHECK(again.Swatches() == p.Swatches());
    CHECK(again.VibrantSwatch() && p.VibrantSwatch() && again.VibrantSwatch()->Rgb() == p.VibrantSwatch()->Rgb());
    // Exclusive targets do not share a swatch.
    std::vector<std::uint32_t> picked;
    for (auto* s : { p.LightVibrantSwatch(), p.VibrantSwatch(), p.DarkVibrantSwatch(), p.LightMutedSwatch(), p.MutedSwatch(), p.DarkMutedSwatch() })
        if (s) {
            CHECK(std::find(picked.begin(), picked.end(), s->Rgb()) == picked.end());
            picked.push_back(s->Rgb());
        }
}

TEST(PaletteFromSwatchesAndSwatchEquality) {
    // testSwatchesBuilder: from swatches, with targets added.
    std::vector<Palette::Swatch> swatches{ { 0xFF2196F3, 10 }, { 0xFF607D8B, 20 }, { 0xFFFFEB3B, 5 } };
    Palette p = Palette::FromSwatches(swatches).AddTarget(gfx::PaletteTarget::Vibrant()).AddTarget(gfx::PaletteTarget::Muted()).Generate();
    CHECK(p.Swatches() == swatches && p.DominantColor(0) == 0xFF607D8B);
    CHECK(p.MutedSwatch() && p.MutedSwatch()->Rgb() == 0xFF607D8B && p.VibrantSwatch() && p.VibrantSwatch()->Rgb() == 0xFF2196F3);
    CHECK(!p.DarkMutedSwatch() && p.ColorFor(gfx::PaletteTarget::DarkMuted(), 7) == 7);
    // SwatchTests' equality: colour and population.
    CHECK((Palette::Swatch(0xFF000000, 50) == Palette::Swatch(0xFF000000, 50)));
    CHECK(!(Palette::Swatch(0xFF000000, 50) == Palette::Swatch(0xFFFFFFFF, 50)));
    CHECK(!(Palette::Swatch(0xFF000000, 50) == Palette::Swatch(0xFF000000, 100)));
}
