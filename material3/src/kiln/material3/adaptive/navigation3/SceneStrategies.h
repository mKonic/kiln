#pragma once

// adaptive-navigation3 ListDetailSceneStrategy.kt and SupportingPaneSceneStrategy.kt: scene
// strategies that put entries with pane metadata into a list-detail or supporting-pane scaffold
// when it shows more than one pane (or always, with shouldHandleSinglePaneLayout).

#include "kiln/material3/adaptive/navigation3/ThreePaneScaffoldScene.h"

namespace kiln::material3::adaptive::navigation3 {

    struct ThreePaneSceneStrategyOptions {
        bool shouldHandleSinglePaneLayout = false;
        DragHandleContent paneExpansionDragHandle;
        PaneExpansionState* paneExpansionState = nullptr;
    };

    template<typename T>
    class ListDetailSceneStrategy final : public nav3::SceneStrategy<T> {
    public:
        static constexpr const char* ListDetailRoleKey = "androidx.compose.material3.adaptive.layout.ListDetailPaneScaffoldRole";

        ListDetailSceneStrategy(BackNavigationBehavior backNavigationBehavior, PaneScaffoldDirective directive,
                                ThreePaneScaffoldAdaptStrategies adaptStrategies, ThreePaneSceneStrategyOptions options = {})
            : backNavigationBehavior(backNavigationBehavior), directive(std::move(directive)), adaptStrategies(std::move(adaptStrategies)),
              options(std::move(options)) {}

        nav3::ScenePtr<T> CalculateScene(const nav3::SceneStrategyScope<T>& scope, const nav3::NavEntries<T>& entries) const override {
            return detail::CalculateScaffoldScene<T>(scope, entries, ListDetailRoleKey, ThreePaneScaffoldType::ListDetail, options.shouldHandleSinglePaneLayout,
                                                     backNavigationBehavior, directive, adaptStrategies, options.paneExpansionDragHandle,
                                                     options.paneExpansionState);
        }

