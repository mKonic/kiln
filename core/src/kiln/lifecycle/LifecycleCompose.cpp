#include "kiln/lifecycle/LifecycleCompose.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

namespace kiln::lifecycle {

    namespace {
        const char* const kHostTag = "lifecycleOwner";

        // A window's owner where the host provides none: RESUMED from the start.
        struct ResumedOwner final : LifecycleOwner {
            LifecycleRegistry registry{ *this };
            ResumedOwner() { registry.SetCurrentState(State::Resumed); }
            Lifecycle& GetLifecycle() override { return registry; }
        };

        // rememberLifecycleOwner's ComposeLifecycleOwner: the lower of the parent's state and the cap.
        class ComposeLifecycleOwner final : public LifecycleOwner {
        public:
            Lifecycle& GetLifecycle() override { return m_Registry; }
            void SetMaxLifecycleState(State s) {
                m_Max = s;
                Update();
            }
            void HandleLifecycleEvent(Event e) {
                m_Parent = TargetState(e);
                Update();
            }
        private:
            void Update() {
                const State target = m_Parent < m_Max ? m_Parent : m_Max;
                if (m_Registry.CurrentState() == State::Initialized && target == State::Destroyed) return;
                m_Registry.SetCurrentState(target);
            }
            LifecycleRegistry m_Registry{ *this };
            State m_Parent = State::Initialized;
            State m_Max = State::Initialized;
        };

        LifecycleOwnerPtr Resolve(const LifecycleOwnerPtr& p) { return p ? p : CurrentLifecycleOwner(); }

        kiln::Function<void()> DropUnless(State state, kiln::Function<void()> block, LifecycleOwnerPtr owner) {
            owner = Resolve(owner);
            return [state, block = std::move(block), owner] {
                if (owner && IsAtLeast(owner->GetLifecycle().CurrentState(), state)) block();
            };
        }

        rt::Keys WithOwner(rt::Keys keys, const void* owner) {
            keys.hashes.push_back(std::hash<const void*>{}(owner));
            return keys;
        }
    }

    void SetHostLifecycleOwner(ui::Owner& owner, LifecycleOwnerPtr lifecycleOwner) { owner.Service(kHostTag) = std::move(lifecycleOwner); }

    LifecycleOwnerPtr HostLifecycleOwner(ui::Owner& owner) {
        auto& slot = owner.Service(kHostTag);
        if (!slot) slot = std::make_shared<ResumedOwner>();
        return std::static_pointer_cast<LifecycleOwner>(slot);
    }

    LifecycleOwnerPtr CurrentLifecycleOwner() {
        auto* c = rt::Composition::Current();
        if (!c) throw std::logic_error("CompositionLocal LocalLifecycleOwner not present");
        if (auto p = c->Read(LocalLifecycleOwner)) return p;
        if (ui::Owner* o = c->Read(ui::LocalOwner)) return HostLifecycleOwner(*o);
        // Composed outside any window (a bare composition, as tests make): RESUMED, as
        // Compose's test rule provides.
        static const LifecycleOwnerPtr detached = std::make_shared<ResumedOwner>();
        return detached;
    }

    kiln::Function<void()> DropUnlessStarted(kiln::Function<void()> block, LifecycleOwnerPtr owner) {
        return DropUnless(State::Started, std::move(block), std::move(owner));
    }
    kiln::Function<void()> DropUnlessResumed(kiln::Function<void()> block, LifecycleOwnerPtr owner) {
        return DropUnless(State::Resumed, std::move(block), std::move(owner));
    }

    void LifecycleEventEffect(Event event, kiln::Function<void()> onEvent, LifecycleOwnerPtr lifecycleOwner, rt::Key key) {
        if (event == Event::OnDestroy)
            throw std::invalid_argument("LifecycleEventEffect cannot be used to listen for Lifecycle.Event.ON_DESTROY, since Compose disposes "
                                        "of the composition before ON_DESTROY observers are invoked.");
        rt::Composition* c = rt::Composition::Current();
        const LifecycleOwnerPtr owner = Resolve(lifecycleOwner);
        // rememberUpdatedState(onEvent)
        auto& current = c->Remember<std::shared_ptr<kiln::Function<void()>>>([] { return std::make_shared<kiln::Function<void()>>(); }, {}, key);
        *current = std::move(onEvent);
        c->DisposableEffect(rt::Keys(static_cast<const void*>(owner.get())), [owner, event, current = current]() {
            auto observer = MakeEventObserver([event, current](LifecycleOwner&, Event e) {
                if (e == event && *current) (*current)();
            });
            owner->GetLifecycle().AddObserver(observer);
            return kiln::Function<void()>([owner, observer] { owner->GetLifecycle().RemoveObserver(observer); });
        }, key);
    }

