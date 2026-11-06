// M2 sample: the declarative C++ layer. Rows, columns, weights, arrangements, padding,
// backgrounds and wrapped text, plus a counter driven by a coroutine to show recomposition.
#include <kiln/kiln.hpp>

#include <cmath>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/Insets.h>
#include <kiln/foundation/Back.h>
#include <kiln/foundation/Draw.h>
#include <kiln/foundation/Image.h>

#include <kiln/Assets.h>
#include <kiln/foundation/text/BasicTextField.h>
#include <kiln/runtime/Task.h>
#include <kiln/runtime/Saveable.h>

using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    const Color kSurface{ 0xFFFEF7FFu }, kContainer{ 0xFFF3EDF7u }, kPrimary{ 0xFF6750A4u },
                kOnPrimary{ 0xFFFFFFFFu }, kOnSurface{ 0xFF1D1B20u }, kVariant{ 0xFF49454Fu },
                kPrimaryContainer{ 0xFFEADDFFu }, kTertiary{ 0xFF7D5260u };

    kiln::rt::Task<> Tick(kiln::rt::State<int>& seconds) {
        for (;;) {
            co_await kiln::rt::Delay(1000);
            seconds.Set(seconds.Peek() + 1);
        }
    }

    // A coroutine must not be a capturing lambda: its frame would outlive the captures.
    kiln::rt::Task<> Spin(kiln::rt::State<float>& angle) {
        const std::int64_t start = co_await kiln::rt::NextFrame();
        for (;;) {
            const std::int64_t t = co_await kiln::rt::NextFrame();
            angle.Set(float(double(t - start) * 1e-9 * 45.0));   // 45 degrees per second
        }
    }

    void Chip(const std::string& label, Color bg, Color fg) {
        Box(Background(Padding(Modifier{}, 0_dp), bg, CornerShape::Rounded(8_dp)), Alignment::Center(), false, [=] {
            BasicText(label, Padding(Modifier{}, 12_dp, 6_dp), { 14, fg });
        });
    }

    void Gallery() {
        auto& c = *kiln::rt::Composition::Current();
        auto& seconds = RememberState(0);
        c.LaunchedEffect(kiln::rt::Keys(0), [&seconds] { return Tick(seconds); });

        auto& scroll = c.RememberNew<ScrollState>({}, {});
        // Edge to edge: the background fills the window, content stays inside the safe area and
        // above the keyboard.
        Column(VerticalScroll(SafeDrawingPadding(Background(FillMax(Modifier{}, Axis::Both), kSurface)), scroll), Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {
            Spacer(Height(Modifier{}, 8_dp));
            BasicText("kiln layout", Padding(Modifier{}, 16_dp, 0_dp), { 28, kOnSurface });
            Column(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), kContainer, CornerShape::Rounded(12_dp)),
                   Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&] {
                BasicText("Row with weights 1 : 2 : 1", Padding(Modifier{}, PaddingValues{ 16_dp, 16_dp, 16_dp, 0_dp }), { 16, kVariant });
                Row(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 48_dp), Arrangement::SpacedBy(8_dp), AlignmentV::Top(), [&] {
                    Box(Background(Height(Weight(Modifier{}, 1), 48_dp), kPrimary, CornerShape::Rounded(8_dp)));
                    Box(Background(Height(Weight(Modifier{}, 2), 48_dp), kTertiary, CornerShape::Rounded(8_dp)));
                    Box(Background(Height(Weight(Modifier{}, 1), 48_dp), kPrimary, CornerShape::Rounded(8_dp)));
                });
                BasicText("SpaceBetween, centred vertically", Padding(Modifier{}, 16_dp, 0_dp), { 16, kVariant });
                Row(Padding(FillMax(Modifier{}, Axis::Horizontal), PaddingValues{ 16_dp, 0_dp, 16_dp, 16_dp }), Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [&] {
                    Chip("one", kPrimaryContainer, kOnSurface);
                    Chip("two", kPrimary, kOnPrimary);
                    Chip("three", kPrimaryContainer, kOnSurface);
                });
            });
            Box(Background(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 120_dp), kPrimaryContainer, CornerShape::Rounded(28_dp)),
                Alignment::Center(), false, [&] {
                BasicText("Running for " + std::to_string(seconds.Get()) + " s", {}, { 22, kOnSurface });
            });
            // A text field: focus starts the platform input session (keyboard, InputConnection).
            auto& field = c.RememberNew<TextFieldState>({}, {}, "Edit me");
            Box(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), kContainer, CornerShape::Rounded(12_dp)),
                Alignment::CenterStart(), false, [&] {
                TextFieldOptions opt;
                opt.style = { 16, kOnSurface };
                opt.ime.singleLine = true;
                BasicTextField(field, Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 16_dp), opt);
            });
            BasicText("Field text: " + field.Text(), Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
            auto& taps = kiln::rt::RememberSaveableState(0);
            auto& pressed = RememberState(false);
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                // Pressed colour animates in Oklab with a spring.
                const Color buttonColor = AnimateColorAsState(pressed.Get() ? Color(0xFF21005Du) : kPrimary,
                                                              kiln::anim::Spring(1.0f, kiln::anim::spring::StiffnessMediumLow));
                Box(Clickable(Background(Height(Modifier{}, 40_dp), buttonColor, CornerShape::Pill()),
                              [&taps] { taps.Set(taps.Peek() + 1); }, [&pressed](bool p) { pressed.Set(p); }),
                    Alignment::Center(), false, [&] {
                    BasicText("Tap me", Padding(Modifier{}, 24_dp, 0_dp), { 14, kOnPrimary });
                });
                BasicText("Tapped " + std::to_string(taps.Get()) + " times", {}, { 16, kOnSurface });
            });
            // Predictive back: the panel follows the gesture and closes when it completes.
            auto& panelOpen = kiln::rt::RememberSaveableState(false);
            auto& backProgress = RememberState(0.0f);
            PredictiveBackHandler(panelOpen.Get(), [&](BackProgress& p) -> kiln::rt::Task<> {
                while (auto e = co_await p.Next()) backProgress.Set(e->progress);
                backProgress.Set(0.0f);
                panelOpen.Set(false);
            });
            Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                          [&panelOpen] { panelOpen.Set(!panelOpen.Peek()); }),
                Alignment::Center(), false, [&] {
                BasicText(panelOpen.Get() ? "Close panel" : "Open panel", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface });
            });
            if (panelOpen.Get()) {
                GraphicsLayerParams layer;
                layer.scaleX = layer.scaleY = 1.0f - 0.1f * backProgress.Get();
                Box(Background(GraphicsLayer(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), layer),
                               kTertiary, CornerShape::Rounded(16_dp)), Alignment::Center(), false, [&] {
                    BasicText("Panel: back closes it", Padding(Modifier{}, 16_dp, 24_dp), { 16, kOnPrimary });
                });
            }
            // Rotation: a clipped card with text and a path, spinning.
            {
                auto& angle = RememberState(0.0f);
                c.LaunchedEffect({}, [&angle] { return Spin(angle); });
                GraphicsLayerParams spin;
                spin.rotationZ = angle.Get();
                spin.clip = true;
                spin.shape = CornerShape::Rounded(12_dp);
                Box(Padding(Modifier{}, 32_dp, 8_dp), Alignment::TopStart(), false, [&] {
                    Box(Background(GraphicsLayer(SizeMod(Modifier{}, 120_dp, 64_dp), spin), kPrimaryContainer),
                        Alignment::Center(), false, [&] {
                        BasicText("Rotated", {}, { 18, kOnSurface });
                    });
                });
            }
            // Group alpha: overlapping circles fade as one image (offscreen layer).
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                Box(Alpha(SizeMod(Modifier{}, 80_dp, 48_dp), 0.5f), Alignment::TopStart(), false, [&] {
                    Box(Background(SizeMod(Modifier{}, 48_dp, 48_dp), kPrimary, CornerShape::Pill()));
                    Box(Background(SizeMod(OffsetMod(Modifier{}, 32_dp, 0_dp), 48_dp, 48_dp), kTertiary, CornerShape::Pill()));
                });
            });
            // Mixed text sizes sharing a baseline (alignByBaseline).
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(8_dp), AlignmentV::Top(), [&] {
                BasicText("Baseline", AlignByBaseline({}), { 32, kOnSurface });
                BasicText("shared by", AlignByBaseline({}), { 14, kVariant });
                BasicText("three sizes", AlignByBaseline({}), { 20, kPrimary });
            });
            // Paths: even-odd star, filled heart, stroked wave with round caps, bevel-joined zigzag.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    kiln::gfx::Path star;
                    star.fillType = kiln::gfx::FillType::EvenOdd;
                    const float cx = d.GetSize().width / 2, cy = d.GetSize().height / 2, r = d.GetSize().width / 2;
                    for (int i = 0; i < 5; ++i) {
                        const float a = -1.5707964f + float(i) * 2.5132742f;   // 144 degrees: a pentagram
                        if (i == 0) star.MoveTo(cx + r * std::cos(a), cy + r * std::sin(a));
                        else star.LineTo(cx + r * std::cos(a), cy + r * std::sin(a));
                    }
                    star.Close();
                    d.DrawPath(star, kPrimary);
                });
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height;
                    kiln::gfx::Path heart;
                    heart.MoveTo(w / 2, h * 0.9f);
                    heart.CubicTo(w * -0.1f, h * 0.45f, w * 0.15f, h * -0.05f, w / 2, h * 0.28f);
                    heart.CubicTo(w * 0.85f, h * -0.05f, w * 1.1f, h * 0.45f, w / 2, h * 0.9f);
                    heart.Close();
                    d.DrawPath(heart, kTertiary);
                });
                Canvas(SizeMod(Modifier{}, 96_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height;
                    kiln::gfx::Path wave;
                    wave.MoveTo(d.ToPx(6_dp), h / 2);
                    for (int i = 0; i < 4; ++i) {
                        const float x0 = d.ToPx(6_dp) + (w - d.ToPx(12_dp)) * float(i) / 4, step = (w - d.ToPx(12_dp)) / 4;
                        wave.QuadraticTo(x0 + step / 2, i % 2 ? h * 0.85f : h * 0.15f, x0 + step, h / 2);
                    }
                    kiln::gfx::Stroke stroke{ d.ToPx(4_dp), 4, kiln::gfx::StrokeCap::Round, kiln::gfx::StrokeJoin::Round };
                    d.DrawPath(wave, kPrimary, &stroke);
                });
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height, m = d.ToPx(6_dp);
                    kiln::gfx::Path tri;
                    tri.MoveTo(w / 2, m); tri.LineTo(w - m, h - m); tri.LineTo(m, h - m); tri.Close();
                    kiln::gfx::Stroke stroke{ d.ToPx(3_dp), 4, kiln::gfx::StrokeCap::Butt, kiln::gfx::StrokeJoin::Bevel };
                    d.DrawPath(tri, kVariant, &stroke);
                });
            });
            // Gradients: linear background, radial circle, sweep ring stroke, repeated diagonal.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                using kiln::gfx::Brush;
                Box(Background(SizeMod(Modifier{}, 96_dp, 56_dp), Brush::HorizontalGradient({ kPrimary, kTertiary }), CornerShape::Pill()));
                Box(Background(SizeMod(Modifier{}, 56_dp, 56_dp), Brush::RadialGradient({ kPrimaryContainer, kPrimary }), CornerShape::Pill()));
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    kiln::gfx::Path ring;
                    const float w = d.GetSize().width, inset = d.ToPx(6_dp);
                    ring.AddOval({ inset, inset, w - inset, w - inset });
                    kiln::gfx::Stroke stroke{ d.ToPx(6_dp) };
                    d.DrawPath(ring, Brush::SweepGradient({ kPrimary, kTertiary, Color(0xFFFFB4ABu), kPrimary }), &stroke);
                });
                Box(Background(SizeMod(Modifier{}, 56_dp, 56_dp),
                               Brush::LinearGradient({ kPrimaryContainer, kPrimary }, { 0, 0 }, { 24, 24 }, kiln::gfx::TileMode::Mirror),
                               CornerShape::Rounded(12_dp)));
            });
            // Images: a decoded PNG at its intrinsic size, scaled down with Fit, and cropped.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                auto& c2 = *kiln::rt::Composition::Current();
                const auto& orb = c2.Remember<kiln::gfx::ImageBitmap>([] {
                    auto bytes = kiln::ReadAsset("orb.png");
                    return bytes ? kiln::gfx::ImageBitmap::Decode(*bytes) : kiln::gfx::ImageBitmap{};
                });
                Image(orb, "Orb");
                Image(orb, "Small orb", SizeMod(Modifier{}, 32_dp, 32_dp));
                Image(orb, "Cropped orb", SizeMod(Modifier{}, 96_dp, 32_dp), Alignment::Center(), ContentScale::Crop());
                // Clip by a path (GenericShape): a hexagon over a gradient, a star over the image.
                static const auto hexagon = std::make_shared<kiln::gfx::GenericShape>([](kiln::gfx::Path& p, Size s, LayoutDirection) {
                    for (int i = 0; i < 6; ++i) {
                        const float a = 3.14159265f / 3 * float(i);
                        const float x = s.width / 2 + s.width / 2 * std::cos(a), y = s.height / 2 + s.height / 2 * std::sin(a);
                        if (i == 0) p.MoveTo(x, y); else p.LineTo(x, y);
                    }
                    p.Close();
                });
                static const auto star = std::make_shared<kiln::gfx::GenericShape>([](kiln::gfx::Path& p, Size s, LayoutDirection) {
                    for (int i = 0; i < 10; ++i) {
                        const float a = -1.5707964f + 3.14159265f / 5 * float(i), r = (i % 2 ? 0.45f : 1.0f) * s.width / 2;
                        const float x = s.width / 2 + r * std::cos(a), y = s.height / 2 + r * std::sin(a);
                        if (i == 0) p.MoveTo(x, y); else p.LineTo(x, y);
                    }
                    p.Close();
                });
                Box(Background(Clip(SizeMod(Modifier{}, 48_dp, 48_dp), hexagon),
                               kiln::gfx::Brush::LinearGradient({ kPrimary, kTertiary })));
                Image(orb, "Star orb", Clip(SizeMod(Modifier{}, 48_dp, 48_dp), star), Alignment::Center(), ContentScale::Crop());
                // ColorFilter.tint (SrcIn) and a greyscale colour matrix.
                Image(orb, "Tinted orb", SizeMod(Modifier{}, 32_dp, 32_dp), Alignment::Center(), ContentScale::Fit(), 1,
                      kiln::gfx::ColorFilter::Tint(kTertiary));
                Image(orb, "Grey orb", SizeMod(Modifier{}, 32_dp, 32_dp), Alignment::Center(), ContentScale::Fit(), 1,
                      kiln::gfx::ColorFilter::ColorMatrix({ 0.3f, 0.59f, 0.11f, 0, 0, 0.3f, 0.59f, 0.11f, 0, 0,
                                                            0.3f, 0.59f, 0.11f, 0, 0, 0, 0, 0, 1, 0 }));
            });
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                // Rounded clip around a child that overflows it.
                // Modifiers wrap what follows them: clip before the background so both are clipped.
                Box(Background(Clip(SizeMod(Modifier{}, 64_dp, 64_dp), CornerShape::Rounded(20_dp)), kPrimaryContainer),
                    Alignment::TopStart(), false, [&] {
                    Box(Background(OffsetMod(RequiredSize(Modifier{}, 80_dp, 80_dp), 24_dp, 24_dp), kTertiary));
                });
                Box(Background(Alpha(SizeMod(Modifier{}, 64_dp, 64_dp), 0.5f), kPrimary, CornerShape::Rounded(12_dp)));
                GraphicsLayerParams scaled;
                scaled.scaleX = scaled.scaleY = 0.75f;
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 64_dp, 64_dp), scaled), kTertiary, CornerShape::Pill()));
            });
            BasicText("Text wraps to the width it is given. The paragraph below the counter is laid out by the "
                      "same platform path as before, now measured by a layout node.",
                      Padding(Modifier{}, 16_dp, 0_dp), { 16, kVariant });
            for (int i = 1; i <= 30; ++i) {
                Box(Background(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 56_dp), kContainer, CornerShape::Rounded(12_dp)),
                    Alignment::CenterStart(), false, [i] {
                    BasicText("Row " + std::to_string(i), Padding(Modifier{}, 16_dp, 0_dp), { 16, kOnSurface });
                }, kiln::rt::Key(i));
            }
            Spacer(Height(Modifier{}, 32_dp));
        });
    }
}

extern "C" void kiln_main(kiln_app* app) {
    kiln::SetContent(app, [] { Gallery(); });
}
