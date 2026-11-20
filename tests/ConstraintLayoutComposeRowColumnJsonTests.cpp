// Ported from constraintlayout-compose RowColumnTest.kt (androidx): rows and columns from a
// JSON5 ConstraintSet. Expected values are upstream's (the same as RowColumnDslTest's).
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

using namespace cltest;
using foundation::SizeMod;

namespace {
    void RunJson(test::ComposeUiTest& t, const std::string& type, const std::string& spans, const std::string& skips) {
        const int boxesCount = 4;
        std::string contains;
        for (int i = 0; i < boxesCount; i++) contains += std::string(i ? ", " : "") + "'box" + std::to_string(i) + "'";
        const std::string json = "{\n    grid: {\n        width: 'parent',\n        height: 'parent',\n        type: '" + type +
                                 "',\n        vGap: 0,\n        hGap: 0,\n        spans: " + spans + ",\n        skips: " + skips +
                                 ",\n        rowWeights: '',\n        columnWeights: ''\n        orientation: 0,\n        contains: [" + contains +
                                 "],\n      }\n}";
        Content(t, [json] {
            ConstraintLayout(MakeConstraintSet(json), SizeMod({}, Dp(200), Dp(200)), [] {
                for (int i = 0; i < 4; i++) {
                    const std::string id = "box" + std::to_string(i);
                    Box(foundation::TestTag(SizeMod(foundation::LayoutId({}, id), Dp(10), Dp(10)), id));
                }
            });
        });
        t.WaitForIdle();
    }
}

TEST(ClRowColumnJson_testColumn) {
    test::ComposeUiTest t;
    RunJson(t, "column", "''", "''");
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

TEST(ClRowColumnJson_testColumnSkips) {
    test::ComposeUiTest t;
    RunJson(t, "column", "''", "'1:2'");
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

TEST(ClRowColumnJson_testColumnSpans) {
    test::ComposeUiTest t;
    RunJson(t, "column", "'0:2'", "''");
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

TEST(ClRowColumnJson_testRow) {
    test::ComposeUiTest t;
    RunJson(t, "row", "''", "''");
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

TEST(ClRowColumnJson_testRowSkips) {
    test::ComposeUiTest t;
    RunJson(t, "row", "''", "'1:2'");
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

TEST(ClRowColumnJson_testRowSpans) {
    test::ComposeUiTest t;
    RunJson(t, "row", "'0:2'", "''");
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
