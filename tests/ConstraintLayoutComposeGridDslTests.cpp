// Ported from constraintlayout-compose GridDslTest.kt (androidx). Expected values are upstream's.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

#include <stdexcept>

using namespace cltest;
using foundation::SizeMod;

namespace {
    struct GridSpec {
        int numRows = 0, numColumns = 0;
        std::vector<Span> spans;
        std::vector<Skip> skips;
        std::vector<float> rowWeights, columnWeights;
        int boxesCount = 0;
        bool isHorizontalArrangement = true;
        GridFlag flags = GridFlag::None();
        float rootSize = 200;
    };

    void GridComposableTest(const GridSpec& spec) {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             std::vector<ConstrainedLayoutReference> elem;
                             for (int i = 0; i < spec.boxesCount; i++) elem.push_back(s.CreateRefFor("box" + std::to_string(i)));
                             std::vector<const LayoutReference*> elements;
                             for (auto& e : elem) elements.push_back(&e);
                             GridParams p;
                             p.isHorizontalArrangement = spec.isHorizontalArrangement;
                             p.skips = spec.skips;
                             p.spans = spec.spans;
                             p.rowWeights = spec.rowWeights;
                             p.columnWeights = spec.columnWeights;
                             p.flags = spec.flags;
                             auto g1 = s.CreateGrid(elements, spec.numRows, spec.numColumns, p);
                             s.Constrain(g1, [](ConstrainScope& c) {
                                 c.width = Dimension::MatchParent();
                                 c.height = Dimension::MatchParent();
                             });
                         }),
                         SizeMod({}, Dp(spec.rootSize), Dp(spec.rootSize)), [n = spec.boxesCount] {
                             for (int i = 0; i < n; i++) Box(SizeMod(LayoutTestId({}, "box" + std::to_string(i)), Dp(10), Dp(10)));
                         });
    }

    void Run(test::ComposeUiTest& t, const GridSpec& spec) {
        Content(t, [spec] { GridComposableTest(spec); });
        t.WaitForIdle();
    }
}

TEST(ClGridDsl_testTwoByTwo) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, {}, {}, {}, {}, 4 });
    const float rootSize = 200, columns = 2;
    const float gapSize = (rootSize - (10.0f * 2)) / (columns * 2);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", leftX, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", rightX, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", leftX, bottomY));
    CHECK(PositionIs(t, "box3", rightX, bottomY));
}

TEST(ClGridDsl_testOrientation) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, {}, {}, {}, {}, 4, false });
    const float rootSize = 200, columns = 2;
    const float gapSize = (rootSize - (10.0f * 2)) / (columns * 2);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", leftX, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", rightX, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box3", rightX, bottomY));
}

TEST(ClGridDsl_testRows) {
    test::ComposeUiTest t;
    Run(t, { 0, 1, {}, {}, {}, {}, 4 });
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - 10) / 2;
    const float vGapSize = (rootSize - (10.0f * 4)) / (boxesCount * 2.0f);
    float expectedX = hGapSize, expectedY = vGapSize;
    for (int i = 0; i < 4; i++) {
        CHECK(PositionIs(t, "box" + std::to_string(i), expectedX, expectedY));
        expectedY += vGapSize + vGapSize + 10;
    }
}

TEST(ClGridDsl_testColumns) {
    test::ComposeUiTest t;
    Run(t, { 1, 0, {}, {}, {}, {}, 4 });
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - (10.0f * 4)) / (boxesCount * 2.0f);
    const float vGapSize = (rootSize - 10) / 2;
    float expectedX = hGapSize, expectedY = vGapSize;
    for (int i = 0; i < 4; i++) {
        CHECK(PositionIs(t, "box" + std::to_string(i), expectedX, expectedY));
        expectedX += hGapSize + hGapSize + 10;
    }
}

TEST(ClGridDsl_testSkips) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, {}, { Skip(0, 1, 1) }, {}, {}, 3 });
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box0", rightX, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridDsl_testReversedDirectionSkips) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, {}, { Skip(0, 2, 1) }, {}, {}, 2, true, GridFlag::SubGridByColRow() });
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box0", leftX, bottomY));
    CHECK(PositionIs(t, "box1", rightX, bottomY));
}

TEST(ClGridDsl_testSpans) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, { Span(0, 1, 2) }, {}, {}, {}, 3 });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridDsl_testSpans_placeOnSpansFirstFlag) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, { Span(2, 1, 2) }, {}, {}, {}, 3, true, GridFlag::PlaceLayoutsOnSpansFirst() });
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    float leftX = gapSize, topY = gapSize;
    // box0 goes on the span first (cells 2 and 3), so cells 0 and 1 hold box1 and box2.
    CHECK(PositionIs(t, "box1", leftX, topY));
    leftX += 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", leftX, topY));
    const float spanLeft = (200 - 10.0f) / 2;
    topY += 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
}

