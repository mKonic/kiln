#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { rt::Composition::RecomposeAll(); owner.MeasureAndLayout({ 200, 200 }); }
    };
    const VerticalRuler kLeft;
    const HorizontalRuler kTop;

    // Provides kLeft / kTop (from a state, so they can change) and places its child at (10, 5).
    std::shared_ptr<MeasurePolicy> Provider(rt::State<float>* left, float top) {
        return std::make_shared<LambdaMeasurePolicy>([left, top](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
            Placeable& p = children[0]->Measure(Constraints{ 0, c.maxWidth, 0, c.maxHeight });
            return s.Layout(100, 100, {}, [left, top](RulerScope& r) {
                r.Provides(kLeft, left ? left->Get() : 30);
                r.Provides(kTop, top);
            }, [&p](PlacementScope& ps) { ps.Place(p, 10, 5); });
        });
    }
    // A child that reads both rulers while placing its own child.
    std::shared_ptr<MeasurePolicy> Reader(float* x, float* y, float* missing) {
        return std::make_shared<LambdaMeasurePolicy>([x, y, missing](MeasureScope& s, std::span<Measurable* const>, const Constraints&) {
            return s.Layout(20, 20, [x, y, missing](PlacementScope& ps) {
                *x = ps.Current(kLeft, -1);
                *y = ps.Current(kTop, -1);
                if (missing) *missing = ps.Current(HorizontalRuler::MaxOf({ HorizontalRuler() }), 7);
            });
        });
    }
}

TEST(RulersReachDescendantsInTheirCoordinates) {
    Harness h;
    rt::State<float> left(30);
    float x = 0, y = 0, missing = 0;
    h.Run([&] {
        Layout(Modifier{}, Provider(&left, 20), [&] {
            Box(Padding(Modifier{}, Dp(4)), Alignment::TopStart(), false, [&] { Layout(Modifier{}, Reader(&x, &y, &missing)); });
        });
    });
    // The reader sits at (10 + 4, 5 + 4) in the provider: 30 - 14, 20 - 9.
    CHECK_EQ(x, 16.0f);
    CHECK_EQ(y, 11.0f);
    CHECK_EQ(missing, 7.0f);   // no provider: the default
    // A new value re-places the reader.
    left.Set(50);
    h.Pump();
    CHECK_EQ(x, 36.0f);
}

TEST(RulersMergeAndMirror) {
    Harness h;
    const VerticalRuler a, b;
    const RectRulers r1, r2;
    float maxed = 0, minned = 0, inner = 0, outer = 0, relative = 0;
    const VerticalRuler rel;
    h.owner.SetDirection(LayoutDirection::Rtl);
    h.Run([&] {
        auto provider = std::make_shared<LambdaMeasurePolicy>([&](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
            Placeable& p = children[0]->Measure(Constraints{ 0, c.maxWidth, 0, c.maxHeight });
            return s.Layout(100, 100, {}, [&](RulerScope& r) {
                r.Provides(a, 10);
                r.Provides(b, 40);
                r.Provides(r1.left, 5);
                r.Provides(r2.left, 15);
                r.ProvidesRelative(rel, 30);
            }, [&p](PlacementScope& ps) { ps.Place(p, 0, 0); });
        });
        auto reader = std::make_shared<LambdaMeasurePolicy>([&](MeasureScope& s, std::span<Measurable* const>, const Constraints&) {
            return s.Layout(10, 10, [&](PlacementScope& ps) {
                maxed = ps.Current(VerticalRuler::MaxOf({ a, b }), -1);
                minned = ps.Current(VerticalRuler::MinOf({ a, b }), -1);
                inner = ps.Current(RectRulers::InnermostOf({ r1, r2 }).left, -1);
                outer = ps.Current(RectRulers::OutermostOf({ r1, r2 }).left, -1);
                relative = ps.Current(rel, -1);
            });
        });
        Box(Modifier{}, Alignment::TopStart(), false, [&] { Layout(Modifier{}, provider, [&] { Layout(Modifier{}, reader); }); });
    });
    CHECK_EQ(maxed, 40.0f);
    CHECK_EQ(minned, 10.0f);
    CHECK_EQ(inner, 15.0f);
    CHECK_EQ(outer, 5.0f);
    CHECK_EQ(relative, 70.0f);   // 30 from the right edge of a 100 px layout in RTL
}

#include "kiln/ui/InsetsRulers.h"

