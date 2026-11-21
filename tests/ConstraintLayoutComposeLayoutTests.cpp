// Ported from constraintlayout-compose ConstraintLayoutTest.kt (androidx). Expected values are
// upstream's. onGloballyPositioned sizes are read with OnSizeChanged; the display size is the
// test window's.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scroll.h"

#include <cmath>
#include <optional>

using namespace cltest;
using foundation::Axis;
using foundation::AspectRatio;
using foundation::FillMax;
using foundation::OnSizeChanged;
using foundation::SizeMod;
using foundation::Width;

namespace {
    constexpr ui::IntSize kDisplay{ 1080, 1920 };   // ComposeUiTestOptions' window
    int RoundToInt(float v) { return int(std::lround(v)); }

    // dividerMatchTextHeight_*: an aspect-ratio box right of a centre guideline and a divider
    // beside it, in a fixed-width, wrap-height ConstraintLayout.
    struct DividerSizes {
        std::optional<ui::IntSize> clSize, aspectRatioBoxSize, dividerSize;
    };
    void DividerLayout(DividerSizes& out, Modifier clModifier, Dp aspectWidth, Dimension aspectHeight, Dimension dividerHeight) {
        ConstraintLayout(OnSizeChanged(std::move(clModifier), [&out](ui::IntSize s) { out.clSize = s; }), [&, aspectWidth, aspectHeight, dividerHeight](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto guideline = s.CreateGuidelineFromAbsoluteLeft(0.5f);
            Box(OnSizeChanged(AspectRatio(Width(s.ConstrainAs({}, refs[0],
                                                              [guideline, aspectHeight](ConstrainScope& c) {
                                                                  c.CenterTo(c.parent);
                                                                  c.start.LinkTo(guideline);
                                                                  c.width = Dimension::PreferredWrapContent();
                                                                  c.height = aspectHeight;
                                                              }),
                                                aspectWidth),
                                          2.0f),
                              [&out](ui::IntSize sz) { out.aspectRatioBoxSize = sz; }));
            Box(OnSizeChanged(s.ConstrainAs({}, refs[1],
                                            [dividerHeight](ConstrainScope& c) {
                                                c.CenterTo(c.parent);
                                                c.width = Dimension::Value(Dp(1));
                                                c.height = dividerHeight;
                                            }),
                              [&out](ui::IntSize sz) { out.dividerSize = sz; }));
        });
    }
}

TEST(ClLayout_dividerMatchTextHeight_spread) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    DividerSizes sizes;
    Content(t, [&] {
        DividerLayout(sizes, FillMax({}, Axis::Horizontal), px(float(kDisplay.width)), Dimension::WrapContent(), Dimension::FillToConstraints());
    });
    t.WaitForIdle();
    CHECK(sizes.aspectRatioBoxSize && sizes.dividerSize);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f), sizes.aspectRatioBoxSize->width);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f / 2.0f), sizes.aspectRatioBoxSize->height);
    CHECK_EQ(RoundToInt(1 * px.density), sizes.dividerSize->width);
    CHECK_EQ(sizes.aspectRatioBoxSize->height, sizes.dividerSize->height);
}

TEST(ClLayout_dividerMatchTextHeight_spread_withPreferredWrapHeightText) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    DividerSizes sizes;
    Content(t, [&] {
        DividerLayout(sizes, FillMax({}, Axis::Horizontal), px(float(kDisplay.width)), Dimension::PreferredWrapContent(),
                      Dimension::FillToConstraints());
    });
    t.WaitForIdle();
    CHECK(sizes.aspectRatioBoxSize && sizes.dividerSize);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f), sizes.aspectRatioBoxSize->width);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f / 2.0f), sizes.aspectRatioBoxSize->height);
    CHECK_EQ(RoundToInt(1 * px.density), sizes.dividerSize->width);
    CHECK_EQ(sizes.aspectRatioBoxSize->height, sizes.dividerSize->height);
}

TEST(ClLayout_dividerMatchTextHeight_percent) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    DividerSizes sizes;
    Content(t, [&] {
        DividerLayout(sizes, FillMax({}, Axis::Horizontal), px(float(kDisplay.width)), Dimension::WrapContent(), Dimension::Percent(0.8f));
    });
    t.WaitForIdle();
    CHECK(sizes.aspectRatioBoxSize && sizes.dividerSize);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f), sizes.aspectRatioBoxSize->width);
    CHECK_EQ(RoundToInt(kDisplay.width / 2.0f / 2.0f), sizes.aspectRatioBoxSize->height);
    CHECK_EQ(RoundToInt(1 * px.density), sizes.dividerSize->width);
    const int boxHeight = sizes.aspectRatioBoxSize->height;
    const float margin = (boxHeight - boxHeight * 0.8f) / 2.0f;
    const int expectedDividerHeight = RoundToInt(margin + boxHeight * 0.8f) - RoundToInt(margin);
    CHECK_EQ(expectedDividerHeight, sizes.dividerSize->height);
}

TEST(ClLayout_dividerMatchTextHeight_inWrapConstraintLayout_shortText) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    DividerSizes sizes;
    const Dp size = px(40);
    Content(t, [&] { DividerLayout(sizes, Modifier{}, size, Dimension::WrapContent(), Dimension::FillToConstraints()); });
    t.WaitForIdle();
    CHECK(sizes.clSize && sizes.aspectRatioBoxSize && sizes.dividerSize);
    const int sizePx = RoundToInt(size.value * px.density);
    CHECK_EQ(sizePx * 2, sizes.clSize->width);
    CHECK_EQ(sizePx / 2, sizes.clSize->height);
    CHECK_EQ(sizePx, sizes.aspectRatioBoxSize->width);
    CHECK_EQ(sizePx / 2, sizes.aspectRatioBoxSize->height);
    CHECK_EQ(RoundToInt(1 * px.density), sizes.dividerSize->width);
    CHECK_EQ(sizes.aspectRatioBoxSize->height, sizes.dividerSize->height);
}

namespace {
    float Left(test::ComposeUiTest& t, const std::string& tag) { return t.OnNodeWithTag(tag).GetUnclippedBoundsInRoot().left; }
    ui::IntOffset Position(test::ComposeUiTest& t, const std::string& tag) {
        const ui::Rect b = t.OnNodeWithTag(tag).GetUnclippedBoundsInRoot();
        return { RoundToInt(b.left), RoundToInt(b.top) };
    }
    void WithDirection(bool rtl, const kiln::Function<void()>& content) {
        if (!rtl) {
            content();
            return;
        }
        rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Rtl, [&] { content(); });
    }
    std::vector<float> Lefts(test::ComposeUiTest& t, int count) {
        std::vector<float> out;
        for (int i = 0; i < count; i++) out.push_back(Left(t, "p" + std::to_string(i)));
        return out;
    }
    void CheckPositions(const std::vector<float>& actual, std::initializer_list<float> expected) {
        CHECK_EQ(actual.size(), expected.size());
        std::size_t i = 0;
        for (float e : expected) {
            if (i < actual.size()) CHECK_EQ(actual[i], e);
            i++;
        }
    }
    Modifier Probe(Modifier m, int index) { return foundation::TestTag(std::move(m), "p" + std::to_string(index)); }
}

TEST(ClLayout_testConstraintLayout_withInlineDSL) {
    test::ComposeUiTest t;
    const int boxSize = 100, offset = 150;
    ui::IntSize rootSize;
    Content(t, [&] {
        ConstraintLayout(OnSizeChanged(FillMax({}, Axis::Both), [&](ui::IntSize s) { rootSize = s; }), [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(3);
            auto box0 = refs[0];
            Box(Probe(SizeMod(s.ConstrainAs({}, refs[0], [](ConstrainScope& c) { c.CenterTo(c.parent); }), Dp(boxSize), Dp(boxSize)), 0));
            auto half = s.CreateGuidelineFromAbsoluteLeft(0.5f);
            Box(Probe(SizeMod(s.ConstrainAs({}, refs[1],
                                            [half, box0, offset](ConstrainScope& c) {
                                                c.start.LinkTo(half, Dp(offset));
                                                c.bottom.LinkTo(box0.top);
                                            }),
                              Dp(boxSize), Dp(boxSize)),
                      1));
            Box(Probe(SizeMod(s.ConstrainAs({}, refs[2],
                                            [offset](ConstrainScope& c) {
                                                c.start.LinkTo(c.parent.start, Dp(offset));
                                                c.bottom.LinkTo(c.parent.bottom, Dp(offset));
                                            }),
                              Dp(boxSize), Dp(boxSize)),
                      2));
        });
    });
    t.WaitForIdle();
    const int displayWidth = rootSize.width, displayHeight = rootSize.height;
    CHECK(Position(t, "p0") == ui::IntOffset(RoundToInt((displayWidth - boxSize) / 2.0f), RoundToInt((displayHeight - boxSize) / 2.0f)));
    CHECK(Position(t, "p1") ==
          ui::IntOffset(RoundToInt(displayWidth / 2.0f + offset), RoundToInt((displayHeight - boxSize) / 2.0f - boxSize)));
    CHECK(Position(t, "p2") == ui::IntOffset(offset, displayHeight - boxSize - offset));
}

