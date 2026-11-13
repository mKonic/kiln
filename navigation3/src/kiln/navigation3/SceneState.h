#pragma once

// SceneState (navigation3-ui SceneState.kt, NavigationEventHandler.kt,
// RememberNavigationEventState.kt): the scenes for the current entries.

#include "kiln/navigation3/Scene.h"
#include <kiln/Function.h>

namespace kiln::navigation3 {

    template<typename T>
    using OverlayScenePtr = std::shared_ptr<const OverlayScene<T>>;

    // SceneState: the decorated entries, the overlay scenes (topmost first), the current scene
    // and the scenes back navigation goes through (nearest last).
    template<typename T>
    struct SceneState {
        NavEntries<T> entries;
        std::vector<OverlayScenePtr<T>> overlayScenes;
        ScenePtr<T> currentScene;
        std::vector<ScenePtr<T>> previousScenes;
    };

    namespace detail {
        template<typename T>
        ScenePtr<T> ProvideScene(const SceneStrategyScope<T>& scope, const NavEntries<T>& entries, const std::vector<SceneStrategyPtr<T>>& strategies,
                                 const std::vector<SceneDecoratorStrategyPtr<T>>& decorators) {
            ScenePtr<T> scene = CalculateSceneWithSinglePaneFallback(strategies, scope, entries);
            for (const auto& d : decorators)
                if (!scene->IsOverlay()) scene = d->DecorateScene(scope, scene);
            return scene;
        }
    }

    // rememberSceneState(entries, sceneStrategies, sceneDecoratorStrategies, sharedTransitionScope,
    // onBack): entries go through the shared-entry decorator (with a SharedTransitionScope), the
    // scene-setup and lifecycle decorators, then the strategies pick the current scene, the
    // overlay scenes above it and the previous scenes.
    template<typename T>
    SceneState<T> RememberSceneState(const NavEntries<T>& entries, const std::vector<SceneStrategyPtr<T>>& sceneStrategies,
                                     const std::vector<SceneDecoratorStrategyPtr<T>>& sceneDecoratorStrategies,
                                     foundation::SharedTransitionScope* sharedTransitionScope, kiln::Function<void()> onBack, rt::Key key = {}) {
        // Returns a value, so it is not a restartable scope of its own (like Compose).
        auto& c = *rt::Composition::Current();
        SceneState<T> result;
        {
            auto& currentOnBack = c.Remember<std::shared_ptr<kiln::Function<void()>>>([] { return std::make_shared<kiln::Function<void()>>(); }, {}, key);
            *currentOnBack = onBack;
            std::vector<NavEntryDecorator<T>> decorators;
            if (sharedTransitionScope) decorators.push_back(RememberSharedEntryInSceneNavEntryDecorator<T>(*sharedTransitionScope));
            decorators.push_back(RememberSceneSetupNavEntryDecorator<T>());
            decorators.push_back(RememberBackStackAwareLifecycleNavEntryDecorator<T>(entries));
            const NavEntries<T> decorated = RememberDecoratedNavEntries(entries, decorators);
            struct Cache {
                std::vector<SceneStrategyPtr<T>> strategies;
                NavEntries<T> entries;
                std::optional<SceneState<T>> state;
            };
            auto& cache = c.Remember<std::shared_ptr<Cache>>([] { return std::make_shared<Cache>(); }, {}, key);
            if (cache->state && cache->strategies == sceneStrategies && cache->entries == decorated) return *cache->state;
            auto backRef = currentOnBack;
            const SceneStrategyScope<T> scope([backRef] { if (*backRef) (*backRef)(); });
            std::vector<ScenePtr<T>> all{ detail::ProvideScene(scope, decorated, sceneStrategies, sceneDecoratorStrategies) };
            // The overlay scenes, down to the first scene that is not one.
            while (all.back()->IsOverlay()) {
                const auto& overlaid = static_cast<const OverlayScene<T>&>(*all.back()).OverlaidEntries();
                if (overlaid.empty()) throw std::logic_error("Overlaid entries from an OverlayScene must not be empty");
                all.push_back(detail::ProvideScene(scope, overlaid, sceneStrategies, sceneDecoratorStrategies));
            }
            SceneState<T> state;
            state.entries = decorated;
            for (std::size_t i = 0; i + 1 < all.size(); ++i) state.overlayScenes.push_back(std::static_pointer_cast<const OverlayScene<T>>(all[i]));
            state.currentScene = all.back();
            std::vector<ScenePtr<T>> previous{ state.currentScene };
            while (!previous.front()->PreviousEntries().empty())
                previous.insert(previous.begin(), detail::ProvideScene(scope, previous.front()->PreviousEntries(), sceneStrategies, sceneDecoratorStrategies));
            previous.pop_back();
            state.previousScenes = std::move(previous);
            cache->strategies = sceneStrategies;
            cache->entries = decorated;
            cache->state = state;
            result = std::move(state);
        }
        return result;
    }

    namespace detail {
        template<typename T>
        SceneInfo<T> InfoOf(const ScenePtr<T>& scene) {
            const NavMetadata metadata = scene->Metadata();
            std::optional<std::string> title, url;
            if (const auto* t = Get(metadata, TitleMetadataKey)) title = (*t)();
            if (const auto* u = Get(metadata, UrlMetadataKey)) url = (*u)();
            return SceneInfo<T>(scene, title, url);
        }
    }

    // rememberNavigationEventState(sceneState): the current scene and the previous ones as infos.
    template<typename T>
    foundation::NavigationEventState<SceneInfo<T>>& RememberNavigationEventState(const SceneState<T>& sceneState, rt::Key key = {}) {
        std::vector<SceneInfo<T>> back;
        for (const auto& s : sceneState.previousScenes) back.push_back(detail::InfoOf(s));
        return foundation::RememberNavigationEventState(detail::InfoOf(sceneState.currentScene), std::move(back), {}, key);
    }

    // NavigationBackHandler(sceneState, state, onBackCancelled, onBackCompleted): back pops the
    // entries of the current scene that are not among its previous entries, one onBackCompleted
    // each.
    template<typename T>
    void NavigationBackHandler(const SceneState<T>& sceneState, foundation::NavigationEventState<SceneInfo<T>>& state,
                               kiln::Function<void()> onBackCancelled, kiln::Function<void()> onBackCompleted, rt::Key key = {}) {
        const std::size_t total = sceneState.entries.size(), previous = sceneState.currentScene->PreviousEntries().size();
        foundation::NavigationBackHandler(state, previous > 0, std::move(onBackCancelled), [onBackCompleted, total, previous] {
            for (std::size_t i = previous; i < total; ++i) onBackCompleted();
        }, key);
    }

    // The deprecated form without a SharedTransitionScope.
    template<typename T>
    SceneState<T> RememberSceneState(const NavEntries<T>& entries, const std::vector<SceneStrategyPtr<T>>& sceneStrategies,
                                     const std::vector<SceneDecoratorStrategyPtr<T>>& sceneDecoratorStrategies, kiln::Function<void()> onBack,
                                     rt::Key key = {}) {
        return RememberSceneState(entries, sceneStrategies, sceneDecoratorStrategies, nullptr, std::move(onBack), key);
    }

}
