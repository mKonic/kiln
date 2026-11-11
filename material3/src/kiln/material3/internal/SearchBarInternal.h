#pragma once

// Shared between SearchBar.cpp (state, behaviours, defaults, input fields) and
// SearchBarLayouts.cpp (the collapsed, expanded and expanded-flag bars).

#include "kiln/animation/Spec.h"
#include "kiln/ui/Geometry.h"

namespace kiln::material3::internal {

    inline constexpr int AnimationDelayMillis = 100;   // MotionTokens.DurationShort2
    inline constexpr ui::Dp SearchBarIconOffsetX{ 4 }, FullScreenExpandedHorizontalPadding{ 8 }, AppBarWithSearchHorizontalPadding{ 4 },
        SearchBarPredictiveBackMinMargin{ 8 }, SearchBarPredictiveBackMaxOffsetY{ 24 };
    inline constexpr float SearchBarPredictiveBackMinScale = 9.0f / 10.0f, SearchBarPredictiveBackMaxOffsetXRatio = 1.0f / 20.0f;
    inline constexpr float DockedExpandedTableMaxHeightScreenRatio = 2.0f / 3.0f, DockedExpandedWithGapTableMaxHeightScreenRatio = 1.0f / 2.0f;

    anim::SpecPtr AnimationEnterFloatSpec();
    anim::SpecPtr AnimationExitFloatSpec();
    anim::SpecPtr AnimationPredictiveBackExitFloatSpec();
    anim::SpecPtr AnimationForContentFadeInSpec();
    anim::SpecPtr AnimationForContentFadeOutSpec();
    ui::Dp WindowContainerHeight();
    int Lerp(int a, int b, float f);
    float PredictiveBackTransform(float progress);

}
