// Ported from constraintlayout-compose TransitionStabilityTest.kt, MotionSceneStabilityTest.kt
// and DebugFlagsTest.kt (androidHostTest, androidx).
#include "Check.h"

#include "kiln/constraintlayout/MotionLayout.h"

using namespace kiln;
using namespace kiln::constraintlayout;

namespace {
    std::shared_ptr<Transition> AlphaTransition(float second) {
        return MakeTransition("start", "end", [second](TransitionScope& t) {
            const ConstrainedLayoutReference a = t.CreateRefFor("a");
            t.KeyAttributes({ a }, [second](KeyAttributesScope& k) {
                k.Frame(20, [](KeyAttributeScope& f) { f.alpha = 0.2f; });
                k.Frame(80, [second](KeyAttributeScope& f) { f.alpha = second; });
            });
        });
    }

    const char* kAlphaTransitionJson = R"({
  from: 'start',
  to: 'end',
  KeyFrames: {
    KeyAttributes: [
      {
        target: ['a'],
        frames: [20, 80],
        alpha: [0.2, 0.8],
      }
    ],
  }
})";
}

TEST(ClTransitionStability_testTransitionDslEquality) {
    std::shared_ptr<Transition> transition = AlphaTransition(0.8f);
    CHECK(transition->Equals(*AlphaTransition(0.8f)));
    CHECK(!transition->Equals(*AlphaTransition(0.85f)));
}

TEST(ClTransitionStability_testTransitionJsonEquality) {
    CHECK(MakeTransition(kAlphaTransitionJson)->Equals(*MakeTransition(kAlphaTransitionJson)));
}

TEST(ClMotionSceneStability_testMotionSceneDslEquality) {
    // The references made in the scene, or in each set and transition: the same scene.
    auto fromSet = [](ConstraintSetScope& cs, bool horizontalForm) {
        auto box0 = cs.CreateRefFor("box0"), box1 = cs.CreateRefFor("box1"), box2 = cs.CreateRefFor("box2");
        cs.Constrain(box0, [](ConstrainScope& c) {
            c.width = Dimension::FillToConstraints();
            c.height = Dimension::Value(Dp(20));
            c.CenterVerticallyTo(c.parent);
        });
        cs.Constrain(box1, [](ConstrainScope& c) {
            c.width = Dimension::FillToConstraints();
            c.height = Dimension::Ratio("2:1");
            c.CenterVerticallyTo(c.parent);
        });
        cs.Constrain(box2, [](ConstrainScope& c) {
            c.width = Dimension::FillToConstraints();
            c.height = Dimension::Value(Dp(20));
            c.CenterVerticallyTo(c.parent);
        });
        ChainParams params;
        params.startMargin = Dp(8);
        params.endMargin = Dp(8);
        LayoutReference& middle = horizontalForm ? cs.WithHorizontalChainParams(box1, Dp(8), Dp(8)) : cs.WithChainParams(box1, params);
        cs.CreateHorizontalChain({ &box0, &middle, &box2 });
    };
    auto toSet = [](ConstraintSetScope& cs, bool verticalForm) {
        auto box0 = cs.CreateRefFor("box0"), box1 = cs.CreateRefFor("box1"), box2 = cs.CreateRefFor("box2");
        cs.Constrain(box0, [](ConstrainScope& c) {
            c.height = Dimension::FillToConstraints();
            c.width = Dimension::Value(Dp(20));
            c.CenterHorizontallyTo(c.parent);
        });
        cs.Constrain(box1, [](ConstrainScope& c) {
            c.height = Dimension::FillToConstraints();
            c.width = Dimension::Ratio("2:1");
            c.CenterHorizontallyTo(c.parent);
        });
        cs.Constrain(box2, [](ConstrainScope& c) {
            c.height = Dimension::FillToConstraints();
            c.width = Dimension::Value(Dp(20));
            c.CenterHorizontallyTo(c.parent);
        });
        ChainParams params;
        params.topMargin = Dp(8);
        params.bottomMargin = Dp(8);
        LayoutReference& middle = verticalForm ? cs.WithVerticalChainParams(box1, Dp(8), Dp(8)) : cs.WithChainParams(box1, params);
        cs.CreateVerticalChain({ &box0, &middle, &box2 });
    };
    auto keyFrames = [](TransitionScope& t, const ConstrainedLayoutReference& box1) {
        t.KeyAttributes({ box1 }, [](KeyAttributesScope& k) {
            k.Frame(33, [](KeyAttributeScope& f) { f.alpha = 0.25f; });
            k.Frame(66, [](KeyAttributeScope& f) { f.alpha = 0.8f; });
        });
    };
    std::shared_ptr<MotionScene> scene = MakeMotionScene([&](MotionSceneScope& s) {
        const ConstrainedLayoutReference box1 = s.CreateRefFor("box1");
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) { fromSet(cs, true); }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) { toSet(cs, true); }), [&](TransitionScope& t) { keyFrames(t, box1); });
    });
    std::shared_ptr<MotionScene> same = MakeMotionScene([&](MotionSceneScope& s) {
        s.DefaultTransition(s.ConstraintSet([&](ConstraintSetScope& cs) { fromSet(cs, false); }),
                            s.ConstraintSet([&](ConstraintSetScope& cs) { toSet(cs, false); }),
                            [&](TransitionScope& t) { keyFrames(t, t.CreateRefFor("box1")); });
    });
    CHECK(scene->Equals(*same));
}

TEST(ClDebugFlags_testFlags) {
    CHECK(DebugFlags::None() == DebugFlags());
    CHECK(DebugFlags::None() == DebugFlags(false, false, false));
    CHECK(!(DebugFlags::All() == DebugFlags(true, true, true)));
    DebugFlags flags = DebugFlags::All();
    CHECK(flags.ShowBounds() && flags.ShowPaths() && flags.ShowKeyPositions());
    flags = DebugFlags::None();
    CHECK(!flags.ShowBounds() && !flags.ShowPaths() && !flags.ShowKeyPositions());
    flags = DebugFlags(true, true, true);
    CHECK(flags.ShowBounds() && flags.ShowPaths() && flags.ShowKeyPositions());
    flags = DebugFlags(false, false, false);
    CHECK(!flags.ShowBounds() && !flags.ShowPaths() && !flags.ShowKeyPositions());
    flags = DebugFlags(true);
    CHECK(flags.ShowBounds() && !flags.ShowPaths() && !flags.ShowKeyPositions());
    flags = DebugFlags(false, true);
    CHECK(!flags.ShowBounds() && flags.ShowPaths() && !flags.ShowKeyPositions());
    flags = DebugFlags(false, false, true);
    CHECK(!flags.ShowBounds() && !flags.ShowPaths() && flags.ShowKeyPositions());
}

TEST(ClDebugFlags_testToString) {
    CHECK_EQ(std::string("DebugFlags(showBounds = true, showPaths = true, showKeyPositions = true)"), DebugFlags::All().ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = false, showPaths = false, showKeyPositions = false)"), DebugFlags::None().ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = false, showPaths = false, showKeyPositions = false)"), DebugFlags().ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = true, showPaths = true, showKeyPositions = true)"), DebugFlags(true, true, true).ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = true, showPaths = false, showKeyPositions = false)"), DebugFlags(true, false, false).ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = false, showPaths = true, showKeyPositions = false)"), DebugFlags(false, true, false).ToString());
    CHECK_EQ(std::string("DebugFlags(showBounds = false, showPaths = false, showKeyPositions = true)"), DebugFlags(false, false, true).ToString());
}
