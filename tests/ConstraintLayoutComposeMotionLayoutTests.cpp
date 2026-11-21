// Ported from constraintlayout-compose MotionLayoutTest.kt (androidx). Expected values are
// upstream's, except where they are Android font metrics (monospace text at 10sp, "Hello" at
// 18sp): there the MotionLayout's text is compared with the same text laid out on its own.
// onGloballyPositioned positions are read with OnGloballyPositioned.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/constraintlayout/MotionLayout.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/Text.h"
#include "kiln/runtime/Composition.h"

#include <charconv>
#include <cmath>
#include <map>
#include <string>

using namespace cltest;
using foundation::Background;
using foundation::OnGloballyPositioned;
using foundation::SizeMod;

namespace {

    int RoundToInt(float v) { return int(std::lround(v)); }
    ui::IntOffset Round(ui::Offset o) { return { RoundToInt(o.x), RoundToInt(o.y) }; }

    // Kotlin's Float.toString, Dp.toString and TextUnit.toString.
    std::string KFloat(float v) {
        if (std::isnan(v)) return "NaN";
        char buf[32];
        auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
        (void)ec;
        std::string s(buf, end);
        if (s.find_first_of(".e") == std::string::npos) s += ".0";
        return s;
    }
    std::string DpString(Dp d) { return std::isnan(d.value) ? "Dp.Unspecified" : KFloat(d.value) + ".dp"; }
    std::string SpString(text::TextUnit t) { return KFloat(t.value) + ".sp"; }
    // Color.toHexString: toArgb().toUInt().toString(16); Color.Unspecified is 0 as ARGB.
    std::string HexString(Color c) {
        const std::uint32_t argb = c.Specified() ? c.argb : 0u;
        char buf[16];
        auto [end, ec] = std::to_chars(buf, buf + sizeof buf, argb, 16);
        (void)ec;
        return std::string(buf, end);
    }

    material3::TextOptions FontSize(text::TextUnit size) {
        material3::TextOptions options;
        options.fontSize = size;
        options.fontFamily = text::FontFamily::Monospace();
        return options;
    }

    // createCornerToCornerMotionScene: a box from the top-left corner to the bottom-right one.
    std::shared_ptr<MotionScene> CornerToCornerMotionScene(const Px& px, const std::string& boxId, int boxSizePx,
                                                           kiln::Function<void(TransitionScope&, const ConstrainedLayoutReference&)> transitionContent) {
        return MakeMotionScene([&](MotionSceneScope& s) {
            const ConstrainedLayoutReference boxRef = s.CreateRefFor(boxId);
            const Dp boxSize = px(float(boxSizePx));
            s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) {
                cs.Constrain(boxRef, [&](ConstrainScope& c) {
                    c.width = Dimension::Value(boxSize);
                    c.height = Dimension::Value(boxSize);
                    c.top.LinkTo(c.parent.top);
                    c.start.LinkTo(c.parent.start);
                });
            }),
                                s.ConstraintSet([&](ConstraintSetScope& cs) {
                                    cs.Constrain(boxRef, [&](ConstrainScope& c) {
                                        c.width = Dimension::Value(boxSize);
                                        c.height = Dimension::Value(boxSize);
                                        c.bottom.LinkTo(c.parent.bottom);
                                        c.end.LinkTo(c.parent.end);
                                    });
                                }),
                                [&](TransitionScope& t) { transitionContent(t, boxRef); });
        });
    }

    // custom_text_size_scene.json5 (androidDeviceTest/res/raw).
    const char* kCustomTextSizeScene = R"({
  ConstraintSets: {
    start: {
      profile_pic: {
        width: 20,
        height: 20,
        start: ['parent', 'start', 16],
        top: ['parent', 'top', 16],
        custom: {
          background: '#08ff04'
        }
      },
      username: {
        top: ['profile_pic', 'top'],
        bottom: ['profile_pic', 'bottom'],
        start: ['profile_pic', 'end', 16],
        custom: {
          textSize: 18
        }
      },
      box: {
        width: 'spread',
        height: 'spread',
        start: ['parent', 'start'],
        end: ['parent', 'end'],
        top: ['parent', 'top'],
        bottom: ['parent', 'bottom', -16],
      }
    },
    end: {
      profile_pic: {
        width: 40,
        height: 40,
        start: ['parent', 'start'],
        end: ['parent', 'end'],
        top: ['parent', 'top', 16],
        custom: {
          background: '#FFFFFF'
        }
      },
      username: {
        top: ['profile_pic', 'bottom', 16],
        end: ['parent', 'end'],
        start: ['parent', 'start'],
        custom: {
          textSize: 12
        }
      },
      box: {
        width: 'spread',
        height: 'spread',
        start: ['parent', 'start'],
        end: ['parent', 'end'],
        top: ['parent', 'top'],
        bottom: ['parent', 'bottom', -16],
      }
    }
  },
  Transitions: {
    default: {
      from: 'start',
      to: 'end',
      pathMotionArc: 'startHorizontal',
      KeyFrames: {
        KeyAttributes: [
          {
            target: ['profile_pic'],
            frames: [0, 100]
          },
          {
            target: ['username'],
            frames: [0, 50, 100],
            translationX: [0, 70, 0],
          }
        ]
      }
    }
  }
})";

}

