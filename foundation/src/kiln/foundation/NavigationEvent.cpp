#include "kiln/foundation/NavigationEvent.h"
#include <kiln/Function.h>

#include <cassert>

namespace kiln::foundation::detail {

    namespace {
        NavigationEvent ToEvent(const ui::BackEvent& e) { return { e.swipeEdge, e.progress, e.touchX, e.touchY, e.frameTimeMillis }; }

        // ComposeNavigationEventHandler: the transition state follows the gesture.
        struct Handler final : ui::BackCallback {
            kiln::Function<void(NavigationEventTransitionState)> onTransition;
            kiln::Function<void()> onCancelled, onCompleted;
            void OnBackStarted(const ui::BackEvent& e) override {
                if (onTransition) onTransition(NavigationEventTransitionState::InProgress(ToEvent(e), NavigationEventTransitionState::TransitioningBack));
            }
            void OnBackProgressed(const ui::BackEvent& e) override {
                if (onTransition) onTransition(NavigationEventTransitionState::InProgress(ToEvent(e), NavigationEventTransitionState::TransitioningBack));
            }
            void OnBackCancelled() override {
                if (onTransition) onTransition(NavigationEventTransitionState::Idle());
                if (onCancelled) onCancelled();
            }
            void OnBack() override {
                if (onTransition) onTransition(NavigationEventTransitionState::Idle());
                if (onCompleted) onCompleted();
            }
        };
    }

    void NavigationEventHandlerImpl(void* stateId, kiln::Function<void(NavigationEventTransitionState)> onTransition, bool& registered,
                                    bool isBackEnabled, kiln::Function<void()> onBackCancelled, kiln::Function<void()> onBackCompleted,
                                    rt::Key key) {
        auto& c = *rt::Composition::Current();
        ui::BackDispatcher* d = c.Read(LocalBackDispatcher);
        auto& h = c.Remember<std::shared_ptr<Handler>>([] { auto p = std::make_shared<Handler>(); p->enabled = false; return p; },
                                                       rt::Keys(stateId), key);
        Handler* raw = h.get();
        std::shared_ptr<Handler> keep = h;
        bool* reg = &registered;
        c.DisposableEffect(rt::Keys(d, stateId), [d, keep, reg] {
            assert(!*reg && "NavigationEventState is already registered with a NavigationEventHandler");
            *reg = true;
            d->Add(keep.get());
            return [d, keep, reg] { d->Remove(keep.get()); *reg = false; };
        }, key);
        c.SideEffect([d, raw, isBackEnabled, onTransition = std::move(onTransition), onBackCancelled = std::move(onBackCancelled),
                      onBackCompleted = std::move(onBackCompleted)] {
            raw->onTransition = onTransition;
            raw->onCancelled = onBackCancelled;
            raw->onCompleted = onBackCompleted;
            raw->enabled = isBackEnabled;
            d->EnabledChanged();
        });
    }

}
