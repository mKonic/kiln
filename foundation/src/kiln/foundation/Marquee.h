#pragma once

// basicMarquee (foundation BasicMarquee.kt): content wider than its space scrolls horizontally
// in a loop, a copy following after `spacing`. It scrolls only while the content overflows (and,
// with WhileFocused, while focused).

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>
#include "kiln/ui/Modifier.h"

#include <climits>
#include <cstdint>
#include <functional>
#include <memory>

namespace kiln::foundation {

    enum class MarqueeAnimationMode : std::uint8_t { Immediately, WhileFocused };

    // MarqueeSpacing: the gap between the end of the content and its copy, from the content's
    // and the container's widths (px).
    struct MarqueeSpacing {
        std::shared_ptr<const kiln::Function<int(const ui::Density&, int contentWidth, int containerWidth)>> calculate;
        int Calculate(const ui::Density& d, int contentWidth, int containerWidth) const { return calculate ? (*calculate)(d, contentWidth, containerWidth) : 0; }
        static MarqueeSpacing Of(ui::Dp spacing);
        static MarqueeSpacing FractionOfContainer(float fraction);
        bool operator==(const MarqueeSpacing&) const = default;
    };

    struct MarqueeDefaults {
        static constexpr int Iterations = 3;
        static constexpr int RepeatDelayMillis = 1200;
        static MarqueeSpacing Spacing();          // a third of the container
        static constexpr ui::Dp Velocity{ 30 };   // per second; negative scrolls the other way
    };

    struct MarqueeOptions {
        int iterations = MarqueeDefaults::Iterations;   // INT_MAX: forever
        MarqueeAnimationMode animationMode = MarqueeAnimationMode::Immediately;
        int repeatDelayMillis = MarqueeDefaults::RepeatDelayMillis;
        int initialDelayMillis = -1;   // -1: repeatDelayMillis when Immediately, else 0
        MarqueeSpacing spacing = MarqueeDefaults::Spacing();
        ui::Dp velocity = MarqueeDefaults::Velocity;
    };

    ui::Modifier BasicMarquee(ui::Modifier m, MarqueeOptions options = {});

}
