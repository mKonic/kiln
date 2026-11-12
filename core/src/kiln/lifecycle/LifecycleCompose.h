#pragma once

// lifecycle-runtime-compose: the lifecycle in composition.
//   - LocalLifecycleOwner: the owner content runs under. Where none is provided it is the
//     window's (the ui::Owner's host lifecycle: the Android activity's, or one that is RESUMED
//     on hosts without one).
//   - LifecycleEventEffect / LifecycleStartEffect / LifecycleResumeEffect: callbacks on events,
//     the start and resume ones paired with a stop/pause-or-dispose block.
//   - CollectAsStateWithLifecycle: a stream collected into state only while the lifecycle is at
//     least minActiveState (STARTED by default).
//   - RememberLifecycleOwner: a child owner following its parent, capped at maxLifecycle
//     (navigation3 caps entries and scenes this way).
// Owner arguments default to null, meaning LocalLifecycleOwner.

#include "kiln/lifecycle/RepeatOnLifecycle.h"
#include "kiln/runtime/Composition.h"

namespace kiln::ui { class Owner; }

namespace kiln::lifecycle {

    using LifecycleOwnerPtr = std::shared_ptr<LifecycleOwner>;

    inline rt::Local<LifecycleOwnerPtr> LocalLifecycleOwner{ nullptr };

    // LocalLifecycleOwner.current: provided, else the composing window's (outside any window, a
    // shared RESUMED owner). Throws std::logic_error outside composition.
    LifecycleOwnerPtr CurrentLifecycleOwner();
    // The window's lifecycle owner (the host sets it before composing; without one, an owner
    // moved to RESUMED is made on first use).
    void SetHostLifecycleOwner(ui::Owner& owner, LifecycleOwnerPtr lifecycleOwner);
    LifecycleOwnerPtr HostLifecycleOwner(ui::Owner& owner);

    // Lifecycle.currentStateAsState(): the state, read as observable state.
    inline const rt::State<State>& CurrentStateAsState(Lifecycle& lifecycle) { return lifecycle.CurrentStateFlow(); }

    // dropUnlessStarted / dropUnlessResumed: `block` wrapped to run only while the lifecycle is
    // at least STARTED / RESUMED (a click arriving during a transition is dropped).
    std::function<void()> DropUnlessStarted(std::function<void()> block, LifecycleOwnerPtr lifecycleOwner = nullptr);
    std::function<void()> DropUnlessResumed(std::function<void()> block, LifecycleOwnerPtr lifecycleOwner = nullptr);

    // LifecycleEventEffect: onEvent (the latest one passed) on each `event`. OnDestroy is
    // refused (std::invalid_argument): the composition is gone before it is sent.
    void LifecycleEventEffect(Event event, std::function<void()> onEvent, LifecycleOwnerPtr lifecycleOwner = nullptr, rt::Key key = {});

    // LifecycleStopOrDisposeEffectResult: what OnStopOrDispose returns.
    class LifecycleStopOrDisposeEffectResult {
    public:
        explicit LifecycleStopOrDisposeEffectResult(std::function<void()> f) : m_F(std::move(f)) {}
        void RunStopOrDisposeEffect() const { if (m_F) m_F(); }
    private:
        std::function<void()> m_F;
    };
    // LifecycleStartStopEffectScope: the start block's receiver.
    class LifecycleStartStopEffectScope : public LifecycleOwner {
    public:
        explicit LifecycleStartStopEffectScope(Lifecycle& lifecycle) : m_Lifecycle(&lifecycle) {}
        Lifecycle& GetLifecycle() override { return *m_Lifecycle; }
        LifecycleStopOrDisposeEffectResult OnStopOrDispose(std::function<void(LifecycleOwner&)> onStopOrDisposeEffect) {
            return LifecycleStopOrDisposeEffectResult([this, f = std::move(onStopOrDisposeEffect)] { f(*this); });
        }
    private:
        Lifecycle* m_Lifecycle;
    };
    // LifecycleStartEffect(keys): `effects` on each ON_START; its result runs on ON_STOP or when
    // the effect leaves (keys changing, the owner changing, or the call going away).
    void LifecycleStartEffect(rt::Keys keys, std::function<LifecycleStopOrDisposeEffectResult(LifecycleStartStopEffectScope&)> effects,
                              LifecycleOwnerPtr lifecycleOwner = nullptr, rt::Key key = {});