TEST(ClMotionLayout_testCustomTextSize) {
    test::ComposeUiTest t;
    rt::State<bool> animateToEnd(false);
    Content(t, [&] {
        // animateFloatAsState(if (animateToEnd) 1 else 0): the end of the animation is what is read.
        const float progress = animateToEnd.Get() ? 1.0f : 0.0f;
        foundation::Column(Modifier{}, [&] {
            MotionLayout(RememberMotionScene(kCustomTextSizeScene), progress, SizeMod({}, Dp(200), Dp(200)), [](MotionLayoutScope& scope) {
                const MotionLayoutScope::CustomProperties profilePicProperties = scope.GetCustomProperties("profile_pic");
                Box(Background(LayoutTestId({}, "box"), Color::DarkGray()));
                Box(LayoutTestId({}, "profile_pic"));
                material3::TextOptions options = FontSize(scope.CustomFontSize("username", "textSize"));
                options.color = profilePicProperties.GetColor("background");
                material3::Text("Hello", LayoutTestId({}, "username"), options);
            });
            material3::Text("Hello", foundation::TestTag({}, "hello18"), FontSize(text::Sp(18)));
            material3::Text("Hello", foundation::TestTag({}, "hello12"), FontSize(text::Sp(12)));
        });
    });
    t.WaitForIdle();
    ui::Rect usernameSize = t.OnNodeWithTag("username").GetUnclippedBoundsInRoot();
    ui::Rect expected = t.OnNodeWithTag("hello18").GetUnclippedBoundsInRoot();
    CHECK_NEAR(expected.Width(), usernameSize.Width(), 0.5f);
    CHECK_NEAR(expected.Height(), usernameSize.Height(), 0.5f);
    animateToEnd.Set(true);
    t.WaitForIdle();
    usernameSize = t.OnNodeWithTag("username").GetUnclippedBoundsInRoot();
    expected = t.OnNodeWithTag("hello12").GetUnclippedBoundsInRoot();
    CHECK_NEAR(expected.Width(), usernameSize.Width(), 0.5f);
    CHECK_NEAR(expected.Height(), usernameSize.Height(), 0.5f);
}

