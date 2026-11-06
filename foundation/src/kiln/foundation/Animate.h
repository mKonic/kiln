#pragma once

// animate*AsState (animation-core AnimateAsState.kt): remember an Animatable for this call site
// and animate it to the latest target whenever the target changes. Default spec spring().

#include "kiln/animation/Animatable.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/ColorSpace.h"

namespace kiln::foundation {

    float AnimateFloatAsState(float target, anim::SpecPtr spec = anim::Spring(), rt::Key key = {});
    ui::Color AnimateColorAsState(ui::Color target, anim::SpecPtr spec = anim::Spring(), rt::Key key = {});

}
