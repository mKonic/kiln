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
