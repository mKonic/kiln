// TransitionSamples.kt
#include "TransitionSamples.h"
#include "Common.h"

#include <kiln/foundation/AnimatedVisibility.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/NavigationTransitions.h>
#include <kiln/material3/Surface.h>

#include <string>

namespace catalog::samples {
    namespace {
        // The samples' manual navigation (until NavDisplay takes MutableContentTransform): a
        // deferred transition that defers while a gesture is in progress.
        struct Navigation {
            kiln::rt::State<int>& target;
            kiln::rt::State<bool>& isGestureInProgress;
            kiln::rt::State<kiln::ui::BackEvent>& backEvent;
            kiln::foundation::Transition<std::uint64_t>& transition;
        };
        Navigation RememberNavigation() {
            auto& target = RememberState(1);
            auto& isGestureInProgress = RememberState(false);
            auto& backEvent = RememberState(kiln::ui::BackEvent{});
            auto& transitionState = Here().RememberNew<kiln::foundation::DeferredTransitionState<std::uint64_t>>({}, {}, std::uint64_t(target.Peek()));
            auto& transition = kiln::foundation::RememberDeferredTransition(transitionState);
            const bool gesture = isGestureInProgress.Get();
            const int screen = target.Get();
            Here().LaunchedEffect(kiln::rt::Keys(gesture, screen), [&transitionState, gesture, screen]() -> kiln::rt::Task<> {
                if (gesture) transitionState.Defer(std::uint64_t(screen));
                else transitionState.AnimateTo(std::uint64_t(screen));
                co_return;
            });
            return { target, isGestureInProgress, backEvent, transition };
        }

        // A full-size page of `color` with its name in headlineLarge.
        void Page(const std::string& name, Color color) {
            m3::SurfaceOptions o;
            o.color = color;
            m3::Surface(FillMax({}, Axis::Both), o, [name] {
                Box(Padding(FillMax({}, Axis::Both), 16_dp), Alignment::Center(), false,
                    [name] { TextStyled(name, m3::MaterialTheme::typography().headlineLarge); });
            });
        }
    }

    void HorizontalSharedAxisTransitionSample() {
        const Navigation nav = RememberNavigation();
        const Color scrimColor = m3::HorizontalSharedAxisNavigationTransitions::scrimColor();
        const kiln::ui::Density density = Here().Read(LocalDensity);
        auto* backEvent = &nav.backEvent;
        DeferredAnimatedContent(
            nav.transition, {},
            [](kiln::rt::Key screen) {
                Page("Screen " + std::to_string(screen.value),
                     screen.value == 1 ? m3::MaterialTheme::colorScheme().primaryContainer : m3::MaterialTheme::colorScheme().secondaryContainer);
            },
            [](AnimatedContentTransitionScope& s) {
                return s.targetState.value > s.initialState.value ? m3::HorizontalSharedAxisNavigationTransitions::forwardTransform()
                                                                  : m3::HorizontalSharedAxisNavigationTransitions::backwardTransform();
            },
            Alignment::TopStart(),
            [backEvent, density, scrimColor](AnimatedContentTransitionScope& s) -> std::shared_ptr<MutableContentTransform> {
                if (s.initialState.value > s.targetState.value)
                    return m3::HorizontalSharedAxisNavigationTransitions::predictiveBackTransform([backEvent] { return backEvent->Get(); }, density,
                                                                                                   scrimColor);
                return nullptr;
            });
    }

    void FadeThroughTransitionSample() {
        const Navigation nav = RememberNavigation();
        const kiln::ui::Density density = Here().Read(LocalDensity);
        auto* backEvent = &nav.backEvent;
        DeferredAnimatedContent(
            nav.transition, {},
            [](kiln::rt::Key tab) {
                Page("Tab " + std::to_string(tab.value),
                     tab.value == 1 ? m3::MaterialTheme::colorScheme().primaryContainer : m3::MaterialTheme::colorScheme().tertiaryContainer);
            },
            [](AnimatedContentTransitionScope&) { return m3::FadeThroughNavigationTransitions::transform(); }, Alignment::TopStart(),
            [backEvent, density](AnimatedContentTransitionScope&) {
                return m3::FadeThroughNavigationTransitions::predictiveBackTransform([backEvent] { return backEvent->Get(); }, density);
            });
    }
}
