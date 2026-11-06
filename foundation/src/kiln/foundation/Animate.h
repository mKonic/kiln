#pragma once

// animate*AsState (animation-core AnimateAsState.kt): remember an Animatable for this call site
// and animate it to the latest target whenever the target changes. Default spec spring().
// finishedListener hears the value when an animation runs to its end (not when a new target
// interrupts it). An InterruptionAware spec uses its interruption spec for a retarget mid-flight.

#include "kiln/animation/Animatable.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/ColorSpace.h"

#include <cmath>
#include <functional>

namespace kiln::foundation {

    // TwoWayConverter: a value to up to four animated components and back.
    template<typename T>
    struct VectorConverter {
        int dims = 1;
        std::function<anim::Vec(const T&)> toVector;
        std::function<T(const anim::Vec&)> fromVector;
    };

    // Float, Dp and px types convert as is; Int truncates; IntOffset and IntSize round (sizes
    // not below 0); Color animates in Oklab (VectorConverters.kt, ColorVectorConverter).
    namespace converters {
        VectorConverter<float> Float();
        VectorConverter<int> Int();
        VectorConverter<ui::Dp> Dp();
        VectorConverter<ui::Offset> Offset();
        VectorConverter<ui::Size> Size();
        VectorConverter<ui::Rect> Rect();
        VectorConverter<ui::IntOffset> IntOffset();
        VectorConverter<ui::IntSize> IntSize();
        VectorConverter<ui::Color> Color();
    }

    namespace detail {
        // The remembered animation: the latest listener, run to each new target.
        const anim::Vec& AnimateVector(const anim::Vec& target, int dims, anim::SpecPtr spec,
                                       std::function<void(const anim::Vec&)> finished, rt::Key key);
    }

    // animateValueAsState(targetValue, typeConverter, animationSpec, finishedListener)
    template<typename T>
    T AnimateValueAsState(const T& target, const VectorConverter<T>& converter, anim::SpecPtr spec = anim::Spring(),
                          std::function<void(T)> finishedListener = {}, rt::Key key = {}) {
        std::function<void(const anim::Vec&)> finished;
        if (finishedListener) finished = [converter, finishedListener](const anim::Vec& v) { finishedListener(converter.fromVector(v)); };
        return converter.fromVector(detail::AnimateVector(converter.toVector(target), converter.dims, std::move(spec), std::move(finished), key));
    }

    float AnimateFloatAsState(float target, anim::SpecPtr spec = anim::Spring(), std::function<void(float)> finishedListener = {}, rt::Key key = {});
    // Visibility thresholds per type (VisibilityThresholds.kt): Dp 0.4, px values 1, Int 1.
    ui::Dp AnimateDpAsState(ui::Dp target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 0.4f),
                            std::function<void(ui::Dp)> finishedListener = {}, rt::Key key = {});
    ui::Offset AnimateOffsetAsState(ui::Offset target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                                    std::function<void(ui::Offset)> finishedListener = {}, rt::Key key = {});
    ui::Size AnimateSizeAsState(ui::Size target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                                std::function<void(ui::Size)> finishedListener = {}, rt::Key key = {});
    ui::Rect AnimateRectAsState(ui::Rect target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                                std::function<void(ui::Rect)> finishedListener = {}, rt::Key key = {});
    int AnimateIntAsState(int target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                          std::function<void(int)> finishedListener = {}, rt::Key key = {});
    ui::IntOffset AnimateIntOffsetAsState(ui::IntOffset target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                                          std::function<void(ui::IntOffset)> finishedListener = {}, rt::Key key = {});
    ui::IntSize AnimateIntSizeAsState(ui::IntSize target, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f),
                                      std::function<void(ui::IntSize)> finishedListener = {}, rt::Key key = {});
    ui::Color AnimateColorAsState(ui::Color target, anim::SpecPtr spec = anim::Spring(),
                                  std::function<void(ui::Color)> finishedListener = {}, rt::Key key = {});

}