TEST(ClLayout_testConstraintLayout_withConstraintSet) {
    test::ComposeUiTest t;
    const int boxSize = 100, offset = 150;
    ui::IntSize rootSize;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             auto box0 = s.CreateRefFor("box0");
                             auto box1 = s.CreateRefFor("box1");
                             auto box2 = s.CreateRefFor("box2");
                             s.Constrain(box0, [](ConstrainScope& c) { c.CenterTo(c.parent); });
                             auto half = s.CreateGuidelineFromAbsoluteLeft(0.5f);
                             s.Constrain(box1, [&](ConstrainScope& c) {
                                 c.start.LinkTo(half, Dp(offset));
                                 c.bottom.LinkTo(box0.top);
                             });
                             s.Constrain(box2, [&](ConstrainScope& c) {
                                 c.start.LinkTo(c.parent.start, Dp(offset));
                                 c.bottom.LinkTo(c.parent.bottom, Dp(offset));
                             });
                         }),
                         OnSizeChanged(FillMax({}, Axis::Both), [&](ui::IntSize s) { rootSize = s; }), [] {
                             for (int i = 0; i < 3; i++) Box(Probe(SizeMod(foundation::LayoutId({}, "box" + std::to_string(i)), Dp(100), Dp(100)), i));
                         });
    });
    t.WaitForIdle();
    const int displayWidth = rootSize.width, displayHeight = rootSize.height;
    CHECK(Position(t, "p0") == ui::IntOffset(RoundToInt((displayWidth - boxSize) / 2.0f), RoundToInt((displayHeight - boxSize) / 2.0f)));
    CHECK(Position(t, "p1") ==
          ui::IntOffset(RoundToInt(displayWidth / 2.0f + offset), RoundToInt((displayHeight - boxSize) / 2.0f - boxSize)));
    CHECK(Position(t, "p2") == ui::IntOffset(offset, displayHeight - boxSize - offset));
}

namespace {
    void GuidelinesDsl(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(SizeMod({}, Dp(200), Dp(200)), [](ConstraintLayoutScope& s) {
                const Dp offset(50);
                const VerticalAnchor guidelines[] = {
                    s.CreateGuidelineFromStart(offset),  s.CreateGuidelineFromAbsoluteLeft(offset), s.CreateGuidelineFromEnd(offset),
                    s.CreateGuidelineFromAbsoluteRight(offset), s.CreateGuidelineFromStart(0.25f), s.CreateGuidelineFromAbsoluteLeft(0.25f),
                    s.CreateGuidelineFromEnd(0.25f), s.CreateGuidelineFromAbsoluteRight(0.25f),
                };
                int index = 0;
                for (const VerticalAnchor& guideline : guidelines) {
                    auto ref = s.CreateRef();
                    Box(Probe(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), ref, [guideline](ConstrainScope& c) { c.absoluteLeft.LinkTo(guideline); }),
                              index++));
                }
            });
        });
    }

    const char* JsonGuidelines() {
        return "{\n"
               "  g0: { type: 'vGuideline', start: 50.0 },\n"
               "  g1: { type: 'vGuideline', left: 50.0 },\n"
               "  g2: { type: 'vGuideline', end: 50.0 },\n"
               "  g3: { type: 'vGuideline', right: 50.0 },\n"
               "  g4: { type: 'vGuideline', percent: [\"start\", 0.25] },\n"
               "  g5: { type: 'vGuideline', percent: [\"left\", 0.25] },\n"
               "  g6: { type: 'vGuideline', percent: [\"end\", 0.25] },\n"
               "  g7: { type: 'vGuideline', percent: [\"right\", 0.25] },\n"
               "  box0: { left: ['g0', 'start', 0] },\n"
               "  box1: { left: ['g1', 'start', 0] },\n"
               "  box2: { left: ['g2', 'start', 0] },\n"
               "  box3: { left: ['g3', 'start', 0] },\n"
               "  box4: { left: ['g4', 'start', 0] },\n"
               "  box5: { left: ['g5', 'start', 0] },\n"
               "  box6: { left: ['g6', 'start', 0] },\n"
               "  box7: { left: ['g7', 'start', 0] }\n"
               "}";
    }

    void GuidelinesJson(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(MakeConstraintSet(JsonGuidelines()), SizeMod({}, Dp(200), Dp(200)), [] {
                for (int i = 0; i < 8; i++) Box(Probe(SizeMod(foundation::LayoutId({}, "box" + std::to_string(i)), Dp(1), Dp(1)), i));
            });
        });
    }
}

TEST(ClLayout_testConstraintLayout_guidelines_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { GuidelinesDsl(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 8), { 50, 50, 150, 150, 50, 50, 150, 150 });
}

TEST(ClLayout_testConstraintLayout_json_guidelines_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { GuidelinesJson(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 8), { 50, 50, 150, 150, 50, 50, 150, 150 });
}

TEST(ClLayout_testConstraintLayout_guidelines_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { GuidelinesDsl(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 8), { 150, 50, 50, 150, 150, 50, 50, 150 });
}

TEST(ClLayout_testConstraintLayout_json_guidelines_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { GuidelinesJson(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 8), { 150, 50, 50, 150, 150, 50, 50, 150 });
}

namespace {
    void BarriersDsl(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(SizeMod({}, Dp(200), Dp(200)), [](ConstraintLayoutScope& s) {
                auto refs = s.CreateRefs(2);
                auto guideline1 = s.CreateGuidelineFromAbsoluteLeft(Dp(50));
                auto guideline2 = s.CreateGuidelineFromAbsoluteRight(Dp(50));
                Box(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), refs[0], [guideline1](ConstrainScope& c) { c.absoluteLeft.LinkTo(guideline1); }));
                Box(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), refs[1], [guideline2](ConstrainScope& c) { c.absoluteLeft.LinkTo(guideline2); }));
                const VerticalAnchor barriers[] = {
                    s.CreateStartBarrier({ &refs[0], &refs[1] }),
                    s.CreateAbsoluteLeftBarrier({ &refs[0], &refs[1] }),
                    s.CreateEndBarrier({ &refs[0], &refs[1] }),
                    s.CreateAbsoluteRightBarrier({ &refs[0], &refs[1] }),
                };
                int index = 0;
                for (const VerticalAnchor& barrier : barriers) {
                    auto ref = s.CreateRef();
                    Box(Probe(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), ref, [barrier](ConstrainScope& c) { c.absoluteLeft.LinkTo(barrier); }),
                              index++));
                }
            });
        });
    }

    const char* JsonBarriers() {
        return "{\n"
               "  g0: { type: 'vGuideline', left: 50.0 },\n"
               "  g1: { type: 'vGuideline', right: 50.0 },\n"
               "  boxA: { left: ['g0', 'start', 0] },\n"
               "  boxB: { left: ['g1', 'start', 0] },\n"
               "  b0: { type: 'barrier', direction: 'start', contains: ['boxA','boxB'] },\n"
               "  b1: { type: 'barrier', direction: 'left', contains: ['boxA','boxB'] },\n"
               "  b2: { type: 'barrier', direction: 'end', contains: ['boxA','boxB'] },\n"
               "  b3: { type: 'barrier', direction: 'right', contains: ['boxA','boxB'] },\n"
               "  box0: { left: ['b0', 'start', 0] },\n"
               "  box1: { left: ['b1', 'start', 0] },\n"
               "  box2: { left: ['b2', 'start', 0] },\n"
               "  box3: { left: ['b3', 'start', 0] },\n"
               "}";
    }

    void BarriersJson(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(MakeConstraintSet(JsonBarriers()), SizeMod({}, Dp(200), Dp(200)), [] {
                Box(SizeMod(foundation::LayoutId({}, "boxA"), Dp(1), Dp(1)));
                Box(SizeMod(foundation::LayoutId({}, "boxB"), Dp(1), Dp(1)));
                for (int i = 0; i < 4; i++) Box(Probe(SizeMod(foundation::LayoutId({}, "box" + std::to_string(i)), Dp(1), Dp(1)), i));
            });
        });
    }
}

