#pragma once

// animate*AsState (animation-core AnimateAsState.kt): remember an Animatable for this call site
// and animate it to the latest target whenever the target changes. Default spec spring().

#include "kiln/animation/Animatable.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/ColorSpace.h"

namespace kiln::foundation {

    float AnimateFloatAsState(float target, anim::SpecPtr spec = anim::Spring(), rt::Key key = {});
    // Visibility thresholds per type (VisibilityThresholds.kt): Dp 0.4, px values 1, Int 1.
    ui::Dp AnimateDpAsState(ui::Dp target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 0.4f), rt::Key key = {});
    ui::Offset AnimateOffsetAsState(ui::Offset target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f), rt::Key key = {});
    ui::Size AnimateSizeAsState(ui::Size target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f), rt::Key key = {});
    // Int converts back by truncation (VectorConverters.kt).
    int AnimateIntAsState(int target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f), rt::Key key = {});
    ui::Color AnimateColorAsState(ui::Color target, anim::SpecPtr spec = anim::Spring(), rt::Key key = {});

}
