// Ported from constraintlayout-compose FlowTest.kt (androidx). Expected values are upstream's.
// Positions read through onGloballyPositioned upstream are read from the semantics bounds here
// (the layout sits at the root's origin, so parent and root positions agree).
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"

using namespace cltest;
using foundation::SizeMod;

namespace {
    void FlowComposableTest(Modifier modifier, const std::string& width, const std::string& height, int boxesCount, bool isHorizontal) {
        std::vector<std::string> ids;
        for (int i = 0; i < boxesCount; i++) ids.push_back("box" + std::to_string(i));
        std::string flowContains;
        for (std::size_t i = 0; i < ids.size(); i++) flowContains += (i ? ", '" : "'") + ids[i] + "'";
        const std::string json = "{\n    flow1: {\n        width: " + width + ",\n        height: " + height + ",\n        type: '" +
                                 (isHorizontal ? "hFlow" : "vFlow") +
                                 "',\n        hStyle: 'spread_inside',\n        vStyle: 'spread_inside',\n        contains: [" + flowContains +
                                 "],\n      }\n}";
        ConstraintLayout(MakeConstraintSet(json), modifier, [ids] {
            for (const std::string& id : ids) Box(foundation::TestTag(SizeMod(foundation::LayoutId({}, id), Dp(10), Dp(10)), id));
        });
    }

    void CheckRow(test::ComposeUiTest& t, float rootOrSize, int boxesCount, float cross, bool horizontal) {
        float expected = 0;
        const float gapSize = ((rootOrSize - (10.0f * boxesCount)) / (boxesCount - 1)) + 10.0f;
        for (int i = 0; i < boxesCount; i++) {
            const std::string tag = "box" + std::to_string(i);
            CHECK(horizontal ? PositionIs(t, tag, expected, cross) : PositionIs(t, tag, cross, expected));
            expected += gapSize;
        }
    }
}

TEST(ClFlowCompose_testMatchParent_horizontal) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "'parent'", "'parent'", 4, true); });
    t.WaitForIdle();
    CheckRow(t, 200, 4, 94.9f, true);
}

TEST(ClFlowCompose_testMatchParent_vertical) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "'parent'", "'parent'", 4, false); });
    t.WaitForIdle();
    CheckRow(t, 200, 4, 94.9f, false);
}

TEST(ClFlowCompose_testFixedSizeMatchesRoot_horizontal) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "200", "200", 4, true); });
    t.WaitForIdle();
    CheckRow(t, 200, 4, 94.9f, true);
}

TEST(ClFlowCompose_testFixedSizeMatchesRoot_vertical) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "200", "200", 4, false); });
    t.WaitForIdle();
    CheckRow(t, 200, 4, 94.9f, false);
}

TEST(ClFlowCompose_testFixedSizeSmallerThanRoot_horizontal) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "100", "100", 4, true); });
    t.WaitForIdle();
    CheckRow(t, 100, 4, 45.1f, true);
}

TEST(ClFlowCompose_testFixedSizeBiggerThanRoot_horizontal) {
    test::ComposeUiTest t;
    Content(t, [] { FlowComposableTest(SizeMod({}, Dp(200), Dp(200)), "400", "400", 4, true); });
    t.WaitForIdle();
    CheckRow(t, 400, 4, 194.9f, true);
}

