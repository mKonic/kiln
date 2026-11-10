#include "kiln/material3/internal/AnimatedProgress.h"

namespace kiln::material3::internal {

    namespace {
        rt::Task<> AnimateTo(std::shared_ptr<anim::Animatable> value, float target, anim::SpecPtr spec) {
            co_await value->AnimateTo(target, std::move(spec));
        }
    }

    std::shared_ptr<anim::Animatable> AnimatedProgress(bool on, anim::SpecPtr spec, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto value = c.Remember<std::shared_ptr<anim::Animatable>>([on] { return std::make_shared<anim::Animatable>(on ? 1.0f : 0.0f); }, {}, key);
        c.LaunchedEffect(rt::Keys(on), [value, on, spec]() -> rt::Task<> { return AnimateTo(value, on ? 1.0f : 0.0f, spec); }, key);
        return value;
    }

}