TEST(ClMotionLayout_testCustomKeyFrameAttributes) {
    test::ComposeUiTest t;
    rt::State<float> progress(0.0f);
    std::shared_ptr<MotionScene> scene = MakeMotionScene([](MotionSceneScope& s) {
        const ConstrainedLayoutReference element = s.CreateRefFor("element");
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(element, [](ConstrainScope& c) {
                MotionSceneScope::CustomColor(c, "color", Color::White());
                MotionSceneScope::CustomDistance(c, "distance", Dp(0));
                MotionSceneScope::CustomFontSize(c, "fontSize", text::Sp(0));
                MotionSceneScope::CustomInt(c, "int", 0);
            });
        }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) {
                                cs.Constrain(element, [](ConstrainScope& c) {
                                    MotionSceneScope::CustomColor(c, "color", Color::Black());
                                    MotionSceneScope::CustomDistance(c, "distance", Dp(10));
                                    MotionSceneScope::CustomFontSize(c, "fontSize", text::Sp(20));
                                    MotionSceneScope::CustomInt(c, "int", 30);
                                });
                            }),
                            [&](TransitionScope& tr) {
                                tr.KeyAttributes({ element }, [](KeyAttributesScope& k) {
                                    k.Frame(50, [](KeyAttributeScope& f) {
                                        f.CustomColor("color", Color(0x00ff0000u));
                                        f.CustomDistance("distance", Dp(20));
                                        f.CustomFontSize("fontSize", text::Sp(30));
                                        f.CustomInt("int", 40);
                                    });
                                });
                            });
    });
    Content(t, [&] {
        MotionLayout(scene, progress.Get(), SizeMod({}, Dp(200), Dp(200)), [](MotionLayoutScope& scope) {
            const MotionLayoutScope::CustomProperties props = scope.GetCustomProperties("element");
            foundation::Column(foundation::LayoutId({}, "element"), [props] {
                material3::Text("1) Color: #" + HexString(props.GetColor("color")));
                material3::Text("2) Distance: " + DpString(props.Distance("distance")));
                material3::Text("3) FontSize: " + SpString(props.FontSize("fontSize")));
                material3::Text("4) Int: " + std::to_string(props.GetInt("int")));
                material3::Text("5) Color: #" + HexString(props.GetColor("a")));
                material3::Text("6) Distance: " + DpString(props.Distance("b")));
                material3::Text("7) FontSize: " + SpString(props.FontSize("c")));
                material3::Text("8) Int: " + std::to_string(props.GetInt("d")));
            });
        });
    });
    t.WaitForIdle();
    progress.Set(0.25f);
    t.WaitForIdle();
    t.OnNodeWithText("1) Color: #7fffbaba").AssertExists();
    t.OnNodeWithText("2) Distance: 10.0.dp").AssertExists();
    t.OnNodeWithText("3) FontSize: 15.0.sp").AssertExists();
    t.OnNodeWithText("4) Int: 20").AssertExists();
    t.OnNodeWithText("5) Color: #0").AssertExists();
    t.OnNodeWithText("6) Distance: Dp.Unspecified").AssertExists();
    t.OnNodeWithText("7) FontSize: NaN.sp").AssertExists();
    t.OnNodeWithText("8) Int: 0").AssertExists();
    progress.Set(0.75f);
    t.WaitForIdle();
    t.OnNodeWithText("1) Color: #7fba0000").AssertExists();
    t.OnNodeWithText("2) Distance: 15.0.dp").AssertExists();
    t.OnNodeWithText("3) FontSize: 25.0.sp").AssertExists();
    t.OnNodeWithText("4) Int: 35").AssertExists();
    t.OnNodeWithText("5) Color: #0").AssertExists();
    t.OnNodeWithText("6) Distance: Dp.Unspecified").AssertExists();
    t.OnNodeWithText("7) FontSize: NaN.sp").AssertExists();
    t.OnNodeWithText("8) Int: 0").AssertExists();
}

