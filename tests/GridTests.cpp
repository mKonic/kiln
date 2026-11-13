// Grid: cases from GridTest (foundation-layout).
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Grid.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Locals.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    // Lays `content` out in a loose 400x400 window.
    void Show(kiln::Function<void()> content) {
        Owner owner;
        owner.SetDensity({ 1, 1 });
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        composition.SetContent([&] { Box(Modifier{}, Alignment::TopStart(), false, content); });
        rt::Composition::RecomposeAll();
        owner.MeasureAndLayout({ 400, 400 });
        composition.SetContent([] {});
    }
    struct Info { IntSize size{ -1, -1 }; Offset at{ -1, -1 }; };
    Modifier Save(Modifier m, Info* info) {
        return OnGloballyPositioned(std::move(m), [info](const LayoutCoordinates& c) { info->size = c.Size(); info->at = c.PositionInParent(); });
    }
    void Cell(Modifier m) { Box(std::move(m), Alignment::TopStart(), false, [] {}); }
    Modifier Filled(Modifier m) { return FillMax(std::move(m), Axis::Both, 1); }
    // An item with the given intrinsic widths (IntrinsicItem).
    void IntrinsicItem(Modifier m, int minWidth, int minIntrinsicWidth, int maxIntrinsicWidth) {
        struct P final : MeasurePolicy {
            int minWidth, minI, maxI;
            P(int a, int b, int c) : minWidth(a), minI(b), maxI(c) {}
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                // Reaches for the max intrinsic width within the constraints, at least minWidth.
                return s.Layout(std::max(c.ConstrainWidth(maxI), minWidth), c.minHeight);
            }
            int MinIntrinsicWidth(MeasureScope&, std::span<Measurable* const>, int) override { return minI; }
            int MaxIntrinsicWidth(MeasureScope&, std::span<Measurable* const>, int) override { return maxI; }
            int MinIntrinsicHeight(MeasureScope&, std::span<Measurable* const>, int) override { return 0; }
            int MaxIntrinsicHeight(MeasureScope&, std::span<Measurable* const>, int) override { return 0; }
        };
        Layout(std::move(m), std::make_shared<P>(minWidth, minIntrinsicWidth, maxIntrinsicWidth));
    }
}

TEST(GridSizesFixedPercentageAndFlexTracks) {
    Info a, b, f0, f1, f2, p0, p1;
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(Dp(50)); g.Column(Dp(100)); g.Row(Dp(50)); }, Modifier{}, [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 1, 1)), &a));
            Cell(Save(Filled(GridItem(Modifier{}, 1, 2)), &b));
        });
    });
    CHECK(a.size == (IntSize{ 50, 50 }) && a.at == (Offset{ 0, 0 }));
    CHECK(b.size == (IntSize{ 100, 50 }) && b.at == (Offset{ 50, 0 }));
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(Dp(100)); g.Column(Fr(1)); g.Column(Fr(3)); g.Row(Dp(50)); }, Width(Modifier{}, Dp(300)), [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 1, 1)), &f0));
            Cell(Save(Filled(GridItem(Modifier{}, 1, 2)), &f1));
            Cell(Save(Filled(GridItem(Modifier{}, 1, 3)), &f2));
        });
    });
    // 200 left: 1fr = 50, 3fr = 150.
    CHECK_EQ(f0.size.width, 100);
    CHECK(f1.size.width == 50 && f1.at.x == 100.0f);
    CHECK(f2.size.width == 150 && f2.at.x == 150.0f);
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(0.25f); g.Column(0.75f); g.Row(Dp(10)); }, Width(Modifier{}, Dp(200)), [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 1, 1)), &p0));
            Cell(Save(Filled(GridItem(Modifier{}, 1, 2)), &p1));
        });
    });
    CHECK_EQ(p0.size.width, 50);
    CHECK_EQ(p1.size.width, 150);
}

TEST(GridGapsSpansAndNegativeIndices) {
    Info g0, g1, span, neg[4];
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(Dp(50)); g.Column(Dp(50)); g.Row(Dp(50)); g.Gap(Dp(10)); }, Modifier{}, [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 1, 1)), &g0));
            Cell(Save(Filled(GridItem(Modifier{}, 1, 2)), &g1));
        });
    });
    CHECK(g0.at == (Offset{ 0, 0 }));
    CHECK(g1.at == (Offset{ 60, 0 }));
    Show([&] {
        Grid([](GridConfigurationScope& g) { for (int i = 0; i < 3; ++i) { g.Column(Dp(50)); g.Row(Dp(50)); } }, Modifier{}, [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 2, 2, 2, 2)), &span));
        });
    });
    CHECK(span.size == (IntSize{ 100, 100 }) && span.at == (Offset{ 50, 50 }));
    Show([&] {
        Grid([](GridConfigurationScope& g) { for (int i = 0; i < 3; ++i) { g.Column(Dp(50)); g.Row(Dp(50)); } }, Modifier{}, [&] {
            Cell(Save(Filled(GridItem(Modifier{}, 1, 1)), &neg[0]));
            Cell(Save(Filled(GridItem(Modifier{}, 1, -1)), &neg[1]));
            Cell(Save(Filled(GridItem(Modifier{}, -1, 1)), &neg[2]));
            Cell(Save(Filled(GridItem(Modifier{}, { -1, -1 }, { -1, -1 })), &neg[3]));
        });
    });
    CHECK(neg[0].at == (Offset{ 0, 0 }));
    CHECK(neg[1].at == (Offset{ 100, 0 }));
    CHECK(neg[2].at == (Offset{ 0, 100 }));
    CHECK(neg[3].at == (Offset{ 100, 100 }));
}

