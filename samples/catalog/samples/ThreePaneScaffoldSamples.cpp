// ThreePaneScaffoldSample.kt (adaptive samples)
#include "ThreePaneScaffoldSamples.h"
#include "Common.h"

#include <kiln/foundation/Gestures.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/BottomSheet.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Card.h>
#include <kiln/material3/DragHandle.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/adaptive/ThreePaneScaffoldNavigator.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catalog::samples {
    namespace A = m3::adaptive;
    using A::ThreePaneScaffoldPaneScope;
    using A::ThreePaneScaffoldRole;

    namespace {
        struct NavItemData {
            int index = 0;
            bool showExtra = false;
            bool operator==(const NavItemData&) const = default;
        };
        // The Parcelable: index and showExtra.
        kiln::rt::Saver<NavItemData> NavItemSaver() {
            return {
                [](const NavItemData& d) { return kiln::rt::SavedValue(kiln::rt::SavedValue::List{ d.index, d.showExtra }); },
                [](const kiln::rt::SavedValue& s) -> std::optional<NavItemData> {
                    const auto* l = std::get_if<kiln::rt::SavedValue::List>(&s.v);
                    if (!l || l->size() != 2) return std::nullopt;
                    const auto* i = std::get_if<std::int64_t>(&(*l)[0].v);
                    const auto* b = std::get_if<bool>(&(*l)[1].v);
                    if (!i || !b) return std::nullopt;
                    return NavItemData{ int(*i), *b };
                },
            };
        }
        using Navigator = A::ThreePaneScaffoldNavigator<NavItemData>;

        bool IsExpanded(const Navigator& n, ThreePaneScaffoldRole role) { return n.ScaffoldValue()[role] == A::PaneAdaptedValue::Expanded(); }

        const std::vector<A::PaneExpansionAnchor>& PaneExpansionAnchors() {
            static const std::vector<A::PaneExpansionAnchor> anchors{
                A::PaneExpansionAnchor::Proportion(0), A::PaneExpansionAnchor::OffsetFromStart(240_dp), A::PaneExpansionAnchor::Proportion(0.5f),
                A::PaneExpansionAnchor::OffsetFromEnd(240_dp), A::PaneExpansionAnchor::Proportion(1),
            };
            return anchors;
        }

        void ListCard(const std::string& title, bool isSelected, std::function<void()> onClick) {
            m3::CardOptions o;
            o.colors = m3::CardDefaults::outlinedCardColors(isSelected ? m3::MaterialTheme::colorScheme().primaryContainer
                                                                       : m3::MaterialTheme::colorScheme().surface);
            m3::OutlinedCard(onClick, Selectable(FillMax(Height({}, 80_dp), Axis::Horizontal), isSelected, onClick), o, [title, isSelected] {
                Row(Padding({}, 12_dp), Arrangement::SpacedBy(12_dp), AlignmentV::CenterVertically(), [title, isSelected] {
                    m3::Icon(isSelected ? Icons::Filled::CheckCircle() : Icons::Outlined::CheckCircle(), std::nullopt, FillMax({}, Axis::Vertical));
                    TextStyled(title, m3::MaterialTheme::typography().headlineSmall, WrapContent(FillMax({}, Axis::Both), Axis::Vertical));
                });
            });
        }

        void ListPaneContent(const std::vector<std::string>& items, std::optional<NavItemData> selectedItem, Navigator& navigator, LaunchScope scope) {
            Column(SelectableGroup({}), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&items, selectedItem, &navigator, scope] {
                for (int index = 0; index < int(items.size()); ++index) {
                    Navigator* n = &navigator;
                    ListCard(items[std::size_t(index)], selectedItem && index == selectedItem->index && IsExpanded(navigator, A::ListDetailPaneScaffoldRole::Detail),
                             [n, index, scope] { scope.Launch(n->NavigateTo(A::ListDetailPaneScaffoldRole::Detail, NavItemData{ index })); });
                }
            });
        }

        void BackButton(std::function<void()> onClick, bool visible) {
            AnimatedVisibility(visible, {}, [onClick] {
                m3::IconButton(onClick, {}, {}, [] { m3::Icon(Icons::AutoMirrored::Filled::ArrowBack(), std::string("Back")); });
            }, FadeIn() + ExpandHorizontally(), ShrinkHorizontally() + FadeOut());
        }

        // A screen: a top app bar over a card holding the content.
        void BasicScreen(const std::string& title, std::function<void()> backButton, Color containerColor, std::function<void()> content) {
            m3::ScaffoldOptions o;
            o.topBar = [title, backButton] {
                m3::TopAppBarOptions t;
                t.navigationIcon = backButton;
                m3::TopAppBar([title] { m3::Text(title); }, {}, t);
            };
            m3::Scaffold({}, o, [containerColor, content](const PaddingValues& padding) {
                m3::CardOptions c;
                c.colors = m3::CardDefaults::cardColors(containerColor);
                m3::Card(FillMax(Padding({}, padding), Axis::Both), c, [content] {
                    Column(Padding({}, 16_dp), Arrangement::SpacedBy(12_dp), AlignmentH::Start(), content);
                });
            });
        }

        void DetailPaneContent(const std::vector<std::string>& items, std::optional<NavItemData> selectedItem, Navigator& navigator, LaunchScope scope,
                               bool hasExtraPane = false, A::BackNavigationBehavior backBehavior = A::BackNavigationBehavior::PopUntilScaffoldValueChange) {
            const std::string title = selectedItem ? items[std::size_t(selectedItem->index)] : "No item selected";
            const std::string description = selectedItem ? "This is a description about " + items[std::size_t(selectedItem->index)] + "." : "Select an item from the list.";
            Navigator* n = &navigator;
            BasicScreen(title,
                        [n, scope, backBehavior] {
                            BackButton([n, scope, backBehavior] { scope.Launch([](Navigator* nav, A::BackNavigationBehavior b) -> kiln::rt::Task<> { co_await nav->NavigateBack(b); }(n, backBehavior)); },
                                       !IsExpanded(*n, A::ListDetailPaneScaffoldRole::List));
                        },
                        m3::MaterialTheme::colorScheme().surfaceContainer,
                        [description, selectedItem, hasExtraPane, n, scope] {
                            m3::Text(description);
                            if (selectedItem && hasExtraPane) {
                                const int index = selectedItem->index;
                                m3::Button([n, scope, index] { scope.Launch(n->NavigateTo(A::ListDetailPaneScaffoldRole::Extra, NavItemData{ index, true })); }, {}, {},
                                           [] { m3::Text("Show extra"); });
                            }
                        });
        }

        void MainPaneContent(Navigator& navigator, LaunchScope scope, bool hasExtraPane) {
            Navigator* n = &navigator;
            BasicScreen("My content", [] {}, m3::MaterialTheme::colorScheme().surfaceContainer, [n, scope, hasExtraPane] {
                m3::Text("lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum lorem ipsum "
                         "lorem ipsum lorem ipsum lorem ipsum ");
                if (hasExtraPane)
                    m3::Button([n, scope] { scope.Launch(n->NavigateTo(A::SupportingPaneScaffoldRole::Extra, NavItemData{ 0, true })); }, {}, {},
                               [] { m3::Text("Show extra"); });
            });
        }

        void SupportingPaneContent() {
            static const std::vector<std::string> items{ "Item 1", "Item 2", "Item 3" };
            auto& selectedIndex = RememberSaveableState(-1);
            Column(SelectableGroup({}), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&selectedIndex] {
                for (int index = 0; index < int(items.size()); ++index)
                    ListCard(items[std::size_t(index)], index == selectedIndex.Get(), [&selectedIndex, index] { selectedIndex.Set(index); });
            });
        }

        void ExtraPaneContent(const std::vector<std::string>& extraItems, std::optional<NavItemData> selectedItem, Navigator& navigator, LaunchScope scope,
                              A::BackNavigationBehavior backBehavior = A::BackNavigationBehavior::PopUntilScaffoldValueChange) {
            const std::string item = selectedItem && selectedItem->showExtra ? extraItems[std::size_t(selectedItem->index)] : "";
            Navigator* n = &navigator;
            BasicScreen(item,
                        [n, scope, backBehavior] {
                            BackButton([n, scope, backBehavior] { scope.Launch([](Navigator* nav, A::BackNavigationBehavior b) -> kiln::rt::Task<> { co_await nav->NavigateBack(b); }(n, backBehavior)); },
                                       n->CanNavigateBack(backBehavior));
                        },
                        m3::MaterialTheme::colorScheme().surfaceContainerHighest, [item] { m3::Text("This is extra content about " + item + "."); });
        }

        // PaneExpansionDragHandleSample
        void PaneExpansionDragHandle(const A::ThreePaneScaffoldScope& scope, A::PaneExpansionState& state) {
            auto& c = Here();
            auto interactionSource = RememberInteractionSource();
            m3::VerticalDragHandleOptions o;
            o.interactionSource = interactionSource;
            m3::VerticalDragHandle(scope.PaneExpansionDraggable({}, state, c.Read(m3::LocalMinimumInteractiveComponentSize), interactionSource), o);
        }

        A::DragHandleContent DragHandle() { return [](const A::ThreePaneScaffoldScope& s, A::PaneExpansionState& state) { PaneExpansionDragHandle(s, state); }; }

        A::PaneExpansionStateOptions Anchors(int initialAnchoredIndex = -1) {
            A::PaneExpansionStateOptions o;
            o.anchors = PaneExpansionAnchors();
            o.initialAnchoredIndex = initialAnchoredIndex;
            return o;
        }

        // levitateAsDialogSample: the extra pane as a centred dialog over a scrim (one pane only).
        Navigator& LevitateAsDialog(LaunchScope scope) {
            const auto directive = A::CalculatePaneScaffoldDirective(A::CurrentWindowAdaptiveInfoV2());
            auto& navigatorRef = Here().Remember<std::shared_ptr<Navigator*>>([] { return std::make_shared<Navigator*>(nullptr); });
            auto ref = navigatorRef;
            auto onClick = [ref, scope] {
                if (*ref) scope.Launch([](Navigator* n) -> kiln::rt::Task<> { co_await n->NavigateBack(); }(*ref));
            };
            auto& scrim = Here().Remember<A::ScrimContent>([&] {
                return std::make_shared<const std::function<void()>>([onClick] {
                    A::LevitatedPaneScrim(Semantics({}, [onClick](SemanticsConfiguration& c) {
                        c.contentDescription = { "Scrim" };
                        c.onClickLabel = "Dismiss the extra pane";
                        c.onClick = [onClick] { onClick(); return true; };
                    }), onClick);
                });
            });
            Navigator& navigator = A::RememberListDetailPaneScaffoldNavigator<NavItemData>(
                directive,
                A::SupportingPaneScaffoldDefaults::adaptStrategies(
                    A::AdaptStrategy::Hide(), A::AdaptStrategy::Reflow(A::SupportingPaneScaffoldRole::Main),
                    A::AdaptStrategy::Levitate(Alignment::Center(), scrim).OnlyIfSinglePane(directive)),
                true, std::nullopt, NavItemSaver());
            *ref = &navigator;
            return navigator;
        }

        // levitateAsBottomSheetSample: the extra pane as a resizable bottom sheet (one pane only).
        Navigator& LevitateAsBottomSheet() {
            const auto directive = A::CalculatePaneScaffoldDirective(A::CurrentWindowAdaptiveInfoV2());
            auto dragToResizeState = A::RememberDragToResizeState(A::DockedEdge::Bottom);
            return A::RememberSupportingPaneScaffoldNavigator<NavItemData>(
                directive,
                A::SupportingPaneScaffoldDefaults::adaptStrategies(
                    A::AdaptStrategy::Hide(), A::AdaptStrategy::Reflow(A::SupportingPaneScaffoldRole::Main),
                    A::AdaptStrategy::Levitate(Alignment::BottomCenter(), nullptr, dragToResizeState).OnlyIfSinglePane(directive)),
                true, std::nullopt, NavItemSaver());
        }

        const std::vector<std::string>& Items() { static const std::vector<std::string> v{ "Item 1", "Item 2", "Item 3" }; return v; }
        const std::vector<std::string>& ExtraItems() { static const std::vector<std::string> v{ "Extra 1", "Extra 2", "Extra 3" }; return v; }
    }

    void ListDetailPaneScaffoldSample() {
        const LaunchScope scope = RememberCoroutineScope();
        Navigator& navigator = A::RememberListDetailPaneScaffoldNavigator<NavItemData>(std::nullopt, A::ListDetailPaneScaffoldDefaults::adaptStrategies(), true,
                                                                                         std::nullopt, NavItemSaver());
        const auto destination = navigator.CurrentDestination();
        const std::optional<NavItemData> selectedItem = destination ? destination->contentKey : std::nullopt;
        Navigator* n = &navigator;
        A::ListDetailPaneScaffold(navigator.ScaffoldDirective(), navigator.ScaffoldState(),
            [n, selectedItem, scope](const ThreePaneScaffoldPaneScope& s) {
                A::AnimatedPane(s, s.PreferredWidth({}, 200_dp), {}, [n, selectedItem, scope] { ListPaneContent(Items(), selectedItem, *n, scope); });
            },
            [n, selectedItem, scope](const ThreePaneScaffoldPaneScope& s) {
                A::AnimatedPane(s, [n, selectedItem, scope] { DetailPaneContent(Items(), selectedItem, *n, scope); });
            });
    }

    namespace {
        void ListDetailWithExtra(Navigator& navigator, LaunchScope scope, bool roundedExtra) {
            const auto destination = navigator.CurrentDestination();
            const std::optional<NavItemData> selectedItem = destination ? destination->contentKey : std::nullopt;
            Navigator* n = &navigator;
            A::ListDetailPaneScaffoldOptions o;
            o.extraPane = [n, selectedItem, scope, roundedExtra](const ThreePaneScaffoldPaneScope& s) {
                A::AnimatedPaneOptions p;
                if (roundedExtra) p.shape = RoundedCorner(16_dp);
                A::AnimatedPane(s, {}, p, [n, selectedItem, scope] { ExtraPaneContent(ExtraItems(), selectedItem, *n, scope); });
            };
            o.paneExpansionState = &A::RememberPaneExpansionState(navigator.ScaffoldValue(), Anchors(1));
            o.paneExpansionDragHandle = DragHandle();
            A::ListDetailPaneScaffold(navigator.ScaffoldDirective(), navigator.ScaffoldState(),
                [n, selectedItem, scope](const ThreePaneScaffoldPaneScope& s) {
                    A::AnimatedPane(s, s.PreferredWidth({}, 200_dp), {}, [n, selectedItem, scope] { ListPaneContent(Items(), selectedItem, *n, scope); });
                },
                [n, selectedItem, scope](const ThreePaneScaffoldPaneScope& s) {
                    A::AnimatedPane(s, [n, selectedItem, scope] { DetailPaneContent(Items(), selectedItem, *n, scope, true); });
                },
                {}, o);
        }
    }

    void ListDetailPaneScaffoldSampleWithExtraPane() {
        const LaunchScope scope = RememberCoroutineScope();
        Navigator& navigator = A::RememberListDetailPaneScaffoldNavigator<NavItemData>(std::nullopt, A::ListDetailPaneScaffoldDefaults::adaptStrategies(), true,
                                                                                         std::nullopt, NavItemSaver());
        ListDetailWithExtra(navigator, scope, false);
    }

    void ListDetailPaneScaffoldSampleWithExtraPaneLevitatedAsDialog() {
        const LaunchScope scope = RememberCoroutineScope();
        ListDetailWithExtra(LevitateAsDialog(scope), scope, true);
    }

    namespace {
        void Supporting(Navigator& navigator, LaunchScope scope, bool bottomSheet) {
            const auto destination = navigator.CurrentDestination();
            const std::optional<NavItemData> selectedItem = destination ? destination->contentKey : std::nullopt;
            static const std::vector<std::string> extraItems{ "Extra content" };
            Navigator* n = &navigator;
            A::SupportingPaneScaffoldOptions o;
            o.extraPane = [n, selectedItem, scope, bottomSheet](const ThreePaneScaffoldPaneScope& s) {
                Modifier m;
                A::AnimatedPaneOptions p;
                if (bottomSheet) {
                    m = Background(s.PreferredHeight(s.PreferredWidth({}, 1.0f), 0.5f), m3::MaterialTheme::colorScheme().surface);
                    p.dragToResizeHandle = [](A::DragToResizeState&) { m3::BottomSheetDefaults::DragHandle(); };
                }
                A::AnimatedPane(s, m, p, [n, selectedItem, scope] { ExtraPaneContent(extraItems, selectedItem, *n, scope); });
            };
            o.paneExpansionState = &A::RememberPaneExpansionState(navigator.ScaffoldValue(), Anchors());
            o.paneExpansionDragHandle = DragHandle();
            A::SupportingPaneScaffold(navigator.ScaffoldDirective(), navigator.ScaffoldState(),
                [n, scope](const ThreePaneScaffoldPaneScope& s) { A::AnimatedPane(s, [n, scope] { MainPaneContent(*n, scope, true); }); },
                [](const ThreePaneScaffoldPaneScope& s) { A::AnimatedPane(s, s.PreferredWidth({}, 200_dp), {}, [] { SupportingPaneContent(); }); },
                {}, o);
        }
    }

    void SupportingPaneScaffoldSample() {
        const LaunchScope scope = RememberCoroutineScope();
        Navigator& navigator = A::RememberSupportingPaneScaffoldNavigator<NavItemData>(std::nullopt, A::SupportingPaneScaffoldDefaults::adaptStrategies(), true,
                                                                                         std::nullopt, NavItemSaver());
        Supporting(navigator, scope, false);
    }

    void SupportingPaneScaffoldSampleWithExtraPaneLevitatedAsBottomSheet() {
        const LaunchScope scope = RememberCoroutineScope();
        Supporting(LevitateAsBottomSheet(), scope, true);
    }

}