TEST(ClMotionLayout_testMotionLayout_withParentIntrinsics) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    std::shared_ptr<ConstraintSet> constraintSet = MakeConstraintSet([](ConstraintSetScope& s) {
        auto refs = s.CreateRefsFor({ "one", "two" });
        auto horChain = s.CreateHorizontalChain({ &refs[0], &refs[1] }, ChainStyle::Packed(0.0f));
        s.Constrain(horChain, [](HorizontalChainScope& c) {
            c.start.LinkTo(c.parent.start);
            c.end.LinkTo(c.parent.end);
        });
        s.Constrain(refs[0], [](ConstrainScope& c) {
            c.top.LinkTo(c.parent.top);
            c.bottom.LinkTo(c.parent.bottom);
        });
        s.Constrain(refs[1], [](ConstrainScope& c) {
            c.width = Dimension::PreferredWrapContent();
            c.top.LinkTo(c.parent.top);
            c.bottom.LinkTo(c.parent.bottom);
        });
    });
    const int rootBoxWidth = 200, box1Size = 40, box2Size = 70;
    ui::IntSize rootSize{}, mlSize{};
    ui::IntOffset box1Position{}, box2Position{};
    std::shared_ptr<Transition> transition = MakeTransition([](TransitionScope&) {});
    Content(t, [&] {
        Box(OnGloballyPositioned(Background(foundation::Height(foundation::Width({}, px(float(rootBoxWidth))), foundation::IntrinsicSize::Max),
                                            Color::LightGray()),
                                 [&](const ui::LayoutCoordinates& c) { rootSize = c.Size(); }),
            foundation::Alignment::TopStart(), false, [&] {
                MotionLayout(constraintSet, constraintSet, 0.0f,
                             OnGloballyPositioned(Background(foundation::WrapContent(foundation::FillMax({}, foundation::Axis::Horizontal),
                                                                                     foundation::Axis::Vertical),
                                                             Color::Yellow()),
                                                  [&](const ui::LayoutCoordinates& c) { mlSize = c.Size(); }),
                             [&](MotionLayoutScope&) {
                                 Box(OnGloballyPositioned(foundation::LayoutId(Background(SizeMod({}, px(float(box1Size)), px(float(box1Size))), Color::Green()), "one"),
                                                          [&](const ui::LayoutCoordinates& c) { box1Position = Round(c.PositionInRoot()); }));
                                 Box(OnGloballyPositioned(foundation::LayoutId(Background(SizeMod({}, px(float(box2Size)), px(float(box2Size))), Color::Red()), "two"),
                                                          [&](const ui::LayoutCoordinates& c) { box2Position = Round(c.PositionInRoot()); }));
                             },
                             transition);
            });
    });
    const ui::IntSize expectedSize{ rootBoxWidth, box2Size };
    const int expectedBox1Y = RoundToInt((box2Size / 2.0f) - (box1Size / 2.0f));
    t.RunOnIdle([&] {
        CHECK(expectedSize == rootSize);
        CHECK(expectedSize == mlSize);
        CHECK((ui::IntOffset{ 0, expectedBox1Y }) == box1Position);
        CHECK((ui::IntOffset{ box1Size, 0 }) == box2Position);
    });
}

TEST(ClMotionLayout_testTransitionChange_hasCorrectStartAndEnd) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootWidthPx = 200, rootHeightPx = 50;
    std::shared_ptr<MotionScene> scene = MakeMotionScene([&](MotionSceneScope& s) {
        const ConstrainedLayoutReference circleRef = s.CreateRefFor("circle");
        const ConstraintSetRef aCSetRef = s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(circleRef, [&](ConstrainScope& c) {
                c.width = Dimension::Value(px(float(rootHeightPx)));
                c.height = Dimension::Value(px(float(rootHeightPx)));
                c.CenterVerticallyTo(c.parent);
                c.start.LinkTo(c.parent.start);
            });
        });
        const ConstraintSetRef bCSetRef = s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(circleRef, [&](ConstrainScope& c) {
                c.width = Dimension::FillToConstraints();
                c.height = Dimension::Value(px(float(rootHeightPx)));
                c.CenterTo(c.parent);
            });
        });
        const ConstraintSetRef cCSetRef = s.ConstraintSet(
            [&](ConstraintSetScope& cs) {
                cs.Constrain(circleRef, [](ConstrainScope& c) {
                    c.ClearHorizontal();
                    c.end.LinkTo(c.parent.end);
                });
            },
            std::nullopt, aCSetRef);
        s.Transition(aCSetRef, bCSetRef, std::string("part1"), [](TransitionScope&) {});
        s.Transition(bCSetRef, cCSetRef, std::string("part2"), [](TransitionScope&) {});
    });
    rt::State<float> progress(0.0f);
    ui::IntRect bounds{};
    Content(t, [&] {
        const float p = progress.Get();
        MotionLayout(scene, p < 0.5f ? p * 2 : p * 2 - 1, SizeMod({}, px(float(rootWidthPx)), px(float(rootHeightPx))),
                     [&](MotionLayoutScope&) {
                         Box(OnGloballyPositioned(Background(foundation::LayoutId({}, "circle"), Color::Red()), [&](const ui::LayoutCoordinates& c) {
                             const ui::Offset pos = c.PositionInParent();
                             const ui::IntSize size = c.Size();
                             bounds = { RoundToInt(pos.x), RoundToInt(pos.y), RoundToInt(pos.x) + size.width, RoundToInt(pos.y) + size.height };
                         }));
                     },
                     p < 0.5f ? "part1" : "part2");
    });
    t.RunOnIdle([&] { CHECK((ui::IntRect{ 0, 0, rootHeightPx, rootHeightPx }) == bounds); });
    const int offset = 25;
    progress.Set(0.25f);
    t.RunOnIdle([&] { CHECK((ui::IntRect{ 0, 0, rootWidthPx / 2 + offset, rootHeightPx }) == bounds); });
    progress.Set(0.75f);
    t.RunOnIdle([&] {
        CHECK((ui::IntRect{ rootWidthPx / 2 - offset, 0, rootWidthPx / 2 - offset + rootWidthPx / 2 + offset, rootHeightPx }) == bounds);
    });
}

