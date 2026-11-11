#include "kiln/foundation/Transition.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::foundation {

    namespace {
        bool IsSpring(const anim::SpecPtr& s) {
            // Springs report a finite duration that depends on velocity; tweens and keyframes do
            // not. Probe: a spring's duration changes with the initial velocity.
            const anim::Vec a{ 0, 0, 0, 0 }, b{ 1, 0, 0, 0 };
            return s && s->DurationNanos(a, b, anim::Vec{ 10, 0, 0, 0 }, 1) != s->DurationNanos(a, b, anim::Vec{}, 1);
        }
    }

    void TransitionCore::Retarget(Child& c, const anim::Vec& target, anim::SpecPtr spec, bool snap, bool forced) {
        const bool interrupted = !forced && !c.finished && (m_Running.Peek() || m_Seeking);
        c.start = c.value.Peek();
        c.startVelocity = c.velocity;
        c.target = target;
        // Transition.kt: an interrupted child keeps moving with a spring unless its spec is one or
        // brings its own interruption spec.
        const auto* aware = dynamic_cast<const anim::InterruptionAwareSpec*>(spec.get());
        if (interrupted && aware) c.spec = aware->interruptionSpec;
        else if (aware) c.spec = aware->animationSpec;
        else c.spec = interrupted && !IsSpring(spec) ? anim::Spring() : (spec ? spec : anim::Spring());
        c.duration = c.spec->DurationNanos(c.start, c.target, c.startVelocity, c.dims);
        c.finished = snap;
        if (snap) { c.value.Set(target); c.velocity = {}; }
        Begin();
    }

    void TransitionCore::Begin() {
        m_StartNanos = -1;
        m_PlayTime = 0;
        m_ResumeFrom = 0;
        // Children start from where they are now.
        for (Child* c : m_Children) {
            if (!c->finished) {
                c->start = c->value.Peek();
                c->startVelocity = c->velocity;
                c->duration = c->spec->DurationNanos(c->start, c->target, c->startVelocity, c->dims);
            }
        }
        if (!m_Running.Peek()) m_Running.Set(true);
        m_Generation.Set(m_Generation.Peek() + 1);
    }

    std::int64_t TransitionCore::TotalDurationNanos() const {
        std::int64_t d = 0;
        for (const Child* c : m_Children) d = std::max(d, c->duration);
        return d;
    }

    void TransitionCore::SetPlayTime(std::int64_t t) {
        m_PlayTime = t;
        for (Child* c : m_Children) {
            if (c->finished && c->Current() == c->target) continue;
            if (t >= c->duration) { c->value.Set(c->target); c->velocity = {}; continue; }
            c->finished = false;
            c->value.Set(c->spec->ValueAt(t, c->start, c->target, c->startVelocity, c->dims));
            c->velocity = c->spec->VelocityAt(t, c->start, c->target, c->startVelocity, c->dims);
        }
    }

    bool TransitionCore::Step(std::int64_t frame) {
        // Resuming after a seek: the clock continues from the seeked play time.
        if (m_StartNanos < 0) { m_StartNanos = frame - m_ResumeFrom; m_ResumeFrom = 0; }
        const float scale = anim::DurationScale();
        m_PlayTime = scale <= 0 ? std::numeric_limits<std::int64_t>::max() / 4 : std::int64_t(double(frame - m_StartNanos) / scale);
        bool any = false;
        for (Child* c : m_Children) {
            if (c->finished) continue;
            if (m_PlayTime >= c->duration) {
                c->value.Set(c->target);
                c->velocity = {};
                c->finished = true;
                continue;
            }
            c->value.Set(c->spec->ValueAt(m_PlayTime, c->start, c->target, c->startVelocity, c->dims));
            c->velocity = c->spec->VelocityAt(m_PlayTime, c->start, c->target, c->startVelocity, c->dims);
            any = true;
        }
        if (!any && m_Running.Peek()) m_Running.Set(false);
        return any;
    }

    namespace detail {
        rt::Task<> DriveTransition(TransitionCore& core, std::function<void()> onFinished) {
            while (core.Running()) {
                const std::int64_t t = co_await rt::NextFrame();
                if (!core.Step(t)) break;
            }
            onFinished();
        }
    }

    rt::Task<> RunInfinite(InfiniteTransition& t) {
        std::int64_t start = -1;
        for (;;) {
            const std::int64_t now = co_await rt::NextFrame();
            if (start < 0) start = now;
            const float scale = anim::DurationScale();
            t.playTime.Set(scale <= 0 ? 0 : std::int64_t(double(now - start) / scale));
        }
    }

    InfiniteTransition& RememberInfiniteTransition(rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<InfiniteTransition>({}, key);
        auto* tp = &t;
        c.LaunchedEffect({}, [tp] { return RunInfinite(*tp); }, key);
        return t;
    }

    float AnimateFloat(InfiniteTransition& t, float initial, float target, anim::SpecPtr spec) {
        return spec->ValueAt(t.playTime.Get(), { initial, 0, 0, 0 }, { target, 0, 0, 0 }, {}, 1)[0];
    }

    ui::Color AnimateColor(InfiniteTransition& t, ui::Color initial, ui::Color target, anim::SpecPtr spec) {
        const auto a = ui::ToOklab(initial), b = ui::ToOklab(target);
        const anim::Vec v = spec->ValueAt(t.playTime.Get(), { a[0], a[1], a[2], a[3] }, { b[0], b[1], b[2], b[3] }, {}, 4);
        return ui::FromOklab({ v[0], v[1], v[2], v[3] });
    }

}
