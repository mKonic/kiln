#include "kiln/foundation/Animate.h"

namespace kiln::foundation {

    namespace {
        rt::Task<> Run(anim::Animatable* a, anim::Vec target, anim::SpecPtr spec) {
            co_await a->AnimateTo(target, spec);
        }

        const anim::Vec& Animate(anim::Vec target, int dims, anim::SpecPtr spec, rt::Key key) {
            auto& c = *rt::Composition::Current();
            auto& a = c.RememberNew<anim::Animatable>({}, key, target, dims);
            // Keys are the target components: a new target restarts (interrupting, keeping velocity).
            c.LaunchedEffect(rt::Keys(target[0], target[1], target[2], target[3]),
                             [&a, target, spec] { return Run(&a, target, spec); }, key);
            return a.Value();
        }
    }

    float AnimateFloatAsState(float target, anim::SpecPtr spec, rt::Key key) {
        return Animate({ target, 0, 0, 0 }, 1, std::move(spec), key)[0];
    }

    ui::Color AnimateColorAsState(ui::Color target, anim::SpecPtr spec, rt::Key key) {
        const auto lab = ui::ToOklab(target);
        const anim::Vec& v = Animate({ lab[0], lab[1], lab[2], lab[3] }, 4, std::move(spec), key);
        return ui::FromOklab({ v[0], v[1], v[2], v[3] });
    }

}
