// M2 sample: the declarative C++ layer. Rows, columns, weights, arrangements, padding,
// backgrounds and wrapped text, plus a counter driven by a coroutine to show recomposition.
#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/Insets.h>
#include <kiln/foundation/Back.h>
#include <kiln/foundation/text/BasicTextField.h>
#include <kiln/runtime/Task.h>

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
            auto& taps = RememberState(0);
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
            auto& panelOpen = RememberState(false);
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