    class LifecyclePauseOrDisposeEffectResult {
    public:
        explicit LifecyclePauseOrDisposeEffectResult(std::function<void()> f) : m_F(std::move(f)) {}
        void RunPauseOrOnDisposeEffect() const { if (m_F) m_F(); }
    private:
        std::function<void()> m_F;
    };
    class LifecycleResumePauseEffectScope : public LifecycleOwner {
    public:
        explicit LifecycleResumePauseEffectScope(Lifecycle& lifecycle) : m_Lifecycle(&lifecycle) {}
        Lifecycle& GetLifecycle() override { return *m_Lifecycle; }
        LifecyclePauseOrDisposeEffectResult OnPauseOrDispose(std::function<void(LifecycleOwner&)> onPauseOrDisposeEffect) {
            return LifecyclePauseOrDisposeEffectResult([this, f = std::move(onPauseOrDisposeEffect)] { f(*this); });
        }
    private:
        Lifecycle* m_Lifecycle;
    };
    // LifecycleResumeEffect(keys): `effects` on each ON_RESUME; its result on ON_PAUSE or dispose.
    void LifecycleResumeEffect(rt::Keys keys, std::function<LifecyclePauseOrDisposeEffectResult(LifecycleResumePauseEffectScope&)> effects,
                               LifecycleOwnerPtr lifecycleOwner = nullptr, rt::Key key = {});

    // rememberLifecycleOwner(maxLifecycle, parent): a child owner at the lower of its parent's
    // state and maxLifecycle, destroyed when the call leaves. The first form's parent is
    // LocalLifecycleOwner; a null parent makes an owner that is RESUMED (capped) by itself.
    LifecycleOwnerPtr RememberLifecycleOwner(State maxLifecycle = State::Resumed, rt::Key key = {});
    LifecycleOwnerPtr RememberLifecycleOwner(State maxLifecycle, LifecycleOwnerPtr parent, rt::Key key = {});

    // Flow.collectAsStateWithLifecycle(initialValue, lifecycle, minActiveState): state holding
    // the latest value `flow` (anything with Collect(collector) -> Task<>, such as
    // rt::SnapshotFlow) emitted while the lifecycle was at least minActiveState. A flow is not
    // comparable here, so `flowKeys` stand for its identity: collection restarts when they, the
    // lifecycle or minActiveState change.
    template<typename T, typename Flow>
    rt::State<T>& CollectAsStateWithLifecycle(Flow flow, T initialValue, Lifecycle& lifecycle, State minActiveState = State::Started,
                                              rt::Keys flowKeys = {}, rt::Key key = {}) {
        rt::Composition* c = rt::Composition::Current();
        rt::State<T>& state = c->RememberState(std::move(initialValue), key);
        rt::State<T>* out = &state;
        Lifecycle* l = &lifecycle;
        rt::Keys keys = flowKeys;
        keys.hashes.push_back(std::hash<const void*>{}(l));
        keys.hashes.push_back(std::size_t(minActiveState));
        c->LaunchedEffect(std::move(keys), [flow = std::move(flow), l, minActiveState, out]() -> rt::Task<> {
            return RepeatOnLifecycle(*l, minActiveState, [flow, out]() { return flow.Collect([out](const T& v) { out->Set(v); }); });
        }, key);
        return state;
    }
    template<typename T, typename Flow>
    rt::State<T>& CollectAsStateWithLifecycle(Flow flow, T initialValue, LifecycleOwnerPtr lifecycleOwner = nullptr,
                                              State minActiveState = State::Started, rt::Keys flowKeys = {}, rt::Key key = {}) {
        LifecycleOwnerPtr owner = lifecycleOwner ? lifecycleOwner : CurrentLifecycleOwner();
        return CollectAsStateWithLifecycle<T>(std::move(flow), std::move(initialValue), owner->GetLifecycle(), minActiveState, std::move(flowKeys), key);
    }

}
