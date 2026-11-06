#include "Check.h"

#include "kiln/graphics/Path.h"

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

#include "kiln/foundation/Animate.h"
#include "kiln/ui/ColorSpace.h"

TEST(OklabRoundTripAndMidpoint) {
    for (std::uint32_t argb : { 0xFF6750A4u, 0xFFFFFFFFu, 0xFF000000u, 0x80123456u }) {
        auto lab = ui::ToOklab(ui::Color(argb));
        CHECK_EQ(ui::FromOklab(lab).argb, argb);
    }
    // White's Oklab lightness is 1 with no chroma.
    auto w = ui::ToOklab(ui::Color(0xFFFFFFFFu));
    CHECK(std::abs(w[1] - 1) < 1e-3f && std::abs(w[2]) < 1e-3f && std::abs(w[3]) < 1e-3f);
}

namespace {
    struct NullNode : rt::TreeNode { void SetChildren(std::vector<rt::TreeNode*>) override {} };
}

TEST(AnimateFloatAsStateFollowsTarget) {
    NullNode root;
    rt::Dispatcher d;
    rt::Composition c(root, d);
    rt::State<float> target(0);
    float seen = -1;
    c.SetContent([&] { seen = foundation::AnimateFloatAsState(target.Get(), Tween(100, 0, Linear())); });
    c.Recompose();
    CHECK_EQ(seen, 0.0f);
    target.Set(10);
    c.Recompose();
    d.RunFrame(0); c.Recompose();
    d.RunFrame(50'000'000); c.Recompose();
    CHECK(std::abs(seen - 5) < 0.1f);
    d.RunFrame(120'000'000); c.Recompose();
    CHECK_EQ(seen, 10.0f);
}

TEST(AnimateAsStateFinishedListenerAndIntTypes) {
    NullNode root;
    rt::Dispatcher d;
    rt::Composition c(root, d);
    rt::State<int> step(0);
    std::vector<ui::IntOffset> finished;
    ui::IntOffset seen{};
    ui::Rect rect{};
    c.SetContent([&] {
        const int s = step.Get();
        seen = foundation::AnimateIntOffsetAsState(ui::IntOffset{ s * 10, 0 }, Tween(100, 0, Linear()),
                                                   [&](ui::IntOffset v) { finished.push_back(v); });
        rect = foundation::AnimateRectAsState(ui::Rect{ 0, 0, float(s * 100), 10 }, Tween(100, 0, Linear()));
    });
    c.Recompose();
    step.Set(1);
    c.Recompose();
    d.RunFrame(0); c.Recompose();
    d.RunFrame(44'000'000); c.Recompose();
    CHECK(seen.x == 4);                            // 4.4 rounds to 4 (IntOffset rounds)
    CHECK(std::abs(rect.right - 44) < 0.5f);
    // Retargeting mid-way: the first animation never finishes, so it reports nothing.
    step.Set(2);
    c.Recompose();
    for (std::int64_t t = 50; t <= 400; t += 16) { d.RunFrame(t * 1'000'000); c.Recompose(); }
    CHECK_EQ(seen.x, 20);
    CHECK_EQ(finished.size(), std::size_t(1));
    CHECK_EQ(finished[0].x, 20);
}

TEST(InterruptionAwareSpecPicksItsInterruptionSpec) {
    rt::Dispatcher d;
    Animatable a(0.0f);
    // Normally a 1 s linear tween; once interrupted, a 100 ms one.
    auto spec = InterruptionAware(Tween(1000, 0, Linear()), Tween(100, 0, Linear()));
    auto first = rt::Launch(d, [](Animatable& an, SpecPtr sp) -> rt::Task<> { co_await an.AnimateTo(Vec{ 100, 0, 0, 0 }, sp); }(a, spec));
    d.RunFrame(0);
    d.RunFrame(100'000'000);
    CHECK(std::abs(a.Float() - 10) < 0.5f);       // the normal spec
    auto second = rt::Launch(d, [](Animatable& an, SpecPtr sp) -> rt::Task<> { co_await an.AnimateTo(Vec{ 0, 0, 0, 0 }, sp); }(a, spec));
    d.RunFrame(200'000'000);
    d.RunFrame(300'000'000);
    CHECK(std::abs(a.Float()) < 0.01f);          // done within the short interruption spec
}