namespace {
    // testChainModeRespectsMaxElements_*: positions in px, keyed by item.
    std::vector<ui::IntOffset> ChainModePositions(bool vertical) {
        test::ComposeUiTest t;
        const Px px{ t.Density() };
        const int baseBoxSizePx = 40;
        const int itemCount = 6;
        const int maxElements = 3;
        const int indexOfBigItem = 4;
        const int padding = 10;
        const int flowSize = baseBoxSizePx * maxElements + padding;
        auto constraintSet = MakeConstraintSet([&](ConstraintSetScope& s) {
            std::vector<ConstrainedLayoutReference> itemRefs;
            for (int i = 0; i < itemCount; i++) itemRefs.push_back(s.CreateRefFor("item" + std::to_string(i)));
            FlowParams params;
            params.flowVertically = vertical;
            params.maxElement = maxElements;
            params.wrapMode = Wrap::Chain();
            if (vertical) params.verticalStyle = FlowStyle::SpreadInside();
            else params.horizontalStyle = FlowStyle::SpreadInside();
            std::vector<const LayoutReference*> elements;
            for (auto& r : itemRefs) elements.push_back(&r);
            auto flow = s.CreateFlow(elements, params);
            s.Constrain(flow, [&](ConstrainScope& c) {
                if (vertical) c.height = Dimension::Value(px(flowSize));
                else c.width = Dimension::Value(px(flowSize));
                c.top.LinkTo(c.parent.top);
                c.start.LinkTo(c.parent.start);
            });
            for (int index = 0; index < itemCount; index++) {
                s.Constrain(itemRefs[std::size_t(index)], [&](ConstrainScope& c) {
                    const int sizePx = index == indexOfBigItem ? baseBoxSizePx * 2 : baseBoxSizePx;
                    if (vertical) {
                        c.height = Dimension::Value(px(sizePx));
                        c.width = Dimension::Value(px(baseBoxSizePx));
                    } else {
                        c.width = Dimension::Value(px(sizePx));
                        c.height = Dimension::Value(px(baseBoxSizePx));
                    }
                });
            }
        });
        Content(t, [&] {
            ConstraintLayout(constraintSet, Modifier{}, [&] {
                for (int i = 0; i < itemCount; i++) {
                    const std::string id = "item" + std::to_string(i);
                    Box(foundation::TestTag(foundation::LayoutId({}, id), id));
                }
            });
        });
        t.WaitForIdle();
        std::vector<ui::IntOffset> positions;
        for (int i = 0; i < itemCount; i++) {
            const ui::Rect b = t.OnNodeWithTag("item" + std::to_string(i)).GetUnclippedBoundsInRoot();
            positions.push_back({ int(std::lround(b.left * px.density)), int(std::lround(b.top * px.density)) });
        }
        return positions;
    }
}

TEST(ClFlowCompose_testChainModeRespectsMaxElements_horizontal) {
    const auto positions = ChainModePositions(false);
    const int baseBoxSizePx = 40, horizontalPadding = 10;
    CHECK_EQ(int(positions.size()), 6);
    int expectedX = 0, expectedY = baseBoxSizePx * 0;
    CHECK(positions[0] == ui::IntOffset(expectedX, expectedY));
    expectedX += baseBoxSizePx + horizontalPadding / 2;
    CHECK(positions[1] == ui::IntOffset(expectedX, expectedY));
    expectedX += baseBoxSizePx + horizontalPadding / 2;
    CHECK(positions[2] == ui::IntOffset(expectedX, expectedY));
    expectedX = 0;
    expectedY = baseBoxSizePx * 1;
    CHECK(positions[3] == ui::IntOffset(expectedX, expectedY));
    expectedX = baseBoxSizePx + horizontalPadding;
    CHECK(positions[4] == ui::IntOffset(expectedX, expectedY));
    expectedX = 0;
    expectedY = baseBoxSizePx * 2;
    CHECK(positions[5] == ui::IntOffset(expectedX, expectedY));
}

TEST(ClFlowCompose_testChainModeRespectsMaxElements_vertical) {
    const auto positions = ChainModePositions(true);
    const int baseBoxSizePx = 40, verticalPadding = 10;
    CHECK_EQ(int(positions.size()), 6);
    int expectedX = baseBoxSizePx * 0, expectedY = 0;
    CHECK(positions[0] == ui::IntOffset(expectedX, expectedY));
    expectedY += baseBoxSizePx + verticalPadding / 2;
    CHECK(positions[1] == ui::IntOffset(expectedX, expectedY));
    expectedY += baseBoxSizePx + verticalPadding / 2;
    CHECK(positions[2] == ui::IntOffset(expectedX, expectedY));
    expectedX = baseBoxSizePx * 1;
    expectedY = 0;
    CHECK(positions[3] == ui::IntOffset(expectedX, expectedY));
    expectedY = baseBoxSizePx + verticalPadding;
    CHECK(positions[4] == ui::IntOffset(expectedX, expectedY));
    expectedX = baseBoxSizePx * 2;
    expectedY = 0;
    CHECK(positions[5] == ui::IntOffset(expectedX, expectedY));
}
