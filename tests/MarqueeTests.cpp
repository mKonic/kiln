// basicMarquee: the content scrolls when it overflows, at the velocity, with a copy after the spacing.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Marquee.h"

#include <chrono>
#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNanos = 0;
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 400 });
                DrawTarget target;
                target.viewportWidth = target.viewportHeight = 400;
                owner.Draw(target);
            }
        }
        void Frames(int n) {
            auto& d = rt::Dispatcher::Main();
            for (int i = 0; i < n; ++i) {
                d.AdvanceTimeBy(std::chrono::milliseconds(16));
                frameNanos += 16'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                Pump();
            }
        }
    };
}

TEST(MarqueeScrollsOverflowingContent) {
    Harness h;
    IntSize size;
    std::vector<float> drawnAt;   // the content's x in the marquee, per copy drawn
    float containerX = 0;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            MarqueeOptions o;
            o.iterations = 1;
            o.repeatDelayMillis = 0;
            o.initialDelayMillis = 0;
            // The probes go before the marquee (outside it); after it they would see the content.
            Box(BasicMarquee(Width(OnSizeChanged(OnGloballyPositioned(Modifier{}, [&](const LayoutCoordinates& c) { containerX = c.PositionInRoot().x; }),
                                                 [&](IntSize s) { size = s; }), Dp(100)), o),
                Alignment::TopStart(), false, [&] {
                    Box(DrawBehind(SizeMod(Modifier{}, Dp(300), Dp(20)), [&](DrawScope& s) { drawnAt.push_back(s.CurrentTransform().e); }),
                        Alignment::TopStart(), false, [] {});
                });
        });
    });
    // The marquee takes its space; the content keeps its own width.
    CHECK(size == (IntSize{ 100, 20 }));
    // After a second at 30 px/s the content has moved 30 left (a frame of slack either way).
    h.Frames(63);
    drawnAt.clear();
    h.Pump();
    CHECK(!drawnAt.empty());
    const float x = drawnAt.front() - containerX;
    CHECK(x < -28.0f && x > -34.0f);
}

TEST(MarqueeStaysStillWhenContentFits) {
    Harness h;
    std::vector<float> drawnAt;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(BasicMarquee(Width(Modifier{}, Dp(100))), Alignment::TopStart(), false, [&] {
                Box(DrawBehind(SizeMod(Modifier{}, Dp(50), Dp(20)), [&](DrawScope& s) { drawnAt.push_back(s.CurrentTransform().e); }),
                    Alignment::TopStart(), false, [] {});
            });
        });
    });
    h.Frames(100);
    drawnAt.clear();
    h.Pump();
    CHECK(!drawnAt.empty());
    CHECK_EQ(drawnAt.front(), 0.0f);
}
