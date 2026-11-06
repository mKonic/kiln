#include "Check.h"

#include "kiln/animation/Animatable.h"

#include <cmath>

using namespace kiln;
using namespace kiln::anim;

TEST(EasingCurves) {
    CHECK(std::abs(FastOutSlowIn().Transform(0.5f) - 0.7743f) < 0.002f);
    CHECK_EQ(FastOutSlowIn().Transform(0.0f), 0.0f);
    CHECK_EQ(FastOutSlowIn().Transform(1.0f), 1.0f);
    CHECK(std::abs(LinearOutSlowIn().Transform(0.5f) - 0.8f) < 0.05f);   // decelerating: well above linear
    CHECK(FastOutLinearIn().Transform(0.5f) < 0.5f);                       // accelerating
}

TEST(SpringSettlesByItsEstimatedDuration) {
    auto spec = Spring(spring::DampingRatioNoBouncy, spring::StiffnessMedium, 0.01f);
    const Vec from{ 0 }, to{ 100 }, v0{ 0 };
    const std::int64_t d = spec->DurationNanos(from, to, v0, 1);
    CHECK(d > 100'000'000 && d < 600'000'000);                       // critically damped 1500: a few hundred ms
    const float atEnd = spec->ValueAt(d, from, to, v0, 1)[0];
    CHECK(std::abs(atEnd - 100) <= 0.011f);                         // within the visibility threshold
    const float half = spec->ValueAt(d / 4, from, to, v0, 1)[0];
    CHECK(half > 0 && half < 100);
    // An underdamped spring overshoots.
    auto bouncy = Spring(spring::DampingRatioHighBouncy, spring::StiffnessLow);
    float peak = 0;
    for (std::int64_t t = 0; t < 2'000'000'000; t += 4'000'000) peak = std::max(peak, bouncy->ValueAt(t, from, to, v0, 1)[0]);
    CHECK(peak > 140);
}

TEST(AnimatableInterruptsAndHonoursScale) {
    rt::Dispatcher d;
    Animatable a(0.0f);
    AnimationResult first{}, second{};
    auto runner = [&](float target, AnimationResult& out) -> rt::Task<> { out = co_await a.AnimateTo(target, Tween(100, 0, Linear())); };
    rt::Job j1 = rt::Launch(d, runner(100, first));
    d.RunFrame(0);
    d.RunFrame(50'000'000);
    CHECK(std::abs(a.Float() - 50) < 0.5f);
    rt::Job j2 = rt::Launch(d, runner(0, second));   // interrupts, starts from 50
    d.RunFrame(60'000'000);
    CHECK(first.reason == EndReason::Interrupted);
    d.RunFrame(110'000'000);
    CHECK(std::abs(a.Float() - 25) < 0.5f);          // halfway from 50 back to 0
    d.RunFrame(170'000'000);
    CHECK(second.reason == EndReason::Finished);
    CHECK_EQ(a.Float(), 0.0f);

    SetDurationScale(0);
    AnimationResult third{};
    rt::Job j3 = rt::Launch(d, runner(10, third));
    d.RunFrame(200'000'000);
    CHECK(third.reason == EndReason::Finished);
    CHECK_EQ(a.Float(), 10.0f);
    SetDurationScale(1);
}