TEST(ClLayout_testConstraintLayout_barriers_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { BarriersDsl(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 4), { 50, 50, 151, 151 });
}

TEST(ClLayout_testConstraintLayout_json_barriers_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { BarriersJson(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 4), { 50, 50, 151, 151 });
}

TEST(ClLayout_testConstraintLayout_barriers_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { BarriersDsl(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 4), { 151, 50, 50, 151 });
}

TEST(ClLayout_testConstraintLayout_json_barriers_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { BarriersJson(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 4), { 151, 50, 50, 151 });
}

namespace {
    // listAnchors: the sixteen start/end/absoluteLeft/absoluteRight combinations.
    std::vector<kiln::Function<void(ConstrainScope&)>> ListAnchors(const ConstrainedLayoutReference& box) {
        return {
            [box](ConstrainScope& c) { c.start.LinkTo(box.start); },
            [box](ConstrainScope& c) { c.absoluteLeft.LinkTo(box.start); },
            [box](ConstrainScope& c) { c.start.LinkTo(box.absoluteLeft); },
            [box](ConstrainScope& c) { c.absoluteLeft.LinkTo(box.absoluteLeft); },
            [box](ConstrainScope& c) { c.end.LinkTo(box.start); },
            [box](ConstrainScope& c) { c.absoluteRight.LinkTo(box.start); },
            [box](ConstrainScope& c) { c.end.LinkTo(box.absoluteLeft); },
            [box](ConstrainScope& c) { c.absoluteRight.LinkTo(box.absoluteLeft); },
            [box](ConstrainScope& c) { c.start.LinkTo(box.end); },
            [box](ConstrainScope& c) { c.absoluteLeft.LinkTo(box.end); },
            [box](ConstrainScope& c) { c.start.LinkTo(box.absoluteRight); },
            [box](ConstrainScope& c) { c.absoluteLeft.LinkTo(box.absoluteRight); },
            [box](ConstrainScope& c) { c.end.LinkTo(box.end); },
            [box](ConstrainScope& c) { c.absoluteRight.LinkTo(box.end); },
            [box](ConstrainScope& c) { c.end.LinkTo(box.absoluteRight); },
            [box](ConstrainScope& c) { c.absoluteRight.LinkTo(box.absoluteRight); },
        };
    }

    void AnchorsDsl(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(SizeMod({}, Dp(200), Dp(200)), [](ConstraintLayoutScope& s) {
                auto box = s.CreateRef();
                auto guideline = s.CreateGuidelineFromAbsoluteLeft(Dp(50));
                Box(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), box, [guideline](ConstrainScope& c) { c.absoluteLeft.LinkTo(guideline); }));
                int index = 0;
                for (auto& anchor : ListAnchors(box)) {
                    auto ref = s.CreateRef();
                    Box(Probe(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), ref, std::move(anchor)), index++));
                }
            });
        });
    }

    const char* JsonAnchors() {
        return "{\n"
               "  g1: { type: 'vGuideline', left: 50.0 },\n"
               "  box: { left: ['g1', 'left', 0] },\n"
               "  box0: { start: ['box','start',0] },\n"
               "  box1: { left: ['box','start',0] },\n"
               "  box2: { start: ['box','left',0] },\n"
               "  box3: { left: ['box','left',0] },\n"
               "  box4: { end: ['box','start',0] },\n"
               "  box5: { right: ['box','start',0] },\n"
               "  box6: { end: ['box','left',0] },\n"
               "  box7: { right: ['box','left',0] },\n"
               "  box8: { start: ['box','end',0] },\n"
               "  box9: { left: ['box','end',0] },\n"
               "  box10: { start: ['box','right',0] },\n"
               "  box11: { left: ['box','right',0] },\n"
               "  box12: { end: ['box','end',0] },\n"
               "  box13: { right: ['box','end',0] },\n"
               "  box14: { end: ['box','right',0] },\n"
               "  box15: { right: ['box','right',0] }\n"
               "}";
    }

    void AnchorsJson(bool rtl) {
        WithDirection(rtl, [] {
            ConstraintLayout(MakeConstraintSet(JsonAnchors()), SizeMod({}, Dp(200), Dp(200)), [] {
                Box(SizeMod(foundation::LayoutId({}, "box"), Dp(1), Dp(1)));
                for (int i = 0; i < 16; i++) Box(Probe(SizeMod(foundation::LayoutId({}, "box" + std::to_string(i)), Dp(1), Dp(1)), i));
            });
        });
    }
}

TEST(ClLayout_testConstraintLayout_anchors_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { AnchorsDsl(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 16), { 50, 50, 50, 50, 49, 49, 49, 49, 51, 51, 51, 51, 50, 50, 50, 50 });
}

TEST(ClLayout_testConstraintLayout_json_anchors_ltr) {
    test::ComposeUiTest t;
    Content(t, [] { AnchorsJson(false); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 16), { 50, 50, 50, 50, 49, 49, 49, 49, 51, 51, 51, 51, 50, 50, 50, 50 });
}

TEST(ClLayout_testConstraintLayout_anchors_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { AnchorsDsl(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 16), { 50, 51, 49, 50, 51, 50, 50, 49, 49, 50, 50, 51, 50, 49, 51, 50 });
}

TEST(ClLayout_testConstraintLayout_json_anchors_rtl) {
    test::ComposeUiTest t;
    Content(t, [] { AnchorsJson(true); });
    t.WaitForIdle();
    CheckPositions(Lefts(t, 16), { 50, 51, 49, 50, 51, 50, 50, 49, 49, 50, 50, 51, 50, 49, 51, 50 });
}

TEST(ClLayout_testConstraintLayout_barriers_margins) {
    test::ComposeUiTest t;
    Content(t, [] {
        ConstraintLayout(SizeMod({}, Dp(200), Dp(200)), [](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            auto guideline1 = s.CreateGuidelineFromAbsoluteLeft(Dp(50));
            auto guideline2 = s.CreateGuidelineFromTop(Dp(50));
            Box(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), box, [guideline1, guideline2](ConstrainScope& c) {
                c.absoluteLeft.LinkTo(guideline1);
                c.top.LinkTo(guideline2);
            }));
            auto leftBarrier = s.CreateAbsoluteLeftBarrier({ &box }, Dp(10));
            auto topBarrier = s.CreateTopBarrier({ &box }, Dp(10));
            auto rightBarrier = s.CreateAbsoluteRightBarrier({ &box }, Dp(10));
            auto bottomBarrier = s.CreateBottomBarrier({ &box }, Dp(10));
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), s.CreateRef(),
                                    [leftBarrier, topBarrier](ConstrainScope& c) {
                                        c.absoluteLeft.LinkTo(leftBarrier);
                                        c.top.LinkTo(topBarrier);
                                    }),
                      0));
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(1), Dp(1)), s.CreateRef(),
                                    [rightBarrier, bottomBarrier](ConstrainScope& c) {
                                        c.absoluteLeft.LinkTo(rightBarrier);
                                        c.top.LinkTo(bottomBarrier);
                                    }),
                      1));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(60, 60));
    CHECK(Position(t, "p1") == ui::IntOffset(61, 61));
}

TEST(ClLayout_links_canBeOverridden) {
    test::ComposeUiTest t;
    Content(t, [] {
        ConstraintLayout(Width({}, Dp(10)), [](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            Box(Probe(s.ConstrainAs({}, box,
                                    [](ConstrainScope& c) {
                                        c.start.LinkTo(c.parent.end);
                                        c.start.LinkTo(c.parent.start);
                                    }),
                      0));
        });
    });
    t.WaitForIdle();
    CHECK_EQ(Left(t, "p0"), 0.0f);
}

namespace {
    template<typename T, typename Init>
    T& Remember(Init&& init) { return rt::Composition::Current()->Remember<T>(std::forward<Init>(init)); }
}

TEST(ClLayout_testConstraintLayout_inlineDSL_recompositionDoesNotCrash) {
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            if (first.Get()) Box(s.ConstrainAs({}, box, [](ConstrainScope&) {}));
            else Box(s.ConstrainAs({}, box, [](ConstrainScope&) {}));
        });
    });
    t.WaitForIdle();
    first.Set(false);
    t.WaitForIdle();
    CHECK(true);
}