TEST(ClMotionLayout_testStartAndEndBoundsModifier) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootSizePx = 100, boxHeight = 10, boxWidthStartPx = 10, boxWidthEndPx = 70;
    const std::string boxId = "box";
    ui::Rect startBoundsOfBox{}, endBoundsOfBox{};
    ui::IntRect globallyPositionedBounds{};
    int boundsProvidedCount = 0;
    rt::State<float> progress(0.0f);
    std::shared_ptr<MotionScene> scene = MakeMotionScene([&](MotionSceneScope& s) {
        const ConstrainedLayoutReference box = s.CreateRefFor(boxId);
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(box, [&](ConstrainScope& c) {
                c.width = Dimension::Value(px(float(boxWidthStartPx)));
                c.height = Dimension::Value(px(float(boxHeight)));
                c.top.LinkTo(c.parent.top);
                c.CenterHorizontallyTo(c.parent);
            });
        }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) {
                                cs.Constrain(box, [&](ConstrainScope& c) {
                                    c.width = Dimension::Value(px(float(boxWidthEndPx)));
                                    c.height = Dimension::Value(px(float(boxHeight)));
                                    c.CenterHorizontallyTo(c.parent);
                                    c.bottom.LinkTo(c.parent.bottom);
                                });
                            }));
    });
    auto roundRect = [](const ui::Rect& r) { return ui::IntRect{ RoundToInt(r.left), RoundToInt(r.top), RoundToInt(r.right), RoundToInt(r.bottom) }; };
    Content(t, [&] {
        MotionLayout(scene, progress.Get(), SizeMod({}, px(float(rootSizePx)), px(float(rootSizePx))), [&](MotionLayoutScope& scope) {
            Box(OnGloballyPositioned(scope.OnStartEndBoundsChanged(Background(foundation::LayoutId({}, boxId), Color::Red()), boxId,
                                                                   [&](const ui::Rect& startBounds, const ui::Rect& endBounds) {
                                                                       boundsProvidedCount++;
                                                                       startBoundsOfBox = startBounds;
                                                                       endBoundsOfBox = endBounds;
                                                                   }),
                                     [&](const ui::LayoutCoordinates& c) {
                                         const ui::Offset pos = c.PositionInParent();
                                         const ui::IntSize size = c.Size();
                                         globallyPositionedBounds = { RoundToInt(pos.x), RoundToInt(pos.y), RoundToInt(pos.x) + size.width,
                                                                      RoundToInt(pos.y) + size.height };
                                     }));
        });
    });
    t.WaitForIdle();
    t.RunOnIdle([&] {
        CHECK_EQ(1, boundsProvidedCount);
        const int left = (rootSizePx - boxWidthStartPx) / 2;
        CHECK((ui::IntRect{ left, 0, left + boxWidthStartPx, boxHeight }) == globallyPositionedBounds);
        CHECK(globallyPositionedBounds == roundRect(startBoundsOfBox));
    });
    progress.Set(1.0f);
    t.RunOnIdle([&] {
        CHECK_EQ(1, boundsProvidedCount);
        const int left = (rootSizePx - boxWidthEndPx) / 2;
        CHECK((ui::IntRect{ left, rootSizePx - boxHeight, left + boxWidthEndPx, rootSizePx }) == globallyPositionedBounds);
        CHECK(globallyPositionedBounds == roundRect(endBoundsOfBox));
    });
}

