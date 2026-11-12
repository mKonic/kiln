// animation-core's low-level pieces: Animation objects sampled by time, AnimationState with
// animateTo / animateDecay and a cancellable scope, DeferredTargetAnimation.
#include "Check.h"

#include "kiln/animation/Animation.h"

#include <cmath>

using namespace kiln;
using namespace kiln::anim;

namespace {
    void Frames(int n, std::int64_t& t) { for (int i = 0; i < n; ++i) rt::Dispatcher::Main().RunFrame(t += 16'000'000); }
}

TEST(TargetBasedAndDecayAnimationsSampleByTime) {
    TargetBasedAnimation tween(Tween(100, 0, Linear()), 0.0f, 10.0f);
    CHECK_EQ(tween.DurationNanos(), std::int64_t(100'000'000));
    CHECK(std::abs(tween.ValueFromNanos(50'000'000)[0] - 5) < 0.01f);
    CHECK(std::abs(tween.VelocityFromNanos(50'000'000)[0] - 100) < 1.0f);   // 10 per 0.1 s
    CHECK_EQ(tween.ValueFromNanos(200'000'000)[0], 10.0f);
    CHECK(tween.IsFinishedFromNanos(100'000'000));
    DecayAnimation decay(std::make_shared<ExponentialDecay>(), 0, 1000);
    CHECK(decay.TargetValue()[0] > 0);
    CHECK_EQ(decay.ValueFromNanos(decay.DurationNanos())[0], decay.TargetValue()[0]);
    CHECK(decay.VelocityFromNanos(0)[0] > 900);
}

TEST(AnimationStateAnimatesAndCancels) {
    std::int64_t t = 0;
    AnimationState state(0.0f);
    int frames = 0;
    auto job = rt::Launch(rt::Dispatcher::Main(), AnimateTo(state, 100.0f, Tween(160, 0, Linear()), false, [&](AnimationScope&) { ++frames; }));
    Frames(5, t);
    CHECK(state.IsRunning());
    CHECK(state.Float() > 0 && state.Float() < 100);
    Frames(20, t);
    CHECK(!state.IsRunning());
    CHECK_EQ(state.Float(), 100.0f);
    CHECK(frames >= 10);
    // The block cancels a decay part way: the value stays, the velocity goes.
    AnimationState fling(0.0f, 2000.0f);
    auto job2 = rt::Launch(rt::Dispatcher::Main(), AnimateDecay(fling, std::make_shared<ExponentialDecay>(), false, [](AnimationScope& s) {
        if (s.value[0] > 50) s.CancelAnimation();
    }));
    Frames(60, t);
    CHECK(!fling.IsRunning());
    CHECK(fling.Float() > 50);
    CHECK_EQ(fling.Velocity()[0], 0.0f);
}

TEST(DeferredTargetAnimationSnapsThenAnimates) {
    std::int64_t t = 0;
    DeferredTargetAnimation a;
    CHECK(!a.PendingTarget().has_value());
    CHECK_EQ(a.UpdateTarget(10.0f), 10.0f);   // the first target snaps
    CHECK(a.IsIdle());
    CHECK_EQ(a.UpdateTarget(50.0f, Tween(100, 0, Linear())), 10.0f);   // a new one animates
    CHECK(!a.IsIdle());
    Frames(3, t);
    const float mid = a.UpdateTarget(50.0f);
    CHECK(mid > 10 && mid < 50);
    Frames(20, t);
    CHECK_EQ(a.UpdateTarget(50.0f), 50.0f);
    CHECK(a.IsIdle());
}
