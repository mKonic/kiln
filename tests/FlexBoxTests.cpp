// FlexBox: cases from FlexBoxTest and FlexBoxDirectionTest (foundation-layout), in the Row
// direction unless they are about direction.
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/FlexBox.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Locals.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); rt::Composition::RecomposeAll(); owner.MeasureAndLayout({ 400, 400 }); }
    };
    Dp D(float v) { return Dp(v); }
    // A box that reports its size and position in its parent.
    Modifier Probe(Modifier m, IntSize* size, Offset* at = nullptr) {
        return OnGloballyPositioned(std::move(m), [size, at](const LayoutCoordinates& c) {
            if (size) *size = c.Size();
            if (at) *at = c.PositionInParent();
        });
    }
    void Leaf(Modifier m) { Box(std::move(m), Alignment::TopStart(), false, [] {}); }
    // A box of a fixed size inside a 200x200 parent (loose constraints to the FlexBox).
    void In200(kiln::Function<void()> content) { Box(SizeMod(Modifier{}, D(200), D(200)), Alignment::TopStart(), false, std::move(content)); }
    // A layout with a first baseline at `baseline`.
    void BaselineLeaf(Modifier m, int width, int height, int baseline) {
        Layout(std::move(m), std::make_shared<LambdaMeasurePolicy>([=](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) {
            return s.Layout(std::max(width, c.minWidth), std::max(height, c.minHeight), {}, { { &FirstBaseline, baseline } });
        }));
    }
}

TEST(FlexBoxAlignsItemsOnTheirBaselines) {
    Harness h;
    Offset a, b;
    h.Run([&] {
        FlexBox(Modifier{}, [](FlexBoxConfigScope& s) { s.AlignItems(FlexAlignItems::Baseline); }, [&] {
            BaselineLeaf(Probe(Modifier{}, nullptr, &a), 50, 40, 10);
            BaselineLeaf(Probe(Modifier{}, nullptr, &b), 50, 40, 30);
        });
    });
    // The highest ascent is 30: the first item moves down 20.
    CHECK_EQ(a.y, 20.0f);
    CHECK_EQ(b.y, 0.0f);
}

TEST(FlexBoxMirrorsInRtl) {
    Harness h;
    Offset p[3], r[3];
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalLayoutDirection, LayoutDirection::Rtl, [&] {
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                In200([&] {
                    FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [](FlexBoxConfigScope&) {}, [&] {
                        for (int i = 0; i < 3; ++i) Leaf(Probe(SizeMod(Modifier{}, D(20), D(20)), nullptr, &p[i]));
                    });
                });
            });
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                In200([&] {
                    FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [](FlexBoxConfigScope& s) { s.Direction(FlexDirection::RowReverse); }, [&] {
                        for (int i = 0; i < 3; ++i) Leaf(Probe(SizeMod(Modifier{}, D(20), D(20)), nullptr, &r[i]));
                    });
                });
            });
        });
    });
    // RTL starts at the right edge; RowReverse in RTL flows left to right.
    CHECK_EQ(p[0].x, 180.0f);
    CHECK_EQ(p[1].x, 160.0f);
    CHECK_EQ(p[2].x, 140.0f);
    CHECK_EQ(r[0].x, 0.0f);
    CHECK_EQ(r[1].x, 20.0f);
    CHECK_EQ(r[2].x, 40.0f);
}