TEST(ArcAndSplineKeyframes) {
    // ArcBelow from (0, 0) to (100, 100): a quarter circle around (100, 0); halfway is at 45 degrees.
    auto arc = ArcAnimation(ArcMode::Below, 1000, 0, Linear());
    const Vec from{ 0, 0, 0, 0 }, to{ 100, 100, 0, 0 };
    const Vec mid = arc->ValueAt(500'000'000, from, to, {}, 2);
    CHECK(std::abs(mid[0] - 29.29f) < 0.3f);
    CHECK(std::abs(mid[1] - 70.71f) < 0.3f);
    CHECK(arc->ValueAt(0, from, to, {}, 2) == from);
    CHECK(arc->ValueAt(1'000'000'000, from, to, {}, 2) == to);
    // The arc moves at a steady speed along its length (quarter circle of radius 100 in 1 s).
    const Vec v = arc->VelocityAt(500'000'000, from, to, {}, 2);
    CHECK(std::abs(std::hypot(v[0], v[1]) - 157.08f) < 1.0f);
    // A one-component arc still runs (the pair is padded).
    CHECK(std::abs(arc->ValueAt(500'000'000, Vec{ 0, 0, 0, 0 }, Vec{ 10, 0, 0, 0 }, {}, 1)[0] - 5) < 0.01f);

    // keyframesWithSpline 0 -> 100 (at 500 ms) -> 0: flat at the peak, a cubic on the way down.
    auto spline = KeyframesWithSpline(1000, { Keyframe{ 500, Vec{ 100, 0, 0, 0 } } });
    const Vec zero{ 0, 0, 0, 0 };
    CHECK_EQ(spline->ValueAt(500'000'000, zero, zero, {}, 1)[0], 100.0f);
    CHECK(std::abs(spline->ValueAt(750'000'000, zero, zero, {}, 1)[0] - 62.5f) < 0.01f);
    CHECK(std::abs(spline->VelocityAt(500'000'000, zero, zero, {}, 1)[0]) < 0.01f);
    // Plain keyframes stay straight.
    auto straight = Keyframes(1000, { Keyframe{ 500, Vec{ 100, 0, 0, 0 } } });
    CHECK(std::abs(straight->ValueAt(750'000'000, zero, zero, {}, 1)[0] - 50) < 0.01f);
}

TEST(CssEasingsAndBounce) {
    using namespace kiln::anim::easing;
    CHECK(std::abs(EaseInOut().Transform(0.5f) - 0.5f) < 1e-3f);           // symmetric
    CHECK(EaseInBack().Transform(0.2f) < 0);                                // anticipates below 0
    CHECK(EaseOutBack().Transform(0.6f) > 1);                               // overshoots
    CHECK(std::abs(EaseOutBounce().Transform(1) - 1) < 1e-6f);
    CHECK(std::abs(EaseOutBounce().Transform(1 / 2.75f) - 1) < 1e-4f);     // first bounce touches 1
    CHECK_EQ(EaseInElastic().Transform(0), 0.0f);
}

TEST(PathEasingFollowsThePath) {
    kiln::gfx::Path p;
    p.MoveTo(0, 0);
    p.LineTo(0.5f, 0.8f);
    p.LineTo(1, 1);
    const auto e = kiln::anim::PathEasing(p);
    CHECK(std::abs(e.Transform(0.25f) - 0.4f) < 1e-3f);
    CHECK(std::abs(e.Transform(0.75f) - 0.9f) < 1e-3f);
}

TEST(InfiniteReverseRunsTimeBackwards) {
    // Reverse mirrors time (E(1 - x)), not the endpoints (1 - E(x)).
    auto spec = kiln::anim::InfiniteRepeatable(kiln::anim::Tween(100, 0, kiln::anim::easing::EaseInQuad()), kiln::anim::RepeatMode::Reverse);
    const float at125 = spec->ValueAt(125'000'000, {}, { 1, 0, 0, 0 }, {}, 1)[0];
    CHECK(std::abs(at125 - kiln::anim::easing::EaseInQuad().Transform(0.75f)) < 1e-4f);
    // A Delay start offset holds the first frame.
    auto delayed = kiln::anim::InfiniteRepeatable(kiln::anim::Tween(100, 0, kiln::anim::Linear()), kiln::anim::RepeatMode::Restart,
                                                  kiln::anim::StartOffset(50));
    CHECK_EQ(delayed->ValueAt(30'000'000, {}, { 1, 0, 0, 0 }, {}, 1)[0], 0.0f);
}