TEST(ClMotionLayout_testStaggeredAndCustomWeights) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootSizePx = 100, boxSizePx = 10;
    rt::State<float> progress(0.0f);
    rt::State<float> staggeredValue(0.31f);
    rt::State<float> weights[3] = { rt::State<float>(NAN), rt::State<float>(NAN), rt::State<float>(NAN) };
    std::map<int, ui::IntOffset> positions;
    Content(t, [&] {
        // remember { derivedStateOf { MotionScene { ... } } }.value: rebuilt when what it reads changes.
        const float stagger = staggeredValue.Get();
        const float w[3] = { weights[0].Get(), weights[1].Get(), weights[2].Get() };
        auto* c = rt::Composition::Current();
        std::shared_ptr<MotionScene> scene = c->Remember<std::shared_ptr<MotionScene>>(
            [&] {
                return MakeMotionScene([&](MotionSceneScope& s) {
                    std::vector<ConstrainedLayoutReference> refs;
                    for (int id = 0; id < 3; id++) refs.push_back(s.CreateRefFor(std::to_string(id)));
                    s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) {
                        cs.CreateVerticalChain({ &refs[0], &refs[1], &refs[2] }, ChainStyle::Packed(0.0f));
                        for (int index = 0; index < 3; index++)
                            cs.Constrain(refs[std::size_t(index)], [&](ConstrainScope& cc) { MotionSceneScope::SetStaggeredWeight(cc, w[index]); });
                    }),
                                        s.ConstraintSet([&](ConstraintSetScope& cs) {
                                            cs.CreateVerticalChain({ &refs[0], &refs[1], &refs[2] }, ChainStyle::Packed(0.0f));
                                            cs.Constrain({ &refs[0], &refs[1], &refs[2] }, [](ConstrainScope& cc) { cc.end.LinkTo(cc.parent.end); });
                                        }),
                                        [&](TransitionScope& tr) { tr.maxStaggerDelay = stagger; });
                });
            },
            rt::Keys(stagger, std::isnan(w[0]) ? -1.0f : w[0], std::isnan(w[1]) ? -1.0f : w[1], std::isnan(w[2]) ? -1.0f : w[2]));
        MotionLayout(scene, progress.Get(), SizeMod({}, px(float(rootSizePx)), px(float(rootSizePx))), [&](MotionLayoutScope&) {
            for (int id = 0; id < 3; id++) {
                Box(OnGloballyPositioned(foundation::LayoutId(SizeMod({}, px(float(boxSizePx)), px(float(boxSizePx))), std::to_string(id)),
                                         [&positions, id](const ui::LayoutCoordinates& lc) { positions[id] = Round(lc.PositionInParent()); }));
            }
        });
    });
    progress.Set(0.3f);
    t.RunOnIdle([&] {
        CHECK_EQ(0, positions[0].x);
        CHECK(positions[1].x != 0);
        CHECK(positions[2].x != 0);
        CHECK(positions[2].x > positions[1].x);
    });
    staggeredValue.Set(-staggeredValue.Get());
    t.RunOnIdle([&] {
        CHECK(positions[0].x != 0);
        CHECK(positions[1].x != 0);
        CHECK_EQ(0, positions[2].x);
        CHECK(positions[0].x > positions[1].x);
    });
    weights[0].Set(3.0f);
    weights[1].Set(1.0f);
    weights[2].Set(2.0f);
    staggeredValue.Set(-staggeredValue.Get());
    t.RunOnIdle([&] {
        CHECK(positions[0].x != 0);
        CHECK_EQ(0, positions[1].x);
        CHECK(positions[2].x != 0);
        CHECK(positions[0].x > positions[2].x);
    });
}

TEST(ClMotionLayout_testRemeasureOnContentChanged) {
    test::ComposeUiTest t;
    rt::State<float> progress(0.0f);
    rt::State<std::string> textContent(std::string("Foo"));
    std::shared_ptr<MotionScene> scene = MakeMotionScene([](MotionSceneScope& s) {
        const ConstrainedLayoutReference textRef = s.CreateRefFor("text");
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) {
            cs.Constrain(textRef, [](ConstrainScope& c) {
                c.CenterHorizontallyTo(c.parent);
                c.CenterVerticallyTo(c.parent, 0.0f);
            });
        }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) {
                                cs.Constrain(textRef, [](ConstrainScope& c) {
                                    c.CenterHorizontallyTo(c.parent);
                                    c.CenterVerticallyTo(c.parent, 1.0f);
                                });
                            }));
    });
    Content(t, [&] {
        foundation::Column(Modifier{}, [&] {
            MotionLayout(scene, progress.Get(), Background(SizeMod({}, Dp(300), Dp(300)), Color::LightGray()), [&](MotionLayoutScope&) {
                material3::Text(textContent.Get(), LayoutTestId({}, "text"), FontSize(text::Sp(10)));
            });
            material3::Text("Foo", foundation::TestTag({}, "foo"), FontSize(text::Sp(10)));
            material3::Text("FooBar", foundation::TestTag({}, "foobar"), FontSize(text::Sp(10)));
        });
    });
    t.WaitForIdle();
    const ui::Rect foo = t.OnNodeWithTag("foo").GetUnclippedBoundsInRoot();
    const ui::Rect fooBar = t.OnNodeWithTag("foobar").GetUnclippedBoundsInRoot();
    // Upstream's 18 x 14 and 36 x 14: monospace "FooBar" is twice "Foo", on one line.
    CHECK_EQ(RoundToInt(fooBar.Width()), 2 * RoundToInt(foo.Width()));
    ui::Rect actualTextSize = t.OnNodeWithTag("text").GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(foo.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(foo.Height()), RoundToInt(actualTextSize.Height()));
    progress.Set(0.5f);
    t.WaitForIdle();
    actualTextSize = t.OnNodeWithTag("text").GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(foo.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(foo.Height()), RoundToInt(actualTextSize.Height()));
    textContent.Set("FooBar");
    t.WaitForIdle();
    actualTextSize = t.OnNodeWithTag("text").GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(fooBar.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(fooBar.Height()), RoundToInt(actualTextSize.Height()));
}