TEST(ClLayout_testConstraintLayout_ConstraintSetDSL_recompositionDoesNotCrash) {
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([](ConstraintSetScope& s) { s.Constrain(s.CreateRefFor("box"), [](ConstrainScope&) {}); }), Modifier{}, [&] {
            if (first.Get()) Box(foundation::LayoutId({}, "box"));
            else Box(foundation::LayoutId({}, "box"));
        });
    });
    t.WaitForIdle();
    first.Set(false);
    t.WaitForIdle();
    CHECK(true);
}

TEST(ClLayout_testConstraintLayout_inlineDSL_remeasureDoesNotCrash) {
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(first.Get() ? Modifier{} : foundation::Padding({}, Dp(10)), [&](ConstraintLayoutScope&) {
            Box(first.Get() ? Modifier{} : SizeMod({}, Dp(20), Dp(20)));
        });
    });
    t.WaitForIdle();
    first.Set(false);
    t.WaitForIdle();
    CHECK(true);
}

TEST(ClLayout_testConstraintLayout_ConstraintSetDSL_remeasureDoesNotCrash) {
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([](ConstraintSetScope&) {}), first.Get() ? Modifier{} : foundation::Padding({}, Dp(10)),
                         [&] { Box(first.Get() ? Modifier{} : SizeMod({}, Dp(20), Dp(20))); });
    });
    t.WaitForIdle();
    first.Set(false);
    t.WaitForIdle();
    CHECK(true);
}

TEST(ClLayout_testConstraintLayout_doesNotCrashWhenOnlyContentIsRecomposed) {
    test::ComposeUiTest t;
    rt::State<bool> smallSize{ true };
    Content(t, [&] {
        Box(Modifier{}, foundation::Alignment::TopStart(), false, [&] {
            ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
                auto refs = s.CreateRefs(2);
                s.CreateBottomBarrier({ &refs[0] });
                Box(s.ConstrainAs(foundation::Height({}, Dp(smallSize.Get() ? 30 : 40)), refs[0], [](ConstrainScope&) {}));
                Box(Modifier{});
            });
        });
    });
    t.WaitForIdle();
    smallSize.Set(false);
    t.WaitForIdle();
    CHECK(true);
}

TEST(ClLayout_testConstraintLayout_doesNotRemeasureUnnecessarily) {
    // Compose's compiler keeps the constrainAs lambda (its captures are stable); here the
    // block is remembered, which is what that amounts to.
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    int dslExecutions = 0;
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto box1 = s.CreateRefs(1)[0];
            auto box2 = s.CreateRef();
            auto guideline = s.CreateGuidelineFromStart(0.5f);
            auto barrier = s.CreateAbsoluteLeftBarrier({ &box1 });
            (void)first.Get();   // the content runs again when `first` changes
            auto& empty = Remember<ConstraintLayoutScope::ConstrainBlock>(
                [] { return std::make_shared<const kiln::Function<void(ConstrainScope&)>>([](ConstrainScope&) {}); });
            Box(s.ConstrainAs({}, box1, empty));
            auto& block = Remember<ConstraintLayoutScope::ConstrainBlock>([&, box1, guideline, barrier] {
                return std::make_shared<const kiln::Function<void(ConstrainScope&)>>([&dslExecutions, box1, guideline, barrier](ConstrainScope& c) {
                    c.start.LinkTo(box1.end);
                    c.end.LinkTo(guideline);
                    c.start.LinkTo(barrier);
                    ++dslExecutions;
                });
            });
            Box(s.ConstrainAs({}, box2, block));
        });
    });
    t.WaitForIdle();
    CHECK_EQ(1, dslExecutions);
    first.Set(false);
    t.WaitForIdle();
    CHECK_EQ(1, dslExecutions);
}

TEST(ClLayout_testConstraintLayout_doesRemeasure_whenHelpersChange_butConstraintsDont) {
    test::ComposeUiTest t;
    const int size = 100;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            if (!first.Get()) s.CreateVerticalChain({ &refs[0], &refs[1] });
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(size), Dp(size)), refs[0], [](ConstrainScope&) {}), 0));
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(size), Dp(size)), refs[1], [](ConstrainScope&) {}), 1));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(0, 0));
    CHECK(Position(t, "p1") == ui::IntOffset(0, 0));
    first.Set(false);
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(0, 0));
    CHECK(Position(t, "p1") == ui::IntOffset(0, size));
}

TEST(ClLayout_testConstraintLayout_doesRemeasure_whenHelpersDontChange_butConstraintsDo) {
    test::ComposeUiTest t;
    const int size = 100;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto topBarrier = s.CreateTopBarrier({ &refs[0] });
            auto bottomBarrier = s.CreateBottomBarrier({ &refs[0] });
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(size), Dp(size)), refs[0], [](ConstrainScope&) {}), 0));
            Box(Probe(s.ConstrainAs(SizeMod({}, Dp(size), Dp(size)), refs[1],
                                    [&first, topBarrier, bottomBarrier](ConstrainScope& c) {
                                        if (first.Get()) c.top.LinkTo(topBarrier);
                                        else c.top.LinkTo(bottomBarrier);
                                    }),
                      1));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(0, 0));
    CHECK(Position(t, "p1") == ui::IntOffset(0, 0));
    first.Set(false);
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(0, 0));
    CHECK(Position(t, "p1") == ui::IntOffset(0, size));
}

TEST(ClLayout_testConstraintLayout_updates_whenConstraintSetChanges) {
    test::ComposeUiTest t;
    const int box1Size = 20;
    rt::State<bool> first{ true };
    auto constraintSet1 = MakeConstraintSet([](ConstraintSetScope& s) {
        auto box1 = s.CreateRefFor("box1");
        auto box2 = s.CreateRefFor("box2");
        s.Constrain(box2, [&](ConstrainScope& c) { c.start.LinkTo(box1.end); });
    });
    auto constraintSet2 = MakeConstraintSet([](ConstraintSetScope& s) {
        auto box1 = s.CreateRefFor("box1");
        auto box2 = s.CreateRefFor("box2");
        s.Constrain(box2, [&](ConstrainScope& c) { c.top.LinkTo(box1.bottom); });
    });
    Content(t, [&] {
        ConstraintLayout(first.Get() ? constraintSet1 : constraintSet2, Modifier{}, [] {
            Box(SizeMod(foundation::LayoutId({}, "box1"), Dp(box1Size), Dp(box1Size)));
            Box(Probe(foundation::LayoutId({}, "box2"), 2));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(box1Size, 0));
    first.Set(false);
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(0, box1Size));
}

TEST(ClLayout_testConstraintLayout_doesNotRebuildFromDsl_whenResizedOnly) {
    test::ComposeUiTest t;
    rt::State<float> size{ 100 };
    int builds = 0;
    Content(t, [&] {
        const float sz = size.Get();
        ConstraintLayout(SizeMod({}, Dp(sz), Dp(sz)), [&](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            auto& onBuild = Remember<ConstraintLayoutScope::ConstrainBlock>(
                [&] { return std::make_shared<const kiln::Function<void(ConstrainScope&)>>([&builds](ConstrainScope&) { ++builds; }); });
            Box(s.ConstrainAs({}, box, onBuild));
        });
    });
    t.WaitForIdle();
    CHECK_EQ(1, builds);
    size.Set(200);
    t.WaitForIdle();
    CHECK_EQ(1, builds);
}

TEST(ClLayout_testConstraintLayout_rebuildsConstraintSet_whenHelpersChange) {
    test::ComposeUiTest t;
    rt::State<float> offset{ 10 };
    int builds = 0;
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            auto g = s.CreateGuidelineFromStart(Dp(offset.Get()));
            Box(Probe(s.ConstrainAs({}, box,
                                    [&builds, g](ConstrainScope& c) {
                                        c.start.LinkTo(g);
                                        ++builds;
                                    }),
                      0));
        });
    });
    t.WaitForIdle();
    CHECK_EQ(Left(t, "p0"), 10.0f);
    offset.Set(20);
    CHECK_EQ(1, builds);
    t.WaitForIdle();
    CHECK_EQ(Left(t, "p0"), 20.0f);
    CHECK_EQ(2, builds);
}

