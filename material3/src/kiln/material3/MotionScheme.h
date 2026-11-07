#pragma once

// MotionScheme (material3 MotionScheme.kt): the theme's springs for spatial movement and for
// effects (colour, alpha), each fast, default and slow. Standard and expressive schemes.

#include "kiln/animation/Spec.h"
#include "kiln/material3/tokens/TokenTypes.h"

#include <memory>

namespace kiln::material3 {

    class MotionScheme {
    public:
        virtual ~MotionScheme() = default;
        virtual anim::SpecPtr DefaultSpatialSpec() const = 0;
        virtual anim::SpecPtr FastSpatialSpec() const = 0;
        virtual anim::SpecPtr SlowSpatialSpec() const = 0;
        virtual anim::SpecPtr DefaultEffectsSpec() const = 0;
        virtual anim::SpecPtr FastEffectsSpec() const = 0;
        virtual anim::SpecPtr SlowEffectsSpec() const = 0;

        static std::shared_ptr<const MotionScheme> Standard();
        static std::shared_ptr<const MotionScheme> Expressive();
    };
    using MotionSchemePtr = std::shared_ptr<const MotionScheme>;

    // MotionScheme.fromToken (MotionSchemeKeyTokens id to spec; default spatial for an unknown id).
    anim::SpecPtr FromToken(const MotionScheme& scheme, tokens::MotionSchemeToken token);

}
