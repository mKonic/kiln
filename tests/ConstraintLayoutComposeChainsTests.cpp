// Ported from constraintlayout-compose ChainsTest.kt (androidx). Expected values are upstream's.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

using namespace cltest;
using foundation::SizeMod;

TEST(ClChains_testHorizontalPacked_withConstraintSet) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const float rootSizePx = 100;
    const int boxSizesPx[] = { 10, 20, 30 };
    const int marginPx = 10;
    auto constraintSet = MakeConstraintSet([&](ConstraintSetScope& s) {
        auto box0 = s.CreateRefFor("box0");
        auto box1 = s.CreateRefFor("box1");
        auto box2 = s.CreateRefFor("box2");
        auto chain0 = s.CreateHorizontalChain({ &box0, &box1 }, ChainStyle::Packed());
        s.Constrain(box0, [&](ConstrainScope& c) {
            c.width = Dimension::Value(px(boxSizesPx[0]));
            c.height = Dimension::Value(px(boxSizesPx[0]));
            c.CenterVerticallyTo(c.parent);
        });
        s.Constrain(box1, [&](ConstrainScope& c) {
            c.width = Dimension::Value(px(boxSizesPx[1]));
            c.height = Dimension::Value(px(boxSizesPx[1]));
            c.CenterVerticallyTo(box0);
        });
        s.Constrain(box2, [&](ConstrainScope& c) {
            c.width = Dimension::Value(px(boxSizesPx[2]));
            c.height = Dimension::Value(px(boxSizesPx[2]));
            c.top.LinkTo(c.parent.top, px(marginPx));
            c.start.LinkTo(c.parent.start, px(marginPx));
        });
        s.Constrain(chain0, [&](HorizontalChainScope& c) { c.start.LinkTo(box2.end, px(marginPx)); });
    });
    Content(t, [&] {
        ConstraintLayout(constraintSet, SizeMod({}, px(rootSizePx), px(rootSizePx)), [] {
            Box(LayoutTestId({}, "box0"));
            Box(LayoutTestId({}, "box1"));
            Box(LayoutTestId({}, "box2"));
        });
    });
    t.WaitForIdle();
    const float spaceForChain = rootSizePx - boxSizesPx[2] - (marginPx * 2);
    const float spaceAroundChain = spaceForChain - boxSizesPx[0] - boxSizesPx[1];
    const float spaceAtLeftOfChain = spaceAroundChain * 0.5f;
    const float offsetFromBox2 = marginPx + boxSizesPx[2] + marginPx;
    const float box0Left = offsetFromBox2 + spaceAtLeftOfChain;
    const float box0Top = (rootSizePx - boxSizesPx[0]) * 0.5f;
    const float box1Left = box0Left + boxSizesPx[0];
    const float box1Top = box0Top - ((boxSizesPx[1] - boxSizesPx[0]) * 0.5f);
    CHECK(PositionIs(t, "box0", px.dp(box0Left), px.dp(box0Top)));
    CHECK(PositionIs(t, "box1", px.dp(box1Left), px.dp(box1Top)));
    CHECK(PositionIs(t, "box2", px.dp(marginPx), px.dp(marginPx)));
}

TEST(ClChains_testHorizontalPacked_withModifier) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const float rootSizePx = 100;
    const int boxSizesPx[] = { 10, 20, 30 };
    const float marginPx = 10;
    Content(t, [&] {
        ConstraintLayout(SizeMod({}, px(rootSizePx), px(rootSizePx)), [&](ConstraintLayoutScope& s) {
            auto refs = s.CreateRefs(3);
            auto &box0 = refs[0], &box1 = refs[1], &box2 = refs[2];
            auto chain0 = s.CreateHorizontalChain({ &box0, &box1 }, ChainStyle::Packed());
            s.Constrain(chain0, [&](HorizontalChainScope& c) { c.start.LinkTo(box2.end, px(marginPx)); });
            Box(LayoutTestId(s.ConstrainAs({}, box0,
                                           [&](ConstrainScope& c) {
                                               c.width = Dimension::Value(px(boxSizesPx[0]));
                                               c.height = Dimension::Value(px(boxSizesPx[0]));
                                               c.CenterVerticallyTo(c.parent);
                                           }),
                             "box0"));
            Box(LayoutTestId(s.ConstrainAs({}, box1,
                                           [&, box0](ConstrainScope& c) {
                                               c.width = Dimension::Value(px(boxSizesPx[1]));
                                               c.height = Dimension::Value(px(boxSizesPx[1]));
                                               c.CenterVerticallyTo(box0);
                                           }),
                             "box1"));
            Box(LayoutTestId(s.ConstrainAs({}, box2,
                                           [&](ConstrainScope& c) {
                                               c.width = Dimension::Value(px(boxSizesPx[2]));
                                               c.height = Dimension::Value(px(boxSizesPx[2]));
                                               c.top.LinkTo(c.parent.top, px(marginPx));
                                               c.start.LinkTo(c.parent.start, px(marginPx));
                                           }),
                             "box2"));
        });
    });
    t.WaitForIdle();
    const float spaceForChain = rootSizePx - boxSizesPx[2] - (marginPx * 2);
    const float spaceAroundChain = spaceForChain - boxSizesPx[0] - boxSizesPx[1];
    const float spaceAtLeftOfChain = spaceAroundChain * 0.5f;
    const float offsetFromBox2 = marginPx + boxSizesPx[2] + marginPx;
    const float box0Left = offsetFromBox2 + spaceAtLeftOfChain;
    const float box0Top = (rootSizePx - boxSizesPx[0]) * 0.5f;
    const float box1Left = box0Left + boxSizesPx[0];
    const float box1Top = box0Top - ((boxSizesPx[1] - boxSizesPx[0]) * 0.5f);
    CHECK(PositionIs(t, "box0", px.dp(box0Left), px.dp(box0Top)));
    CHECK(PositionIs(t, "box1", px.dp(box1Left), px.dp(box1Top)));
    CHECK(PositionIs(t, "box2", px.dp(marginPx), px.dp(marginPx)));
}

