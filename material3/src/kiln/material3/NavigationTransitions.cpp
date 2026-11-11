#include "kiln/material3/NavigationTransitions.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/SearchBarInternal.h"
#include "kiln/ui/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using ui::IntOffset;
    using ui::IntSize;

    namespace {
        constexpr float ScaleProgressMultiplier = 0.15f;
        constexpr ui::Dp DisplayBoundsMargin{ 8 };
    }

    namespace internal {
        IntOffset CalculateSlideOffset(IntSize fullSize, float currentScale, float rawYDelta, float displayBoundsMargin, float pivotFractionX) {
            const float horizontal = (1.0f - currentScale) * (pivotFractionX - 0.5f) * float(fullSize.width);
            const float vertical = CalculateDampedVerticalOffset(rawYDelta, float(fullSize.height), currentScale, displayBoundsMargin);
            return { int(horizontal), int(vertical) };
        }

        float CalculateDampedVerticalOffset(float rawYDelta, float containerHeight, float currentScale, float displayBoundsMargin) {
            if (containerHeight <= 0) return 0;
            const float maxDeltaY = std::max(0.0f, (containerHeight - containerHeight * currentScale) / 2.0f - displayBoundsMargin);
            const float ratio = std::min(1.0f, std::abs(rawYDelta) / (containerHeight / 2.0f));
            const float interpolated = 1.0f - (1.0f - ratio) * (1.0f - ratio);   // quadratic decelerate
            return maxDeltaY * interpolated * (rawYDelta < 0 ? -1.0f : 1.0f);
        }
    }

    namespace HorizontalSharedAxisNavigationTransitions {
        namespace {
            constexpr float ScrimAlphaLight = 0.2f, ScrimAlphaDark = 0.8f, SlideFraction = 0.2f, TargetScaleDelta = 0.05f;
            constexpr float BackwardTargetContentZIndex = -10, PivotFractionXRightEdge = 0.5f, PivotFractionXDefault = 0.9f;
            anim::SpecPtr FadeAnimationSpec() { return anim::Tween(90, 50, anim::Linear()); }
        }

        ui::Color scrimColor() {
            const bool isDark = ui::Luminance(MaterialTheme::colorScheme().onSurface) > 0.5f;
            return MaterialTheme::colorScheme().scrim.WithAlpha(isDark ? ScrimAlphaDark : ScrimAlphaLight);
        }

        ContentTransform forwardTransform() {
            return TogetherWith(SlideInHorizontally([](int w) { return int(float(w) * SlideFraction); }) + FadeIn(FadeAnimationSpec()),
                                SlideOutHorizontally([](int w) { return int(float(-w) * SlideFraction); }) + FadeOut(FadeAnimationSpec()));
        }

        ContentTransform backwardTransform() {
            ContentTransform t = TogetherWith(SlideInHorizontally([](int w) { return int(float(-w) * SlideFraction); }) + FadeIn(FadeAnimationSpec()),
                                              SlideOutHorizontally([](int w) { return int(float(w) * SlideFraction); }) + ScaleOut(DefaultEnterExitSpring(), 1.0f) +
                                                  FadeOut(FadeAnimationSpec()));
            t.targetContentZIndex = BackwardTargetContentZIndex;
            return t;
        }

        std::shared_ptr<MutableContentTransform> predictiveBackTransform(std::function<ui::BackEvent()> backEvent, const ui::Density& density,
                                                                         ui::Color scrim) {
            const float margin = density.ToPx(DisplayBoundsMargin);
            const float initialTouchY = backEvent().touchY;
            return MakeMutableContentTransform(false, true, {}, {}, [=](MutableContentTransform& m) {
                m.InitialContentTransform([=](TransformScope& s, IntSize fullSize) {
                    const ui::BackEvent event = backEvent();
                    const float rawYDelta = event.touchY - initialTouchY;
                    const float eased = internal::PredictiveBackTransform(event.progress);
                    const float currentScale = 1.0f - eased * ScaleProgressMultiplier;
                    const float pivotX = event.swipeEdge == ui::SwipeEdge::Right ? PivotFractionXRightEdge : PivotFractionXDefault;
                    s.SetAlpha(1);
                    s.SetScale(currentScale);
                    s.SetOffset(internal::CalculateSlideOffset(fullSize, currentScale, rawYDelta, margin, pivotX));
                });
                m.TargetContentTransform([=](TransformScope& s, IntSize fullSize) {
                    const ui::BackEvent event = backEvent();
                    const float rawYDelta = event.touchY - initialTouchY;
                    const float eased = internal::PredictiveBackTransform(event.progress);
                    const float baseScale = 1.0f - eased * ScaleProgressMultiplier;
                    s.SetAlpha(1);
                    s.SetVeil(scrim);
                    s.SetScale(baseScale - TargetScaleDelta);
                    const IntOffset vertical = internal::CalculateSlideOffset(fullSize, baseScale, rawYDelta, margin);
                    s.SetOffset({ int(-SlideFraction * float(fullSize.width)), vertical.y });
                });
            });
        }
    }

    namespace FadeThroughNavigationTransitions {
        namespace {
            constexpr int FadeOutDurationMillis = 90, FadeInDurationMillis = 210;
            constexpr float InitialScale = 0.8f, TargetScale = 1.05f;
        }

        ContentTransform transform() {
            return TogetherWith(FadeIn(anim::Tween(FadeInDurationMillis, FadeOutDurationMillis, anim::Linear())) + ScaleIn(DefaultEnterExitSpring(), InitialScale),
                                FadeOut(anim::Tween(FadeOutDurationMillis, 0, anim::Linear())) + ScaleOut(DefaultEnterExitSpring(), TargetScale));
        }

        std::shared_ptr<MutableContentTransform> predictiveBackTransform(std::function<ui::BackEvent()> backEvent, const ui::Density& density) {
            const float margin = density.ToPx(DisplayBoundsMargin);
            const float initialTouchY = backEvent().touchY;
            return MakeMutableContentTransform(false, false, {}, {}, [=](MutableContentTransform& m) {
                m.InitialContentTransform([=](TransformScope& s, IntSize fullSize) {
                    const ui::BackEvent event = backEvent();
                    const float rawYDelta = event.touchY - initialTouchY;
                    const float currentScale = 1.0f - internal::PredictiveBackTransform(event.progress) * ScaleProgressMultiplier;
                    s.SetAlpha(1);
                    s.SetScale(currentScale);
                    s.SetTransformOrigin(TransformOrigin::Center());
                    s.SetOffset(internal::CalculateSlideOffset(fullSize, currentScale, rawYDelta, margin));
                });
                m.TargetContentTransform([](TransformScope& s, IntSize) {
                    s.SetAlpha(0);
                    s.SetScale(InitialScale);
                    s.SetTransformOrigin(TransformOrigin::Center());
                });
            });
        }
    }

}
