#pragma once

// Transition (animation-core Transition.kt): a state machine from currentState to targetState
// whose child animations (AnimateFloat / AnimateColor / AnimateValue) run together on one clock.
// A new target restarts every child from its current value and velocity; a child interrupted
// mid-flight with a non-spring spec continues with the default spring (interruptionSpec).
// InfiniteTransition runs repeatable specs on a free clock.

#include "kiln/animation/Animatable.h"
#include "kiln/animation/Spec.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <memory>
#include <vector>

namespace kiln::foundation {

    // The type-erased part: children and the clock.
    class TransitionCore {
    public:
        struct Child {
            rt::State<anim::Vec, rt::NeverEqual> value;
            anim::Vec start{}, target{}, velocity{}, startVelocity{};
            anim::SpecPtr spec;
            int dims = 1;
            std::int64_t duration = 0;
            bool finished = true;
            explicit Child(anim::Vec v, int d) : value(v), start(v), target(v), dims(d) {}
            anim::Vec Current() const { return value.Peek(); }
        };

        // Retargets a child (when its target value or the transition's target changed).
        void Retarget(Child& c, const anim::Vec& target, anim::SpecPtr spec, bool snap);
        void Add(Child* c) { m_Children.push_back(c); }
        void Remove(Child* c) { std::erase(m_Children, c); }

        // Starts a new segment: play time restarts for every child.
        void Begin();
        // One frame: advances play time; true while anything still runs.
        bool Step(std::int64_t frameNanos);
        bool Running() const { return m_Running.Peek(); }
        bool RunningObserved() const { return m_Running.Get(); }
        std::int64_t PlayTimeNanos() const { return m_PlayTime; }
        std::uint64_t Generation() const { return m_Generation.Get(); }

    private:
        std::vector<Child*> m_Children;
        std::int64_t m_StartNanos = -1, m_PlayTime = 0;
        rt::State<bool> m_Running{ false };
        rt::State<std::uint64_t> m_Generation{ 0 };
    };

    rt::Task<> RunTransition(TransitionCore& core);

    // MutableTransitionState: lets a transition start somewhere other than its target (enter
    // animations on first composition).
    template<typename S>
    class MutableTransitionState {
    public:
        explicit MutableTransitionState(S initial) : m_Current(initial), m_Target(initial) {}
        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        void SetTargetState(S s) { m_Target.Set(std::move(s)); }
        bool IsIdle() const { return m_Current.Get() == m_Target.Get() && !m_Running; }
        // internal
        rt::State<S> m_Current, m_Target;
        bool m_Running = false;
    };

    template<typename S>
    class Transition {
    public:
        struct Segment {
            S initialState, targetState;
            bool IsTransitioningTo(const S& from, const S& to) const { return initialState == from && targetState == to; }
        };
        explicit Transition(S initial) : m_Current(initial), m_Target(initial) {}

        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        Segment CurrentSegment() const { return { m_Current.Peek(), m_Target.Peek() }; }
        bool IsRunning() const { return m_Core.RunningObserved(); }
        TransitionCore& Core() { return m_Core; }

        // Called by UpdateTransition each composition.
        void UpdateTarget(const S& target) {
            if (target == m_Target.Peek()) return;
            m_Target.Set(target);
            m_Core.Begin();
        }
        void Finish() { if (!(m_Current.Peek() == m_Target.Peek())) m_Current.Set(m_Target.Peek()); }

    private:
        rt::State<S> m_Current, m_Target;
        TransitionCore m_Core;
    };

    namespace detail {
        rt::Task<> DriveTransition(TransitionCore& core, std::function<void()> onFinished);
    }

    // updateTransition(targetState)
    template<typename S>
    Transition<S>& UpdateTransition(const S& target, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<Transition<S>>({}, key, target);
        t.UpdateTarget(target);
        auto* tp = &t;
        c.LaunchedEffect(rt::Keys(t.Core().Generation()), [tp] {
            return detail::DriveTransition(tp->Core(), [tp] { tp->Finish(); });
        }, key);
        return t;
    }

    // updateTransition(transitionState): follows the state object's target.
    template<typename S>
    Transition<S>& UpdateTransition(MutableTransitionState<S>& state, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<Transition<S>>({}, key, state.CurrentState());
        t.UpdateTarget(state.TargetState());
        auto* tp = &t;
        auto* sp = &state;
        sp->m_Running = t.Core().Running();
        c.LaunchedEffect(rt::Keys(t.Core().Generation()), [tp, sp] {
            return detail::DriveTransition(tp->Core(), [tp, sp] {
                tp->Finish();
                sp->m_Running = false;
                sp->m_Current.Set(tp->TargetState());
            });
        }, key);
        return t;
    }

    namespace detail {
        // A child animation of `t` towards valueOf(target) with specFor(segment).
        template<typename S>
        const anim::Vec& AnimateChild(Transition<S>& t, int dims, const std::function<anim::Vec(const S&)>& valueOf,
                                      const std::function<anim::SpecPtr(const typename Transition<S>::Segment&)>& specFor, rt::Key key) {
            auto& c = *rt::Composition::Current();
            const anim::Vec initial = valueOf(t.CurrentState());
            auto& child = c.RememberNew<TransitionCore::Child>({}, key, initial, dims);
            auto* core = &t.Core();
            auto* cp = &child;
            c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(core)), [core, cp] {
                core->Add(cp);
                return std::function<void()>([core, cp] { core->Remove(cp); });
            }, key);
            const anim::Vec target = valueOf(t.TargetState());
            if (!(target == child.target)) core->Retarget(child, target, specFor ? specFor(t.CurrentSegment()) : anim::Spring(), false);
            return child.value.Get();
        }
    }

    using SegmentSpec = std::function<anim::SpecPtr()>;

    template<typename S>
    float AnimateFloat(Transition<S>& t, std::function<float(const S&)> valueOf,
                       std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return detail::AnimateChild<S>(t, 1, [&](const S& s) { return anim::Vec{ valueOf(s), 0, 0, 0 }; }, spec, key)[0];
    }

    // Colours interpolate in Oklab (ColorVectorConverter).
    template<typename S>
    ui::Color AnimateColor(Transition<S>& t, std::function<ui::Color(const S&)> valueOf,
                           std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        const anim::Vec& v = detail::AnimateChild<S>(t, 4, [&](const S& s) {
            const auto lab = ui::ToOklab(valueOf(s));
            return anim::Vec{ lab[0], lab[1], lab[2], lab[3] };
        }, spec, key);
        return ui::FromOklab({ v[0], v[1], v[2], v[3] });
    }

    // --- InfiniteTransition --------------------------------------------------------------------

    class InfiniteTransition {
    public:
        rt::State<std::int64_t> playTime{ 0 };
    };
    rt::Task<> RunInfinite(InfiniteTransition& t);
    InfiniteTransition& RememberInfiniteTransition(rt::Key key = {});
    // animateFloat(initialValue, targetValue, infiniteRepeatable(...)): read during composition
    // or (better) layout/draw, where only that phase re-runs each frame.
    float AnimateFloat(InfiniteTransition& t, float initial, float target, anim::SpecPtr infiniteSpec);
    ui::Color AnimateColor(InfiniteTransition& t, ui::Color initial, ui::Color target, anim::SpecPtr infiniteSpec);

}