TEST(ClLayout_testConstraintLayout_doesNotRecomposeAgain_whenHelpersChange) {
    test::ComposeUiTest t;
    rt::State<float> offset{ 10 };
    int compositions = 0;
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            ++compositions;
            auto box = s.CreateRef();
            auto g = s.CreateGuidelineFromStart(Dp(offset.Get()));
            Box(s.ConstrainAs({}, box, [g](ConstrainScope& c) { c.start.LinkTo(g); }));
        });
    });
    t.WaitForIdle();
    offset.Set(20);
    CHECK_EQ(1, compositions);
    t.WaitForIdle();
    CHECK_EQ(2, compositions);
}

TEST(ClLayout_testConstraintLayout_rebuilds_whenLambdaChanges) {
    test::ComposeUiTest t;
    rt::State<bool> first{ true };
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto& l1 = Remember<ConstraintLayoutScope::ConstrainBlock>([] {
                return std::make_shared<const kiln::Function<void(ConstrainScope&)>>([](ConstrainScope& c) { c.start.LinkTo(c.parent.start, Dp(10)); });
            });
            auto& l2 = Remember<ConstraintLayoutScope::ConstrainBlock>([] {
                return std::make_shared<const kiln::Function<void(ConstrainScope&)>>([](ConstrainScope& c) { c.start.LinkTo(c.parent.start, Dp(20)); });
            });
            auto box = s.CreateRef();
            Box(Probe(s.ConstrainAs({}, box, first.Get() ? l1 : l2), 0));
        });
    });
    t.WaitForIdle();
    CHECK_EQ(Left(t, "p0"), 10.0f);
    first.Set(false);
    t.WaitForIdle();
    CHECK_EQ(Left(t, "p0"), 20.0f);
}

