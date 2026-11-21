// MotionCarousel (constraintlayout-compose has no tests for it): three slots, five items; a
// swipe to the left runs the forward transition and moves the items along by one slot.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/constraintlayout/MotionLayout.h"
#include "kiln/foundation/Layouts.h"

using namespace cltest;

namespace {
    // slot0 left, slot1 centre, slot2 right; forward slides each slot one place left.
    const char* kCarouselScene = R"({
  ConstraintSets: {
    start: {
      slot0: { width: 40, height: 40, centerVertically: 'parent', end: ['slot1', 'start', 20] },
      slot1: { width: 40, height: 40, centerVertically: 'parent', centerHorizontally: 'parent' },
      slot2: { width: 40, height: 40, centerVertically: 'parent', start: ['slot1', 'end', 20] }
    },
    next: {
      slot0: { width: 40, height: 40, centerVertically: 'parent', end: ['slot1', 'start', 80] },
      slot1: { width: 40, height: 40, centerVertically: 'parent', end: ['parent', 'start', -100] },
      slot2: { width: 40, height: 40, centerVertically: 'parent', centerHorizontally: 'parent' }
    },
    previous: {
      slot0: { width: 40, height: 40, centerVertically: 'parent', centerHorizontally: 'parent' },
      slot1: { width: 40, height: 40, centerVertically: 'parent', start: ['slot0', 'end', 20] },
      slot2: { width: 40, height: 40, centerVertically: 'parent', start: ['slot1', 'end', 20] }
    }
  },
  Transitions: {
    forward: { from: 'start', to: 'next' },
    backward: { from: 'start', to: 'previous' }
  }
})";
}

TEST(ClMotionCarousel_swipeAdvancesItems) {
    test::ComposeUiTest t;
    Content(t, [] {
        Box(foundation::SizeMod(foundation::TestTag({}, "carousel"), Dp(300), Dp(100)), foundation::Alignment::TopStart(), false, [] {
            MotionCarousel(RememberMotionScene(kCarouselScene), 1, 3, [](MotionCarouselScope& scope) {
                scope.Items(5, [](int index) { Box(foundation::TestTag(foundation::SizeMod({}, Dp(10), Dp(10)), "item" + std::to_string(index))); });
            });
        });
    });
    t.WaitForIdle();
    // item0 sits in the centre slot, item1 in the right one; nothing in the left slot.
    const float centreX = 150 - 5;
    CHECK(PositionIs(t, "item0", centreX, 45));
    CHECK(PositionIs(t, "item1", centreX + 60, 45));
    t.OnNodeWithTag("item2").AssertDoesNotExist();
    t.OnNodeWithTag("carousel").PerformTouchInput([](test::TouchInjectionScope& s) {
        s.Swipe({ s.Width() * 0.9f, s.Height() / 2 }, { s.Width() * 0.1f, s.Height() / 2 });
    });
    t.WaitForIdle();
    CHECK(PositionIs(t, "item1", centreX, 45));
    CHECK(PositionIs(t, "item0", centreX - 60, 45));
    CHECK(PositionIs(t, "item2", centreX + 60, 45));
}
