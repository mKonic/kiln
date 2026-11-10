#pragma once

// animateFloatAsState(if (on) 1f else 0f), returned as the Animatable so callers read it where
// it is used (in layout or a graphics layer) instead of recomposing on every frame.

#include "kiln/animation/Animatable.h"
#include "kiln/runtime/Composition.h"

#include <memory>

namespace kiln::material3::internal {

    std::shared_ptr<anim::Animatable> AnimatedProgress(bool on, anim::SpecPtr spec, rt::Key key = {});
    // animateFloatAsState / animateDpAsState (in dp) for any target, read the same way.
    std::shared_ptr<anim::Animatable> AnimatedFloat(float target, anim::SpecPtr spec, rt::Key key = {});

}
