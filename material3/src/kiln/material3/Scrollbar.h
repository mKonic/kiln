#pragma once

// Scrollbar.kt: a visual-only scrollbar for a scrolling component. It cannot be dragged. It is
// drawn at the end edge of a vertical scroller (start in RTL) and the bottom of a horizontal
// one, only while the content is longer than the viewport. By default it shows while the
// content scrolls and fades out once scrolling stops.

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/ui/Modifier.h"

#include <optional>

namespace kiln::ui { class LayoutNode; }

namespace kiln::material3 {

    namespace NonInteractiveScrollbarDefaults {
        inline constexpr float ThumbOpacity = 0.7f;
        inline constexpr int ThumbFadeDurationMillis = 250;
        inline constexpr int ThumbFadeDelayMillis = 400;
        // outline at ThumbOpacity.
        ui::Color thumbColor();
        inline constexpr ui::Dp Thickness{ 4 };
        inline constexpr ui::Dp ThumbMinLength{ 24 };
        // Of the track's length.
        inline constexpr float ThumbMaxLengthFraction = 0.9f;
        inline constexpr ui::Dp MainAxisTrackInset{ 2 };
        inline constexpr ui::Dp CrossAxisTrackInset{ 0 };
    }

    struct NonInteractiveScrollbarOptions {
        ui::Color thumbColor = ui::Color::Unspecified();   // unspecified: NonInteractiveScrollbarDefaults::thumbColor
        ui::Color trackColor = ui::Color::Transparent();
        ui::Dp thickness = NonInteractiveScrollbarDefaults::Thickness;
        // The scrollbar is not drawn when the track is shorter than this.
        ui::Dp thumbMinLength = NonInteractiveScrollbarDefaults::ThumbMinLength;
        float thumbMaxLengthFraction = NonInteractiveScrollbarDefaults::ThumbMaxLengthFraction;   // 0..1
        // Off: always visible.
        bool isFadeEnabled = true;
        int fadeDurationMillis = NonInteractiveScrollbarDefaults::ThumbFadeDurationMillis;
        int fadeDelayMillis = NonInteractiveScrollbarDefaults::ThumbFadeDelayMillis;
        ui::Dp mainAxisTrackInset = NonInteractiveScrollbarDefaults::MainAxisTrackInset;
        ui::Dp crossAxisTrackInset = NonInteractiveScrollbarDefaults::CrossAxisTrackInset;
    };

    // Modifier.nonInteractiveScrollbar, on the scrolling component: with VerticalScroll /
    // HorizontalScroll it goes outside them (NonInteractiveScrollbar(m) passed to VerticalScroll),
    // or it is drawn along the scrolled content. `state` (a scroll state's
    // ScrollIndicator()) must outlive the modifier. Throws std::invalid_argument when
    // thumbMaxLengthFraction is outside 0..1. Call during composition for the default colour.
    ui::Modifier NonInteractiveScrollbar(ui::Modifier m, const foundation::ScrollIndicatorState& state, foundation::Orientation orientation,
                                         const NonInteractiveScrollbarOptions& options = {});

    namespace internal {
        // What a scrollbar drew last (tests): nothing, or its colours (alpha applied) and rects,
        // in the node's local pixels.
        struct ScrollbarFrame {
            bool drawn = false;
            ui::Color thumbColor, trackColor;
            ui::Rect thumb, track;
        };
        // The last frame of the first scrollbar on `node`, if it has one.
        std::optional<ScrollbarFrame> LastScrollbarFrame(const ui::LayoutNode& node);
    }

}
