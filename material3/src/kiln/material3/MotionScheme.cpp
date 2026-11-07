#include "kiln/material3/MotionScheme.h"

#include "kiln/material3/tokens/ExpressiveMotionTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/StandardMotionTokens.h"

namespace kiln::material3 {

    namespace {
        // StandardMotionSchemeImpl / ExpressiveMotionSchemeImpl: springs built once.
        class SpringMotionScheme final : public MotionScheme {
        public:
            SpringMotionScheme(anim::SpecPtr ds, anim::SpecPtr fs, anim::SpecPtr ss, anim::SpecPtr de, anim::SpecPtr fe, anim::SpecPtr se)
                : m_DefaultSpatial(std::move(ds)), m_FastSpatial(std::move(fs)), m_SlowSpatial(std::move(ss)),
                  m_DefaultEffects(std::move(de)), m_FastEffects(std::move(fe)), m_SlowEffects(std::move(se)) {}
            anim::SpecPtr DefaultSpatialSpec() const override { return m_DefaultSpatial; }
            anim::SpecPtr FastSpatialSpec() const override { return m_FastSpatial; }
            anim::SpecPtr SlowSpatialSpec() const override { return m_SlowSpatial; }
            anim::SpecPtr DefaultEffectsSpec() const override { return m_DefaultEffects; }
            anim::SpecPtr FastEffectsSpec() const override { return m_FastEffects; }
            anim::SpecPtr SlowEffectsSpec() const override { return m_SlowEffects; }
        private:
            anim::SpecPtr m_DefaultSpatial, m_FastSpatial, m_SlowSpatial, m_DefaultEffects, m_FastEffects, m_SlowEffects;
        };
    }

    std::shared_ptr<const MotionScheme> MotionScheme::Standard() {
        namespace T = tokens::StandardMotionTokens;
        static const auto scheme = std::make_shared<const SpringMotionScheme>(
            anim::Spring(T::SpringDefaultSpatialDamping, T::SpringDefaultSpatialStiffness),
            anim::Spring(T::SpringFastSpatialDamping, T::SpringFastSpatialStiffness),
            anim::Spring(T::SpringSlowSpatialDamping, T::SpringSlowSpatialStiffness),
            anim::Spring(T::SpringDefaultEffectsDamping, T::SpringDefaultEffectsStiffness),
            anim::Spring(T::SpringFastEffectsDamping, T::SpringFastEffectsStiffness),
            anim::Spring(T::SpringSlowEffectsDamping, T::SpringSlowEffectsStiffness));
        return scheme;
    }

    std::shared_ptr<const MotionScheme> MotionScheme::Expressive() {
        namespace T = tokens::ExpressiveMotionTokens;
        static const auto scheme = std::make_shared<const SpringMotionScheme>(
            anim::Spring(T::SpringDefaultSpatialDamping, T::SpringDefaultSpatialStiffness),
            anim::Spring(T::SpringFastSpatialDamping, T::SpringFastSpatialStiffness),
            anim::Spring(T::SpringSlowSpatialDamping, T::SpringSlowSpatialStiffness),
            anim::Spring(T::SpringDefaultEffectsDamping, T::SpringDefaultEffectsStiffness),
            anim::Spring(T::SpringFastEffectsDamping, T::SpringFastEffectsStiffness),
            anim::Spring(T::SpringSlowEffectsDamping, T::SpringSlowEffectsStiffness));
        return scheme;
    }

    anim::SpecPtr FromToken(const MotionScheme& s, tokens::MotionSchemeToken t) {
        namespace K = tokens::MotionSchemeKeyTokens;
        if (t == K::DefaultSpatial) return s.DefaultSpatialSpec();
        if (t == K::FastSpatial) return s.FastSpatialSpec();
        if (t == K::SlowSpatial) return s.SlowSpatialSpec();
        if (t == K::DefaultEffects) return s.DefaultEffectsSpec();
        if (t == K::FastEffects) return s.FastEffectsSpec();
        if (t == K::SlowEffects) return s.SlowEffectsSpec();
        return s.DefaultSpatialSpec();
    }

}
