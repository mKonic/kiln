// Ported from constraintlayout-compose RowColumnDslTest.kt (androidx). Expected values are
// upstream's.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

#include <set>
#include <stdexcept>

using namespace cltest;
using foundation::SizeMod;

namespace {
    // Row/ColumnComposableTest: skipped indices are "placeholder" references (fake boxes).
    void LineComposableTest(bool column, float rootSize, std::vector<float> weights, std::set<int> skipIndices, int boxesCount) {
        ConstraintLayout(MakeConstraintSet([&](ConstraintSetScope& s) {
                             const int totalBoxes = boxesCount + int(skipIndices.size());
                             int skipCount = 0;
                             std::vector<ConstrainedLayoutReference> ids;
                             for (int index = 0; index < totalBoxes; index++) {
                                 if (skipIndices.count(index)) {
                                     skipCount++;
                                     ids.push_back(s.CreateRefFor("placeholder"));
                                 } else {
                                     ids.push_back(s.CreateRefFor("box" + std::to_string(index - skipCount)));
                                 }
                             }
                             std::vector<const LayoutReference*> elements;
                             for (auto& r : ids) elements.push_back(&r);
                             auto g1 = column ? s.CreateColumn(elements, Dp(0), weights) : s.CreateRow(elements, Dp(0), weights);
                             s.Constrain(g1, [](ConstrainScope& c) {
                                 c.width = Dimension::MatchParent();
                                 c.height = Dimension::MatchParent();
                             });
                         }),
                         SizeMod({}, Dp(rootSize), Dp(rootSize)), [boxesCount] {
                             for (int i = 0; i < boxesCount; i++) Box(SizeMod(LayoutTestId({}, "box" + std::to_string(i)), Dp(10), Dp(10)));
                         });
    }

    void Run(test::ComposeUiTest& t, bool column, std::vector<float> weights, std::set<int> skips, int boxesCount, float rootSize = 200) {
        Content(t, [=] { LineComposableTest(column, rootSize, weights, skips, boxesCount); });
        t.WaitForIdle();
    }
}

TEST(ClRowColumnDsl_testColumn) {
    test::ComposeUiTest t;
    Run(t, true, {}, {}, 4);
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

TEST(ClRowColumnDsl_testColumnSkips) {
    test::ComposeUiTest t;
    Run(t, true, {}, { 1, 2 }, 4);   // Skip(position = 1, size = 2)
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - 10) / 2;
    const float vGapSize = (rootSize - (10.0f * 6)) / ((boxesCount + 2) * 2.0f);
    float expectedX = hGapSize, expectedY = vGapSize;
    CHECK(PositionIs(t, "box0", expectedX, expectedY));
    expectedY += 3 * (vGapSize + vGapSize + 10);
    CHECK(PositionIs(t, "box1", expectedX, expectedY));
    expectedY += vGapSize + vGapSize + 10;
    CHECK(PositionIs(t, "box2", expectedX, expectedY));
    expectedY += vGapSize + vGapSize + 10;
    CHECK(PositionIs(t, "box3", expectedX, expectedY));
}

TEST(ClRowColumnDsl_testColumnSpans) {
    test::ComposeUiTest t;
    Run(t, true, { 2, 1, 1, 1 }, {}, 4);
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - 10) / 2;
    const float vGapSize = (rootSize - (10.0f * 5)) / ((boxesCount + 1) * 2.0f);
    const float rowSize = 10 + vGapSize * 2;
    float expectedX = hGapSize, expectedY = rowSize - 5;
    CHECK(PositionIs(t, "box0", expectedX, expectedY));
    expectedY += expectedY + vGapSize + 10;
    CHECK(PositionIs(t, "box1", expectedX, expectedY));
    expectedY += vGapSize + vGapSize + 10;
    CHECK(PositionIs(t, "box2", expectedX, expectedY));
    expectedY += vGapSize + vGapSize + 10;
    CHECK(PositionIs(t, "box3", expectedX, expectedY));
}

TEST(ClRowColumnDsl_testRow) {
    test::ComposeUiTest t;
    Run(t, false, {}, {}, 4);
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

TEST(ClRowColumnDsl_testRowSkips) {
    test::ComposeUiTest t;
    Run(t, false, {}, { 1, 2 }, 4);
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - (10.0f * 6)) / ((boxesCount + 2) * 2.0f);
    const float vGapSize = (rootSize - 10) / 2;
    float expectedX = hGapSize, expectedY = vGapSize;
    CHECK(PositionIs(t, "box0", expectedX, expectedY));
    expectedX += 3 * (hGapSize + hGapSize + 10);
    CHECK(PositionIs(t, "box1", expectedX, expectedY));
    expectedX += hGapSize + hGapSize + 10;
    CHECK(PositionIs(t, "box2", expectedX, expectedY));
    expectedX += hGapSize + hGapSize + 10;
    CHECK(PositionIs(t, "box3", expectedX, expectedY));
}

TEST(ClRowColumnDsl_testRowSpans) {
    test::ComposeUiTest t;
    Run(t, false, { 2, 1, 1, 1 }, {}, 4);
    const float rootSize = 200;
    const int boxesCount = 4;
    const float hGapSize = (rootSize - (10.0f * 5)) / ((boxesCount + 1) * 2.0f);
    const float vGapSize = (rootSize - 10) / 2;
    const float columnSize = 10 + hGapSize * 2;
    float expectedX = columnSize - 5, expectedY = vGapSize;
    CHECK(PositionIs(t, "box0", expectedX, expectedY));
    expectedX += expectedX + hGapSize + 10;
    CHECK(PositionIs(t, "box1", expectedX, expectedY));
    expectedX += hGapSize + hGapSize + 10;
    CHECK(PositionIs(t, "box2", expectedX, expectedY));
    expectedX += hGapSize + hGapSize + 10;
    CHECK(PositionIs(t, "box3", expectedX, expectedY));
}

TEST(ClRowColumnDsl_testInconsistentWeightsOnColumnTrows) {
    test::ComposeUiTest t;
    std::string message;
    try {
        Run(t, true, { 0, 1 }, {}, 3, 100);
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    CHECK_EQ(message, std::string("Number of weights (2) should match number of elements (3)."));
}

TEST(ClRowColumnDsl_testInconsistentWeightsOnRowTrows) {
    test::ComposeUiTest t;
    std::string message;
    try {
        Run(t, false, { 0, 1, 2, 3 }, {}, 3, 100);
    } catch (const std::invalid_argument& e) {
        message = e.what();
    }
    CHECK_EQ(message, std::string("Number of weights (4) should match number of elements (3)."));
}
