#pragma once

// Navigation events (navigationevent, navigationevent-compose): a handler whose state tracks a
// back gesture in progress, on kiln's back dispatcher. Forward navigation has no platform input
// here, so the forward callbacks never run.

#include "kiln/foundation/Back.h"
#include "kiln/runtime/State.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::foundation {

    // NavigationEvent: one step of a gesture (swipeEdge: Left, Right or None).
    struct NavigationEvent {
        ui::SwipeEdge swipeEdge = ui::SwipeEdge::None;
        float progress = 0;
        float touchX = 0, touchY = 0;
        std::int64_t frameTimeMillis = 0;
        bool operator==(const NavigationEvent&) const = default;
    };

    // NavigationEventTransitionState: Idle, or InProgress with the latest event and direction.
    struct NavigationEventTransitionState {
        static constexpr int TransitioningForward = 1;
        static constexpr int TransitioningBack = -1;
        bool inProgress = false;
        NavigationEvent latestEvent;
        int direction = 0;
        static NavigationEventTransitionState Idle() { return {}; }
        static NavigationEventTransitionState InProgress(NavigationEvent e, int direction) { return { true, e, direction }; }
        bool operator==(const NavigationEventTransitionState&) const = default;
    };

    // NavigationEventInfo: what a destination tells the system about itself.
    struct NavigationEventInfo {
        std::optional<std::string> title, url;
        bool operator==(const NavigationEventInfo&) const = default;
    };

    // NavigationEventState: the handler's gesture state plus the current, back and forward infos.
    template<typename I = NavigationEventInfo>
    class NavigationEventState {
    public:
        explicit NavigationEventState(I current, std::vector<I> back = {}, std::vector<I> forward = {})
            : m_Current(std::move(current)), m_Back(std::move(back)), m_Forward(std::move(forward)) {}
        NavigationEventTransitionState TransitionState() const { return m_Transition.Get(); }
        const I& CurrentInfo() const { return m_Current.Get(); }
        const std::vector<I>& BackInfo() const { return m_Back.Get(); }
        const std::vector<I>& ForwardInfo() const { return m_Forward.Get(); }

        // internal
        rt::State<NavigationEventTransitionState> m_Transition{ NavigationEventTransitionState::Idle() };
        rt::State<I, rt::NeverEqual> m_Current;
        rt::State<std::vector<I>, rt::NeverEqual> m_Back, m_Forward;
        bool m_Registered = false;
    };

    // rememberNavigationEventState(currentInfo, backInfo, forwardInfo).
    template<typename I>
    NavigationEventState<I>& RememberNavigationEventState(I current, std::vector<I> back = {}, std::vector<I> forward = {}, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& state = c.Remember<std::shared_ptr<NavigationEventState<I>>>([&] { return std::make_shared<NavigationEventState<I>>(current, back, forward); }, {}, key);
        auto* s = state.get();
        c.SideEffect([s, current = std::move(current), back = std::move(back), forward = std::move(forward)] {
            s->m_Current.Set(current);
            s->m_Back.Set(back);
            s->m_Forward.Set(forward);
        });
        return *state;
    }

    namespace detail {
        // The handler behind NavigationEventHandler: reports the gesture as transition states.
        void NavigationEventHandlerImpl(void* stateId, std::function<void(NavigationEventTransitionState)> onTransition, bool& registered,
                                        bool isBackEnabled, std::function<void()> onBackCancelled, std::function<void()> onBackCompleted,
                                        rt::Key key);
    }

    // NavigationEventHandler(state, ...): the back gesture drives state's transition state;
    // completion and cancellation call back.
    template<typename I>
    void NavigationEventHandler(NavigationEventState<I>& state, bool isForwardEnabled = true, std::function<void()> onForwardCancelled = {},
                                std::function<void()> onForwardCompleted = {}, bool isBackEnabled = true,
                                std::function<void()> onBackCancelled = {}, std::function<void()> onBackCompleted = {}, rt::Key key = {}) {
        (void)isForwardEnabled; (void)onForwardCancelled; (void)onForwardCompleted;
        auto* s = &state;
        detail::NavigationEventHandlerImpl(s, [s](NavigationEventTransitionState t) { s->m_Transition.Set(t); }, s->m_Registered, isBackEnabled,
                                           std::move(onBackCancelled), std::move(onBackCompleted), key);
    }

    // NavigationBackHandler(state, isBackEnabled, onBackCancelled, onBackCompleted).
    template<typename I>
    void NavigationBackHandler(NavigationEventState<I>& state, bool isBackEnabled, std::function<void()> onBackCancelled,
                               std::function<void()> onBackCompleted, rt::Key key = {}) {
        NavigationEventHandler(state, false, {}, {}, isBackEnabled, std::move(onBackCancelled), std::move(onBackCompleted), key);
    }

}