TEST(WindowInsetsRulersFollowTheInsets) {
    Harness h;
    auto& insets = h.owner.Insets();
    float statusTop = 0, safeBottom = 0, navMaxBottom = 0, imeTarget = 0, unset = 0;
    WindowInsetsAnimationInfo ime;
    h.Run([&] {
        auto reader = std::make_shared<LambdaMeasurePolicy>([&](MeasureScope& s, std::span<Measurable* const>, const Constraints&) {
            return s.Layout(10, 10, [&](PlacementScope& ps) {
                statusTop = ps.Current(WindowInsetsRulers::StatusBars().Current().top, -1);
                safeBottom = ps.Current(WindowInsetsRulers::SafeDrawing().Current().bottom, -1);
                navMaxBottom = ps.Current(WindowInsetsRulers::NavigationBars().Maximum().bottom, -1);
                ime = WindowInsetsRulers::Ime().GetAnimation(ps);
                imeTarget = ps.Current(ime.target.bottom, -1);
                unset = ps.Current(WindowInsetsRulers::Ime().Maximum().bottom, -1);
            });
        });
        Box(Modifier{}, Alignment::TopStart(), false, [&] { Layout(Modifier{}, reader); });
    });
    CHECK_EQ(statusTop, -1.0f);   // no insets delivered yet: defaults
    insets.Set(InsetsType::StatusBars, { 0, 24, 0, 0 });
    insets.Set(InsetsType::NavigationBars, { 0, 0, 0, 0 });
    insets.SetIgnoringVisibility(InsetsType::NavigationBars, { 0, 0, 0, 48 });
    insets.Set(InsetsType::Ime, { 0, 0, 0, 80 });
    insets.SetVisible(InsetsType::Ime, true);
    insets.SetAnimation(InsetsType::Ime, InsetsAnimation{ { 0, 0, 0, 0 }, { 0, 0, 0, 120 }, 0.5f, 1, 250 });
    insets.SetHasInsets(true);
    h.Pump();
    CHECK_EQ(statusTop, 24.0f);
    CHECK_EQ(safeBottom, 120.0f);      // 200 - the IME's 80
    CHECK_EQ(navMaxBottom, 152.0f);    // hidden bars still report where they would be
    CHECK(ime.isAnimating && ime.isVisible && ime.fraction == 0.5f && ime.durationMillis == 250);
    CHECK_EQ(imeTarget, 80.0f);        // 200 - 120
    CHECK_EQ(unset, -1.0f);            // the IME has no maximum
    // A change re-places the reader.
    insets.Set(InsetsType::StatusBars, { 0, 30, 0, 0 });
    h.Pump();
    CHECK_EQ(statusTop, 30.0f);
}

TEST(FitInsideAndOutsideSizeToTheRulers) {
    // A 200x200 parent providing an area 30..170 x 20..190.
    const RectRulers area;
    auto provider = std::make_shared<LambdaMeasurePolicy>([area](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
        Placeable& p = children[0]->Measure(c);
        return s.Layout(c.maxWidth, c.maxHeight, {}, [area](RulerScope& r) {
            r.Provides(area.left, 30);
            r.Provides(area.top, 20);
            r.Provides(area.right, 170);
            r.Provides(area.bottom, 190);
        }, [&p](PlacementScope& ps) { ps.Place(p, 0, 0); });
    });
    IntSize inside, outside, unbounded;
    IntOffset insideAt, unboundedAt;
    Harness h;
    h.Run([&] {
        Layout(Modifier{}, provider, [&] {
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                Box(OnGloballyPositioned(FitInside(FillMax(Modifier{}, Axis::Both, 1), area), [&](const LayoutCoordinates& lc) {
                    inside = lc.Size();
                    insideAt = RoundToIntOffset(lc.PositionInRoot());
                }), Alignment::TopStart(), false, [] {});
                Box(OnGloballyPositioned(FitOutside(FillMax(Modifier{}, Axis::Both, 1), area), [&](const LayoutCoordinates& lc) { outside = lc.Size(); }),
                    Alignment::TopStart(), false, [] {});
                // Unbounded: measured as it likes (40x40), centred on the area.
                Box(WrapContent(Modifier{}, Axis::Both, Alignment::TopStart(), true), Alignment::TopStart(), false, [&] {
                    Box(OnGloballyPositioned(SizeMod(FitInside(Modifier{}, area), Dp(40), Dp(40)), [&](const LayoutCoordinates& lc) {
                        unbounded = lc.Size();
                        unboundedAt = RoundToIntOffset(lc.PositionInRoot());
                    }), Alignment::TopStart(), false, [] {});
                });
            });
        });
    });
    CHECK(inside == (IntSize{ 140, 170 }));
    CHECK(insideAt == (IntOffset{ 30, 20 }));
    CHECK(outside == (IntSize{ 30, 200 }));   // the space left of the area
    CHECK(unbounded == (IntSize{ 40, 40 }));
    // The ruler values are clamped to its own 40x40: (30 + 40 - 40) / 2, (20 + 40 - 40) / 2.
    CHECK(unboundedAt == (IntOffset{ 15, 10 }));
}
