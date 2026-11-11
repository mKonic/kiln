#pragma once

// NavigationTransitions.kt: Material's navigation transitions for (Deferred)AnimatedContent,
// with the predictive back gesture's hand-set phase. Horizontal shared axis: pages slide 20%
// and fade, the outgoing one drawn below on the way back; during the gesture the leaving page
// shrinks towards its edge and the page below waits behind a scrim. Fade through: the old page
// fades out quickly, the new one fades and scales in.

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/ui/Back.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <memory>

namespace kiln::material3 {

    namespace HorizontalSharedAxisNavigationTransitions {
        // colorScheme.scrim at 20% (80% on a dark scheme, judged by onSurface).
        ui::Color scrimColor();
        foundation::ContentTransform forwardTransform();
        // The commit of a backward move: the leaving page draws below the arriving one.
        foundation::ContentTransform backwardTransform();
        // The gesture phase: the leaving page shrinks by up to 15% towards the swipe's far side
        // and follows the finger vertically (damped); the page behind waits shifted 20% to the
        // left, slightly smaller, under `scrimColor`.
        std::shared_ptr<foundation::MutableContentTransform> predictiveBackTransform(std::function<ui::BackEvent()> backEvent, const ui::Density& density,
                                                                                    ui::Color scrimColor);
    }

    namespace FadeThroughNavigationTransitions {
        foundation::ContentTransform transform();
        // The gesture phase: the leaving page shrinks about its centre and follows the finger;
        // the page behind stays hidden at 80% scale.
        std::shared_ptr<foundation::MutableContentTransform> predictiveBackTransform(std::function<ui::BackEvent()> backEvent, const ui::Density& density);
    }

    namespace internal {
        ui::IntOffset CalculateSlideOffset(ui::IntSize fullSize, float currentScale, float rawYDelta, float displayBoundsMargin,
                                           float pivotFractionX = 0.5f);
        // How far content shrunk to currentScale may follow a vertical drag: up to the room the
        // shrink leaves less the margin, easing out (quadratic) over half the height.
        float CalculateDampedVerticalOffset(float rawYDelta, float containerHeight, float currentScale, float displayBoundsMargin);
    }

}
