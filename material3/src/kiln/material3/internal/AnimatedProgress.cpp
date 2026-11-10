#include "kiln/material3/internal/AnimatedProgress.h"

namespace kiln::material3::internal {

    namespace {
        rt::Task<> AnimateTo(std::shared_ptr<anim::Animatable> value, float target, anim::SpecPtr spec) {
            co_await value->AnimateTo(target, std::move(spec));
        }
    }

    std::shared_ptr<anim::Animatable> AnimatedFloat(float target, anim::SpecPtr spec, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto value = c.Remember<std::shared_ptr<anim::Animatable>>([target] { return std::make_shared<anim::Animatable>(target); }, {}, key);
        c.LaunchedEffect(rt::Keys(target), [value, target, spec]() -> rt::Task<> { return AnimateTo(value, target, spec); }, key);
        return value;
    }
    std::shared_ptr<anim::Animatable> AnimatedProgress(bool on, anim::SpecPtr spec, rt::Key key) { return AnimatedFloat(on ? 1.0f : 0.0f, std::move(spec), key); }

}
