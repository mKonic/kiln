#pragma once

// adaptive-navigation: ThreePaneScaffoldNavigator.kt (the destination history of a three-pane
// scaffold and its scaffold value; navigating forward and back, and seeking back with a
// predictive back gesture), BackNavigationBehavior.kt, ThreePaneScaffoldPredictiveBackHandler and
// the navigable list-detail and supporting-pane scaffolds (AndroidThreePaneScaffold.android.kt).

#include "kiln/foundation/Back.h"
#include "kiln/material3/adaptive/ThreePaneScaffold.h"
#include "kiln/material3/adaptive/WindowAdaptiveInfo.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Saveable.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace kiln::material3::adaptive {

    // BackNavigationBehavior: how far back navigation pops the destination history.
    enum class BackNavigationBehavior : std::uint8_t {
        PopLatest,                          // the latest destination
        PopUntilScaffoldValueChange,        // until the scaffold value changes
        PopUntilCurrentDestinationChange,   // until the current destination's pane changes
        PopUntilContentChange,              // until the content key (or the scaffold value) changes
    };

    namespace detail {
        // calculateScaffoldValue over the history up to `index` (-1: no destination).
        ThreePaneScaffoldValue CalculateScaffoldValue(const PaneScaffoldDirective& directive, const ThreePaneScaffoldAdaptStrategies& strategies,
                                                      bool historyAware, const std::vector<ThreePaneScaffoldRole>& panes, int index);
        std::size_t SettingsHash(const PaneScaffoldDirective& directive, const ThreePaneScaffoldAdaptStrategies& strategies, bool historyAware);
        // backProgressToStateProgress: the gesture's eased progress, scaled by the panes shown.
        float BackProgressToStateProgress(float progress, const ThreePaneScaffoldValue& value);
    }

    // ThreePaneScaffoldNavigator<T> (DefaultThreePaneScaffoldNavigator): T is the content key.
    template<typename T>
    class ThreePaneScaffoldNavigator {
    public:
        using Destination = ThreePaneScaffoldDestinationItem<T>;
        ThreePaneScaffoldNavigator(std::vector<Destination> initialHistory, PaneScaffoldDirective directive, ThreePaneScaffoldAdaptStrategies strategies,
                                   bool historyAware)
            : m_History(std::move(initialHistory)), m_Directive(std::move(directive)), m_Strategies(std::move(strategies)),
              m_HistoryAware(historyAware), m_State(ValueAt(int(m_History.Peek().size()) - 1)) {}

        PaneScaffoldDirective ScaffoldDirective() const { return m_Directive.Get(); }
        ThreePaneScaffoldState& ScaffoldState() { return m_State; }
        // scaffoldValue: from the destination history (derived).
        ThreePaneScaffoldValue ScaffoldValue() const { return ValueAt(int(m_History.Get().size()) - 1); }
        ThreePaneScaffoldValue PeekPreviousScaffoldValue(BackNavigationBehavior b = BackNavigationBehavior::PopUntilScaffoldValueChange) const {
            const int index = PreviousDestinationIndex(b);
            return index == -1 ? ScaffoldValue() : ValueAt(index);
        }
        std::optional<Destination> CurrentDestination() const {
            const auto& h = m_History.Get();
            return h.empty() ? std::nullopt : std::optional(h.back());
        }
        bool IsDestinationHistoryAware() const { return m_HistoryAware.Get(); }
        void SetDestinationHistoryAware(bool aware) { m_HistoryAware.Set(aware); }

        rt::Task<> NavigateTo(ThreePaneScaffoldRole pane, std::optional<T> contentKey = std::nullopt) {
            auto h = m_History.Peek();
            h.push_back(Destination{ pane, std::move(contentKey) });
            m_History.Set(std::move(h));
            co_await m_State.AnimateTo(ScaffoldValue());
        }
        bool CanNavigateBack(BackNavigationBehavior b = BackNavigationBehavior::PopUntilScaffoldValueChange) const { return PreviousDestinationIndex(b) >= 0; }
        rt::Task<bool> NavigateBack(BackNavigationBehavior b = BackNavigationBehavior::PopUntilScaffoldValueChange) {
            const int previous = PreviousDestinationIndex(b);
            auto h = m_History.Peek();
            if (previous < 0) {
                h.clear();
                m_History.Set(std::move(h));
                co_await m_State.AnimateTo(ScaffoldValue());
                co_return false;
            }
            h.resize(std::size_t(previous + 1));
            m_History.Set(std::move(h));
            co_await m_State.AnimateTo(ScaffoldValue());
            co_return true;
        }
        rt::Task<> SeekBack(BackNavigationBehavior b = BackNavigationBehavior::PopUntilScaffoldValueChange, float fraction = 1.0f) {
            if (fraction == 0) co_await m_State.AnimateTo(ScaffoldValue());
            else co_await m_State.SeekTo(fraction, PeekPreviousScaffoldValue(b), true);
        }

        // internal (rememberThreePaneScaffoldNavigator)
        void Update(PaneScaffoldDirective directive, ThreePaneScaffoldAdaptStrategies strategies, bool historyAware) {
            if (!(m_Directive.Peek() == directive)) m_Directive.Set(std::move(directive));
            if (!(m_Strategies.Peek() == strategies)) m_Strategies.Set(std::move(strategies));
            if (m_HistoryAware.Peek() != historyAware) m_HistoryAware.Set(historyAware);
        }
        const std::vector<Destination>& History() const { return m_History.Peek(); }
        rt::Job backJob;   // the cancelled gesture's return

    private:
        ThreePaneScaffoldValue ValueAt(int index) const {
            std::vector<ThreePaneScaffoldRole> panes;
            for (const auto& d : m_History.Get()) panes.push_back(d.pane);
            return detail::CalculateScaffoldValue(m_Directive.Get(), m_Strategies.Get(), m_HistoryAware.Get(), panes, index);
        }
        int PreviousDestinationIndex(BackNavigationBehavior b) const {
            const auto& h = m_History.Get();
            if (h.size() <= 1) return -1;
            const int last = int(h.size()) - 1;
            if (b == BackNavigationBehavior::PopLatest) return last - 1;
            const ThreePaneScaffoldValue current = ScaffoldValue();
            for (int i = last - 1; i >= 0; --i) {
                switch (b) {
                    case BackNavigationBehavior::PopUntilScaffoldValueChange:
                        if (!(ValueAt(i) == current)) return i;
                        break;
                    case BackNavigationBehavior::PopUntilCurrentDestinationChange:
                        if (h[std::size_t(i)].pane != h.back().pane) return i;
                        break;
                    case BackNavigationBehavior::PopUntilContentChange:
                        if (!(h[std::size_t(i)].contentKey == h.back().contentKey)) return i;
                        if (!(ValueAt(i) == current)) return i;   // a scaffold change counts as a content change
                        break;
                    case BackNavigationBehavior::PopLatest: break;
                }
            }
            return -1;
        }

        rt::State<std::vector<Destination>> m_History;
        rt::State<PaneScaffoldDirective> m_Directive;
        rt::State<ThreePaneScaffoldAdaptStrategies> m_Strategies;
        rt::State<bool> m_HistoryAware;
        ThreePaneScaffoldState m_State;
    };

    namespace detail {
        // destinationItemSaver, as a list of [pane, has key, key] per destination.
        template<typename T>
        rt::Saver<std::vector<ThreePaneScaffoldDestinationItem<T>>> HistorySaver(rt::Saver<T> keySaver) {
            return {
                [keySaver](const std::vector<ThreePaneScaffoldDestinationItem<T>>& h) {
                    rt::SavedValue::List list;
                    for (const auto& d : h)
                        list.push_back(rt::SavedValue::List{ int(d.pane), d.contentKey.has_value(), d.contentKey ? keySaver.save(*d.contentKey) : rt::SavedValue() });
                    return rt::SavedValue(std::move(list));
                },
                [keySaver](const rt::SavedValue& s) -> std::optional<std::vector<ThreePaneScaffoldDestinationItem<T>>> {
                    const auto* list = std::get_if<rt::SavedValue::List>(&s.v);
                    if (!list) return std::nullopt;
                    std::vector<ThreePaneScaffoldDestinationItem<T>> h;
                    for (const auto& e : *list) {
                        const auto* item = std::get_if<rt::SavedValue::List>(&e.v);
                        if (!item || item->size() != 3) return std::nullopt;
                        const auto* pane = std::get_if<std::int64_t>(&(*item)[0].v);
                        const auto* hasKey = std::get_if<bool>(&(*item)[1].v);
                        if (!pane || !hasKey) return std::nullopt;
                        ThreePaneScaffoldDestinationItem<T> d{ ThreePaneScaffoldRole(*pane), std::nullopt };
                        if (*hasKey) d.contentKey = keySaver.restore((*item)[2]);
                        h.push_back(std::move(d));
                    }
                    return h;
                },
            };
        }

        // rememberThreePaneScaffoldNavigator
        template<typename T>
        ThreePaneScaffoldNavigator<T>& RememberThreePaneScaffoldNavigator(PaneScaffoldDirective directive, ThreePaneScaffoldAdaptStrategies strategies,
                                                                          bool historyAware, std::vector<ThreePaneScaffoldDestinationItem<T>> initialHistory,
                                                                          rt::Saver<T> keySaver, rt::Key key) {
            auto& c = *rt::Composition::Current();
            // The history is what is saved; the navigator is rebuilt around it.
            auto& history = rt::RememberSaveable<std::vector<ThreePaneScaffoldDestinationItem<T>>>([&] { return initialHistory; }, HistorySaver<T>(keySaver), {}, key);
            auto& navigator = c.RememberNew<ThreePaneScaffoldNavigator<T>>({}, key, history, directive, strategies, historyAware);
            navigator.Update(directive, strategies, historyAware);
            history = navigator.History();   // kept for the next save
            auto* np = &navigator;
            c.LaunchedEffect(rt::Keys(SettingsHash(directive, strategies, historyAware)), [np]() -> rt::Task<> {
                const ThreePaneScaffoldValue target = np->ScaffoldValue();
                if (!(np->ScaffoldState().TargetState() == target)) co_await np->ScaffoldState().SnapTo(target);
            }, key);
            return navigator;
        }
    }

    // rememberListDetailPaneScaffoldNavigator: starts at the list pane.
    template<typename T>
    ThreePaneScaffoldNavigator<T>& RememberListDetailPaneScaffoldNavigator(
        std::optional<PaneScaffoldDirective> scaffoldDirective = std::nullopt,
        ThreePaneScaffoldAdaptStrategies adaptStrategies = ListDetailPaneScaffoldDefaults::adaptStrategies(), bool isDestinationHistoryAware = true,
        std::optional<std::vector<ThreePaneScaffoldDestinationItem<T>>> initialDestinationHistory = std::nullopt,
        rt::Saver<T> contentKeySaver = rt::AutoSaver<T>(), rt::Key key = {}) {
        return detail::RememberThreePaneScaffoldNavigator<T>(
            scaffoldDirective ? *scaffoldDirective : CalculatePaneScaffoldDirective(CurrentWindowAdaptiveInfoV2()), std::move(adaptStrategies),
            isDestinationHistoryAware,
            initialDestinationHistory ? *initialDestinationHistory : std::vector<ThreePaneScaffoldDestinationItem<T>>{ { ListDetailPaneScaffoldRole::List, std::nullopt } },
            std::move(contentKeySaver), key);
    }

    // rememberSupportingPaneScaffoldNavigator: starts at the main pane.
    template<typename T>
    ThreePaneScaffoldNavigator<T>& RememberSupportingPaneScaffoldNavigator(
        std::optional<PaneScaffoldDirective> scaffoldDirective = std::nullopt,
        ThreePaneScaffoldAdaptStrategies adaptStrategies = SupportingPaneScaffoldDefaults::adaptStrategies(), bool isDestinationHistoryAware = true,
        std::optional<std::vector<ThreePaneScaffoldDestinationItem<T>>> initialDestinationHistory = std::nullopt,
        rt::Saver<T> contentKeySaver = rt::AutoSaver<T>(), rt::Key key = {}) {
        return detail::RememberThreePaneScaffoldNavigator<T>(
            scaffoldDirective ? *scaffoldDirective : CalculatePaneScaffoldDirective(CurrentWindowAdaptiveInfoV2()), std::move(adaptStrategies),
            isDestinationHistoryAware,
            initialDestinationHistory ? *initialDestinationHistory : std::vector<ThreePaneScaffoldDestinationItem<T>>{ { SupportingPaneScaffoldRole::Main, std::nullopt } },
            std::move(contentKeySaver), key);
    }

    namespace detail {
        template<typename T>
        rt::Task<> HandleBack(ThreePaneScaffoldNavigator<T>* navigator, BackNavigationBehavior behavior, foundation::BackProgress* progress) {
            // Cancelled (the gesture was): seek back to where it started (NonCancellable).
            struct OnCancel {
                ThreePaneScaffoldNavigator<T>* n; BackNavigationBehavior b; bool done = false;
                ~OnCancel() { if (!done) n->backJob = rt::Launch(rt::Dispatcher::Main(), n->SeekBack(b, 0)); }
            } guard{ navigator, behavior };
            while (auto event = co_await progress->Next())
                co_await navigator->SeekBack(behavior, BackProgressToStateProgress(event->progress, navigator->ScaffoldValue()));
            guard.done = true;
            co_await navigator->NavigateBack(behavior);
        }
    }

    // ThreePaneScaffoldPredictiveBackHandler: a back gesture seeks the scaffold back while it
    // runs, then navigates back (or returns when cancelled).
    template<typename T>
    void ThreePaneScaffoldPredictiveBackHandler(ThreePaneScaffoldNavigator<T>& navigator, BackNavigationBehavior backBehavior) {
        auto& c = *rt::Composition::Current();
        auto* np = &navigator;
        c.Child([&] {
            foundation::PredictiveBackHandler(navigator.CanNavigateBack(backBehavior),
                                              [np, backBehavior](foundation::BackProgress& p) { return detail::HandleBack(np, backBehavior, &p); });
        }, rt::Key(std::int64_t(reinterpret_cast<std::uintptr_t>(np)) * 4 + int(backBehavior)));
    }

    struct NavigableScaffoldOptions {
        PaneContent extraPane;
        BackNavigationBehavior defaultBackBehavior = BackNavigationBehavior::PopUntilScaffoldValueChange;
        DragHandleContent paneExpansionDragHandle;
        PaneExpansionState* paneExpansionState = nullptr;
    };

    // NavigableListDetailPaneScaffold: a list-detail scaffold driven by `navigator`, with back
    // handling.
    template<typename T>
    void NavigableListDetailPaneScaffold(ThreePaneScaffoldNavigator<T>& navigator, PaneContent listPane, PaneContent detailPane,
                                         const ui::Modifier& modifier = {}, const NavigableScaffoldOptions& options = {}) {
        ThreePaneScaffoldPredictiveBackHandler(navigator, options.defaultBackBehavior);
        ListDetailPaneScaffold(navigator.ScaffoldDirective(), navigator.ScaffoldState(), std::move(listPane), std::move(detailPane), modifier,
                               ListDetailPaneScaffoldOptions{ options.extraPane, options.paneExpansionDragHandle, options.paneExpansionState });
    }

    template<typename T>
    void NavigableSupportingPaneScaffold(ThreePaneScaffoldNavigator<T>& navigator, PaneContent mainPane, PaneContent supportingPane,
                                         const ui::Modifier& modifier = {}, const NavigableScaffoldOptions& options = {}) {
        ThreePaneScaffoldPredictiveBackHandler(navigator, options.defaultBackBehavior);
        SupportingPaneScaffold(navigator.ScaffoldDirective(), navigator.ScaffoldState(), std::move(mainPane), std::move(supportingPane), modifier,
                               SupportingPaneScaffoldOptions{ options.extraPane, options.paneExpansionDragHandle, options.paneExpansionState });
    }

}
