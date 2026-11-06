// M2 sample: the declarative C++ layer. Rows, columns, weights, arrangements, padding,
// backgrounds and wrapped text, plus a counter driven by a coroutine to show recomposition.
#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Animate.h>
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

        Column(Background(FillMax(Modifier{}, Axis::Both), kSurface), Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {
            Spacer(Height(Modifier{}, 32_dp));
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
            BasicText("Text wraps to the width it is given. The paragraph below the counter is laid out by the "
                      "same platform path as before, now measured by a layout node.",
                      Padding(Modifier{}, 16_dp, 0_dp), { 16, kVariant });
        });
    }
}

extern "C" void kiln_main(kiln_app* app) {
    kiln::SetContent(app, [] { Gallery(); });
}
