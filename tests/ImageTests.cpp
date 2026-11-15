#include "Check.h"

#include "kiln/graphics/Exif.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Image.h"
#include "kiln/foundation/Layouts.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct FixedPainter final : Painter {
        Size size;
        explicit FixedPainter(Size s) : size(s) {}
        std::optional<Size> IntrinsicSize() const override { return size; }
        void Draw(DrawScope&, Size, float, const gfx::ColorFilter*) override {}
    };
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
    };
}

TEST(ContentScaleFactors) {
    const Size src{ 100, 50 }, dst{ 200, 200 };
    CHECK_EQ(ContentScale::Fit().ComputeScaleFactor(src, dst).x, 2.0f);
    CHECK_EQ(ContentScale::Crop().ComputeScaleFactor(src, dst).x, 4.0f);
    CHECK_EQ(ContentScale::FillBounds().ComputeScaleFactor(src, dst).y, 4.0f);
    CHECK_EQ(ContentScale::Inside().ComputeScaleFactor(src, dst).x, 1.0f);   // already fits
    CHECK_EQ(ContentScale::Inside().ComputeScaleFactor(src, { 50, 50 }).x, 0.5f);
}

TEST(ImageSizesToItsPainter) {
    Harness h;
    auto size = [&](Modifier imageMod) {
        LayoutNode* root = nullptr;
        h.composition.SetContent([&] {
            Box(Modifier{}, Alignment::TopStart(), false, [&] {   // loose constraints for the image
                Image(std::make_shared<FixedPainter>(Size{ 100, 50 }), std::nullopt, imageMod);
            });
        });
        h.composition.Recompose();
        h.owner.MeasureAndLayout({ 400, 400 });
        root = h.owner.Root().Children()[0]->Children()[0];
        return root ? root->Size() : IntSize{};
    };
    // Loose constraints: the intrinsic size.
    IntSize s = size(Modifier{});
    CHECK_EQ(s.width, 100); CHECK_EQ(s.height, 50);
    // Width fixed at 50: Fit keeps the aspect ratio.
    s = size(Width(Modifier{}, Dp(50)));
    CHECK_EQ(s.width, 50); CHECK_EQ(s.height, 25);
    // Both fixed: the fixed size, whatever the painter.
    s = size(SizeMod(Modifier{}, Dp(200), Dp(120)));
    CHECK_EQ(s.width, 200); CHECK_EQ(s.height, 120);
}

TEST(BitmapPixelsArePremultiplied) {
    const std::uint8_t px[8] = { 255, 128, 0, 128, 10, 20, 30, 255 };
    const auto img = gfx::ImageBitmap::FromPixels(2, 1, px);
    CHECK_EQ(img.Width(), 2);
    CHECK_EQ(int(img.Pixels()[0]), 128);   // 255 * 128 / 255
    CHECK_EQ(int(img.Pixels()[1]), 64);
    CHECK_EQ(int(img.Pixels()[3]), 128);
    CHECK_EQ(int(img.Pixels()[6]), 30);
    CHECK(gfx::ImageBitmap::FromPixels(2, 2, px) == gfx::ImageBitmap{});   // too few bytes
}

TEST(PixelMapReadsStraightColours) {
    // 2x1: opaque red, half-transparent white (straight alpha in, premultiplied inside).
    const std::uint8_t rgba[] = { 255, 0, 0, 255, 255, 255, 255, 128 };
    const kiln::gfx::ImageBitmap b = kiln::gfx::ImageBitmap::FromPixels(2, 1, rgba);
    const kiln::gfx::PixelMap m = b.ToPixelMap();
    CHECK(m.Width() == 2 && m.Height() == 1);
    CHECK(m(0, 0) == kiln::ui::Color(0xFFFF0000u));
    CHECK(m(1, 0).argb >> 24 == 128 && (m(1, 0).argb & 0xFF) >= 254);
    CHECK(b.ToPixelMap(1, 0, 1, 1)(0, 0) == m(1, 0));
}

// ExifInterface's orientation (JPEG APP1, WebP EXIF) and Coil's reverseTransformations.
TEST(ExifOrientationIsReadAndReversed) {
    // A big-endian TIFF with IFD0 holding orientation 6 (rotate 90).
    std::vector<std::uint8_t> tiff = { 'M', 'M', 0, 42, 0, 0, 0, 8, 0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0 };
    std::vector<std::uint8_t> app1 = { 'E', 'x', 'i', 'f', 0, 0 };
    app1.insert(app1.end(), tiff.begin(), tiff.end());
    std::vector<std::uint8_t> jpeg = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 4, 0, 0, 0xFF, 0xE1, std::uint8_t((app1.size() + 2) >> 8), std::uint8_t(app1.size() + 2) };
    jpeg.insert(jpeg.end(), app1.begin(), app1.end());
    jpeg.insert(jpeg.end(), { 0xFF, 0xDA, 0, 2 });
    CHECK((gfx::ReadExifOrientation(jpeg) == gfx::ExifOrientation{ false, 90 }));
    // A little-endian one with orientation 5 (transpose) in a WebP EXIF chunk, no Exif header.
    std::vector<std::uint8_t> le = { 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0 };
    std::vector<std::uint8_t> webp = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', 'X', 2, 0, 0, 0, 0, 0,
                                       'E', 'X', 'I', 'F', std::uint8_t(le.size()), 0, 0, 0 };
    webp.insert(webp.end(), le.begin(), le.end());
    CHECK((gfx::ReadExifOrientation(webp) == gfx::ExifOrientation{ true, 270 }));
    CHECK((gfx::ReadExifOrientation(std::vector<std::uint8_t>{ 0x89, 'P', 'N', 'G' }) == gfx::ExifOrientation{}));
    CHECK((gfx::OrientationFromTag(4) == gfx::ExifOrientation{ true, 180 }) && (gfx::OrientationFromTag(7) == gfx::ExifOrientation{ true, 90 }));
    // A 2x1 image: A B. Turned 90 clockwise: A over B (1x2). Flipped: B A.
    std::vector<std::uint8_t> px = { 1, 0, 0, 255, 2, 0, 0, 255 };
    int w = 2, h = 1;
    auto turned = gfx::ApplyExifOrientation(px, w, h, { false, 90 });
    CHECK(w == 1 && h == 2 && turned[0] == 1 && turned[4] == 2);
    w = 2;
    h = 1;
    auto flipped = gfx::ApplyExifOrientation(px, w, h, { true, 0 });
    CHECK(w == 2 && flipped[0] == 2 && flipped[4] == 1);
    w = 2;
    h = 1;
    auto ccw = gfx::ApplyExifOrientation(px, w, h, { false, 270 });
    CHECK(w == 1 && h == 2 && ccw[0] == 2 && ccw[4] == 1);
}
