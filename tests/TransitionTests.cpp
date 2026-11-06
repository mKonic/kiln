#include "Check.h"

#include "kiln/foundation/Transition.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        rt::Composition composition{ owner.Root(), d };
        std::int64_t now = 1'000'000'000;
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { d.RunPending(); composition.Recompose(); d.RunPending(); }
        void Frames(int n, std::int64_t stepMs = 16) {
            for (int i = 0; i < n; ++i) { now += stepMs * 1'000'000; d.RunFrame(now); Pump(); }
        }
    };
    bool Near(float a, float b, float eps) { return std::abs(a - b) <= eps; }
}

TEST(TransitionAnimatesChildrenAndSettles) {
    Harness h;
    rt::State<bool> on(false);
    float value = -1;
    Transition<bool>* tr = nullptr;
    h.Run([&] {
        auto& t = UpdateTransition(on.Get());
        tr = &t;
        value = AnimateFloat<bool>(t, [](bool s) { return s ? 100.0f : 0.0f; },
                                   [](const Transition<bool>::Segment&) { return anim::Tween(160, 0, anim::Linear()); });
    });
    CHECK_EQ(value, 0.0f);
    on.Set(true);
    h.Pump();
    h.Frames(1);                 // first frame starts the clock
    h.Frames(5);                 // 80 ms of 160
    CHECK(Near(value, 50, 1));
    CHECK(!tr->CurrentState());  // still on its way
    h.Frames(10);
    CHECK_EQ(value, 100.0f);
    CHECK(tr->CurrentState());
    CHECK(!tr->IsRunning());
}

TEST(InterruptedTransitionContinuesFromTheCurrentValue) {
    Harness h;
    rt::State<int> state(0);
    float value = 0;
    h.Run([&] {
        auto& t = UpdateTransition(state.Get());
        value = AnimateFloat<int>(t, [](int s) { return float(s) * 100; },
                                  [](const Transition<int>::Segment&) { return anim::Tween(200, 0, anim::Linear()); });
    });
    state.Set(1);
    h.Pump();
    h.Frames(1);
    h.Frames(5);                 // ~40
    const float mid = value;
    CHECK(mid > 20 && mid < 60);
    state.Set(0);                // back: starts from mid, no jump
    h.Pump();
    h.Frames(1);
    CHECK(std::abs(value - mid) < 15);
    h.Frames(80);
    CHECK(Near(value, 0, 0.05f));
}

TEST(InfiniteTransitionRepeats) {
    Harness h;
    float v = -1;
    h.Run([&] {
        auto& t = RememberInfiniteTransition();
        v = AnimateFloat(t, 0, 1, anim::InfiniteRepeatable(anim::Tween(100, 0, anim::Linear()), anim::RepeatMode::Reverse));
    });
    h.Frames(1);                 // t = 0
    CHECK_EQ(v, 0.0f);
    h.Frames(5, 10);             // 50 ms
    CHECK(Near(v, 0.5f, 0.02f));
    h.Frames(10, 10);            // 150 ms: reversing, halfway back
    CHECK(Near(v, 0.5f, 0.02f));
    h.Frames(5, 10);             // 200 ms: back at the start
    CHECK(Near(v, 0.0f, 0.02f));
}

TEST(KeyframesHitTheirValuesAndEasings) {
    auto spec = anim::Keyframes(400, { { 100, { 10, 0, 0, 0 } }, { 300, { 90, 0, 0, 0 }, anim::Linear() } });
    auto at = [&](int ms) { return spec->ValueAt(std::int64_t(ms) * 1'000'000, {}, { 100, 0, 0, 0 }, {}, 1)[0]; };
    CHECK_EQ(at(0), 0.0f);
    CHECK_EQ(at(100), 10.0f);
    CHECK(Near(at(200), 50, 0.01f));     // linear 10 -> 90
    CHECK(Near(at(350), 95, 0.01f));     // 90 -> target 100
    CHECK_EQ(at(400), 100.0f);
    auto rep = anim::Repeatable(3, anim::Tween(100, 0, anim::Linear()), anim::RepeatMode::Reverse);
    CHECK_EQ(rep->DurationNanos({}, { 1, 0, 0, 0 }, {}, 1), 300'000'000);
    CHECK(Near(rep->ValueAt(150'000'000, {}, { 1, 0, 0, 0 }, {}, 1)[0], 0.5f, 0.01f));   // second (reversed) pass
}