TEST(ClChains_testHorizontalPacked_withMargins) {
    test::ComposeUiTest t;
    const float rootSize = 100;
    const float boxSizes[] = { 10, 20, 30 };
    const float boxMargin = 5;
    const float boxGoneMargin = 7;
    auto constraintSet = MakeConstraintSet([&](ConstraintSetScope& s) {
        auto box0 = s.CreateRefFor("box0");
        auto boxGone = s.CreateRefFor("boxGone");
        auto box1 = s.CreateRefFor("box1");
        auto box2 = s.CreateRefFor("box2");
        ChainParams p0;
        p0.startMargin = Dp(0);
        p0.endMargin = Dp(boxMargin);   // not applied since the next box is gone
        p0.endGoneMargin = Dp(boxGoneMargin);
        ChainParams pGone;   // none of these apply since it's gone
        pGone.startMargin = pGone.endMargin = pGone.startGoneMargin = pGone.endGoneMargin = Dp(100);
        s.CreateHorizontalChain({ &s.WithChainParams(box0, p0), &s.WithChainParams(boxGone, pGone), &box1,
                                  &s.WithHorizontalChainParams(box2, Dp(boxMargin), Dp(0)) },
                                ChainStyle::Packed());
        s.Constrain(box0, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[0]));
            c.height = Dimension::Value(Dp(boxSizes[0]));
            c.CenterVerticallyTo(c.parent);
        });
        s.Constrain(boxGone, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[1]));
            c.height = Dimension::Value(Dp(boxSizes[1]));
            c.CenterVerticallyTo(box0);
            c.visibility = Visibility::Gone();
        });
        s.Constrain(box1, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[1]));
            c.height = Dimension::Value(Dp(boxSizes[1]));
            c.CenterVerticallyTo(box0);
        });
        s.Constrain(box2, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[2]));
            c.height = Dimension::Value(Dp(boxSizes[2]));
            c.CenterVerticallyTo(box0);
        });
    });
    Content(t, [&] {
        ConstraintLayout(constraintSet, SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
            Box(LayoutTestId({}, "box0"));
            Box(LayoutTestId({}, "box1"));
            Box(LayoutTestId({}, "box2"));
        });
    });
    t.WaitForIdle();
    const float totalMargins = boxMargin + boxGoneMargin;
    const float totalChainSpace = boxSizes[0] + boxSizes[1] + boxSizes[2] + totalMargins;
    const float box0Left = (rootSize - totalChainSpace) * 0.5f;
    const float box0Top = (rootSize - boxSizes[0]) * 0.5f;
    const float box1Left = box0Left + boxSizes[0] + boxGoneMargin;
    const float box1Top = (rootSize - boxSizes[1]) * 0.5f;
    const float box2Left = box1Left + boxSizes[1] + boxMargin;
    const float box2Top = (rootSize - boxSizes[2]) * 0.5f;
    CHECK(PositionIs(t, "box0", box0Left, box0Top, 1));
    CHECK(PositionIs(t, "box1", box1Left, box1Top, 1));
    CHECK(PositionIs(t, "box2", box2Left, box2Top, 1));
}

