// Ported from constraintlayout-compose MotionFlowTest.kt, MotionGridTest.kt, MotionParserTest.kt
// and OnSwipeTest.kt (androidx). Expected values are upstream's. animateFloatAsState's target is
// what the asserts read, so the progress jumps to it. OnSwipeTest is @Ignore'd upstream for its
// mid-swipe position, which depends on the device; only the end position is checked.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/constraintlayout/MotionLayout.h"
#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/state/Transition.h"
#include "kiln/constraintlayout/core/state/TransitionParser.h"
#include "kiln/foundation/Layouts.h"

#include <string>

using namespace cltest;
using foundation::Background;
using foundation::SizeMod;

namespace {
    void Boxes(std::initializer_list<const char*> ids) {
        for (const char* id : ids) Box(Background(SizeMod(LayoutTestId({}, id), Dp(10), Dp(10)), Color::Red()));
    }
}

TEST(ClMotionFlow_horizontalToVerticalFlow) {
    test::ComposeUiTest t;
    const float rootSize = 200;
    rt::State<bool> animateToEnd(false);
    Content(t, [&] {
        const float progress = animateToEnd.Get() ? 1.0f : 0.0f;
        MotionLayout(RememberMotionScene(R"(
            {
                ConstraintSets: {
                  start: {
                    flow1: {
                        width: 'parent',
                        height: 'parent',
                        type: 'hFlow',
                        wrap: 'none',
                        centerVertically: 'parent',
                        centerHorizontally: 'parent',
                        contains: ['1', '2', '3', '4'],
                      }
                  },
                  end: {
                    flow2: {
                        width: 'parent',
                        height: 'parent',
                        type: 'vFlow',
                        wrap: 'none',
                        centerVertically: 'parent',
                        centerHorizontally: 'parent',
                        contains: ['1', '2', '3', '4'],
                      }
                  }
                },
                Transitions: {
                  default: {
                    from: 'start',   to: 'end',
                  }
                }
            }
            )"),
                     progress, SizeMod({}, Dp(rootSize), Dp(rootSize)), [](MotionLayoutScope&) { Boxes({ "1", "2", "3", "4" }); });
    });
    t.WaitForIdle();
    const float gapSize = (rootSize - (10.0f * 4)) / 5;
    float expectedPosition = gapSize;
    for (const char* id : { "1", "2", "3", "4" }) {
        CHECK(PositionIs(t, id, expectedPosition, 94.9f));
        expectedPosition += gapSize + 10;
    }
    animateToEnd.Set(true);
    t.WaitForIdle();
    expectedPosition = gapSize;
    for (const char* id : { "1", "2", "3", "4" }) {
        CHECK(PositionIs(t, id, 94.9f, expectedPosition));
        expectedPosition += gapSize + 10;
    }
}

TEST(ClMotionGrid_rowToColumnGrid) {
    test::ComposeUiTest t;
    const float rootSize = 200;
    rt::State<bool> animateToEnd(false);
    Content(t, [&] {
        const float progress = animateToEnd.Get() ? 1.0f : 0.0f;
        MotionLayout(RememberMotionScene(R"(
            {
                ConstraintSets: {
                  start: {
                    grid1: {
                        width: 200,
                        height: 200,
                        type: "grid",
                        orientation: 0,
                        rows: 0,
                        columns: 1,
                        hGap: 0,
                        vGap: 0,
                        spans: "",
                        skips: "",
                        rowWeights: "",
                        columnWeights: "",
                        contains: ["box1", "box2", "box3", "box4"],
                      }
                  },
                  end: {
                    grid2: {
                        width: 200,
                        height: 200,
                        type: "grid",
                        orientation: 0,
                        rows: 1,
                        columns: 0,
                        hGap: 0,
                        vGap: 0,
                        spans: "",
                        skips: "",
                        rowWeights: "",
                        columnWeights: "",
                        contains: ["box1", "box2", "box3", "box4"],
                      }
                  }
                },
                Transitions: {
                  default: {
                    from: 'start', to: 'end',
                  }
                }
            }
            )"),
                     progress, SizeMod({}, Dp(rootSize), Dp(rootSize)), [](MotionLayoutScope&) { Boxes({ "box1", "box2", "box3", "box4" }); });
    });
    t.WaitForIdle();
    const int boxesCount = 4;
    float hGapSize = (rootSize - 10) / 2.0f;
    float vGapSize = (rootSize - (10.0f * 4)) / (boxesCount * 2.0f);
    float expectedX = hGapSize, expectedY = vGapSize;
    for (const char* id : { "box1", "box2", "box3", "box4" }) {
        CHECK(PositionIs(t, id, expectedX, expectedY));
        expectedY += vGapSize + vGapSize + 10;
    }
    animateToEnd.Set(true);
    t.WaitForIdle();
    hGapSize = (rootSize - (10.0f * 4)) / (boxesCount * 2.0f);
    vGapSize = (rootSize - 10) / 2.0f;
    expectedX = hGapSize;
    expectedY = vGapSize;
    for (const char* id : { "box1", "box2", "box3", "box4" }) {
        CHECK(PositionIs(t, id, expectedX, expectedY));
        expectedX += hGapSize + hGapSize + 10;
    }
}