TEST(ClGridDsl_testMultipleSpans) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, { Span(2, 1, 2), Span(0, 1, 2) }, {}, {}, {}, 2 });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", spanLeft, bottomY));
}

TEST(ClGridDsl_testOrderFirstSpans) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, { Span(1, 2, 1) }, {}, {}, {}, 3 });
    const float spanTop = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", leftX, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", rightX, spanTop));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", leftX, bottomY));
}

TEST(ClGridDsl_testReversedDirectionSpans) {
    test::ComposeUiTest t;
    Run(t, { 2, 2, { Span(0, 2, 1) }, {}, {}, {}, 3, true, GridFlag::SubGridByColRow() });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridDsl_testRowWeights) {
    test::ComposeUiTest t;
    Run(t, { 0, 1, {}, {}, { 1, 3 }, {}, 2 });
    const float rootSize = 200;
    const float expectedLeft = (rootSize - 10) / 2;
    const float firstGapSize = (rootSize / 4 - 10) / 2;
    const float secondGapSize = ((rootSize * 3 / 4) - 10) / 2;
    float expectedTop = firstGapSize;
    CHECK(PositionIs(t, "box0", expectedLeft, expectedTop));
    expectedTop += 10 + firstGapSize + secondGapSize;
    CHECK(PositionIs(t, "box1", expectedLeft, expectedTop));
}

TEST(ClGridDsl_testColumnWeights) {
    test::ComposeUiTest t;
    Run(t, { 1, 0, {}, {}, {}, { 1, 3 }, 2 });
    const float rootSize = 200;
    const float expectedTop = (rootSize - 10) / 2;
    const float firstGapSize = (rootSize / 4 - 10) / 2;
    const float secondGapSize = ((rootSize * 3 / 4) - 10) / 2;
    float expectedLeft = firstGapSize;
    CHECK(PositionIs(t, "box0", expectedLeft, expectedTop));
    expectedLeft += 10 + firstGapSize + secondGapSize;
    CHECK(PositionIs(t, "box1", expectedLeft, expectedTop));
}

TEST(ClGridDsl_testIconsistendRowWeightsThrows) {
    test::ComposeUiTest t;
    std::string message;
    try {
        Run(t, { 3, 1, {}, {}, { 0, 1 }, {}, 1, true, GridFlag::None(), 100 });
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    CHECK_EQ(message, std::string("Number of weights (2) should match number of rows (3)."));
}

TEST(ClGridDsl_testInconsistentColumnWeightsThrows) {
    test::ComposeUiTest t;
    std::string message;
    try {
        Run(t, { 1, 3, {}, {}, {}, { 0, 1, 2, 3 }, 1, true, GridFlag::None(), 100 });
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    CHECK_EQ(message, std::string("Number of weights (4) should match number of columns (3)."));
}

TEST(ClGridDsl_testGaps) {
    test::ComposeUiTest t;
    const float rootSize = 200, hGap = 10, vGap = 20;
    Content(t, [&] {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             auto a = s.CreateRefFor("box0");
                             auto b = s.CreateRefFor("box1");
                             auto c = s.CreateRefFor("box2");
                             auto d = s.CreateRefFor("box3");
                             GridParams p;
                             p.verticalSpacing = Dp(vGap);
                             p.horizontalSpacing = Dp(hGap);
                             auto g1 = s.CreateGrid({ &a, &b, &c, &d }, 2, 2, p);
                             s.Constrain(g1, [](ConstrainScope& cs) {
                                 cs.width = Dimension::MatchParent();
                                 cs.height = Dimension::MatchParent();
                             });
                             s.Constrain({ &a, &b, &c, &d }, [](ConstrainScope& cs) {
                                 cs.width = Dimension::FillToConstraints();
                                 cs.height = Dimension::FillToConstraints();
                             });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [] {
                             for (int i = 0; i < 4; i++) Box(SizeMod(LayoutTestId({}, "box" + std::to_string(i)), Dp(10), Dp(10)));
                         });
    });
    t.WaitForIdle();
    const float boxWidth = (rootSize - hGap) / 2;
    const float boxHeight = (rootSize - vGap) / 2;
    CHECK(PositionIs(t, "box0", 0, 0));
    const float expectedLeft = boxWidth + hGap;
    CHECK(PositionIs(t, "box1", expectedLeft, 0));
    const float expectedTop = boxHeight + vGap;
    CHECK(PositionIs(t, "box2", 0, expectedTop));
    CHECK(PositionIs(t, "box3", expectedLeft, expectedTop));
}