    void LifecycleStartEffect(rt::Keys keys, kiln::Function<LifecycleStopOrDisposeEffectResult(LifecycleStartStopEffectScope&)> effects,
                              LifecycleOwnerPtr lifecycleOwner, rt::Key key) {
        rt::Composition* c = rt::Composition::Current();
        const LifecycleOwnerPtr owner = Resolve(lifecycleOwner);
        auto& scope = c->Remember<std::shared_ptr<LifecycleStartStopEffectScope>>(
            [&] { return std::make_shared<LifecycleStartStopEffectScope>(owner->GetLifecycle()); }, WithOwner(std::move(keys), owner.get()), key);
        c->DisposableEffect(rt::Keys(static_cast<const void*>(owner.get()), static_cast<const void*>(scope.get())),
                            [owner, scope = scope, effects = std::move(effects)]() {
            auto result = std::make_shared<std::optional<LifecycleStopOrDisposeEffectResult>>();
            auto observer = MakeEventObserver([scope, effects, result](LifecycleOwner&, Event e) {
                if (e == Event::OnStart) {
                    result->emplace(effects(*scope));
                } else if (e == Event::OnStop) {
                    if (*result) (*result)->RunStopOrDisposeEffect();
                    result->reset();
                }
            });
            owner->GetLifecycle().AddObserver(observer);
            return kiln::Function<void()>([owner, observer, result] {
                owner->GetLifecycle().RemoveObserver(observer);
                if (*result) (*result)->RunStopOrDisposeEffect();
            });
        }, key);
    }

    void LifecycleResumeEffect(rt::Keys keys, kiln::Function<LifecyclePauseOrDisposeEffectResult(LifecycleResumePauseEffectScope&)> effects,
                               LifecycleOwnerPtr lifecycleOwner, rt::Key key) {
        rt::Composition* c = rt::Composition::Current();
        const LifecycleOwnerPtr owner = Resolve(lifecycleOwner);
        auto& scope = c->Remember<std::shared_ptr<LifecycleResumePauseEffectScope>>(
            [&] { return std::make_shared<LifecycleResumePauseEffectScope>(owner->GetLifecycle()); }, WithOwner(std::move(keys), owner.get()), key);
        c->DisposableEffect(rt::Keys(static_cast<const void*>(owner.get()), static_cast<const void*>(scope.get())),
                            [owner, scope = scope, effects = std::move(effects)]() {
            auto result = std::make_shared<std::optional<LifecyclePauseOrDisposeEffectResult>>();
            auto observer = MakeEventObserver([scope, effects, result](LifecycleOwner&, Event e) {
                if (e == Event::OnResume) {
                    result->emplace(effects(*scope));
                } else if (e == Event::OnPause) {
                    if (*result) (*result)->RunPauseOrOnDisposeEffect();
                    result->reset();
                }
            });
            owner->GetLifecycle().AddObserver(observer);
            return kiln::Function<void()>([owner, observer, result] {
                owner->GetLifecycle().RemoveObserver(observer);
                if (*result) (*result)->RunPauseOrOnDisposeEffect();
            });
        }, key);
    }

    LifecycleOwnerPtr RememberLifecycleOwner(State maxLifecycle, rt::Key key) {
        return RememberLifecycleOwner(maxLifecycle, CurrentLifecycleOwner(), key);
    }

    LifecycleOwnerPtr RememberLifecycleOwner(State maxLifecycle, LifecycleOwnerPtr parent, rt::Key key) {
        rt::Composition* c = rt::Composition::Current();
        const rt::Keys parentKey(static_cast<const void*>(parent.get()));
        auto& local = c->Remember<std::shared_ptr<ComposeLifecycleOwner>>([] { return std::make_shared<ComposeLifecycleOwner>(); }, parentKey, key);
        auto owner = local;
        c->DisposableEffect(rt::Keys(static_cast<const void*>(owner.get()), static_cast<const void*>(parent.get())), [owner, parent]() {
            auto observer = MakeEventObserver([weak = std::weak_ptr<ComposeLifecycleOwner>(owner)](LifecycleOwner&, Event e) {
                if (auto o = weak.lock()) o->HandleLifecycleEvent(e);
            });
            if (parent) parent->GetLifecycle().AddObserver(observer);
            else owner->HandleLifecycleEvent(Event::OnResume);
            return kiln::Function<void()>([owner, parent, observer] {
                if (parent) parent->GetLifecycle().RemoveObserver(observer);
                owner->HandleLifecycleEvent(Event::OnDestroy);
            });
        }, key);
        c->LaunchedEffect(rt::Keys(static_cast<const void*>(owner.get()), int(maxLifecycle)), [owner, maxLifecycle]() -> rt::Task<> {
            owner->SetMaxLifecycleState(maxLifecycle);
            co_return;
        }, key);
        return owner;
    }

}
