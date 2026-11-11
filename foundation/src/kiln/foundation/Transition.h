#pragma once

// Transition (animation-core Transition.kt): a state machine from currentState to targetState
// whose child animations (AnimateFloat / AnimateColor / AnimateValue) run together on one clock.
// A new target restarts every child from its current value and velocity; a child interrupted
// mid-flight with a non-spring spec continues with the default spring (interruptionSpec).
// InfiniteTransition runs repeatable specs on a free clock.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/animation/Spec.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/Geometry.h"

#include <algorithm>
#include <functional>
#include <optional>
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
            // The forced initial value last restarted from (a deferred phase's hand-off).
            std::optional<anim::Vec> forcedFrom;
            explicit Child(anim::Vec v, int d) : value(v), start(v), target(v), dims(d) {}
            anim::Vec Current() const { return value.Peek(); }
        };

        // Retargets a child (when its target value or the transition's target changed). A child
        // restarted from a forced value is not treated as interrupted.
        void Retarget(Child& c, const anim::Vec& target, anim::SpecPtr spec, bool snap, bool forced = false);
        void Add(Child* c) { m_Children.push_back(c); }
        void Remove(Child* c) { std::erase(m_Children, c); }

        // Starts a new segment: play time restarts for every child.
        void Begin();
        // One frame: advances play time; true while anything still runs.
        bool Step(std::int64_t frameNanos);
        // Seeking: children take their values at an explicit play time; no clock runs.
        void SetPlayTime(std::int64_t nanos);
        std::int64_t TotalDurationNanos() const;
        // Continue on the clock from the current play time (after seeking).
        void ResumeClock() { m_ResumeFrom = m_PlayTime; m_StartNanos = -1; if (!m_Running.Peek()) m_Running.Set(true); m_Generation.Set(m_Generation.Peek() + 1); }
        void PauseClock() { if (m_Running.Peek()) m_Running.Set(false); }
        // While seeking, retargets do not start the clock.
        void SetSeeking(bool s) { m_Seeking = s; if (s) PauseClock(); }
        bool Seeking() const { return m_Seeking; }
        bool Running() const { return m_Running.Peek(); }
        bool RunningObserved() const { return m_Running.Get(); }
        std::int64_t PlayTimeNanos() const { return m_PlayTime; }
        std::uint64_t Generation() const { return m_Generation.Get(); }

    private:
        std::vector<Child*> m_Children;
        std::int64_t m_StartNanos = -1, m_PlayTime = 0, m_ResumeFrom = 0;
        bool m_Seeking = false;
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

    // DeferredTransitionState: Defer holds a new target as pending (the transition keeps its
    // target, so its content can be moved by hand, e.g. by a predictive back gesture) until
    // AnimateTo starts the automatic transition, to that state or another.
    template<typename S>
    class DeferredTransitionState {
    public:
        explicit DeferredTransitionState(S initial) : m_Current(initial), m_Target(initial) {}
        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        std::optional<S> PendingTargetState() const { return m_Pending.Get(); }
        void Defer(S target) {
            std::optional<S> next;
            if (!(m_Target.Peek() == target)) next = std::move(target);
            if (!(m_Pending.Peek() == next)) m_Pending.Set(std::move(next));
        }
        void AnimateTo(S target) {
            if (m_Pending.Peek()) m_Pending.Set(std::nullopt);
            if (!(m_Target.Peek() == target)) m_Target.Set(std::move(target));
        }
        // internal
        rt::State<S> m_Current, m_Target;
        rt::State<std::optional<S>> m_Pending{ std::nullopt };
    };

    template<typename S>
    class Transition {
    public:
        struct Segment {
            S initialState, targetState;
            bool IsTransitioningTo(const S& from, const S& to) const { return initialState == from && targetState == to; }
        };
        explicit Transition(S initial) : m_Current(initial), m_Target(initial) {}
        Transition(const Transition&) = delete;
        Transition& operator=(const Transition&) = delete;

        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        // Set while a deferred phase holds a new target (DeferredTransitionState, or a child of
        // such a transition).
        std::optional<S> PendingTargetState() const { return m_Pending.Get(); }
        bool IsDeferred() const { return m_Deferred; }
        Segment CurrentSegment() const { return { m_Current.Peek(), m_Target.Peek() }; }
        bool IsRunning() const { return Core().RunningObserved(); }
        // A child transition runs its animations on its parent's clock.
        TransitionCore& Core() { return m_Parent ? *m_Parent : m_Core; }
        const TransitionCore& Core() const { return m_Parent ? *m_Parent : m_Core; }

        // createChildTransition: states come from the parent each composition, no clock of its own.
        void AttachToParent(TransitionCore& parent, std::function<bool()> parentSnap) {
            m_Parent = &parent;
            m_ParentSnap = std::move(parentSnap);
        }
        void SetStates(const S& current, const S& target) {
            if (!(current == m_Current.Peek())) m_Current.Set(current);
            if (!(target == m_Target.Peek())) m_Target.Set(target);
        }

        // Called by UpdateTransition each composition.
        void UpdateTarget(const S& target) {
            if (target == m_Target.Peek()) return;
            m_Target.Set(target);
            m_Core.Begin();
        }
        void Finish() { if (!(m_Current.Peek() == m_Target.Peek())) m_Current.Set(m_Target.Peek()); }

        // updatePendingTarget: a deferred phase that ends going back to the current state starts
        // from the pending state, so hand-moved content animates back (or settles at once).
        void UpdatePendingTarget(std::optional<S> value, rt::State<S>* stateCurrent = nullptr) {
            const std::optional<S> previous = m_Pending.Peek();
            const bool cleared = previous && !value && m_Target.Peek() == m_Current.Peek();
            if (!(previous == value)) m_Pending.Set(value);
            if (cleared) {
                m_Current.Set(*previous);
                if (stateCurrent) stateCurrent->Set(*previous);
                Core().Begin();
            }
        }
        void MarkDeferred() { m_Deferred = true; }
        // snapTo: both states jump; children follow on their next composition.
        void SnapAll(const S& s) { m_Current.Set(s); m_Target.Set(s); m_Core.PauseClock(); m_SnapNext = true; }
        bool TakeSnap() { return std::exchange(m_SnapNext, false); }
        bool PeekSnap() const { return m_SnapNext || (m_ParentSnap && m_ParentSnap()); }

    private:
        rt::State<S> m_Current, m_Target;
        rt::State<std::optional<S>> m_Pending{ std::nullopt };
        bool m_Deferred = false;
        TransitionCore m_Core;
        TransitionCore* m_Parent = nullptr;
        std::function<bool()> m_ParentSnap;
        bool m_SnapNext = false;
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

    // rememberDeferredTransition(transitionState): follows the state's target and pending target.
    template<typename S>
    Transition<S>& RememberDeferredTransition(DeferredTransitionState<S>& state, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<Transition<S>>({}, key, state.CurrentState());
        t.MarkDeferred();
        t.UpdateTarget(state.TargetState());
        t.UpdatePendingTarget(state.PendingTargetState(), &state.m_Current);
        auto* tp = &t;
        auto* sp = &state;
        c.LaunchedEffect(rt::Keys(t.Core().Generation()), [tp, sp] {
            return detail::DriveTransition(tp->Core(), [tp, sp] {
                tp->Finish();
                if (!(sp->m_Current.Peek() == tp->TargetState())) sp->m_Current.Set(tp->TargetState());
            });
        }, key);
        return t;
    }

    namespace detail {
        // A child animation of `t` towards valueOf(target) with specFor(segment).
        // forcedInitial: restart from this value (with forcedVelocity) when it is new or the target
        // changes (DeferredAnimation.animate's forcedInitialValue).
        template<typename S>
        const anim::Vec& AnimateChild(Transition<S>& t, int dims, const std::function<anim::Vec(const S&)>& valueOf,
                                      const std::function<anim::SpecPtr(const typename Transition<S>::Segment&)>& specFor, rt::Key key,
                                      std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
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
            const bool newForced = forcedInitial && !(child.forcedFrom == forcedInitial);
            if (!forcedInitial) child.forcedFrom.reset();
            if (!(target == child.target) || newForced) {
                if (forcedInitial) {
                    child.value.Set(*forcedInitial);
                    if (forcedVelocity) child.velocity = *forcedVelocity;
                    child.forcedFrom = forcedInitial;
                }
                core->Retarget(child, target, specFor ? specFor(t.CurrentSegment()) : anim::Spring(), t.PeekSnap(), forcedInitial.has_value());
            }
            return child.value.Get();
        }
    }

    using SegmentSpec = std::function<anim::SpecPtr()>;

    // Transition.createChildTransition(transformToChildState): a transition over a derived
    // state. Its animations are part of the parent's: they share its clock, count towards its
    // running state and duration, and snap with it.
    template<typename T, typename S>
    Transition<T>& CreateChildTransition(Transition<S>& parent, std::function<T(const S&)> transform, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& child = c.RememberNew<Transition<T>>({}, key, transform(parent.CurrentState()));
        auto* pp = &parent;
        child.AttachToParent(parent.Core(), [pp] { return pp->PeekSnap(); });
        child.SetStates(transform(parent.CurrentState()), transform(parent.TargetState()));
        const std::optional<S> pending = parent.PendingTargetState();
        child.UpdatePendingTarget(pending ? std::optional<T>(transform(*pending)) : std::nullopt);
        if (parent.IsDeferred()) child.MarkDeferred();
        return child;
    }

    // Transition.animateValue(typeConverter, transitionSpec, targetValueByState)
    template<typename T, typename S>
    T AnimateValue(Transition<S>& t, const VectorConverter<T>& converter, std::function<T(const S&)> valueOf,
                   std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return converter.fromVector(detail::AnimateChild<S>(t, converter.dims, [&](const S& s) { return converter.toVector(valueOf(s)); }, spec, key));
    }
    template<typename S>
    ui::Dp AnimateDp(Transition<S>& t, std::function<ui::Dp(const S&)> valueOf,
                     std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Dp, S>(t, converters::Dp(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Offset AnimateOffset(Transition<S>& t, std::function<ui::Offset(const S&)> valueOf,
                             std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Offset, S>(t, converters::Offset(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Size AnimateSize(Transition<S>& t, std::function<ui::Size(const S&)> valueOf,
                         std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Size, S>(t, converters::Size(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Rect AnimateRect(Transition<S>& t, std::function<ui::Rect(const S&)> valueOf,
                         std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Rect, S>(t, converters::Rect(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    int AnimateInt(Transition<S>& t, std::function<int(const S&)> valueOf,
                   std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<int, S>(t, converters::Int(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::IntOffset AnimateIntOffset(Transition<S>& t, std::function<ui::IntOffset(const S&)> valueOf,
                                   std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::IntOffset, S>(t, converters::IntOffset(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::IntSize AnimateIntSize(Transition<S>& t, std::function<ui::IntSize(const S&)> valueOf,
                               std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::IntSize, S>(t, converters::IntSize(), std::move(valueOf), std::move(spec), key);
    }

    // forcedInitial / forcedVelocity: see detail::AnimateChild.
    template<typename S>
    float AnimateFloat(Transition<S>& t, std::function<float(const S&)> valueOf,
                       std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {},
                       std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
        return detail::AnimateChild<S>(t, 1, [&](const S& s) { return anim::Vec{ valueOf(s), 0, 0, 0 }; }, spec, key, forcedInitial, forcedVelocity)[0];
    }

    // Colours interpolate in Oklab (ColorVectorConverter).
    template<typename S>
    ui::Color AnimateColor(Transition<S>& t, std::function<ui::Color(const S&)> valueOf,
                           std::function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        const anim::Vec v = detail::AnimateChild<S>(t, 4, [&](const S& s) {
            const auto lab = ui::ToOklab(valueOf(s));
            return anim::Vec{ lab[0], lab[1], lab[2], lab[3] };
        }, spec, key);
        return ui::FromOklab({ v[0], v[1], v[2], v[3] });
    }

    // SeekableTransitionState: fraction 0..1 between currentState and targetState, set by
    // SeekTo (a back gesture's progress), then AnimateTo finishes on the clock or goes back.
    template<typename S>
    class SeekableTransitionState {
    public:
        explicit SeekableTransitionState(S initial) : m_Current(initial), m_Target(initial) {}
        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        float Fraction() const { return m_Fraction.Get(); }

        // seekTo(fraction, targetState)
        void SeekTo(float fraction, std::optional<S> target = std::nullopt) {
            m_Seeking = true;
            m_Fraction.Set(std::clamp(fraction, 0.0f, 1.0f));
            if (m_Core) m_Core->SetSeeking(true);
            if (target && !(*target == m_Target.Peek())) { m_Target.Set(*target); return; }   // children retarget on recomposition
            Apply();
        }
        // animateTo(targetState): to the current target it completes from the seeked fraction;
        // to the current state it goes back (the gesture was cancelled).
        void AnimateTo(std::optional<S> target = std::nullopt) {
            m_Seeking = false;
            if (m_Core) m_Core->SetSeeking(false);
            if (target && !(*target == m_Target.Peek())) { m_Target.Set(*target); return; }   // retarget with velocity
            if (m_Core) m_Core->ResumeClock();
        }
        void SnapTo(S target) {
            m_Seeking = false;
            m_Current.Set(target);
            m_Target.Set(target);
            m_Fraction.Set(0);
            m_Snap = true;
        }
        // internal
        void Apply() { if (m_Core) m_Core->SetPlayTime(std::int64_t(double(m_Core->TotalDurationNanos()) * m_Fraction.Peek())); }
        rt::State<S> m_Current, m_Target;
        rt::State<float> m_Fraction{ 0 };
        bool m_Seeking = false, m_Snap = false;
        TransitionCore* m_Core = nullptr;
    };

    template<typename S>
    Transition<S>& UpdateTransition(SeekableTransitionState<S>& state, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<Transition<S>>({}, key, state.CurrentState());
        auto* tp = &t;
        auto* sp = &state;
        state.m_Core = &t.Core();
        t.Core().SetSeeking(state.m_Seeking);
        if (state.m_Snap) { state.m_Snap = false; t.SnapAll(state.TargetState()); }
        else t.UpdateTarget(state.TargetState());
        // A new seek target: once the children have retargeted, apply the fraction.
        if (state.m_Seeking) c.SideEffect([sp] { sp->Apply(); });
        c.LaunchedEffect(rt::Keys(t.Core().Generation()), [tp, sp] {
            return detail::DriveTransition(tp->Core(), [tp, sp] {
                if (sp->m_Seeking) return;
                tp->Finish();
                sp->m_Current.Set(tp->TargetState());
                sp->m_Fraction.Set(0);
            });
        }, key);
        return t;
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