TEST(ClLayout_testConstraintLayout_updates_whenConstraintSetChangesConstraints) {
    test::ComposeUiTest t;
    const int box1Size = 20;
    rt::State<bool> first{ true };
    Content(t, [&] {
        const bool f = first.Get();
        auto constraintSet = MakeConstraintSet([f](ConstraintSetScope& s) {
            auto box1 = s.CreateRefFor("box1");
            auto box2 = s.CreateRefFor("box2");
            s.Constrain(box2, [&](ConstrainScope& c) {
                if (f) c.start.LinkTo(box1.end);
                else c.top.LinkTo(box1.bottom);
            });
        });
        ConstraintLayout(constraintSet, Modifier{}, [] {
            Box(SizeMod(foundation::LayoutId({}, "box1"), Dp(box1Size), Dp(box1Size)));
            Box(Probe(foundation::LayoutId({}, "box2"), 2));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(box1Size, 0));
    first.Set(false);
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(0, box1Size));
}

TEST(ClLayout_testConstraintLayout_doesNotUpdate_withRememberConstraintSet) {
    test::ComposeUiTest t;
    const int box1Size = 20;
    rt::State<bool> first{ true };
    int compCount = 0;
    Content(t, [&] {
        auto& constraintSet = Remember<std::shared_ptr<ConstraintSet>>([&] {
            return MakeConstraintSet([&](ConstraintSetScope& s) {
                auto box1 = s.CreateRefFor("box1");
                auto box2 = s.CreateRefFor("box2");
                s.Constrain(box2, [&](ConstrainScope& c) {
                    if (first.Get()) c.start.LinkTo(box1.end);
                    else c.top.LinkTo(box1.bottom);
                });
            });
        });
        compCount++;
        ConstraintLayout(constraintSet, Modifier{}, [] {
            Box(SizeMod(foundation::LayoutId({}, "box1"), Dp(box1Size), Dp(box1Size)));
            Box(Probe(foundation::LayoutId({}, "box2"), 2));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(box1Size, 0));
    CHECK_EQ(1, compCount);
    first.Set(false);
    t.WaitForIdle();
    CHECK(Position(t, "p2") == ui::IntOffset(box1Size, 0));
    CHECK_EQ(2, compCount);
}

TEST(ClLayout_testClearDerivedConstraints_withConstraintSet) {
    test::ComposeUiTest t;
    rt::State<bool> startOrEnd{ true };
    const std::string boxTag = "box1";
    Content(t, [&] {
        auto& start = Remember<std::shared_ptr<ConstraintSet>>([&] {
            return MakeConstraintSet([&](ConstraintSetScope& s) {
                s.Constrain(s.CreateRefFor(boxTag), [](ConstrainScope& c) {
                    c.width = Dimension::Value(Dp(20));
                    c.height = Dimension::Value(Dp(20));
                    c.start.LinkTo(c.parent.start, Dp(10));
                    c.bottom.LinkTo(c.parent.bottom, Dp(10));
                });
            });
        });
        auto& end = Remember<std::shared_ptr<ConstraintSet>>([&] {
            return MakeConstraintSet(
                [&](ConstraintSetScope& s) {
                    s.Constrain(s.CreateRefFor(boxTag), [](ConstrainScope& c) {
                        c.ClearConstraints();
                        c.top.LinkTo(c.parent.top, Dp(5));
                        c.end.LinkTo(c.parent.end, Dp(5));
                    });
                },
                start);
        });
        ConstraintLayout(startOrEnd.Get() ? start : end, SizeMod({}, Dp(200), Dp(200)), [&] { Box(LayoutTestId({}, boxTag)); });
    });
    t.WaitForIdle();
    CHECK(PositionIs(t, boxTag, 10, 170));
    startOrEnd.Set(!startOrEnd.Get());
    t.WaitForIdle();
    CHECK(PositionIs(t, boxTag, 175, 5));
}

namespace {
    // testLayoutReference_*: a box left of an end guideline, and one after a barrier on the
    // guideline itself (its reference) and the first box.
    struct ReferenceLayout {
        static constexpr int rootSizePx = 500, boxSizePx = 100, g1DistancePx = 50, marginToHelper = 10;
    };
}

TEST(ClLayout_testLayoutReference_withConstraintSet) {
    using R = ReferenceLayout;
    test::ComposeUiTest t;
    auto constraintSet = MakeConstraintSet([](ConstraintSetScope& s) {
        auto box1 = s.CreateRefFor("box1");
        auto g1 = s.CreateGuidelineFromEnd(Dp(R::g1DistancePx));
        auto g1Ref = g1.reference();
        auto b1 = s.CreateEndBarrier({ &g1Ref, &box1 });
        s.Constrain(box1, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(R::boxSizePx));
            c.height = Dimension::Value(Dp(R::boxSizePx));
            c.top.LinkTo(c.parent.top);
            c.end.LinkTo(g1, Dp(R::marginToHelper));
        });
        s.Constrain(s.CreateRefFor("box2"), [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(R::boxSizePx));
            c.height = Dimension::Value(Dp(R::boxSizePx));
            c.top.LinkTo(c.parent.top);
            c.start.LinkTo(b1, Dp(R::marginToHelper));
        });
    });
    Content(t, [&] {
        ConstraintLayout(constraintSet, SizeMod({}, Dp(R::rootSizePx), Dp(R::rootSizePx)), [] {
            Box(LayoutTestId({}, "box1"));
            Box(LayoutTestId({}, "box2"));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "box1") == ui::IntOffset(R::rootSizePx - R::g1DistancePx - R::marginToHelper - R::boxSizePx, 0));
    CHECK(Position(t, "box2") == ui::IntOffset(R::rootSizePx - R::g1DistancePx + R::marginToHelper, 0));
}

TEST(ClLayout_testLayoutReference_withInlineDsl) {
    using R = ReferenceLayout;
    test::ComposeUiTest t;
    Content(t, [&] {
        ConstraintLayout(SizeMod({}, Dp(R::rootSizePx), Dp(R::rootSizePx)), [](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto g1 = s.CreateGuidelineFromEnd(Dp(R::g1DistancePx));
            auto g1Ref = g1.reference();
            auto b1 = s.CreateEndBarrier({ &g1Ref, &refs[0] });
            Box(LayoutTestId(s.ConstrainAs({}, refs[0],
                                           [g1](ConstrainScope& c) {
                                               c.width = Dimension::Value(Dp(R::boxSizePx));
                                               c.height = Dimension::Value(Dp(R::boxSizePx));
                                               c.top.LinkTo(c.parent.top);
                                               c.end.LinkTo(g1, Dp(R::marginToHelper));
                                           }),
                             "box1"));
            Box(LayoutTestId(s.ConstrainAs({}, refs[1],
                                           [b1](ConstrainScope& c) {
                                               c.width = Dimension::Value(Dp(R::boxSizePx));
                                               c.height = Dimension::Value(Dp(R::boxSizePx));
                                               c.top.LinkTo(c.parent.top);
                                               c.start.LinkTo(b1, Dp(R::marginToHelper));
                                           }),
                             "box2"));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "box1") == ui::IntOffset(R::rootSizePx - R::g1DistancePx - R::marginToHelper - R::boxSizePx, 0));
    CHECK(Position(t, "box2") == ui::IntOffset(R::rootSizePx - R::g1DistancePx + R::marginToHelper, 0));
}

TEST(ClLayout_testBias_withConstraintSet) {
    test::ComposeUiTest t;
    const float rootSize = 100, boxSize = 10, horBias = 0.2f, verBias = 1 - horBias;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             s.Constrain(s.CreateRefFor("box"), [&](ConstrainScope& c) {
                                 c.width = Dimension::Value(Dp(boxSize));
                                 c.height = Dimension::Value(Dp(boxSize));
                                 c.CenterTo(c.parent);
                                 c.horizontalBias = horBias;
                                 c.verticalBias = verBias;
                             });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [] { Box(LayoutTestId({}, "box")); });
    });
    t.WaitForIdle();
    CHECK(PositionIs(t, "box", (rootSize - boxSize) * 0.2f, (rootSize - boxSize) * 0.8f));
}

TEST(ClLayout_testBias_withInlineDsl) {
    test::ComposeUiTest t;
    const float rootSize = 100, boxSize = 10, horBias = 0.2f, verBias = 1 - horBias;
    Content(t, [&] {
        ConstraintLayout(SizeMod({}, Dp(rootSize), Dp(rootSize)), [&](ConstraintLayoutScope& s) {
            auto box = s.CreateRef();
            Box(LayoutTestId(s.ConstrainAs({}, box,
                                           [=](ConstrainScope& c) {
                                               c.width = Dimension::Value(Dp(boxSize));
                                               c.height = Dimension::Value(Dp(boxSize));
                                               c.CenterTo(c.parent);
                                               c.horizontalBias = horBias;
                                               c.verticalBias = verBias;
                                           }),
                             "box"));
        });
    });
    t.WaitForIdle();
    CHECK(PositionIs(t, "box", (rootSize - boxSize) * 0.2f, (rootSize - boxSize) * 0.8f));
}

TEST(ClLayout_testConstraintSet_multipleRefs) {
    test::ComposeUiTest t;
    const int rootSize = 50, boxSize = 20, margin = 10;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             // Not enough ids: box2 gets a generated one.
                             auto refs = s.CreateRefsFor({ "box0", "box1" }, 3);
                             auto &box0 = refs[0], &box1 = refs[1], &box2 = refs[2];
                             s.Constrain({ &box0, &box1 }, [&](ConstrainScope& c) {
                                 c.width = Dimension::Value(Dp(boxSize));
                                 c.height = Dimension::Value(Dp(boxSize));
                                 c.top.LinkTo(c.parent.top, Dp(margin));
                                 c.start.LinkTo(c.parent.start, Dp(margin));
                             });
                             s.Constrain(box2, [&](ConstrainScope& c) {
                                 c.width = Dimension::Value(Dp(boxSize));
                                 c.height = Dimension::Value(Dp(boxSize));
                                 c.top.LinkTo(box0.bottom);
                                 c.start.LinkTo(box0.end);
                             });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
                             Box(Probe(foundation::LayoutId({}, "box0"), 0));
                             Box(Probe(foundation::LayoutId({}, "box1"), 1));
                             Box(Probe(foundation::LayoutId({}, "androidx.constraintlayout.id0"), 2));
                         });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") == ui::IntOffset(margin, margin));
    CHECK(Position(t, "p1") == ui::IntOffset(margin, margin));
    CHECK(Position(t, "p2") == ui::IntOffset(margin + boxSize, margin + boxSize));
}

TEST(ClLayout_testLinkToBias_withInlineDsl_rtl) {
    test::ComposeUiTest t;
    const int rootSize = 200, boxSize = 20;
    const float box1Bias = 0.2f, box2Bias = 0.2f;
    Content(t, [&] {
        WithDirection(true, [&] {
            ConstraintLayout(SizeMod({}, Dp(rootSize), Dp(rootSize)), [&](ConstraintLayoutScope& s) {
                auto refs = s.CreateRefs(2);
                auto box1Ref = refs[0];
                Box(Probe(s.ConstrainAs({}, refs[0],
                                        [=](ConstrainScope& c) {
                                            c.width = Dimension::Value(Dp(boxSize));
                                            c.height = Dimension::Value(Dp(boxSize));
                                            c.CenterTo(c.parent);
                                            c.horizontalBias = box1Bias;   // unaffected by RTL
                                        }),
                          0));
                Box(Probe(s.ConstrainAs({}, refs[1],
                                        [=](ConstrainScope& c) {
                                            c.width = Dimension::Value(Dp(boxSize));
                                            c.height = Dimension::Value(Dp(boxSize));
                                            c.top.LinkTo(box1Ref.bottom);
                                            c.LinkTo(c.parent.start, box1Ref.start, Dp(0), Dp(0), Dp(0), Dp(0), box2Bias);   // affected by RTL
                                        }),
                          1));
            });
        });
    });
    t.WaitForIdle();
    const float expectedBox1X = (rootSize - boxSize) * box1Bias;
    const float expectedBox1Y = (rootSize * 0.5f) - (boxSize * 0.5f);
    CHECK(Position(t, "p0") == ui::IntOffset(RoundToInt(expectedBox1X), RoundToInt(expectedBox1Y)));
    const float expectedBox1End = expectedBox1X + boxSize;
    const float expectedBox2X = (rootSize - expectedBox1End - boxSize) * (1.0f - box2Bias) + expectedBox1End;
    CHECK(Position(t, "p1") == ui::IntOffset(RoundToInt(expectedBox2X), RoundToInt(expectedBox1Y + boxSize)));
}

TEST(ClLayout_testContentRecomposition_withConstraintSet) {
    test::ComposeUiTest t;
    int constraintLayoutCompCount = 0;
    const int baseWidth = 10, boxHeight = 30;
    rt::State<int> box0WidthMultiplier{ 2 };
    Content(t, [&] {
        ++constraintLayoutCompCount;
        ConstraintLayout(MakeConstraintSet([](ConstraintSetScope& s) {
                             auto refs = s.CreateRefsFor({ "box0", "box1" });
                             auto &box0 = refs[0], &box1 = refs[1];
                             s.Constrain(box0, [](ConstrainScope& c) {
                                 c.width = Dimension::PreferredWrapContent();
                                 c.start.LinkTo(c.parent.start);
                                 c.end.LinkTo(c.parent.end);
                                 c.horizontalBias = 0.0f;
                                 c.top.LinkTo(c.parent.top);
                             });
                             s.Constrain(box1, [&](ConstrainScope& c) {
                                 c.width = Dimension::FillToConstraints();
                                 c.height = Dimension::WrapContent();
                                 c.start.LinkTo(box0.start);
                                 c.end.LinkTo(box0.end);
                                 c.horizontalBias = 0.0f;
                                 c.top.LinkTo(box0.bottom);
                             });
                         }),
                         Modifier{}, [&] {
                             Box(LayoutTestId(Width(foundation::Height({}, Dp(boxHeight)), Dp(float(baseWidth * box0WidthMultiplier.Get()))), "box0"));
                             Box(LayoutTestId(foundation::Height({}, Dp(boxHeight)), "box1"));
                         });
    });
    for (int multiplier : { 2, 3, 1 }) {
        box0WidthMultiplier.Set(multiplier);
        t.WaitForIdle();
        CHECK(PositionIs(t, "box0", 0, 0));
        CHECK(WidthIs(t, "box0", float(baseWidth * multiplier)));
        CHECK(PositionIs(t, "box1", 0, float(boxHeight)));
        CHECK(WidthIs(t, "box1", float(baseWidth * multiplier)));
    }
    CHECK_EQ(1, constraintLayoutCompCount);
}

TEST(ClLayout_testContentRecomposition_withInlineModifier) {
    test::ComposeUiTest t;
    int constraintLayoutCompCount = 0;
    const int baseWidth = 10, boxHeight = 30;
    rt::State<int> box0WidthMultiplier{ 2 };
    Content(t, [&] {
        ++constraintLayoutCompCount;
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto box0 = refs[0];
            Box(foundation::TestTag(s.ConstrainAs(Width(foundation::Height({}, Dp(boxHeight)), Dp(float(baseWidth * box0WidthMultiplier.Get()))),
                                                  refs[0],
                                                  [](ConstrainScope& c) {
                                                      c.width = Dimension::PreferredWrapContent();
                                                      c.start.LinkTo(c.parent.start);
                                                      c.end.LinkTo(c.parent.end);
                                                      c.horizontalBias = 0.0f;
                                                      c.top.LinkTo(c.parent.top);
                                                  }),
                                    "box0"));
            Box(foundation::TestTag(s.ConstrainAs(foundation::Height({}, Dp(boxHeight)), refs[1],
                                                  [box0](ConstrainScope& c) {
                                                      c.width = Dimension::FillToConstraints();
                                                      c.height = Dimension::WrapContent();
                                                      c.start.LinkTo(box0.start);
                                                      c.end.LinkTo(box0.end);
                                                      c.horizontalBias = 0.0f;
                                                      c.top.LinkTo(box0.bottom);
                                                  }),
                                    "box1"));
        });
    });
    for (int multiplier : { 2, 3, 1 }) {
        box0WidthMultiplier.Set(multiplier);
        t.WaitForIdle();
        CHECK(PositionIs(t, "box0", 0, 0));
        CHECK(WidthIs(t, "box0", float(baseWidth * multiplier)));
        CHECK(PositionIs(t, "box1", 0, float(boxHeight)));
        CHECK(WidthIs(t, "box1", float(baseWidth * multiplier)));
    }
    CHECK_EQ(1, constraintLayoutCompCount);
}

