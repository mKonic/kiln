#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Locals.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() {
            owner.SetDensity({ 1, 1 });
            auto& h = owner.Insets();
            h.Set(InsetsType::StatusBars, { 0, 24, 0, 0 });
            h.Set(InsetsType::NavigationBars, { 0, 0, 0, 48 });
            h.SetVisible(InsetsType::StatusBars, true);
            h.SetVisible(InsetsType::NavigationBars, true);
            h.SetIgnoringVisibility(InsetsType::StatusBars, { 0, 24, 0, 0 });
            h.SetIgnoringVisibility(InsetsType::NavigationBars, { 0, 0, 0, 48 });
            h.Set(InsetsType::Ime, { 0, 0, 0, 300 });
            h.SetVisible(InsetsType::Ime, true);
            h.SetHasInsets(true);
        }
        void Run(std::function<void()> content) {
            composition.SetContent([this, content] { rt::Composition::Current()->Provide(LocalWindowInsets, &owner.Insets(), content); });
            Pump();
        }
        void Pump() { rt::Composition::RecomposeAll(); owner.MeasureAndLayout({ 400, 800 }); }
        InsetsContext Ctx(LayoutDirection d = LayoutDirection::Ltr) { return { &owner.Insets(), { 1, 1 }, d }; }
    };
}

TEST(WindowInsetsCombineLikeCompose) {
    Harness h;
    const auto c = h.Ctx();
    const WindowInsets a = WindowInsets::Fixed(10, 20, 30, 40), b = WindowInsets::Fixed(15, 5, 35, 0);
    const WindowInsets u = a.Union(b), add = a.Add(b), ex = a.Exclude(b);
    CHECK(u.Left(c) == 15 && u.Top(c) == 20 && u.Right(c) == 35 && u.Bottom(c) == 40);
    CHECK(add.Left(c) == 25 && add.Top(c) == 25 && add.Right(c) == 65 && add.Bottom(c) == 40);
    CHECK(ex.Left(c) == 0 && ex.Top(c) == 15 && ex.Right(c) == 0 && ex.Bottom(c) == 40);
    // Start follows the direction: the left side in LTR, the right side in RTL.
    const WindowInsets start = a.Only(WindowInsetsSides::Start() + WindowInsetsSides::Top());
    CHECK(start.Left(c) == 10 && start.Right(c) == 0 && start.Top(c) == 20 && start.Bottom(c) == 0);
    const auto rtl = h.Ctx(LayoutDirection::Rtl);
    CHECK(start.Left(rtl) == 0 && start.Right(rtl) == 30);
    // Window types, and the bars while hidden.
    CHECK_EQ(WindowInsets::SafeDrawing().Bottom(c), 300);
    CHECK_EQ(WindowInsets::SystemBars().Bottom(c), 48);
    h.owner.Insets().Set(InsetsType::NavigationBars, {});
    h.owner.Insets().SetVisible(InsetsType::NavigationBars, false);
    CHECK_EQ(WindowInsets::NavigationBars().Bottom(c), 0);
    CHECK_EQ(WindowInsets::NavigationBarsIgnoringVisibility().Bottom(c), 48);
    // IME animation source / target (the current IME insets when nothing runs).
    CHECK_EQ(WindowInsets::ImeAnimationTarget().Bottom(c), 300);
    h.owner.Insets().SetAnimation(InsetsType::Ime, InsetsAnimation{ { 0, 0, 0, 0 }, { 0, 0, 0, 320 }, 0.2f, 1, 200 });
    CHECK_EQ(WindowInsets::ImeAnimationSource().Bottom(c), 0);
    CHECK_EQ(WindowInsets::ImeAnimationTarget().Bottom(c), 320);
}