TEST(FlexBoxGrowsShrinksAndSizesByBasis) {
    Harness h;
    IntSize g0, g1, g2, s0, s1, bd, bp;
    h.Run([&] {
        Column(Modifier{}, [&] {
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] {
                    Leaf(Probe(SizeMod(Modifier{}, D(20), D(20)), &g0));
                    Leaf(Probe(Flex(SizeMod(Modifier{}, D(20), D(20)), [](FlexConfigScope& f) { f.Grow(1); }), &g1));
                    Leaf(Probe(Flex(SizeMod(Modifier{}, D(20), D(20)), [](FlexConfigScope& f) { f.Grow(2); }), &g2));
                });
            });
            Box(SizeMod(Modifier{}, D(100), D(100)), Alignment::TopStart(), false, [&] {
                FlexBox(Width(Modifier{}, D(100)), [&] {
                    Leaf(Probe(Flex(SizeMod(Modifier{}, D(60), D(20)), [](FlexConfigScope& f) { f.Shrink(0); }), &s0));
                    Leaf(Probe(Flex(SizeIn(Height(Modifier{}, D(20)), D(20), Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified()),
                                    [](FlexConfigScope& f) { f.Basis(D(60)); }), &s1));
                });
            });
            In200([&] {
                FlexBox(Modifier{}, [&] { Leaf(Probe(Height(Flex(Modifier{}, [](FlexConfigScope& f) { f.Basis(D(50)); }), D(20)), &bd)); });
            });
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] { Leaf(Probe(Height(Flex(Modifier{}, [](FlexConfigScope& f) { f.Basis(0.5f); }), D(20)), &bp)); });
            });
        });
    });
    // 140 free: grow 1 takes a third, grow 2 two thirds.
    CHECK_EQ(g0.width, 20);
    CHECK(g1.width > 20);
    CHECK(g2.width > g1.width);
    CHECK_EQ(g0.width + g1.width + g2.width, 200);
    // 120 in 100: only the shrinkable one gives up the 20.
    CHECK_EQ(s0.width, 60);
    CHECK_EQ(s1.width, 40);
    CHECK_EQ(bd.width, 50);
    CHECK_EQ(bp.width, 100);
}

TEST(FlexBoxFillMaxIsTheDefaultGrow) {
    Harness h;
    IntSize a, b, c, d, e;
    Offset bAt;
    h.Run([&] {
        Column(Modifier{}, [&] {
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] {
                    Leaf(Probe(SizeMod(Modifier{}, D(50), D(50)), &a));
                    Leaf(Probe(Height(FillMax(Modifier{}, Axis::Horizontal, 1), D(50)), &b, &bAt));
                });
            });
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] {
                    Leaf(SizeMod(Modifier{}, D(50), D(50)));
                    Leaf(Probe(Height(FillMax(Modifier{}, Axis::Horizontal, 0.5f), D(50)), &c));
                });
            });
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] {
                    Leaf(SizeMod(Modifier{}, D(50), D(50)));
                    Leaf(SizeMod(FillMax(Flex(Probe(Modifier{}, &d), [](FlexConfigScope& f) { f.Grow(0); }), Axis::Horizontal, 1), D(50), D(50)));
                });
            });
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [&] {
                    Leaf(SizeMod(Modifier{}, D(50), D(50)));
                    Leaf(Height(FillMax(Flex(Probe(Modifier{}, &e), [](FlexConfigScope& f) { f.Grow(1); }), Axis::Horizontal, 0.5f), D(50)));
                });
            });
        });
    });
    CHECK_EQ(a.width, 50);
    CHECK_EQ(b.width, 150);
    CHECK_EQ(bAt.x, 50.0f);
    CHECK_EQ(c.width, 75);   // half of the 150 left
    CHECK_EQ(d.width, 50);   // grow(0) opts out
    CHECK_EQ(e.width, 150);  // an explicit grow wins over the fraction
}

