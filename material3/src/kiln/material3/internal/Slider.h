#pragma once

#include "kiln/material3/Slider.h"

namespace kiln::material3::internal {
    inline constexpr ui::AlignmentLine SliderCornerSizeAlignmentLine{ ui::AlignmentLine::Merge::Min, false };
    ui::Modifier SliderGestures(ui::Modifier, SliderState&, std::shared_ptr<foundation::MutableInteractionSource>, bool enabled,
        std::function<void(float)> onChange, std::function<void()> finished);
    ui::Modifier RangeSliderGestures(ui::Modifier, RangeSliderState&, std::shared_ptr<foundation::MutableInteractionSource> start,
        std::shared_ptr<foundation::MutableInteractionSource> end, bool enabled,
        std::function<void(SliderRange)> onChange, std::function<void()> finished);
}