TEST(ClLayout_testBaselineConstraints) {
    test::ComposeUiTest t;
    auto withBaseline = [](Modifier m) {
        return foundation::LayoutMod(std::move(m), [](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& constraints) {
            ui::Placeable& placeable = measurable.Measure(constraints);
            const int halfHeight = RoundToInt(placeable.Height() / 2.0f);
            return s.Layout(placeable.Width(), placeable.Height(), [&placeable](ui::PlacementScope& place) { place.Place(placeable, 0, 0); },
                            { { &ui::FirstBaseline, halfHeight } });
        });
    };
    const int boxSize = 10, box1Margin = 13;
    const float box2Margin = -7.0f;
    Content(t, [&] {
        ConstraintLayout(Modifier{}, [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto box1 = refs[0];
            Box(Probe(s.ConstrainAs(withBaseline(SizeMod({}, Dp(boxSize), Dp(boxSize))), refs[0],
                                    [=](ConstrainScope& c) {
                                        c.baseline.LinkTo(c.parent.top, Dp(box1Margin));
                                        c.start.LinkTo(c.parent.start);
                                    }),
                      0));
            Box(Probe(s.ConstrainAs(withBaseline(SizeMod({}, Dp(boxSize), Dp(boxSize))), refs[1],
                                    [=](ConstrainScope& c) { c.top.LinkTo(box1.baseline, Dp(box2Margin)); }),
                      1));
        });
    });
    t.WaitForIdle();
    const int expectedBox1Y = box1Margin - RoundToInt(boxSize * 0.5f);
    const float expectedBox2Y = expectedBox1Y + box2Margin + int(boxSize * 0.5f);
    CHECK(Position(t, "p0") == ui::IntOffset(0, expectedBox1Y));
    CHECK(Position(t, "p1") == ui::IntOffset(0, RoundToInt(expectedBox2Y)));
}

TEST(ClLayout_testConstraintLayout_withParentIntrinsics) {
    test::ComposeUiTest t;
    const int rootBoxWidth = 200, box1Size = 40, box2Size = 70;
    ui::IntSize rootSize, clSize;
    Content(t, [&] {
        Box(OnSizeChanged(foundation::Height(Width({}, Dp(rootBoxWidth)), foundation::IntrinsicSize::Max), [&](ui::IntSize s) { rootSize = s; }),
            foundation::Alignment::TopStart(), false, [&] {
                ConstraintLayout(OnSizeChanged(foundation::WrapContent(FillMax({}, Axis::Horizontal), Axis::Vertical), [&](ui::IntSize s) { clSize = s; }),
                                 [&](ConstraintLayoutScope& s) {
                                     auto refs = s.CreateRefs(2);
                                     auto horChain = s.CreateHorizontalChain({ &refs[0], &refs[1] }, ChainStyle::Packed(0.0f));
                                     s.Constrain(horChain, [](HorizontalChainScope& c) {
                                         c.start.LinkTo(c.parent.start);
                                         c.end.LinkTo(c.parent.end);
                                     });
                                     Box(Probe(s.ConstrainAs(SizeMod({}, Dp(box1Size), Dp(box1Size)), refs[0],
                                                             [](ConstrainScope& c) {
                                                                 c.top.LinkTo(c.parent.top);
                                                                 c.bottom.LinkTo(c.parent.bottom);
                                                             }),
                                               1));
                                     Box(Probe(s.ConstrainAs(SizeMod({}, Dp(box2Size), Dp(box2Size)), refs[1],
                                                             [](ConstrainScope& c) {
                                                                 c.width = Dimension::PreferredWrapContent();
                                                                 c.top.LinkTo(c.parent.top);
                                                                 c.bottom.LinkTo(c.parent.bottom);
                                                             }),
                                               2));
                                 });
            });
    });
    t.WaitForIdle();
    const ui::IntSize expectedSize{ rootBoxWidth, box2Size };
    const int expectedBox1Y = RoundToInt((box2Size / 2.0f) - (box1Size / 2.0f));
    CHECK(rootSize == expectedSize);
    CHECK(clSize == expectedSize);
    CHECK(Position(t, "p1") == ui::IntOffset(0, expectedBox1Y));
    CHECK(Position(t, "p2") == ui::IntOffset(box1Size, 0));
}

