#include "Check.h"

#include "kiln/foundation/Transition.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using kiln::ui::LayoutNode;

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        rt::Composition composition{ owner.Root(), d };
        std::int64_t now = 1'000'000'000;
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { d.RunPending(); composition.Recompose(); d.RunPending(); composition.Recompose(); }
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

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Compose.h"

namespace {
    struct LayoutHarness : Harness {
        void Layout() { owner.MeasureAndLayout({ 400, 400 }); }
        void Step(int n) { for (int i = 0; i < n; ++i) { Frames(1); Layout(); } }
    };
}

TEST(AnimatedVisibilityExpandsFadesAndLeaves) {
    LayoutHarness h;
    rt::State<bool> visible(false);
    bool composed = false;
    h.Run([&] {
        composed = false;
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            AnimatedVisibility(visible.Get(), Modifier{}, [&] {
                composed = true;
                Box(SizeMod(Modifier{}, Dp(100), Dp(80)));
            }, ExpandVertically() + FadeIn(), ShrinkVertically() + FadeOut());
        });
    });
    h.Layout();
    CHECK(!composed);                                  // hidden: nothing composed
    visible.Set(true);
    h.Pump();
    h.Layout();
    auto wrapper = [&] { return h.owner.Root().Children()[0]->Children().empty() ? nullptr : h.owner.Root().Children()[0]->Children()[0]; };
    CHECK(composed);
    CHECK(wrapper() != nullptr);
    CHECK_EQ(wrapper()->Size().height, 0);             // starts collapsed
    h.Step(6);
    const int mid = wrapper()->Size().height;
    CHECK(mid > 0 && mid < 80);
    h.Step(120);
    CHECK_EQ(wrapper()->Size().height, 80);
    visible.Set(false);
    h.Pump();
    h.Step(3);
    CHECK(wrapper() != nullptr);                       // exiting: still composed
    CHECK(wrapper()->Size().height < 80);
    h.Step(150);
    CHECK(wrapper() == nullptr);                       // exit finished: gone
}

TEST(SeekableTransitionFollowsTheGestureThenCompletesOrReturns) {
    Harness h;
    SeekableTransitionState<bool> state(false);
    float value = -1;
    h.Run([&] {
        auto& t = UpdateTransition(state);
        value = AnimateFloat<bool>(t, [](bool s) { return s ? 100.0f : 0.0f; },
                                   [](const Transition<bool>::Segment&) { return anim::Tween(200, 0, anim::Linear()); });
    });
    // A back gesture: seek towards `true` with the finger.
    state.SeekTo(0.25f, true);
    h.Pump();
    CHECK(Near(value, 25, 0.5f));
    state.SeekTo(0.5f);
    h.Pump();
    CHECK(Near(value, 50, 0.5f));
    CHECK(!state.CurrentState());
    // Released: finish on the clock from 50%.
    state.AnimateTo();
    h.Pump();
    h.Frames(1);
    CHECK(value >= 50);
    h.Frames(10);
    CHECK_EQ(value, 100.0f);
    CHECK(state.CurrentState());
    // Another gesture that is cancelled: back to where it started.
    state.SeekTo(0.4f, false);
    h.Pump();
    CHECK(Near(value, 60, 0.5f));
    state.AnimateTo(true);
    h.Pump();
    h.Frames(60);
    CHECK(Near(value, 100, 0.05f));
    CHECK(state.CurrentState());
}

TEST(AnimatedContentSwapsWithBothShownMeanwhile) {
    LayoutHarness h;
    rt::State<int> page(1);
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            AnimatedContent(rt::Key(page.Get()), Modifier{}, [](rt::Key k) {
                Box(SizeMod(Modifier{}, Dp(100), Dp(k == rt::Key(1) ? 40.0f : 80.0f)));
            });
        });
    });
    h.Layout();
    LayoutNode* container = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(int(container->Children().size()), 1);
    CHECK_EQ(container->Size().height, 40);
    page.Set(2);
    h.Pump();
    h.Layout();
    h.Step(5);
    CHECK_EQ(int(container->Children().size()), 2);          // old and new while swapping
    const int mid = container->Size().height;
    CHECK(mid > 40 && mid < 80);                              // SizeTransform
    h.Step(100);
    CHECK_EQ(int(container->Children().size()), 1);
    CHECK_EQ(container->Size().height, 80);
}