        // listPane(sceneKey, detailPlaceholder): the entry is the scene's list.
        static nav3::NavMetadata ListPane(std::string sceneKey = {}, DetailPlaceholder detailPlaceholder = {}) {
            return { { ListDetailRoleKey, std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), ListDetailPaneScaffoldRole::List,
                                                                                               std::move(detailPlaceholder), true }) } };
        }
        static nav3::NavMetadata DetailPane(std::string sceneKey = {}) {
            return { { ListDetailRoleKey, std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), ListDetailPaneScaffoldRole::Detail }) } };
        }
        static nav3::NavMetadata ExtraPane(std::string sceneKey = {}) {
            return { { ListDetailRoleKey, std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), ListDetailPaneScaffoldRole::Extra }) } };
        }
        static nav3::NavMetadata PreferredPaneSize(std::optional<ui::Dp> width, std::optional<ui::Dp> height = std::nullopt) {
            return detail::PreferredPaneSize(width, height);
        }
        static nav3::NavMetadata PreferredPaneSize(float width, float height = NAN) { return detail::PreferredPaneSize(width, height); }
        static nav3::NavMetadata PaneAnimation(std::optional<foundation::EnterTransition> enter = std::nullopt,
                                               std::optional<foundation::ExitTransition> exit = std::nullopt, anim::SpecPtr boundsAnimationSpec = nullptr) {
            return detail::PaneAnimation(std::move(enter), std::move(exit), std::move(boundsAnimationSpec));
        }

        BackNavigationBehavior backNavigationBehavior;
        PaneScaffoldDirective directive;
        ThreePaneScaffoldAdaptStrategies adaptStrategies;
        ThreePaneSceneStrategyOptions options;
    };

    template<typename T>
    class SupportingPaneSceneStrategy final : public nav3::SceneStrategy<T> {
    public:
        static constexpr const char* SupportingPaneRoleKey = "androidx.compose.material3.adaptive.layout.SupportingPaneScaffoldRole";

        SupportingPaneSceneStrategy(BackNavigationBehavior backNavigationBehavior, PaneScaffoldDirective directive,
                                    ThreePaneScaffoldAdaptStrategies adaptStrategies, ThreePaneSceneStrategyOptions options = {})
            : backNavigationBehavior(backNavigationBehavior), directive(std::move(directive)), adaptStrategies(std::move(adaptStrategies)),
              options(std::move(options)) {}

        nav3::ScenePtr<T> CalculateScene(const nav3::SceneStrategyScope<T>& scope, const nav3::NavEntries<T>& entries) const override {
            return detail::CalculateScaffoldScene<T>(scope, entries, SupportingPaneRoleKey, ThreePaneScaffoldType::SupportingPane,
                                                     options.shouldHandleSinglePaneLayout, backNavigationBehavior, directive, adaptStrategies,
                                                     options.paneExpansionDragHandle, options.paneExpansionState);
        }

        static nav3::NavMetadata MainPane(std::string sceneKey = {}) {
            return { { SupportingPaneRoleKey, std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), SupportingPaneScaffoldRole::Main }) } };
        }
        static nav3::NavMetadata SupportingPane(std::string sceneKey = {}) {
            return { { SupportingPaneRoleKey,
                       std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), SupportingPaneScaffoldRole::Supporting }) } };
        }
        static nav3::NavMetadata ExtraPane(std::string sceneKey = {}) {
            return { { SupportingPaneRoleKey, std::make_shared<const PaneMetadata>(PaneMetadata{ std::move(sceneKey), SupportingPaneScaffoldRole::Extra }) } };
        }
        static nav3::NavMetadata PreferredPaneSize(std::optional<ui::Dp> width, std::optional<ui::Dp> height = std::nullopt) {
            return detail::PreferredPaneSize(width, height);
        }
        static nav3::NavMetadata PreferredPaneSize(float width, float height = NAN) { return detail::PreferredPaneSize(width, height); }
        static nav3::NavMetadata PaneAnimation(std::optional<foundation::EnterTransition> enter = std::nullopt,
                                               std::optional<foundation::ExitTransition> exit = std::nullopt, anim::SpecPtr boundsAnimationSpec = nullptr) {
            return detail::PaneAnimation(std::move(enter), std::move(exit), std::move(boundsAnimationSpec));
        }

        BackNavigationBehavior backNavigationBehavior;
        PaneScaffoldDirective directive;
        ThreePaneScaffoldAdaptStrategies adaptStrategies;
        ThreePaneSceneStrategyOptions options;
    };

    namespace detail {
        template<typename S>
        std::shared_ptr<const S> RememberStrategy(BackNavigationBehavior behavior, const std::optional<PaneScaffoldDirective>& directive,
                                                  const ThreePaneScaffoldAdaptStrategies& adaptStrategies, const ThreePaneSceneStrategyOptions& options,
                                                  rt::Key key) {
            auto& c = *rt::Composition::Current();
            const PaneScaffoldDirective d = directive ? *directive : CalculatePaneScaffoldDirective(CurrentWindowAdaptiveInfoV2());
            struct Cache { std::shared_ptr<const S> strategy; };
            auto& cache = c.Remember<std::shared_ptr<Cache>>([] { return std::make_shared<Cache>(); }, {}, key);
            const auto& s = cache->strategy;
            if (!s || s->backNavigationBehavior != behavior || !(s->directive == d) || !(s->adaptStrategies == adaptStrategies) ||
                s->options.shouldHandleSinglePaneLayout != options.shouldHandleSinglePaneLayout || s->options.paneExpansionState != options.paneExpansionState)
                cache->strategy = std::make_shared<const S>(behavior, d, adaptStrategies, options);
            return cache->strategy;
        }
    }

    // rememberListDetailSceneStrategy(shouldHandleSinglePaneLayout, backNavigationBehavior,
    // directive = calculatePaneScaffoldDirective(currentWindowAdaptiveInfoV2()), adaptStrategies, ...)
    template<typename T>
    std::shared_ptr<const ListDetailSceneStrategy<T>> RememberListDetailSceneStrategy(
        BackNavigationBehavior backNavigationBehavior = BackNavigationBehavior::PopUntilScaffoldValueChange,
        std::optional<PaneScaffoldDirective> directive = std::nullopt,
        ThreePaneScaffoldAdaptStrategies adaptStrategies = ListDetailPaneScaffoldDefaults::adaptStrategies(), ThreePaneSceneStrategyOptions options = {},
        rt::Key key = {}) {
        return detail::RememberStrategy<ListDetailSceneStrategy<T>>(backNavigationBehavior, directive, adaptStrategies, options, key);
    }

    // rememberSupportingPaneSceneStrategy(..., backNavigationBehavior = PopUntilCurrentDestinationChange, ...)
    template<typename T>
    std::shared_ptr<const SupportingPaneSceneStrategy<T>> RememberSupportingPaneSceneStrategy(
        BackNavigationBehavior backNavigationBehavior = BackNavigationBehavior::PopUntilCurrentDestinationChange,
        std::optional<PaneScaffoldDirective> directive = std::nullopt,
        ThreePaneScaffoldAdaptStrategies adaptStrategies = SupportingPaneScaffoldDefaults::adaptStrategies(), ThreePaneSceneStrategyOptions options = {},
        rt::Key key = {}) {
        return detail::RememberStrategy<SupportingPaneSceneStrategy<T>>(backNavigationBehavior, directive, adaptStrategies, options, key);
    }

}