TEST(ClLayout_testTranslationXY_withDsl) {
    test::ComposeUiTest t;
    const int rootSizePx = 100, boxSizePx = 10, translationXPx = 7, translationYPx = 9;
    Content(t, [&] {
        ConstraintLayout(SizeMod({}, Dp(rootSizePx), Dp(rootSizePx)), [&](ConstraintLayoutScope& s) {
            auto boxRef = s.CreateRef();
            Box(Probe(s.ConstrainAs({}, boxRef,
                                    [=](ConstrainScope& c) {
                                        c.width = Dimension::Value(Dp(boxSizePx));
                                        c.height = Dimension::Value(Dp(boxSizePx));
                                        c.CenterTo(c.parent);
                                        c.translationX = Dp(translationXPx);
                                        c.translationY = Dp(translationYPx);
                                    }),
                      0));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") ==
          ui::IntOffset(RoundToInt((rootSizePx - boxSizePx) / 2.0f + translationXPx), RoundToInt((rootSizePx - boxSizePx) / 2.0f + translationYPx)));
}

TEST(ClLayout_testTranslationXY_withJson) {
    test::ComposeUiTest t;
    const int rootSizePx = 100, boxSizePx = 10, translationXPx = 7, translationYPx = 9;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet("{\n  box: {\n    width: 10.0,\n    height: 10.0,\n    center: 'parent',\n    translationX: 7.0,\n"
                                           "    translationY: 9.0\n  }\n}"),
                         SizeMod({}, Dp(rootSizePx), Dp(rootSizePx)), [] { Box(Probe(foundation::LayoutId({}, "box"), 0)); });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p0") ==
          ui::IntOffset(RoundToInt((rootSizePx - boxSizePx) / 2.0f + translationXPx), RoundToInt((rootSizePx - boxSizePx) / 2.0f + translationYPx)));
}

TEST(ClLayout_testVisibility_withInlineDsl) {
    // Upstream also reads box0's pixels (red, then not red when invisible); positions only here.
    test::ComposeUiTest t;
    const int rootSizePx = 100, boxSizePx = 10;
    rt::State<std::string> boxVisibility{ "visible" };
    const ui::IntOffset expectedInitialBox1Position{ RoundToInt((rootSizePx + boxSizePx) / 2.0f), RoundToInt((rootSizePx - boxSizePx) / 2.0f) };
    Content(t, [&] {
        ConstraintLayout(SizeMod({}, Dp(rootSizePx), Dp(rootSizePx)), [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto box0 = refs[0];
            Box(foundation::TestTag(s.ConstrainAs({}, refs[0],
                                                  [&boxVisibility](ConstrainScope& c) {
                                                      c.CenterTo(c.parent);
                                                      const std::string v = boxVisibility.Get();
                                                      c.visibility = v == "visible" ? Visibility::Visible()
                                                                     : v == "invisible" ? Visibility::Invisible()
                                                                                        : Visibility::Gone();
                                                  }),
                                    "box0"),
                foundation::Alignment::TopStart(), false, [] { Box(SizeMod({}, Dp(10), Dp(10))); });
            Box(Probe(s.ConstrainAs({}, refs[1],
                                    [box0](ConstrainScope& c) {
                                        c.width = Dimension::Value(Dp(10));
                                        c.height = Dimension::Value(Dp(10));
                                        c.top.LinkTo(box0.top);
                                        c.start.LinkTo(box0.end);
                                    }),
                      1));
        });
    });
    t.WaitForIdle();
    CHECK(Position(t, "p1") == expectedInitialBox1Position);
    boxVisibility.Set("invisible");
    t.WaitForIdle();
    CHECK(Position(t, "p1") == expectedInitialBox1Position);
    boxVisibility.Set("gone");
    t.WaitForIdle();
    // Gone children are not placed (Compose reports an unspecified size; kiln leaves unplaced
    // nodes out of the semantics tree).
    t.OnNodeWithTag("box0").AssertDoesNotExist();
    CHECK(Position(t, "p1") == ui::IntOffset(RoundToInt(rootSizePx / 2.0f), RoundToInt(rootSizePx / 2.0f)));
}

TEST(ClLayout_testEmptyConstraintLayoutSize) {
    test::ComposeUiTest t;
    const float rootSizePx = 200;
    rt::State<int> mode{ 0 };   // the transformConstraints lambda upstream swaps in
    ui::IntSize layoutSize{ -1, -1 };
    bool sawInfiniteHeight = true;
    Content(t, [&] {
        auto& scroll = foundation::RememberScrollState();
        foundation::Column(foundation::VerticalScroll(SizeMod({}, Dp(rootSizePx), Dp(rootSizePx)), scroll), [&] {
            ConstraintLayout(OnSizeChanged(foundation::LayoutMod({},
                                                                 [&](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& constraints) {
                                                                     ui::Constraints c = constraints;
                                                                     const int m = mode.Get();
                                                                     if (m == 1) {
                                                                         sawInfiniteHeight = sawInfiniteHeight && constraints.maxHeight == ui::Constraints::Infinity;
                                                                         c.minWidth = 123;
                                                                         c.minHeight = 321;
                                                                     } else if (m == 2) {
                                                                         if (constraints.HasBoundedWidth()) c.minWidth = c.maxWidth = constraints.maxWidth;
                                                                         if (constraints.HasBoundedHeight()) c.minHeight = c.maxHeight = constraints.maxHeight;
                                                                     }
                                                                     ui::Placeable& placeable = measurable.Measure(c);
                                                                     return s.Layout(placeable.Width(), placeable.Height(),
                                                                                     [&placeable](ui::PlacementScope& place) { place.Place(placeable, 0, 0); });
                                                                 }),
                                           [&](ui::IntSize sz) { layoutSize = sz; }),
                             [](ConstraintLayoutScope&) {});
        });
    });
    t.WaitForIdle();
    CHECK(layoutSize == ui::IntSize(0, 0));
    mode.Set(1);
    t.WaitForIdle();
    CHECK(sawInfiniteHeight);
    CHECK(layoutSize == ui::IntSize(123, 321));
    mode.Set(2);
    t.WaitForIdle();
    CHECK(layoutSize == ui::IntSize(RoundToInt(rootSizePx), 0));
}

namespace {
    void ToggleVisibilityContent(rt::State<bool>& toggleVisibility, float rootSizePx) {
        ConstraintLayout(SizeMod({}, Dp(rootSizePx), Dp(rootSizePx)), [&toggleVisibility, rootSizePx](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(2);
            auto &titleRef = refs[0], &detailRef = refs[1];
            Box(foundation::TestTag(s.ConstrainAs({}, detailRef,
                                                  [&toggleVisibility, rootSizePx](ConstrainScope& c) {
                                                      c.CenterHorizontallyTo(c.parent);
                                                      c.width = Dimension::FillToConstraints();
                                                      c.height = Dimension::Value(Dp(rootSizePx));
                                                      c.visibility = !toggleVisibility.Get() ? Visibility::Gone() : Visibility::Visible();
                                                  }),
                                    "box1"));
            Box(foundation::TestTag(s.ConstrainAs({}, titleRef,
                                                  [&toggleVisibility, rootSizePx](ConstrainScope& c) {
                                                      c.CenterHorizontallyTo(c.parent);
                                                      c.width = Dimension::FillToConstraints();
                                                      c.height = Dimension::Value(Dp(rootSizePx));
                                                      c.visibility = toggleVisibility.Get() ? Visibility::Gone() : Visibility::Visible();
                                                  }),
                                    "box2"));
        });
    }

    void CheckToggle(test::ComposeUiTest& t, rt::State<bool>& toggleVisibility, float rootSizePx) {
        // A gone child is not placed: Compose reports an unspecified size, kiln no node.
        t.OnNodeWithTag("box1").AssertDoesNotExist();
        t.OnNodeWithTag("box2").AssertWidthIsEqualTo(rootSizePx).AssertHeightIsEqualTo(rootSizePx);
        toggleVisibility.Set(!toggleVisibility.Get());
        t.WaitForIdle();
        t.OnNodeWithTag("box1").AssertWidthIsEqualTo(rootSizePx).AssertHeightIsEqualTo(rootSizePx);
        t.OnNodeWithTag("box2").AssertDoesNotExist();
    }
}

TEST(ClLayout_testToggleVisibilityWithFillConstraintsWidth) {
    test::ComposeUiTest t;
    rt::State<bool> toggleVisibility{ false };
    Content(t, [&] { ToggleVisibilityContent(toggleVisibility, 100); });
    t.WaitForIdle();
    CheckToggle(t, toggleVisibility, 100);
    CHECK(true);
}

TEST(ClLayout_testToggleVisibilityWithFillConstraintsWidth_underLookahead) {
    test::ComposeUiTest t;
    rt::State<bool> toggleVisibility{ false };
    Content(t, [&] { foundation::LookaheadScope([&](ui::LookaheadScope&) { ToggleVisibilityContent(toggleVisibility, 100); }); });
    t.WaitForIdle();
    CheckToggle(t, toggleVisibility, 100);
    CHECK(true);
}

TEST(ClLayout_testAnimateChanges_withInlineDsl) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int durationMs = 200, rootSizePx = 100, boxSizePx = 20;
    const ui::IntOffset expectedEndPosition{ rootSizePx - boxSizePx, rootSizePx - boxSizePx };
    ui::IntOffset box0Position{};
    rt::State<bool> atTopLeftCorner(true);
    Content(t, [&] {
        ConstraintLayout(
            SizeMod({}, px(float(rootSizePx)), px(float(rootSizePx))),
            [&](ConstraintLayoutScope& s) {
                auto boxRef = s.CreateRef();
                Box(foundation::OnGloballyPositioned(foundation::Background(s.ConstrainAs({}, boxRef,
                                                                                          [&](ConstrainScope& c) {
                                                                                              c.width = Dimension::Value(px(float(boxSizePx)));
                                                                                              c.height = Dimension::Value(px(float(boxSizePx)));
                                                                                              if (atTopLeftCorner.Get()) {
                                                                                                  c.top.LinkTo(c.parent.top);
                                                                                                  c.start.LinkTo(c.parent.start);
                                                                                              } else {
                                                                                                  c.bottom.LinkTo(c.parent.bottom);
                                                                                                  c.end.LinkTo(c.parent.end);
                                                                                              }
                                                                                          }),
                                                                            ui::Color::Red()),
                                                     [&](const ui::LayoutCoordinates& lc) {
                                                         const ui::Offset p = lc.PositionInParent();
                                                         box0Position = { RoundToInt(p.x), RoundToInt(p.y) };
                                                     }));
            },
            AnimateChanges{ anim::Tween(durationMs) });
    });
    t.WaitForIdle();
    CHECK((ui::IntOffset{ 0, 0 }) == box0Position);
    t.MainClock().autoAdvance = false;
    atTopLeftCorner.Set(false);
    t.MainClock().AdvanceTimeBy(durationMs / 2);
    t.WaitForIdle();
    CHECK(box0Position.x > 0 && box0Position.y > 0);
    CHECK(box0Position.x < expectedEndPosition.x && box0Position.y < expectedEndPosition.y);
    t.MainClock().autoAdvance = true;
    t.WaitForIdle();
    CHECK(expectedEndPosition == box0Position);
}
