#pragma once

// adaptive-navigation3 ThreePaneScaffoldScene.kt, ListDetailSceneScope.kt,
// SupportingPaneSceneScope.kt: a list-detail or supporting-pane scaffold as a navigation3 scene,
// its panes showing the latest entry of each role.

#include "kiln/material3/adaptive/ThreePaneScaffoldNavigator.h"
#include "kiln/navigation3/SceneState.h"

namespace kiln::material3::adaptive::navigation3 {

    namespace nav3 = kiln::navigation3;

    // ListDetailSceneScope / SupportingPaneSceneScope: given to entries rendered in a scaffold
    // scene (null otherwise).
    struct ListDetailSceneScope {
        const ThreePaneScaffoldScope* scaffoldTransitionScope = nullptr;
        bool operator==(const ListDetailSceneScope&) const = default;
    };
    struct SupportingPaneSceneScope {
        const ThreePaneScaffoldScope* scaffoldTransitionScope = nullptr;
        bool operator==(const SupportingPaneSceneScope&) const = default;
    };
    inline rt::Local<std::optional<ListDetailSceneScope>> LocalListDetailSceneScope{ std::nullopt };
    inline rt::Local<std::optional<SupportingPaneSceneScope>> LocalSupportingPaneSceneScope{ std::nullopt };

    // The pane metadata keys (shared by both strategies).
    inline constexpr const char* MetadataEnterTransitionKey = "androidx.compose.material3.adaptive.layout.PaneMotion.enterTransition";
    inline constexpr const char* MetadataExitTransitionKey = "androidx.compose.material3.adaptive.layout.PaneMotion.exitTransition";
    inline constexpr const char* MetadataBoundsAnimationSpecKey = "androidx.compose.material3.adaptive.layout.PaneMotion.boundsAnimationSpec";
    inline constexpr const char* MetadataPreferredWidthKey = "androidx.compose.material3.adaptive.layout.preferredWidth";
    inline constexpr const char* MetadataPreferredHeightKey = "androidx.compose.material3.adaptive.layout.preferredHeight";

    using DetailPlaceholder = std::function<void(const ThreePaneScaffoldScope&)>;

    // A pane's role in a scaffold scene: which scene (sceneKey) and which pane.
    struct PaneMetadata {
        std::string sceneKey;
        ThreePaneScaffoldRole role = ThreePaneScaffoldRole::Primary;
        DetailPlaceholder detailPlaceholder;   // list panes of a list-detail scene
        bool isList = false;
    };

    namespace detail {
        // preferredPaneSize(width, height): Dp or a proportion (float).
        inline nav3::NavMetadata PreferredPaneSize(std::optional<ui::Dp> width, std::optional<ui::Dp> height) {
            nav3::NavMetadata m;
            if (width && width->Specified()) m[MetadataPreferredWidthKey] = *width;
            if (height && height->Specified()) m[MetadataPreferredHeightKey] = *height;
            return m;
        }
        inline nav3::NavMetadata PreferredPaneSize(float width, float height) {
            nav3::NavMetadata m;
            if (!std::isnan(width)) m[MetadataPreferredWidthKey] = width;
            if (!std::isnan(height)) m[MetadataPreferredHeightKey] = height;
            return m;
        }
        inline nav3::NavMetadata PaneAnimation(std::optional<foundation::EnterTransition> enter, std::optional<foundation::ExitTransition> exit,
                                               anim::SpecPtr boundsAnimationSpec) {
            nav3::NavMetadata m;
            if (enter) m[MetadataEnterTransitionKey] = *enter;
            if (exit) m[MetadataExitTransitionKey] = *exit;
            if (boundsAnimationSpec) m[MetadataBoundsAnimationSpecKey] = boundsAnimationSpec;
            return m;
        }

