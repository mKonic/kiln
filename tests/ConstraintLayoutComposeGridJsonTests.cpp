// Ported from constraintlayout-compose GridTest.kt (androidx): the grid tests with a JSON5
// ConstraintSet. Expected values are upstream's (the same as GridDslTest's). The JSON keeps
// upstream's quirks ("columns" without a colon, a missing comma), which the parser tolerates.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

using namespace cltest;
using foundation::SizeMod;

namespace {
    struct JsonGrid {
        int rows, columns;
        std::string spans, skips, rowWeights, columnWeights;
        int boxesCount, orientation;
        std::string flags;
    };

    void RunJson(test::ComposeUiTest& t, const JsonGrid& g) {
        std::string contains;
        for (int i = 0; i < g.boxesCount; i++) contains += std::string(i ? ", " : "") + "'box" + std::to_string(i) + "'";
        const std::string json = "{\n    grid: {\n        width: 'parent',\n        height: 'parent',\n        type: 'grid',\n        rows: " +
                                 std::to_string(g.rows) + ",\n        columns " + std::to_string(g.columns) +
                                 ",\n        vGap: 0,\n        hGap: 0,\n        spans: " + g.spans + ",\n        skips: " + g.skips +
                                 ",\n        rowWeights: " + g.rowWeights + ",\n        columnWeights: " + g.columnWeights +
                                 "\n        orientation: " + std::to_string(g.orientation) + ",\n        contains: [" + contains +
                                 "],\n        flags: " + g.flags + ",\n      }\n}";
        const int n = g.boxesCount;
        Content(t, [json, n] {
            ConstraintLayout(MakeConstraintSet(json), SizeMod({}, Dp(200), Dp(200)), [n] {
                for (int i = 0; i < n; i++) {
                    const std::string id = "box" + std::to_string(i);
                    Box(foundation::TestTag(SizeMod(foundation::LayoutId({}, id), Dp(10), Dp(10)), id));
                }
            });
        });
        t.WaitForIdle();
    }
}

TEST(ClGridJson_testTwoByTwo) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "''", "''", "''", "''", 4, 0, "''" });
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

TEST(ClGridJson_testOrientation) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "''", "''", "''", "''", 4, 1, "''" });
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

TEST(ClGridJson_testRows) {
    test::ComposeUiTest t;
    RunJson(t, { 0, 1, "''", "''", "''", "''", 4, 0, "''" });
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

TEST(ClGridJson_testColumns) {
    test::ComposeUiTest t;
    RunJson(t, { 1, 0, "''", "''", "''", "''", 4, 0, "''" });
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

TEST(ClGridJson_testSkips) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "''", "'0:1x1'", "''", "''", 3, 0, "''" });
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box0", rightX, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridJson_testReversedDirectionSkips) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "''", "'0:2x1'", "''", "''", 2, 0, "'SubGridByColRow'" });
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box0", leftX, bottomY));
    CHECK(PositionIs(t, "box1", rightX, bottomY));
}

TEST(ClGridJson_testSpans) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "'0:1x2'", "''", "''", "''", 3, 0, "''" });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridJson_testMultipleSpans) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "'2:1x2,0:1x2'", "''", "''", "''", 2, 0, "'SpansRespectWidgetOrder'" });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", spanLeft, bottomY));
}

TEST(ClGridJson_testOrderFirstSpans) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "'1:2x1'", "''", "''", "''", 3, 0, "'SpansRespectWidgetOrder'" });
    const float spanTop = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", leftX, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", rightX, spanTop));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", leftX, bottomY));
}

TEST(ClGridJson_testReversedDirectionSpans) {
    test::ComposeUiTest t;
    RunJson(t, { 2, 2, "'0:2x1'", "''", "''", "''", 3, 0, "'SubGridByColRow'" });
    const float spanLeft = (200 - 10.0f) / 2;
    const float gapSize = (200 - (10.0f * 2)) / (2 * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box0", spanLeft, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box1", leftX, bottomY));
    CHECK(PositionIs(t, "box2", rightX, bottomY));
}

TEST(ClGridJson_testRowWeights) {
    test::ComposeUiTest t;
    RunJson(t, { 0, 1, "''", "''", "'1,3'", "''", 2, 0, "''" });
    const float rootSize = 200;
    const float expectedLeft = (rootSize - 10) / 2;
    const float firstGapSize = (rootSize / 4 - 10) / 2;
    const float secondGapSize = ((rootSize * 3 / 4) - 10) / 2;
    float expectedTop = firstGapSize;
    CHECK(PositionIs(t, "box0", expectedLeft, expectedTop));
    expectedTop += 10 + firstGapSize + secondGapSize;
    CHECK(PositionIs(t, "box1", expectedLeft, expectedTop));
}

TEST(ClGridJson_testColumnWeights) {
    test::ComposeUiTest t;
    RunJson(t, { 1, 0, "''", "''", "''", "'1,3'", 2, 0, "''" });
    const float rootSize = 200;
    const float expectedTop = (rootSize - 10) / 2;
    const float firstGapSize = (rootSize / 4 - 10) / 2;
    const float secondGapSize = ((rootSize * 3 / 4) - 10) / 2;
    float expectedLeft = firstGapSize;
    CHECK(PositionIs(t, "box0", expectedLeft, expectedTop));
    expectedLeft += 10 + firstGapSize + secondGapSize;
    CHECK(PositionIs(t, "box1", expectedLeft, expectedTop));
}

TEST(ClGridJson_testGaps) {
    test::ComposeUiTest t;
    const float rootSize = 200, hGap = 10, vGap = 20;
    const std::string json =
        "{\n    grid: {\n        width: 'parent',\n        height: 'parent',\n        type: 'grid',\n        rows: 2,\n        columns: 2,\n"
        "        vGap: 20,\n        hGap: 10,\n        contains: ['box0', 'box1', 'box2', 'box3'],\n      },\n"
        "      box0: {\n        width: 'spread',\n        height: 'spread',\n      },\n"
        "      box1: {\n        width: 'spread',\n        height: 'spread',\n      },\n"
        "      box2: {\n        width: 'spread',\n        height: 'spread',\n      },\n"
        "      box3: {\n        width: 'spread',\n        height: 'spread',\n      }\n}";
    Content(t, [json] {
        ConstraintLayout(MakeConstraintSet(json), SizeMod({}, Dp(200), Dp(200)), [] {
            for (int i = 0; i < 4; i++) Box(LayoutTestId({}, "box" + std::to_string(i)));
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