TEST(ClMotionLayout_testOnSwipe_withLimitBounds) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootSizePx = 300, boxSizePx = 30;
    const std::string boxId = "box";
    ui::IntOffset boxPosition{};
    std::shared_ptr<MotionScene> scene = CornerToCornerMotionScene(px, boxId, boxSizePx, [](TransitionScope& tr, const ConstrainedLayoutReference& boxRef) {
        tr.onSwipe.emplace(OnSwipe{ boxRef, SwipeSide::End(), SwipeDirection::End(), 1, 10, std::nullopt, boxRef });
    });
    Content(t, [&] {
        MotionLayout(scene, 0.0f, SizeMod(LayoutTestId({}, "MyMotion"), px(float(rootSizePx)), px(float(rootSizePx))), [&](MotionLayoutScope&) {
            Box(OnGloballyPositioned(Background(LayoutTestId({}, boxId), Color::Red()),
                                     [&](const ui::LayoutCoordinates& c) { boxPosition = Round(c.PositionInParent()); }));
        });
    });
    t.WaitForIdle();
    auto motionSemantic = t.OnNodeWithTag("MyMotion");
    motionSemantic.AssertExists().PerformTouchInput([&](test::TouchInjectionScope& s) {
        const float right = s.Width() - 1, centerY = s.Height() / 2;
        s.Swipe({ 0 + boxSizePx / 2.0f, centerY }, { right * 0.9f, centerY });
    });
    t.MainClock().AdvanceTimeByFrame();
    t.WaitForIdle();
    CHECK((ui::IntOffset{ 0, 0 }) == boxPosition);
    motionSemantic.AssertExists().PerformTouchInput([&](test::TouchInjectionScope& s) {
        const float right = s.Width() - 1, centerY = s.Height() / 2;
        s.Swipe({ 0 + boxSizePx / 2.0f, 0 + boxSizePx / 2.0f }, { right * 0.9f, centerY });
    });
    t.MainClock().AdvanceTimeByFrame();
    t.WaitForIdle();
    CHECK((ui::IntOffset{ rootSizePx - boxSizePx, rootSizePx - boxSizePx }) == boxPosition);
}