TEST(InsetsPaddingConsumesForDescendants) {
    Harness h;
    LayoutNode *outer = nullptr, *inner = nullptr, *bar = nullptr;
    WindowInsets seen;
    bool visible = false;
    h.Run([&] {
        visible = AreStatusBarsVisible() && IsImeVisible() && !IsCaptionBarVisible();
        Box(StatusBarsPadding(Modifier{}), Alignment::TopStart(), false, [&] {
            outer = &Layout(SystemBarsPadding(Modifier{}), std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) {
                std::vector<Placeable*> ps;
                for (auto* m : ch) ps.push_back(&m->Measure(Constraints{ 0, c.maxWidth, 0, c.maxHeight }));
                return s.Layout(c.maxWidth, c.maxHeight, [ps](PlacementScope& p) { for (auto* x : ps) p.Place(*x, 0, 0); });
            }), [&] {
                inner = &Layout(OnConsumedWindowInsetsChanged(ConsumeWindowInsets(Modifier{}, PaddingValues{ Dp(0), Dp(0), Dp(0), Dp(100) }),
                                                              [&](const WindowInsets& w) { seen = w; }),
                                std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints&) { return s.Layout(10, 10); }));
                bar = &Layout(WindowInsetsBottomHeight(Modifier{}, WindowInsets::NavigationBars()),
                              std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) { return s.Layout(c.minWidth, c.minHeight); }));
            });
        });
    });
    CHECK(visible);
    // The status bar was consumed outside: the system bars padding adds only the navigation bar.
    CHECK_EQ(int(outer->PositionInRoot().y), 24);
    CHECK_EQ(outer->Size().height, 800 - 24);
    CHECK_EQ(int(inner->PositionInRoot().y), 24);
    // Inside both, plus the 100 dp consumed just before it in the same chain.
    CHECK_EQ(seen.Top(h.Ctx()), 24);
    CHECK_EQ(seen.Bottom(h.Ctx()), 48 + 100);
    // The bottom height is what is left of the navigation bar: nothing.
    CHECK_EQ(bar->Size().height, 0);
}

#include "kiln/foundation/Scroll.h"

TEST(ImeNestedScrollPullsTheKeyboard) {
    Harness h;
    auto& ins = h.owner.Insets();
    ins.Set(InsetsType::Ime, {});
    ins.SetVisible(InsetsType::Ime, false);
    std::vector<int> set;
    int finished = -1, requests = 0;
    InsetsControlHost host;
    host.request = [&](InsetsType) { ++requests; rt::Dispatcher::Main().Post([&] { ins.OnControlReady({}, { 0, 0, 0, 300 }, {}); }); return true; };
    host.setInsetsAndAlpha = [&](const InsetsValue& v, float, float) { set.push_back(v.bottom); };
    host.finish = [&](bool shown) { finished = shown ? 1 : 0; ins.OnControlEnded(); };
    ins.SetControlHost(host);
    ScrollState scroll;
    h.Run([&] {
        Column(VerticalScroll(ImeNestedScroll(FillMax(Modifier{}, Axis::Both)), scroll), [&] {
            Box(SizeMod(Modifier{}, Dp(100), Dp(100)));   // nothing to scroll: every drag is left over
        });
    });
    std::int64_t t = 0;
    auto touch = [&](PointerEventType type, float y, bool down) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t += 16;
        e.pointers.push_back({ 1, { 50, y }, down, PointerType::Touch, {} });
        h.owner.ProcessPointer(e);
        rt::Dispatcher::Main().RunPending();
        h.Pump();
    };
    touch(PointerEventType::Press, 600, true);
    for (int i = 1; i <= 8; ++i) touch(PointerEventType::Move, 600 - 20.0f * float(i), true);
    CHECK_EQ(requests, 1);
    CHECK(!set.empty() && set.back() > 0 && set.back() <= 300);
    // Pulled most of the way: the release finishes it open.
    for (int i = 9; i <= 14; ++i) touch(PointerEventType::Move, 600 - 20.0f * float(i), true);
    touch(PointerEventType::Release, 320, false);
    std::int64_t frame = 0;
    for (int i = 0; i < 120 && finished < 0; ++i) {
        rt::Dispatcher::Main().RunFrame(frame += 16'000'000);
        rt::Dispatcher::Main().RunPending();
    }
    CHECK_EQ(finished, 1);
    CHECK_EQ(set.back(), 300);
}

TEST(RecalculateWindowInsetsConsumesWhatTheElementDoesNotOverlap) {
    Harness h;
    WindowInsets seen;
    IntSize padded;
    // 400x760 at y = 10 in an 800-high window: 10 above it, 30 below it.
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(OffsetMod(SizeMod(Modifier{}, Dp(400), Dp(760)), Dp(0), Dp(10)), Alignment::TopStart(), false, [&] {
                Box(RecalculateWindowInsets(FillMax(Modifier{}, Axis::Both, 1)), Alignment::TopStart(), false, [&] {
                    Box(OnSizeChanged(SystemBarsPadding(OnConsumedWindowInsetsChanged(FillMax(Modifier{}, Axis::Both, 1), [&](const WindowInsets& w) { seen = w; })),
                                      [&](IntSize s) { padded = s; }),
                        Alignment::TopStart(), false, [] {});
                });
            });
        });
    });
    const auto c = h.Ctx();
    CHECK_EQ(seen.Top(c), 10);
    CHECK_EQ(seen.Bottom(c), 30);
    // The status bar (24) pads 14, the navigation bar (48) pads 18.
    CHECK(padded == (IntSize{ 400, 760 - 14 - 18 }));
}