        inline int PaneCount(const ThreePaneScaffoldValue& v) {
            return int(v.primary.kind != PaneAdaptedValue::Kind::Hidden) + int(v.secondary.kind != PaneAdaptedValue::Kind::Hidden) +
                   int(v.tertiary.kind != PaneAdaptedValue::Kind::Hidden);
        }
        inline int ExpandedCount(const ThreePaneScaffoldValue& v) {
            return int(v.primary.kind == PaneAdaptedValue::Kind::Expanded) + int(v.secondary.kind == PaneAdaptedValue::Kind::Expanded) +
                   int(v.tertiary.kind == PaneAdaptedValue::Kind::Expanded);
        }
        // backProgressToStateProgress: the gesture eased, scaled by how many panes are shown.
        inline float SceneBackProgress(float progress, const ThreePaneScaffoldValue& value) {
            static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);
            const int n = ExpandedCount(value);
            return easing.Transform(progress) * (n == 1 ? 0.1f : n == 2 ? 0.15f : 0.2f);
        }
    }

    enum class ThreePaneScaffoldType : std::uint8_t { ListDetail, SupportingPane };

    // ThreePaneScaffoldInfo: the scene as navigation event info.
    struct ThreePaneScaffoldSceneInfo : foundation::NavigationEventInfo {
        std::string key;
        std::vector<std::string> entries;
        bool operator==(const ThreePaneScaffoldSceneInfo&) const = default;
    };

    template<typename T>
    class ThreePaneScaffoldScene final : public nav3::Scene<T> {
    public:
        struct Params {
            std::string key;
            std::function<void()> onBack;
            BackNavigationBehavior backNavBehavior = BackNavigationBehavior::PopUntilScaffoldValueChange;
            PaneScaffoldDirective directive;
            ThreePaneScaffoldAdaptStrategies adaptStrategies;
            nav3::NavEntries<T> allEntries, scaffoldEntries;
            std::vector<std::size_t> scaffoldEntryIndices;
            std::vector<ThreePaneScaffoldDestinationItem<std::string>> entriesAsNavItems;
            std::function<std::optional<ThreePaneScaffoldRole>(const nav3::NavEntry<T>&)> getPaneRole;
            ThreePaneScaffoldType scaffoldType = ThreePaneScaffoldType::ListDetail;
            DetailPlaceholder detailPlaceholder;
            DragHandleContent paneExpansionDragHandle;
            PaneExpansionState* paneExpansionState = nullptr;
        };

        explicit ThreePaneScaffoldScene(Params p) : m_P(std::move(p)) { CalculateOnBackResult(); }

        std::string Key() const override { return m_P.key; }
        const nav3::NavEntries<T>& Entries() const override { return m_P.scaffoldEntries; }
        const nav3::NavEntries<T>& PreviousEntries() const override { return m_PreviousEntries; }
        ThreePaneScaffoldValue CurrentScaffoldValue() const { return ScaffoldValueOf(m_P.entriesAsNavItems.size()); }
        // Where back goes inside the scaffold (null when it leaves the scaffold).
        const std::optional<ThreePaneScaffoldValue>& PreviousScaffoldValue() const { return m_PreviousScaffoldValue; }

        bool Equals(const nav3::Scene<T>& other) const override {
            const auto& o = static_cast<const ThreePaneScaffoldScene&>(other);
            return m_P.key == o.m_P.key && m_P.backNavBehavior == o.m_P.backNavBehavior && m_P.directive == o.m_P.directive &&
                   m_P.adaptStrategies == o.m_P.adaptStrategies && m_P.allEntries == o.m_P.allEntries && m_PreviousEntries == o.m_PreviousEntries &&
                   m_P.scaffoldEntries == o.m_P.scaffoldEntries && m_P.scaffoldEntryIndices == o.m_P.scaffoldEntryIndices &&
                   m_P.entriesAsNavItems == o.m_P.entriesAsNavItems;
        }

        void Content() const override {
            auto& c = *rt::Composition::Current();
            const ThreePaneScaffoldValue scaffoldValue = CurrentScaffoldValue();
            auto& scaffoldState = c.Remember<std::shared_ptr<ThreePaneScaffoldState>>([&] { return std::make_shared<ThreePaneScaffoldState>(scaffoldValue); });
            ThreePaneScaffoldState* state = scaffoldState.get();
            // LaunchedEffect(scaffoldValue) { animateTo(scaffoldValue) }
            auto& valueVersion = c.Remember<std::shared_ptr<std::pair<ThreePaneScaffoldValue, int>>>(
                [&] { return std::make_shared<std::pair<ThreePaneScaffoldValue, int>>(scaffoldValue, 0); });
            if (!(valueVersion->first == scaffoldValue)) { valueVersion->first = scaffoldValue; ++valueVersion->second; }
            c.LaunchedEffect(rt::Keys(valueVersion->second), [state, scaffoldValue]() -> rt::Task<> {
                return [](ThreePaneScaffoldState* s, ThreePaneScaffoldValue v) -> rt::Task<> { co_await s->AnimateTo(v); }(state, scaffoldValue);
            });

            const std::optional<ThreePaneScaffoldValue> previousScaffoldValue = m_PreviousScaffoldValue;
            ThreePaneScaffoldSceneInfo info;
            info.key = m_P.key;
            for (const auto& e : m_P.scaffoldEntries) info.entries.push_back(e.ContentKey());
            auto& gestureState = foundation::RememberNavigationEventState(info);
            const std::size_t pops = m_P.allEntries.size() - m_PreviousEntries.size();
            const std::function<void()> onBack = m_P.onBack;
            foundation::NavigationBackHandler(gestureState, previousScaffoldValue.has_value(), [] {}, [onBack, pops] {
                for (std::size_t i = 0; i < pops; ++i) onBack();
            });
            const foundation::NavigationEventTransitionState transitionState = gestureState.TransitionState();
            c.LaunchedEffect(rt::Keys(transitionState.inProgress, transitionState.latestEvent.progress, int(transitionState.latestEvent.swipeEdge)),
                             [state, transitionState, scaffoldValue, previousScaffoldValue]() -> rt::Task<> {
                return [](ThreePaneScaffoldState* s, foundation::NavigationEventTransitionState t, ThreePaneScaffoldValue value,
                          std::optional<ThreePaneScaffoldValue> previous) -> rt::Task<> {
                    // In progress: scrub the scaffold; completed or cancelled: back to the stable state.
                    if (t.inProgress && previous) co_await s->SeekTo(detail::SceneBackProgress(t.latestEvent.progress, value), *previous);
                    else co_await s->AnimateTo(value);
                }(state, transitionState, scaffoldValue, previousScaffoldValue);
            });

            if (m_P.scaffoldType == ThreePaneScaffoldType::ListDetail) ListDetailContent(*state);
            else SupportingPaneContent(*state);
        }

    private:
        ThreePaneScaffoldValue ScaffoldValueOf(std::size_t historyLength) const {
            std::vector<ThreePaneScaffoldRole> history;
            for (std::size_t i = 0; i < historyLength && i < m_P.entriesAsNavItems.size(); ++i) history.push_back(m_P.entriesAsNavItems[i].pane);
            return CalculateThreePaneScaffoldValue(m_P.directive.maxHorizontalPartitions, m_P.adaptStrategies, history, m_P.directive.maxVerticalPartitions);
        }

        int PreviousDestinationIndex() const {
            const auto& items = m_P.entriesAsNavItems;
            if (items.size() <= 1) return -1;
            const auto& current = items.back();
            const ThreePaneScaffoldValue currentValue = CurrentScaffoldValue();
            const int last = int(items.size()) - 1;
            switch (m_P.backNavBehavior) {
                case BackNavigationBehavior::PopLatest: return last - 1;
                case BackNavigationBehavior::PopUntilScaffoldValueChange:
                    for (int i = last - 1; i >= 0; --i)
                        if (!(ScaffoldValueOf(std::size_t(i) + 1) == currentValue)) return i;
                    break;
                case BackNavigationBehavior::PopUntilCurrentDestinationChange:
                    for (int i = last - 1; i >= 0; --i)
                        if (items[std::size_t(i)].pane != current.pane) return i;
                    break;
                case BackNavigationBehavior::PopUntilContentChange:
                    for (int i = last - 1; i >= 0; --i) {
                        if (items[std::size_t(i)].contentKey != current.contentKey) return i;
                        // A scaffold value change also counts as a content change.
                        if (!(ScaffoldValueOf(std::size_t(i) + 1) == currentValue)) return i;
                    }
                    break;
            }
            return -1;
        }

        // calculateOnBackResult: back stays in the scaffold (to the previous destination's value)
        // or leaves it at the first entry that is not the scaffold's.
        void CalculateOnBackResult() {
            const int prevRelative = PreviousDestinationIndex();
            const long prevAbsolute = prevRelative < 0 ? long(m_P.scaffoldEntryIndices.front()) - 1 : long(m_P.scaffoldEntryIndices[std::size_t(prevRelative)]);
            const auto& all = m_P.allEntries;
            for (long index = long(all.size()) - 1; index >= 0; --index) {
                const bool inScaffold = std::find(m_P.scaffoldEntryIndices.begin(), m_P.scaffoldEntryIndices.end(), std::size_t(index)) != m_P.scaffoldEntryIndices.end();
                if (!inScaffold) {
                    m_PreviousScaffoldValue.reset();
                    m_PreviousEntries.assign(all.begin(), all.begin() + index + 1);
                    return;
                }
                if (index == prevAbsolute) {
                    m_PreviousScaffoldValue = ScaffoldValueOf(std::size_t(prevRelative) + 1);
                    m_PreviousEntries.assign(all.begin(), all.begin() + index + 1);
                    return;
                }
            }
            m_PreviousScaffoldValue.reset();
            m_PreviousEntries.clear();
        }

        std::optional<nav3::NavEntry<T>> LastOfRole(ThreePaneScaffoldRole role) const {
            for (auto it = m_P.scaffoldEntries.rbegin(); it != m_P.scaffoldEntries.rend(); ++it)
                if (m_P.getPaneRole(*it) == role) return *it;
            return std::nullopt;
        }

        static ui::Modifier PreferredSizeModifier(const ThreePaneScaffoldScope& scope, const std::optional<nav3::NavEntry<T>>& entry) {
            ui::Modifier m;
            if (!entry) return m;
            const auto& meta = entry->Metadata();
            if (auto it = meta.find(MetadataPreferredWidthKey); it != meta.end()) {
                if (const auto* f = std::any_cast<float>(&it->second)) m = scope.PreferredWidth(m, *f);
                else if (const auto* d = std::any_cast<ui::Dp>(&it->second)) m = scope.PreferredWidth(m, *d);
            }
            if (auto it = meta.find(MetadataPreferredHeightKey); it != meta.end()) {
                if (const auto* f = std::any_cast<float>(&it->second)) m = scope.PreferredHeight(m, *f);
                else if (const auto* d = std::any_cast<ui::Dp>(&it->second)) m = scope.PreferredHeight(m, *d);
            }
            return m;
        }

        static AnimatedPaneOptions PaneOptions(const ThreePaneScaffoldPaneScope& scope, const std::optional<nav3::NavEntry<T>>& entry,
                                               const std::optional<nav3::NavEntry<T>>& boundsFrom) {
            AnimatedPaneOptions o;
            if (entry) {
                const auto& meta = entry->Metadata();
                if (auto it = meta.find(MetadataEnterTransitionKey); it != meta.end())
                    if (const auto* e = std::any_cast<foundation::EnterTransition>(&it->second)) o.enterTransition = *e;
                if (auto it = meta.find(MetadataExitTransitionKey); it != meta.end())
                    if (const auto* e = std::any_cast<foundation::ExitTransition>(&it->second)) o.exitTransition = *e;
            }
            if (!o.enterTransition) o.enterTransition = CalculateDefaultEnterTransition(scope.MotionDataProvider(), scope.paneRole);
            if (!o.exitTransition) o.exitTransition = CalculateDefaultExitTransition(scope.MotionDataProvider(), scope.paneRole);
            if (boundsFrom) {
                const auto& meta = boundsFrom->Metadata();
                if (auto it = meta.find(MetadataBoundsAnimationSpecKey); it != meta.end())
                    if (const auto* s = std::any_cast<anim::SpecPtr>(&it->second)) o.boundsAnimationSpec = *s;
            }
            return o;
        }

        // A pane showing `entry` (or `fallback` without one), with the scene scope provided.
        template<typename Scope, typename Local>
        static PaneContent Pane(const Local& local, std::optional<nav3::NavEntry<T>> entry, std::optional<nav3::NavEntry<T>> boundsFrom,
                                std::function<void(const ThreePaneScaffoldPaneScope&)> fallback = {}) {
            return [&local, entry, boundsFrom, fallback](const ThreePaneScaffoldPaneScope& paneScope) {
                // Copies: the nested scopes may recompose after this call returns.
                const ThreePaneScaffoldPaneScope s = paneScope;
                rt::Composition::Current()->Provide(local, std::optional<Scope>(Scope{ &paneScope }), [&local, s, entry, boundsFrom, fallback] {
                    AnimatedPane(s, PreferredSizeModifier(s, entry), PaneOptions(s, entry, boundsFrom), [s, entry, fallback] {
                        if (entry) entry->Content();
                        else if (fallback) fallback(s);
                    });
                });
            };
        }

        void ListDetailContent(ThreePaneScaffoldState& state) const {
            const auto lastList = LastOfRole(ListDetailPaneScaffoldRole::List);
            const auto lastDetail = LastOfRole(ListDetailPaneScaffoldRole::Detail);
            const auto lastExtra = LastOfRole(ListDetailPaneScaffoldRole::Extra);
            const DetailPlaceholder placeholder = m_P.detailPlaceholder;
            ListDetailPaneScaffoldOptions options;
            if (lastExtra) options.extraPane = Pane<ListDetailSceneScope>(LocalListDetailSceneScope, lastExtra, lastExtra);
            options.paneExpansionDragHandle = m_P.paneExpansionDragHandle;
            options.paneExpansionState = m_P.paneExpansionState;
            ListDetailPaneScaffold(m_P.directive, state,
                                   lastList ? Pane<ListDetailSceneScope>(LocalListDetailSceneScope, lastList, lastList) : PaneContent([](const ThreePaneScaffoldPaneScope&) {}),
                                   // The detail pane's bounds spec comes from the list entry (as upstream).
                                   Pane<ListDetailSceneScope>(LocalListDetailSceneScope, lastDetail, lastList,
                                                              [placeholder](const ThreePaneScaffoldPaneScope& s) { if (placeholder) placeholder(s); }),
                                   {}, options);
        }

        void SupportingPaneContent(ThreePaneScaffoldState& state) const {
            const auto lastMain = LastOfRole(SupportingPaneScaffoldRole::Main);
            const auto lastSupporting = LastOfRole(SupportingPaneScaffoldRole::Supporting);
            const auto lastExtra = LastOfRole(SupportingPaneScaffoldRole::Extra);
            SupportingPaneScaffoldOptions options;
            if (lastExtra) options.extraPane = Pane<SupportingPaneSceneScope>(LocalSupportingPaneSceneScope, lastExtra, lastExtra);
            options.paneExpansionDragHandle = m_P.paneExpansionDragHandle;
            options.paneExpansionState = m_P.paneExpansionState;
            const PaneContent empty = [](const ThreePaneScaffoldPaneScope&) {};
            SupportingPaneScaffold(m_P.directive, state,
                                   lastMain ? Pane<SupportingPaneSceneScope>(LocalSupportingPaneSceneScope, lastMain, lastMain) : empty,
                                   lastSupporting ? Pane<SupportingPaneSceneScope>(LocalSupportingPaneSceneScope, lastSupporting, lastSupporting) : empty,
                                   {}, options);
        }

        Params m_P;
        std::optional<ThreePaneScaffoldValue> m_PreviousScaffoldValue;
        nav3::NavEntries<T> m_PreviousEntries;
    };

    namespace detail {
        // The scaffold scene for the run of entries at the end of the back stack that belong to
        // the last entry's scene (both strategies).
        template<typename T>
        nav3::ScenePtr<T> CalculateScaffoldScene(const nav3::SceneStrategyScope<T>& scope, const nav3::NavEntries<T>& entries, const char* roleKey,
                                                 ThreePaneScaffoldType type, bool shouldHandleSinglePaneLayout, BackNavigationBehavior behavior,
                                                 const PaneScaffoldDirective& directive, const ThreePaneScaffoldAdaptStrategies& adaptStrategies,
                                                 const DragHandleContent& dragHandle, PaneExpansionState* expansionState) {
            const auto metadataOf = [roleKey](const nav3::NavEntry<T>& e) -> const PaneMetadata* {
                auto it = e.Metadata().find(roleKey);
                if (it == e.Metadata().end()) return nullptr;
                const auto* p = std::any_cast<std::shared_ptr<const PaneMetadata>>(&it->second);
                return p ? p->get() : nullptr;
            };
            if (entries.empty()) return nullptr;
            const PaneMetadata* last = metadataOf(entries.back());
            if (!last) return nullptr;
            typename ThreePaneScaffoldScene<T>::Params p;
            p.key = last->sceneKey;
            for (long idx = long(entries.size()) - 1; idx >= 0; --idx) {
                const auto& entry = entries[std::size_t(idx)];
                const PaneMetadata* m = metadataOf(entry);
                if (!m) break;
                if (m->sceneKey == p.key) {
                    p.scaffoldEntryIndices.insert(p.scaffoldEntryIndices.begin(), std::size_t(idx));
                    p.scaffoldEntries.insert(p.scaffoldEntries.begin(), entry);
                    p.entriesAsNavItems.insert(p.entriesAsNavItems.begin(), ThreePaneScaffoldDestinationItem<std::string>{ m->role, entry.ContentKey() });
                    if (m->isList) p.detailPlaceholder = m->detailPlaceholder;
                }
            }
            if (p.scaffoldEntries.empty()) return nullptr;
            p.onBack = scope.onBack;
            p.backNavBehavior = behavior;
            p.directive = directive;
            p.adaptStrategies = adaptStrategies;
            p.allEntries = entries;
            p.getPaneRole = [metadataOf](const nav3::NavEntry<T>& e) -> std::optional<ThreePaneScaffoldRole> {
                const PaneMetadata* m = metadataOf(e);
                return m ? std::optional<ThreePaneScaffoldRole>(m->role) : std::nullopt;
            };
            p.scaffoldType = type;
            p.paneExpansionDragHandle = dragHandle;
            p.paneExpansionState = expansionState;
            auto scene = std::make_shared<ThreePaneScaffoldScene<T>>(std::move(p));
            const int panes = PaneCount(scene->CurrentScaffoldValue());
            if (panes == 1 && shouldHandleSinglePaneLayout) return scene;
            if (panes <= 1) return nullptr;
            return scene;
        }
    }

}