TEST(ClMotionGrid_wrapToSpreadGrid) {
    test::ComposeUiTest t;
    const float rootSize = 200;
    rt::State<bool> animateToEnd(false);
    const float hGap = 10, vGap = 20;
    const std::string scene = R"(
            {
                ConstraintSets: {
                  start: {
                    grid1: {
                        width: 200,
                        height: 200,
                        type: "grid",
                        orientation: 0,
                        rows: 2,
                        columns: 2,
                        hGap: 0,
                        vGap: 0,
                        contains: ["box1", "box2", "box3", "box4"],
                      }
                  },
                  end: {
                    grid2: {
                        width: 200,
                        height: 200,
                        type: "grid",
                        orientation: 0,
                        rows: 2,
                        columns: 2,
                        hGap: )" + std::to_string(int(hGap)) + R"(,
                        vGap: )" + std::to_string(int(vGap)) + R"(,
                        contains: ["box1", "box2", "box3", "box4"],
                      },
                      box1: {
                        width: 'spread',
                        height: 'spread',
                      },
                      box2: {
                        width: 'spread',
                        height: 'spread',
                      },
                      box3: {
                        width: 'spread',
                        height: 'spread',
                      },
                      box4: {
                        width: 'spread',
                        height: 'spread',
                      }
                  }
                },
                Transitions: {
                  default: {
                    from: 'start', to: 'end',
                  }
                }
            }
            )";
    Content(t, [&] {
        const float progress = animateToEnd.Get() ? 1.0f : 0.0f;
        MotionLayout(RememberMotionScene(scene), progress, SizeMod({}, Dp(rootSize), Dp(rootSize)),
                     [](MotionLayoutScope&) { Boxes({ "box1", "box2", "box3", "box4" }); });
    });
    t.WaitForIdle();
    const int columns = 2;
    const float gapSize = (rootSize - (10.0f * 2)) / (columns * 2.0f);
    const float leftX = gapSize, topY = gapSize;
    CHECK(PositionIs(t, "box1", leftX, topY));
    const float rightX = leftX + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box2", rightX, topY));
    const float bottomY = topY + 10 + gapSize + gapSize;
    CHECK(PositionIs(t, "box3", leftX, bottomY));
    CHECK(PositionIs(t, "box4", rightX, bottomY));
    animateToEnd.Set(true);
    t.WaitForIdle();
    const float boxWidth = (rootSize - hGap) / 2.0f;
    const float boxHeight = (rootSize - vGap) / 2.0f;
    CHECK(PositionIs(t, "box1", 0, 0));
    const float expectedLeft = boxWidth + hGap;
    CHECK(PositionIs(t, "box2", expectedLeft, 0));
    const float expectedTop = boxHeight + vGap;
    CHECK(PositionIs(t, "box3", 0, expectedTop));
    CHECK(PositionIs(t, "box4", expectedLeft, expectedTop));
}

TEST(ClMotionParser_testTransitionParseFailsSilently) {
    test::ComposeUiTest t;
    core::state::Transition coreTransition([](float dp) { return dp; });
    const std::string transitionContent = R"({
  from: "start",
  to: "end",
  KeyFrames: {
    KeyAttributes: [{
      target: ['id1'],
      frames: [10, 20],
      alpha: [0.5,],
    }],
  }
})";
    bool threw = false;
    try {
        core::state::TransitionParser::parse(*core::CLParser::parse(transitionContent), coreTransition);
    } catch (const core::CLParsingException&) {
        threw = true;
    }
    CHECK(threw);
    std::shared_ptr<Transition> transition = MakeTransition(transitionContent);
    std::shared_ptr<ConstraintSet> start = MakeConstraintSet(R"({
  id1: {
    width: 10, height: 10,
    centerVertically: 'parent', start: ['parent', 'start', 0]
  }
})");
    std::shared_ptr<ConstraintSet> end = MakeConstraintSet(R"({
  id1: {
    width: 10, height: 10,
    centerVertically: 'parent', end: ['parent', 'end', 0]
  }
})");
    Content(t, [&] {
        MotionLayout(start, end, 0.0f, SizeMod({}, Dp(100), Dp(100)), [](MotionLayoutScope&) { Box(foundation::LayoutId({}, "id1")); }, transition);
    });
    t.WaitForIdle();
}