TEST(ChildTransitionRunsOnTheParentClock) {
    Harness h;
    rt::State<int> step(0);
    float child = -1;
    ui::IntOffset offset{};
    Transition<int>* parent = nullptr;
    Transition<bool>* kid = nullptr;
    h.Run([&] {
        auto& t = UpdateTransition(step.Get());
        parent = &t;
        // The child only cares whether the step is past 1.
        auto& c = CreateChildTransition<bool, int>(t, [](const int& s) { return s > 1; });
        kid = &c;
        child = AnimateFloat<bool>(c, [](bool far) { return far ? 100.0f : 0.0f; },
                                   [](const Transition<bool>::Segment&) { return anim::Tween(320, 0, anim::Linear()); });
        offset = AnimateIntOffset<int>(t, [](const int& s) { return ui::IntOffset{ s * 10, 0 }; },
                                       [](const Transition<int>::Segment&) { return anim::Tween(160, 0, anim::Linear()); });
    });
    step.Set(2);
    h.Pump();
    CHECK(kid->TargetState());
    CHECK(!kid->CurrentState());
    h.Frames(1);
    h.Frames(10);                // 160 ms: the parent's own animation is done, the child's half way
    CHECK_EQ(offset.x, 20);
    CHECK(Near(child, 50, 2));
    CHECK(parent->IsRunning());  // the child's animation keeps the parent running
    CHECK(kid->IsRunning());
    h.Frames(12);
    CHECK_EQ(child, 100.0f);
    CHECK(!parent->IsRunning());
    CHECK(kid->CurrentState());  // finished with the parent
}

TEST(AnimatedContentSlidesWithTheTransitionScope) {
    LayoutHarness h;
    rt::State<int> page(1);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> segments;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            AnimatedContent(rt::Key(page.Get()), Modifier{}, [](rt::Key) {
                Box(SizeMod(Modifier{}, Dp(100), Dp(50)));
            }, [&](AnimatedContentTransitionScope& s) {
                segments.push_back({ s.initialState.value, s.targetState.value });
                using D = AnimatedContentTransitionScope::SlideDirection;
                ContentTransform t = TogetherWith(s.SlideIntoContainer(D::Left, anim::Tween(160, 0, anim::Linear())),
                                                  s.SlideOutOfContainer(D::Left, anim::Tween(160, 0, anim::Linear())));
                t.targetContentZIndex = 1;
                return Using(t, std::nullopt);
            });
        });
    });
    h.Layout();
    LayoutNode* container = h.owner.Root().Children()[0]->Children()[0];
    page.Set(2);
    h.Pump();
    h.Layout();
    h.Step(6);                                   // about half way
    CHECK_EQ(int(container->Children().size()), 2);
    // Children: the old page's enter/exit box, then the new one's; their content is offset.
    auto contentX = [](LayoutNode* item) { return item->Children()[0]->BoundsInRoot().left; };
    const float oldX = contentX(container->Children()[0]), newX = contentX(container->Children()[1]);
    CHECK(oldX < -10 && oldX > -90);             // leaving to the left
    CHECK(newX > 10 && newX < 90);               // coming in from the right edge
    CHECK_EQ(container->Children()[1]->ZIndex(), 1.0f);   // the incoming page draws on top
    CHECK_EQ(container->Size().width, 100);       // no size transform
    // The spec saw the segment once.
    CHECK(!segments.empty() && segments.back() == (std::pair<std::uint64_t, std::uint64_t>{ rt::Key(1).value, rt::Key(2).value }));
    h.Step(20);
    CHECK_EQ(int(container->Children().size()), 1);
    CHECK_EQ(contentX(container->Children()[0]), 0.0f);
}

TEST(AnimatedVisibilityInRowExpandsAlongTheRowOnly) {
    LayoutHarness h;
    rt::State<bool> visible(false);
    MutableTransitionState<bool> state(false);
    LayoutNode* row = nullptr;
    h.Run([&] {
        Row(Modifier{}, [&] {
            AnimatedVisibilityInRow(visible.Get(), Modifier{}, [] { Box(SizeMod(Modifier{}, Dp(100), Dp(60))); });
            // Starts invisible and animates in from the first composition.
            AnimatedVisibilityInColumn(state, Modifier{}, [] { Box(SizeMod(Modifier{}, Dp(40), Dp(80))); });
        });
    });
    state.SetTargetState(true);
    h.Pump();
    h.Layout();
    row = h.owner.Root().Children()[0];
    visible.Set(true);
    h.Pump();
    h.Layout();
    h.Step(4);
    LayoutNode* first = row->Children()[0];
    CHECK(first->Size().width > 0 && first->Size().width < 100);   // growing across
    CHECK_EQ(first->Size().height, 60);                             // full height throughout
    LayoutNode* second = row->Children()[1];
    CHECK_EQ(second->Size().width, 40);                             // a column version: full width
    CHECK(second->Size().height < 80);
    h.Step(60);
    CHECK_EQ(first->Size().width, 100);
    CHECK(state.IsIdle() && state.CurrentState());
}

TEST(CombinedEnterExitTransitionsTakeTheRightHandPartOfAKind) {   // EnterTransition.plus
    const EnterTransition enter = FadeIn(anim::Tween(100), 0.25f) + FadeIn(anim::Tween(200), 0.5f) + ScaleIn();
    CHECK(enter.parts.fade && enter.parts.fade->first == 0.5f);
    CHECK(enter.parts.scale.has_value());
    const ExitTransition exit = FadeOut(anim::Tween(100), 0.25f) + SlideOutHorizontally() + FadeOut(anim::Tween(100), 0.75f);
    CHECK(exit.parts.fade && exit.parts.fade->first == 0.75f);
    CHECK(exit.parts.slide.has_value());
}