TEST(ClMotionLayout_testInvalidationStrategy_onObservedStateChange) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootSizePx = 200;
    rt::State<float> progress(0.0f);
    rt::State<std::string> textContent(std::string("Foo"));
    rt::State<bool> optimizeCorrectly(false);
    const std::string textId = "text";
    std::shared_ptr<MotionScene> scene = MakeMotionScene([&](MotionSceneScope& s) {
        const ConstrainedLayoutReference textRef = s.CreateRefFor(textId);
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) { cs.Constrain(textRef, [](ConstrainScope& c) { c.CenterTo(c.parent); }); }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) { cs.Constrain(textRef, [](ConstrainScope& c) { c.CenterTo(c.parent); }); }));
    });
    Content(t, [&] {
        auto* c = rt::Composition::Current();
        const bool optimize = optimizeCorrectly.Get();
        std::shared_ptr<InvalidationStrategy> strategy = c->Remember<std::shared_ptr<InvalidationStrategy>>(
            [&] {
                if (optimize) return std::make_shared<InvalidationStrategy>(nullptr, [&textContent] { (void)textContent.Get(); });
                return std::make_shared<InvalidationStrategy>(nullptr, [] {});
            },
            rt::Keys(optimize));
        foundation::Column(Modifier{}, [&] {
            MotionLayoutOptions options;
            options.invalidationStrategy = strategy;
            MotionLayout(scene, progress.Get(), SizeMod({}, px(float(rootSizePx)), px(float(rootSizePx))),
                         [&](MotionLayoutScope&) { material3::Text(textContent.Get(), LayoutTestId({}, textId), FontSize(text::Sp(10))); }, "default",
                         options);
            material3::Text("Foo", foundation::TestTag({}, "foo"), FontSize(text::Sp(10)));
            material3::Text("Foo\nBar", foundation::TestTag({}, "foobar"), FontSize(text::Sp(10)));
        });
    });
    t.WaitForIdle();
    const ui::Rect foo = t.OnNodeWithTag("foo").GetUnclippedBoundsInRoot();
    const ui::Rect fooBar = t.OnNodeWithTag("foobar").GetUnclippedBoundsInRoot();
    CHECK(RoundToInt(fooBar.Height()) > RoundToInt(foo.Height()));   // upstream: 25 against 14
    ui::Rect actualTextSize = t.OnNodeWithTag(textId).GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(foo.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(foo.Height()), RoundToInt(actualTextSize.Height()));
    textContent.Set("Foo\nBar");
    t.WaitForIdle();
    // An empty onObservedStateChange: the content changing does not remeasure.
    actualTextSize = t.OnNodeWithTag(textId).GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(foo.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(foo.Height()), RoundToInt(actualTextSize.Height()));
    textContent.Set("Foo");
    optimizeCorrectly.Set(true);
    t.WaitForIdle();
    actualTextSize = t.OnNodeWithTag(textId).GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(foo.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(foo.Height()), RoundToInt(actualTextSize.Height()));
    textContent.Set("Foo\nBar");
    t.WaitForIdle();
    actualTextSize = t.OnNodeWithTag(textId).GetUnclippedBoundsInRoot();
    CHECK_EQ(RoundToInt(fooBar.Width()), RoundToInt(actualTextSize.Width()));
    CHECK_EQ(RoundToInt(fooBar.Height()), RoundToInt(actualTextSize.Height()));
}

TEST(ClMotionLayout_testOnSwipe_withDragScale) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    const int rootSizePx = 300, boxSizePx = 30;
    const std::string boxId = "box";
    const float dragScale = 3;
    ui::IntOffset boxPosition{};
    std::shared_ptr<MotionScene> scene =
        CornerToCornerMotionScene(px, boxId, boxSizePx, [dragScale](TransitionScope& tr, const ConstrainedLayoutReference& boxRef) {
            tr.onSwipe.emplace(OnSwipe{ boxRef, SwipeSide::Middle(), SwipeDirection::Down(), dragScale, 10, std::nullopt, std::nullopt, SwipeTouchUp::ToStart() });
        });
    Content(t, [&] {
        MotionLayout(scene, 0.0f, SizeMod(LayoutTestId({}, "MyMotion"), px(float(rootSizePx)), px(float(rootSizePx))), [&](MotionLayoutScope&) {
            Box(OnGloballyPositioned(Background(LayoutTestId({}, boxId), Color::Red()),
                                     [&](const ui::LayoutCoordinates& c) { boxPosition = Round(c.PositionInParent()); }));
        });
    });
    t.WaitForIdle();
    t.OnNodeWithTag("MyMotion").AssertExists().PerformTouchInput([&](test::TouchInjectionScope& s) {
        const float top = 0, bottom = s.Height() - 1;
        const float off = ((bottom - (boxSizePx / 2.0f)) - (top + (boxSizePx / 2.0f))) * 0.5f;
        s.Swipe({ s.Center().x, top + (boxSizePx / 2.0f) }, { s.Center().x, (top + (boxSizePx / 2.0f)) + off });
    });
    t.MainClock().AdvanceTimeByFrame();
    t.WaitForIdle();
    CHECK((ui::IntOffset{ rootSizePx - boxSizePx, rootSizePx - boxSizePx }) == boxPosition);
}