TEST(FlexBoxAlignsJustifiesOrdersAndWraps) {
    Harness h;
    Offset al[3], sb[3], ord[3];
    IntSize wrapped;
    h.Run([&] {
        Column(Modifier{}, [&] {
            In200([&] {
                FlexBox(Modifier{}, [&] {
                    Leaf(Probe(SizeMod(Modifier{}, D(20), D(40)), nullptr, &al[0]));
                    Leaf(Probe(Flex(SizeMod(Modifier{}, D(20), D(20)), [](FlexConfigScope& f) { f.AlignSelf(FlexAlignSelf::End); }), nullptr, &al[1]));
                    Leaf(Probe(Flex(SizeMod(Modifier{}, D(20), D(20)), [](FlexConfigScope& f) { f.AlignSelf(FlexAlignSelf::Center); }), nullptr, &al[2]));
                });
            });
            In200([&] {
                FlexBox(FillMax(Modifier{}, Axis::Horizontal, 1), [](FlexBoxConfigScope& s) { s.JustifyContent(FlexJustifyContent::SpaceBetween); }, [&] {
                    for (int i = 0; i < 3; ++i) Leaf(Probe(SizeMod(Modifier{}, D(20), D(20)), nullptr, &sb[i]));
                });
            });
            In200([&] {
                FlexBox(Modifier{}, [&] {
                    for (int i = 0; i < 3; ++i)
                        Leaf(Probe(Flex(SizeMod(Modifier{}, D(20), D(20)), [i](FlexConfigScope& f) { f.Order(2 - i); }), nullptr, &ord[i]));
                });
            });
        });
    });
    Harness w;
    w.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(SizeMod(Modifier{}, D(100), D(100)), Alignment::TopStart(), false, [&] {
                FlexBox(OnSizeChanged(Modifier{}, [&](IntSize s) { wrapped = s; }), [](FlexBoxConfigScope& s) { s.Wrap(FlexWrap::Wrap); }, [&] {
                    for (int i = 0; i < 6; ++i) Leaf(SizeMod(Modifier{}, D(20), D(20)));
                });
            });
        });
    });
    CHECK_EQ(al[0].y, 0.0f);
    CHECK_EQ(al[1].y, 20.0f);
    CHECK_EQ(al[2].y, 10.0f);
    CHECK_EQ(sb[0].x, 0.0f);
    CHECK_EQ(sb[1].x, 90.0f);
    CHECK_EQ(sb[2].x, 180.0f);
    // Ordered 2, 1, 0: the last composed is first.
    CHECK_EQ(ord[2].x, 0.0f);
    CHECK_EQ(ord[1].x, 20.0f);
    CHECK_EQ(ord[0].x, 40.0f);
    // Five per 100-wide line: two lines.
    CHECK_EQ(wrapped.height, 40);
}

TEST(FlexBoxIntrinsicsAndValidation) {
    Harness h;
    int capped = 0, sum = 0, widest = 0;
    auto three = [] { for (float s : { 20.0f, 30.0f, 40.0f }) Leaf(SizeMod(Modifier{}, Dp(s), Dp(s))); };
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Width(Modifier{}, IntrinsicSize::Max), Alignment::TopStart(), false, [&] {
                FlexBox(OnSizeChanged(Modifier{}, [&](IntSize s) { capped = s.width; }), [](FlexBoxConfigScope& s) { s.Wrap(FlexWrap::Wrap); s.MaxItemsInEachLine(2); }, three);
            });
            Box(Width(Modifier{}, IntrinsicSize::Max), Alignment::TopStart(), false, [&] {
                FlexBox(OnSizeChanged(Modifier{}, [&](IntSize s) { sum = s.width; }), [](FlexBoxConfigScope& s) { s.Wrap(FlexWrap::Wrap); }, three);
            });
            Box(Width(Modifier{}, IntrinsicSize::Min), Alignment::TopStart(), false, [&] {
                FlexBox(OnSizeChanged(Modifier{}, [&](IntSize s) { widest = s.width; }), [](FlexBoxConfigScope& s) { s.Wrap(FlexWrap::Wrap); }, three);
            });
        });
    });
    CHECK_EQ(capped, 50);   // lines (20 + 30) and (40)
    CHECK_EQ(sum, 90);
    CHECK_EQ(widest, 40);
    auto throws = [](auto f) { try { f(); } catch (const std::invalid_argument&) { return true; } return false; };
    FlexConfigScope item;
    FlexBoxConfigScope box;
    CHECK(throws([&] { item.Grow(-1); }));
    CHECK(throws([&] { item.Shrink(-1); }));
    CHECK(throws([&] { box.MaxItemsInEachLine(0); }));
}
