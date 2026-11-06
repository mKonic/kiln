#include "kiln/foundation/Animate.h"

#include <algorithm>
#include <memory>

namespace kiln::foundation {

    namespace converters {
        namespace { int Round(float v) { return int(std::lround(v)); } }
        VectorConverter<float> Float() { return { 1, [](const float& v) { return anim::Vec{ v, 0, 0, 0 }; }, [](const anim::Vec& v) { return v[0]; } }; }
        VectorConverter<int> Int() { return { 1, [](const int& v) { return anim::Vec{ float(v), 0, 0, 0 }; }, [](const anim::Vec& v) { return int(v[0]); } }; }
        VectorConverter<ui::Dp> Dp() { return { 1, [](const ui::Dp& v) { return anim::Vec{ v.value, 0, 0, 0 }; }, [](const anim::Vec& v) { return ui::Dp(v[0]); } }; }
        VectorConverter<ui::Offset> Offset() {
            return { 2, [](const ui::Offset& o) { return anim::Vec{ o.x, o.y, 0, 0 }; }, [](const anim::Vec& v) { return ui::Offset{ v[0], v[1] }; } };
        }
        VectorConverter<ui::Size> Size() {
            return { 2, [](const ui::Size& s) { return anim::Vec{ s.width, s.height, 0, 0 }; }, [](const anim::Vec& v) { return ui::Size{ v[0], v[1] }; } };
        }
        VectorConverter<ui::Rect> Rect() {
            return { 4, [](const ui::Rect& r) { return anim::Vec{ r.left, r.top, r.right, r.bottom }; },
                     [](const anim::Vec& v) { return ui::Rect{ v[0], v[1], v[2], v[3] }; } };
        }
        VectorConverter<ui::IntOffset> IntOffset() {
            return { 2, [](const ui::IntOffset& o) { return anim::Vec{ float(o.x), float(o.y), 0, 0 }; },
                     [](const anim::Vec& v) { return ui::IntOffset{ Round(v[0]), Round(v[1]) }; } };
        }
        VectorConverter<ui::IntSize> IntSize() {
            return { 2, [](const ui::IntSize& s) { return anim::Vec{ float(s.width), float(s.height), 0, 0 }; },
                     [](const anim::Vec& v) { return ui::IntSize{ std::max(0, Round(v[0])), std::max(0, Round(v[1])) }; } };
        }
        VectorConverter<ui::Color> Color() {
            return { 4, [](const ui::Color& c) { const auto lab = ui::ToOklab(c); return anim::Vec{ lab[0], lab[1], lab[2], lab[3] }; },
                     [](const anim::Vec& v) { return ui::FromOklab({ v[0], v[1], v[2], v[3] }); } };
        }
    }

    namespace detail {
        namespace {
            struct Listener { std::function<void(const anim::Vec&)> finished; };

            rt::Task<> Run(anim::Animatable* a, anim::Vec target, anim::SpecPtr spec, std::shared_ptr<Listener> listener) {
                const auto result = co_await a->AnimateTo(target, spec);
                // Only an animation that reached its target; a new target cancels this one first.
                if (result.reason == anim::EndReason::Finished && listener->finished) listener->finished(a->Value());
            }
        }

        const anim::Vec& AnimateVector(const anim::Vec& target, int dims, anim::SpecPtr spec,
                                       std::function<void(const anim::Vec&)> finished, rt::Key key) {
            auto& c = *rt::Composition::Current();
            auto& a = c.RememberNew<anim::Animatable>({}, key, target, dims);
            auto& listener = c.RememberNew<std::shared_ptr<Listener>>({}, key, std::make_shared<Listener>());
            listener->finished = std::move(finished);
            // Keys are the target components: a new target restarts (interrupting, keeping velocity).
            c.LaunchedEffect(rt::Keys(target[0], target[1], target[2], target[3]),
                             [&a, target, spec, l = listener] { return Run(&a, target, spec, l); }, key);
            return a.Value();
        }
    }

    float AnimateFloatAsState(float target, anim::SpecPtr spec, std::function<void(float)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Float(), std::move(spec), std::move(f), key);
    }
    ui::Dp AnimateDpAsState(ui::Dp target, anim::SpecPtr spec, std::function<void(ui::Dp)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Dp(), std::move(spec), std::move(f), key);
    }
    ui::Offset AnimateOffsetAsState(ui::Offset target, anim::SpecPtr spec, std::function<void(ui::Offset)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Offset(), std::move(spec), std::move(f), key);
    }
    ui::Size AnimateSizeAsState(ui::Size target, anim::SpecPtr spec, std::function<void(ui::Size)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Size(), std::move(spec), std::move(f), key);
    }
    ui::Rect AnimateRectAsState(ui::Rect target, anim::SpecPtr spec, std::function<void(ui::Rect)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Rect(), std::move(spec), std::move(f), key);
    }
    int AnimateIntAsState(int target, anim::SpecPtr spec, std::function<void(int)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Int(), std::move(spec), std::move(f), key);
    }
    ui::IntOffset AnimateIntOffsetAsState(ui::IntOffset target, anim::SpecPtr spec, std::function<void(ui::IntOffset)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::IntOffset(), std::move(spec), std::move(f), key);
    }
    ui::IntSize AnimateIntSizeAsState(ui::IntSize target, anim::SpecPtr spec, std::function<void(ui::IntSize)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::IntSize(), std::move(spec), std::move(f), key);
    }
    ui::Color AnimateColorAsState(ui::Color target, anim::SpecPtr spec, std::function<void(ui::Color)> f, rt::Key key) {
        return AnimateValueAsState(target, converters::Color(), std::move(spec), std::move(f), key);
    }

}
