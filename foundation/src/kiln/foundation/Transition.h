#pragma once

// Transition (animation-core Transition.kt): a state machine from currentState to targetState
// whose child animations (AnimateFloat / AnimateColor / AnimateValue) run together on one clock.
// A new target restarts every child from its current value and velocity; a child interrupted
// mid-flight with a non-spring spec continues with the default spring (interruptionSpec).
// InfiniteTransition runs repeatable specs on a free clock.

#include "kiln/animation/Animatable.h"
#include <kiln/Function.h>
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/MutatorMutex.h"
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
        static std::int64_t DurationOf(const Child& c);
        // Continue on the clock from the current play time (after seeking).
        void ResumeClock() { m_ResumeFrom = m_PlayTime; m_StartNanos = -1; if (!m_Running.Peek()) m_Running.Set(true); m_Generation.Set(m_Generation.Peek() + 1); }
        void PauseClock() { if (m_Running.Peek()) m_Running.Set(false); }
        // onTransitionEnd: every child rests at its target, play time 0.
        void Complete();
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
        void AttachToParent(TransitionCore& parent, kiln::Function<bool()> parentSnap) {
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
        kiln::Function<bool()> m_ParentSnap;
        bool m_SnapNext = false;
    };

    namespace detail {
        rt::Task<> DriveTransition(TransitionCore& core, kiln::Function<void()> onFinished);
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
        // Retargets `child` to `target` (or restarts it from a new forced value).
        template<typename S>
        void UpdateChildTarget(Transition<S>& t, TransitionCore::Child& child, const anim::Vec& target,
                               const kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)>& specFor,
                               std::optional<anim::Vec> forcedInitial, std::optional<anim::Vec> forcedVelocity) {
            const bool newForced = forcedInitial && !(child.forcedFrom == forcedInitial);
            if (!forcedInitial) child.forcedFrom.reset();
            if (!(target == child.target) || newForced) {
                if (forcedInitial) {
                    child.value.Set(*forcedInitial);
                    if (forcedVelocity) child.velocity = *forcedVelocity;
                    child.forcedFrom = forcedInitial;
                }
                t.Core().Retarget(child, target, specFor ? specFor(t.CurrentSegment()) : anim::Spring(), t.PeekSnap(), forcedInitial.has_value());
            }
        }

        // A child animation of `t` towards valueOf(target) with specFor(segment).
        // forcedInitial: restart from this value (with forcedVelocity) when it is new or the target
        // changes (DeferredAnimation.animate's forcedInitialValue).
        // AnimateChildRef: the child animation itself, its value not read here (a State).
        template<typename S>
        TransitionCore::Child& AnimateChildRef(Transition<S>& t, int dims, const kiln::Function<anim::Vec(const S&)>& valueOf,
                                               const kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)>& specFor, rt::Key key,
                                               std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
            auto& c = *rt::Composition::Current();
            const anim::Vec initial = valueOf(t.CurrentState());
            auto& child = c.RememberNew<TransitionCore::Child>({}, key, initial, dims);
            auto* core = &t.Core();
            auto* cp = &child;
            c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(core)), [core, cp] {
                core->Add(cp);
                return kiln::Function<void()>([core, cp] { core->Remove(cp); });
            }, key);
            const anim::Vec target = valueOf(t.TargetState());
            // A seeking transition (SeekableTransitionState) is a function of its play time from
            // the current state's value to the target's: the initial value follows the current
            // state too (updateInitialAndTargetValue).
            if (core->Seeking() && !forcedInitial && target == child.target && !(initial == child.start) && !(t.CurrentState() == t.TargetState())) {
                child.start = initial;
                child.startVelocity = {};
                child.finished = false;
                if (!child.spec) child.spec = specFor ? specFor(t.CurrentSegment()) : anim::Spring();
                if (!child.spec) child.spec = anim::Spring();
                child.duration = child.spec->DurationNanos(child.start, child.target, {}, child.dims);
            }
            UpdateChildTarget(t, child, target, specFor, forcedInitial, forcedVelocity);
            return child;
        }

        template<typename S>
        const anim::Vec& AnimateChild(Transition<S>& t, int dims, const kiln::Function<anim::Vec(const S&)>& valueOf,
                                      const kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)>& specFor, rt::Key key,
                                      std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
            return AnimateChildRef<S>(t, dims, valueOf, specFor, key, forcedInitial, forcedVelocity).value.Get();
        }

    }

    // Transition.animateFloat / animateColor as State: the animation's value read where Value()
    // is called (a layout or draw lambda, a child scope), so the caller's scope does not
    // recompose on each frame. Valid while the call that made it stays composed.
    struct AnimatedFloat {
        const TransitionCore::Child* child = nullptr;
        float Value() const { return child->value.Get()[0]; }
        float operator()() const { return Value(); }
    };
    struct AnimatedColor {
        const TransitionCore::Child* child = nullptr;
        ui::Color Value() const {
            const anim::Vec& v = child->value.Get();
            return ui::FromOklab({ v[0], v[1], v[2], v[3] });
        }
        ui::Color operator()() const { return Value(); }
    };

    // Transition.createDeferredAnimation: an animation of the transition whose target is only
    // known later (in measure or placement). Animate starts it on its first call and retargets
    // it after; recompositions of the transition keep it following the last valueOf.
    template<typename S>
    class DeferredAnimation {
    public:
        using SpecFor = kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)>;
        using ValueOf = kiln::Function<anim::Vec(const S&)>;
        DeferredAnimation(Transition<S>& t, int dims) : m_Transition(&t), m_Dims(dims) {}
        ~DeferredAnimation() { Detach(); }
        // Leaves the transition's clock (its composition left; see CreateDeferredAnimation).
        void Detach() {
            if (m_Child && m_Core) m_Core->Remove(m_Child.get());
            m_Core = nullptr;
            m_Child.reset();
            if (m_HasData.Peek()) m_HasData.Set(false);
        }
        DeferredAnimation(const DeferredAnimation&) = delete;
        DeferredAnimation& operator=(const DeferredAnimation&) = delete;

        // animate(transitionSpec, forcedInitialValue, forcedInitialVelocity, targetValueByState).
        const anim::Vec& Animate(SpecFor specFor, ValueOf valueOf, std::optional<anim::Vec> forcedInitial = std::nullopt,
                                 std::optional<anim::Vec> forcedVelocity = std::nullopt) {
            m_SpecFor = std::move(specFor);
            m_ValueOf = std::move(valueOf);
            if (!m_Child) {
                m_Child = std::make_unique<TransitionCore::Child>(m_ValueOf(m_Transition->CurrentState()), m_Dims);
                m_Core = &m_Transition->Core();
                m_Core->Add(m_Child.get());
                m_HasData.Set(true);
            }
            Update(forcedInitial, forcedVelocity);
            return m_Child->value.Get();
        }
        // The value now (null before the first Animate).
        const anim::Vec* Value() const { return m_HasData.Get() ? &m_Child->value.Get() : nullptr; }
        // updateAnimationStates on a recomposition of the transition.
        void Update(std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
            if (!m_Child || !m_ValueOf) return;
            detail::UpdateChildTarget(*m_Transition, *m_Child, m_ValueOf(m_Transition->TargetState()), m_SpecFor, forcedInitial, forcedVelocity);
        }
        void Rebind(Transition<S>& t) { m_Transition = &t; }

    private:
        Transition<S>* m_Transition;
        int m_Dims;
        std::unique_ptr<TransitionCore::Child> m_Child;
        TransitionCore* m_Core = nullptr;
        SpecFor m_SpecFor;
        ValueOf m_ValueOf;
        rt::State<bool> m_HasData{ false };
    };

    template<typename S>
    DeferredAnimation<S>& CreateDeferredAnimation(Transition<S>& t, int dims = 4, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& a = c.Remember<std::shared_ptr<DeferredAnimation<S>>>([&] { return std::make_shared<DeferredAnimation<S>>(t, dims); }, rt::Keys(reinterpret_cast<std::uintptr_t>(&t)), key);
        a->Rebind(t);
        a->Update();
        // Leave the clock while the transition is still there (the slots may outlive it).
        std::weak_ptr<DeferredAnimation<S>> w = a;
        c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(a.get())), [w] {
            return kiln::Function<void()>([w] { if (auto p = w.lock()) p->Detach(); });
        }, key);
        return *a;
    }

    using SegmentSpec = kiln::Function<anim::SpecPtr()>;

    // Transition.createChildTransition(transformToChildState): a transition over a derived
    // state. Its animations are part of the parent's: they share its clock, count towards its
    // running state and duration, and snap with it.
    template<typename T, typename S>
    Transition<T>& CreateChildTransition(Transition<S>& parent, kiln::Function<T(const S&)> transform, rt::Key key = {}) {
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
    T AnimateValue(Transition<S>& t, const VectorConverter<T>& converter, kiln::Function<T(const S&)> valueOf,
                   kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return converter.fromVector(detail::AnimateChild<S>(t, converter.dims, [&](const S& s) { return converter.toVector(valueOf(s)); }, spec, key));
    }
    template<typename S>
    ui::Dp AnimateDp(Transition<S>& t, kiln::Function<ui::Dp(const S&)> valueOf,
                     kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Dp, S>(t, converters::Dp(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Offset AnimateOffset(Transition<S>& t, kiln::Function<ui::Offset(const S&)> valueOf,
                             kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Offset, S>(t, converters::Offset(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Size AnimateSize(Transition<S>& t, kiln::Function<ui::Size(const S&)> valueOf,
                         kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Size, S>(t, converters::Size(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::Rect AnimateRect(Transition<S>& t, kiln::Function<ui::Rect(const S&)> valueOf,
                         kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::Rect, S>(t, converters::Rect(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    int AnimateInt(Transition<S>& t, kiln::Function<int(const S&)> valueOf,
                   kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<int, S>(t, converters::Int(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::IntOffset AnimateIntOffset(Transition<S>& t, kiln::Function<ui::IntOffset(const S&)> valueOf,
                                   kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::IntOffset, S>(t, converters::IntOffset(), std::move(valueOf), std::move(spec), key);
    }
    template<typename S>
    ui::IntSize AnimateIntSize(Transition<S>& t, kiln::Function<ui::IntSize(const S&)> valueOf,
                               kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return AnimateValue<ui::IntSize, S>(t, converters::IntSize(), std::move(valueOf), std::move(spec), key);
    }

    // forcedInitial / forcedVelocity: see detail::AnimateChild.
    template<typename S>
    float AnimateFloat(Transition<S>& t, kiln::Function<float(const S&)> valueOf,
                       kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {},
                       std::optional<anim::Vec> forcedInitial = std::nullopt, std::optional<anim::Vec> forcedVelocity = std::nullopt) {
        return detail::AnimateChild<S>(t, 1, [&](const S& s) { return anim::Vec{ valueOf(s), 0, 0, 0 }; }, spec, key, forcedInitial, forcedVelocity)[0];
    }

    template<typename S>
    AnimatedFloat AnimateFloatState(Transition<S>& t, kiln::Function<float(const S&)> valueOf,
                                    kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return { &detail::AnimateChildRef<S>(t, 1, [&](const S& s) { return anim::Vec{ valueOf(s), 0, 0, 0 }; }, spec, key) };
    }
    template<typename S>
    AnimatedColor AnimateColorState(Transition<S>& t, kiln::Function<ui::Color(const S&)> valueOf,
                                    kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        return { &detail::AnimateChildRef<S>(t, 4, [&](const S& s) {
            const auto lab = ui::ToOklab(valueOf(s));
            return anim::Vec{ lab[0], lab[1], lab[2], lab[3] };
        }, spec, key) };
    }

    // Colours interpolate in Oklab (ColorVectorConverter).
    template<typename S>
    ui::Color AnimateColor(Transition<S>& t, kiln::Function<ui::Color(const S&)> valueOf,
                           kiln::Function<anim::SpecPtr(const typename Transition<S>::Segment&)> spec = {}, rt::Key key = {}) {
        const anim::Vec v = detail::AnimateChild<S>(t, 4, [&](const S& s) {
            const auto lab = ui::ToOklab(valueOf(s));
            return anim::Vec{ lab[0], lab[1], lab[2], lab[3] };
        }, spec, key);
        return ui::FromOklab({ v[0], v[1], v[2], v[3] });
    }

    // SeekableTransitionState: the transition's play time follows `fraction` (0..1 of its total
    // duration) between currentState and targetState. SeekTo sets it (a back gesture's
    // progress); AnimateTo runs it to 1 on the clock (linearly over the remaining duration, or
    // with `spec`), then the target becomes current. One mutation at a time: a new one stops the
    // running one.
    template<typename S>
    class SeekableTransitionState {
    public:
        explicit SeekableTransitionState(S initial) : m_Current(initial), m_Target(initial) {}
        S CurrentState() const { return m_Current.Get(); }
        S TargetState() const { return m_Target.Get(); }
        float Fraction() const { return m_Fraction.Get(); }

        // snapTo: current and target become `target`, every value rests there.
        rt::Task<> SnapTo(S target) {
            if (m_Current.Peek() == target && m_Target.Peek() == target) co_return;
            const std::uint64_t token = m_Mutex.Acquire(MutatePriority::Default, {});
            m_Fraction.Set(0);
            m_Target.Set(target);
            m_Current.Set(target);
            m_Snap = true;
            m_Mutex.Release(token);
        }
        // seekTo(fraction, targetState): a new target restarts from its current state.
        rt::Task<> SeekTo(float fraction, std::optional<S> target = std::nullopt) {
            fraction = std::clamp(fraction, 0.0f, 1.0f);
            const std::uint64_t token = m_Mutex.Acquire(MutatePriority::Default, {});
            if (target && !(*target == m_Target.Peek())) {
                m_Target.Set(*target);   // the children retarget on recomposition
            } else if (m_Current.Peek() == m_Target.Peek()) {
                m_Mutex.Release(token);
                co_return;   // nothing to seek between
            }
            m_Fraction.Set(fraction);
            Apply();
            m_Mutex.Release(token);
        }
        // animateTo(targetState, animationSpec): to a new target, from its current state.
        rt::Task<> AnimateTo(std::optional<S> target = std::nullopt, anim::SpecPtr spec = nullptr) {
            const std::uint64_t token = m_Mutex.Acquire(MutatePriority::Default, {});
            const S oldTarget = m_Target.Peek();
            if (target && !(*target == oldTarget)) {
                m_Fraction.Set(0);
                m_Current.Set(oldTarget);
                m_Target.Set(*target);
                co_await rt::NextFrame();   // the composition retargets the children
                if (!m_Mutex.IsHeld(token)) co_return;
            }
            if (!(m_Current.Peek() == m_Target.Peek())) {
                const float start = m_Fraction.Peek();
                std::int64_t last = -1, progress = 0;
                while (m_Fraction.Peek() < 1) {
                    const std::int64_t frame = co_await rt::NextFrame();
                    if (!m_Mutex.IsHeld(token)) co_return;
                    const float scale = anim::DurationScale();
                    if (scale <= 0) { m_Fraction.Set(1); Apply(); break; }
                    if (last < 0) { last = frame; continue; }
                    progress += std::int64_t(double(frame - last) / scale);
                    last = frame;
                    const std::int64_t total = m_Core ? m_Core->TotalDurationNanos() : 0;
                    float value;
                    if (spec) {
                        const std::int64_t d = spec->DurationNanos({ start }, { 1 }, {}, 1);
                        value = progress >= d ? 1.0f : spec->ValueAt(progress, { start }, { 1 }, {}, 1)[0];
                    } else {
                        const std::int64_t d = std::int64_t(double(total) * (1.0 - start));
                        value = d <= 0 || progress >= d ? 1.0f : start + (1 - start) * float(double(progress) / double(d));
                    }
                    m_Fraction.Set(std::isnan(value) ? 1.0f : std::clamp(value, 0.0f, 1.0f));
                    Apply();
                }
                m_Current.Set(m_Target.Peek());
                m_Fraction.Set(0);
                if (m_Core) m_Core->Complete();
                if (m_Transition) m_Transition->Finish();
            }
            m_Mutex.Release(token);
        }
        // internal
        void Apply() { if (m_Core) m_Core->SetPlayTime(std::int64_t(double(m_Core->TotalDurationNanos()) * double(m_Fraction.Peek()))); }
        rt::State<S> m_Current, m_Target;
        rt::State<float> m_Fraction{ 0 };
        bool m_Snap = false;
        MutatorMutex m_Mutex;
        TransitionCore* m_Core = nullptr;
        Transition<S>* m_Transition = nullptr;
    };

    template<typename S>
    Transition<S>& UpdateTransition(SeekableTransitionState<S>& state, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& t = c.RememberNew<Transition<S>>({}, key, state.CurrentState());
        auto* sp = &state;
        state.m_Core = &t.Core();
        state.m_Transition = &t;
        // The clock never runs by itself: the play time is the fraction's.
        t.Core().SetSeeking(true);
        if (state.m_Snap) { state.m_Snap = false; t.SnapAll(state.TargetState()); }
        else t.UpdateTarget(state.TargetState());
        // Once the children have (re)targeted, they take the fraction's play time.
        c.SideEffect([sp] { sp->Apply(); });
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