namespace {

    void TestMotionLayoutSwipe(test::ComposeUiTest& t, kiln::Function<void()> content) {
        Content(t, std::move(content));
        t.WaitForIdle();
        auto motionSemantic = t.OnNodeWithTag("MyMotion");
        motionSemantic.AssertExists().PerformTouchInput([](test::TouchInjectionScope& s) {
            // performSwipe(endWithUp = false), then up() (upstream checks the position between).
            const float right = s.Width() - 1, centerY = s.Height() / 2;
            const ui::Offset from{ right * 0.25f, centerY }, to{ right * 0.5f, centerY };
            s.Down(from);
            const int steps = int(200 / test::TouchInjectionScope::eventPeriodMillis);
            for (int i = 1; i <= steps; i++) {
                s.AdvanceEventTime();
                const float f = float(i) / float(steps);
                s.MoveTo({ from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f });
            }
            s.Up();
        });
        t.MainClock().AdvanceTimeByFrame();
        t.WaitForIdle();
        CHECK(PositionIs(t, "box", 170, 10));
    }

}

TEST(ClOnSwipe_simpleCornerToCornerRightSwipe_Json) {
    test::ComposeUiTest t;
    TestMotionLayoutSwipe(t, [] {
        MotionLayout(RememberMotionScene(R"({
  ConstraintSets: {
    start: {
      box: {
        width: 20, height: 20,
        bottom: ['parent', 'bottom', 10],
        start: ['parent', 'start', 10]
      }
    },
    end: {
      Extends: 'start',
      box: {
        clear: ['constraints'],
        top: ['parent', 'top', 10],
        end: ['parent', 'end', 10],
      }
    }
  },
  Transitions: {
    default: {
       from: 'start',
       to: 'end',
       onSwipe: {
         anchor: 'box',
         direction: 'end',
         side: 'end',
         mode: 'spring',
         touchUp: 'neverCompleteStart',
         stopThreshold: 0.0001
       }
    }
  }
})"),
                     0.0f, SizeMod(foundation::TestTag({}, "MyMotion"), Dp(200), Dp(200)),
                     [](MotionLayoutScope&) { Box(foundation::TestTag(foundation::LayoutId(Background({}, Color::Red()), "box"), "box")); });
    });
}

TEST(ClOnSwipe_simpleCornerToCornerRightSwipe_Dsl) {
    test::ComposeUiTest t;
    std::shared_ptr<MotionScene> scene = MakeMotionScene([](MotionSceneScope& s) {
        const ConstrainedLayoutReference box = s.CreateRefFor("box");
        const ConstraintSetRef from = s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(box, [](ConstrainScope& c) {
                c.width = Dimension::Value(Dp(20));
                c.height = Dimension::Value(Dp(20));
                c.bottom.LinkTo(c.parent.bottom, Dp(10));
                c.start.LinkTo(c.parent.start, Dp(10));
            });
        });
        const ConstraintSetRef to = s.ConstraintSet(
            [&](ConstraintSetScope& cs) {
                cs.Constrain(box, [](ConstrainScope& c) {
                    c.ClearConstraints();
                    c.top.LinkTo(c.parent.top, Dp(10));
                    c.end.LinkTo(c.parent.end, Dp(10));
                });
            },
            std::nullopt, from);
        s.DefaultTransition(from, to, [&](TransitionScope& tr) {
            tr.onSwipe.emplace(OnSwipe{ box, SwipeSide::End(), SwipeDirection::End(), 1, 10, std::nullopt, std::nullopt, SwipeTouchUp::NeverCompleteStart(),
                                        SwipeMode::Spring(1, 400, 10, 0.0001f) });
        });
    });
    TestMotionLayoutSwipe(t, [scene] {
        MotionLayout(scene, 0.0f, SizeMod(foundation::TestTag({}, "MyMotion"), Dp(200), Dp(200)),
                     [](MotionLayoutScope&) { Box(foundation::TestTag(foundation::LayoutId(Background({}, Color::Red()), "box"), "box")); });
    });
}
