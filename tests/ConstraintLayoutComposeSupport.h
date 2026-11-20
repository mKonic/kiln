#pragma once

// Shared by the ConstraintLayout Compose tests ported from constraintlayout-compose's
// androidDeviceTest: px to dp at the test's density, and Modifier.layoutTestId.

#include "Check.h"

#include "kiln/constraintlayout/ConstraintLayout.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/test/ComposeUiTest.h"

#include <cmath>

namespace cltest {
    using namespace kiln;
    using namespace kiln::constraintlayout;
    using kiln::foundation::Box;
    using ui::Modifier;

    struct Px {
        float density;
        Dp operator()(float px) const { return Dp(px / density); }
        // dp, as the asserts take it.
        float dp(float px) const { return px / density; }
    };

    // Compose's test activity root is WRAP_CONTENT (loose constraints); kiln's test root is
    // the window, so content sits in a Box as upstream's sits in a wrap-content view.
    inline void Content(kiln::test::ComposeUiTest& t, kiln::Function<void()> content) {
        t.SetContent([content = std::move(content)] { Box(Modifier{}, foundation::Alignment::TopStart(), false, content); });
    }

    inline Modifier LayoutTestId(Modifier m, const std::string& id) { return foundation::LayoutId(foundation::TestTag(std::move(m), id), id); }
}

namespace cltest {
    // assertPositionInRootIsEqualTo(x, y, tolerance) in dp.
    inline bool PositionIs(kiln::test::ComposeUiTest& t, const std::string& tag, float x, float y, float tolerance = 0.5f) {
        const kiln::ui::Rect b = t.OnNodeWithTag(tag).GetUnclippedBoundsInRoot();
        return std::abs(b.left - x) <= tolerance && std::abs(b.top - y) <= tolerance;
    }
    inline bool WidthIs(kiln::test::ComposeUiTest& t, const std::string& tag, float w, float tolerance = 0.5f) {
        const kiln::ui::Rect b = t.OnNodeWithTag(tag).GetUnclippedBoundsInRoot();
        return std::abs(b.Width() - w) <= tolerance;
    }
    inline bool HeightIs(kiln::test::ComposeUiTest& t, const std::string& tag, float h, float tolerance = 0.5f) {
        const kiln::ui::Rect b = t.OnNodeWithTag(tag).GetUnclippedBoundsInRoot();
        return std::abs(b.Height() - h) <= tolerance;
    }
}
