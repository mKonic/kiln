#include "Check.h"

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