TEST(ClChains_testVerticalPacked_withMargins) {
    test::ComposeUiTest t;
    const float rootSize = 100;
    const float boxSizes[] = { 10, 20, 30 };
    const float boxMargin = 5;
    const float boxGoneMargin = 7;
    auto constraintSet = MakeConstraintSet([&](ConstraintSetScope& s) {
        auto box0 = s.CreateRefFor("box0");
        auto boxGone = s.CreateRefFor("boxGone");
        auto box1 = s.CreateRefFor("box1");
        auto box2 = s.CreateRefFor("box2");
        ChainParams p0;
        p0.topMargin = Dp(0);
        p0.bottomMargin = Dp(boxMargin);
        p0.bottomGoneMargin = Dp(boxGoneMargin);
        ChainParams pGone;
        pGone.topMargin = pGone.bottomMargin = pGone.topGoneMargin = pGone.bottomGoneMargin = Dp(100);
        s.CreateVerticalChain({ &s.WithChainParams(box0, p0), &s.WithChainParams(boxGone, pGone), &box1,
                                &s.WithVerticalChainParams(box2, Dp(boxMargin), Dp(0)) },
                              ChainStyle::Packed());
        s.Constrain(box0, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[0]));
            c.height = Dimension::Value(Dp(boxSizes[0]));
            c.CenterHorizontallyTo(c.parent);
        });
        s.Constrain(boxGone, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(100));
            c.height = Dimension::Value(Dp(100));
            c.CenterHorizontallyTo(box0);
            c.visibility = Visibility::Gone();
        });
        s.Constrain(box1, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[1]));
            c.height = Dimension::Value(Dp(boxSizes[1]));
            c.CenterHorizontallyTo(box0);
        });
        s.Constrain(box2, [&](ConstrainScope& c) {
            c.width = Dimension::Value(Dp(boxSizes[2]));
            c.height = Dimension::Value(Dp(boxSizes[2]));
            c.CenterHorizontallyTo(box0);
        });
    });
    Content(t, [&] {
        ConstraintLayout(constraintSet, SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
            Box(LayoutTestId({}, "box0"));
            Box(LayoutTestId({}, "box1"));
            Box(LayoutTestId({}, "box2"));
        });
    });
    t.WaitForIdle();
    const float totalMargins = boxMargin + boxGoneMargin;
    const float totalChainSpace = boxSizes[0] + boxSizes[1] + boxSizes[2] + totalMargins;
    const float box0Left = (rootSize - boxSizes[0]) * 0.5f;
    const float box0Top = (rootSize - totalChainSpace) * 0.5f;
    const float box1Left = (rootSize - boxSizes[1]) * 0.5f;
    const float box1Top = box0Top + boxSizes[0] + boxGoneMargin;
    const float box2Left = (rootSize - boxSizes[2]) * 0.5f;
    const float box2Top = box1Top + boxSizes[1] + boxMargin;
    CHECK(PositionIs(t, "box0", box0Left, box0Top, 1));
    CHECK(PositionIs(t, "box1", box1Left, box1Top, 1));
    CHECK(PositionIs(t, "box2", box2Left, box2Top, 1));
}

TEST(ClChains_testHorizontalWeight_withConstraintSet) {
    test::ComposeUiTest t;
    const float rootSize = 100;
    const float boxSize = 10;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             auto box0 = s.CreateRefFor("box0");
                             auto box1 = s.CreateRefFor("box1");
                             s.Constrain(box0, [&](ConstrainScope& c) {
                                 c.width = Dimension::FillToConstraints();
                                 c.height = Dimension::Value(Dp(boxSize));
                                 c.horizontalChainWeight = 1.0f;
                                 c.verticalChainWeight = 2.0f;   // ignored in a horizontal chain
                             });
                             s.Constrain(box1, [&](ConstrainScope& c) {
                                 c.width = Dimension::FillToConstraints();
                                 c.height = Dimension::Value(Dp(boxSize));
                             });
                             ChainParams p;
                             p.weight = 0.5f;
                             s.CreateHorizontalChain({ &box0, &s.WithChainParams(box1, p) });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
                             Box(LayoutTestId({}, "box0"));
                             Box(LayoutTestId({}, "box1"));
                         });
    });
    t.WaitForIdle();
    CHECK(WidthIs(t, "box0", rootSize * 0.667f));
    CHECK(PositionIs(t, "box1", rootSize * 0.667f, 0));
    CHECK(WidthIs(t, "box1", rootSize * 0.334f));
}

TEST(ClChains_testVerticalWeight_withConstraintSet) {
    test::ComposeUiTest t;
    const float rootSize = 100;
    const float boxSize = 10;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             auto box0 = s.CreateRefFor("box0");
                             auto box1 = s.CreateRefFor("box1");
                             s.Constrain(box0, [&](ConstrainScope& c) {
                                 c.width = Dimension::Value(Dp(boxSize));
                                 c.height = Dimension::FillToConstraints();
                                 c.horizontalChainWeight = 2.0f;   // ignored in a vertical chain
                                 c.verticalChainWeight = 1.0f;
                             });
                             s.Constrain(box1, [&](ConstrainScope& c) {
                                 c.width = Dimension::Value(Dp(boxSize));
                                 c.height = Dimension::FillToConstraints();
                             });
                             ChainParams p;
                             p.weight = 0.5f;
                             s.CreateVerticalChain({ &box0, &s.WithChainParams(box1, p) });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
                             Box(LayoutTestId({}, "box0"));
                             Box(LayoutTestId({}, "box1"));
                         });
    });
    t.WaitForIdle();
    CHECK(HeightIs(t, "box0", rootSize * 0.667f));
    CHECK(PositionIs(t, "box1", 0, rootSize * 0.667f));
    CHECK(HeightIs(t, "box1", rootSize * 0.334f));
}