TEST(GridAutoPlacesAlongTheFlowAndMirrorsInRtl) {
    Info r[3], c[3], rtl[2];
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(Dp(50)); g.Column(Dp(50)); }, Modifier{}, [&] {
            for (auto& i : r) Cell(Save(SizeMod(Modifier{}, Dp(50), Dp(50)), &i));
        });
    });
    CHECK(r[0].at == (Offset{ 0, 0 }));
    CHECK(r[1].at == (Offset{ 50, 0 }));
    CHECK(r[2].at == (Offset{ 0, 50 }));   // wraps to the next (implicit) row
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Row(Dp(50)); g.Row(Dp(50)); g.flow = GridFlow::Column; }, Modifier{}, [&] {
            for (auto& i : c) Cell(Save(SizeMod(Modifier{}, Dp(50), Dp(50)), &i));
        });
    });
    CHECK(c[1].at == (Offset{ 0, 50 }));
    CHECK(c[2].at == (Offset{ 50, 0 }));
    Show([&] {
        rt::Composition::Current()->Provide(LocalLayoutDirection, LayoutDirection::Rtl, [&] {
            Grid([](GridConfigurationScope& g) { g.Column(Dp(100)); g.Column(Dp(100)); g.Row(Dp(100)); }, Modifier{}, [&] {
                for (auto& i : rtl) Cell(Save(SizeMod(Modifier{}, Dp(100), Dp(100)), &i));
            });
        });
    });
    CHECK(rtl[0].at == (Offset{ 100, 0 }));
    CHECK(rtl[1].at == (Offset{ 0, 0 }));
}

TEST(GridAutoTracksGrowFromMinToMaxContent) {
    Info tight, large;
    IntSize spanned;
    Show([&] {
        Box(Width(Modifier{}, Dp(60)), Alignment::TopStart(), false, [&] {
            Grid([](GridConfigurationScope& g) { g.Column(GridTrackSize::Auto()); }, Modifier{}, [&] {
                IntrinsicItem(Save(FillMax(GridItem(Modifier{}, 1, 1), Axis::Vertical, 1), &tight), 50, 50, 100);
            });
        });
    });
    CHECK_EQ(tight.size.width, 60);   // min 50, plus the 10 left
    Show([&] {
        Box(Width(Modifier{}, Dp(200)), Alignment::TopStart(), false, [&] {
            Grid([](GridConfigurationScope& g) { g.Column(GridTrackSize::Auto()); }, Modifier{}, [&] {
                IntrinsicItem(Save(FillMax(GridItem(Modifier{}, 1, 1), Axis::Vertical, 1), &large), 50, 50, 100);
            });
        });
    });
    CHECK_EQ(large.size.width, 100);   // capped at max content
    // A row-spanning item taller than its rows grows them evenly.
    Show([&] {
        Grid([](GridConfigurationScope& g) { g.Column(Dp(50)); g.Column(Dp(50)); g.Row(GridTrackSize::Auto()); g.Row(GridTrackSize::Auto()); },
             OnSizeChanged(Modifier{}, [&](IntSize s) { spanned = s; }), [&] {
                 Cell(SizeMod(GridItem(Modifier{}, 1, 1), Dp(50), Dp(10)));
                 Cell(SizeMod(GridItem(Modifier{}, 2, 1), Dp(50), Dp(10)));
                 Cell(SizeMod(GridItem(Modifier{}, 1, 2, 2), Dp(50), Dp(100)));
             });
    });
    CHECK_EQ(spanned.height, 100);
}

TEST(GridNamedAreasAndValidation) {
    Info area, unknown;
    Show([&] {
        Grid([](GridConfigurationScope& g) {
            g.Column(Dp(10)); g.Column(Dp(20));
            g.Row(Dp(10)); g.Row(Dp(30));
            g.Area("content", 2, 2);
        }, Modifier{}, [&] {
            Cell(Save(Filled(GridItemArea(Modifier{}, "content")), &area));
            Cell(Save(SizeMod(GridItemArea(Modifier{}, "missing"), Dp(5), Dp(5)), &unknown));
        });
    });
    CHECK(area.at == (Offset{ 10, 10 }));
    CHECK(area.size == (IntSize{ 20, 30 }));
    CHECK(unknown.at == (Offset{ 0, 0 }));   // an unknown area auto-places
    auto throws = [](auto f) { try { f(); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(throws([] { GridItem(Modifier{}, 1001, 1); }));
    CHECK(throws([] { GridItem(Modifier{}, 1, 1, 0); }));
    CHECK(throws([] { GridTrackSize::Flex(Fr(INFINITY)); }));
    CHECK(throws([] { GridTrackSize::MinMax(Dp(10), Fr(INFINITY)); }));
    CHECK(throws([] { GridConfigurationScope g; g.Area("a"); }));
}
