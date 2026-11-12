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
#include <kiln/material3/RadioButton.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/adaptive/ThreePaneScaffoldNavigator.h>
#include <kiln/material3/adaptive/navigation3/SceneStrategies.h>
#include <kiln/navigation3/NavDisplay.h>
#include <kiln/navigation/NavHost.h>
#include <kiln/foundation/Scroll.h>

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


    namespace {
        struct HomeKey { bool operator==(const HomeKey&) const = default; };
        struct ListKey { bool operator==(const ListKey&) const = default; };
        struct DetailKey { int index; bool operator==(const DetailKey&) const = default; };
        struct ExtraKey { int index; bool operator==(const ExtraKey&) const = default; };
        using NavRoute = std::variant<HomeKey, ListKey, DetailKey, ExtraKey>;
        std::string NavKeyToString(const HomeKey&) { return "HomeKey"; }
        std::string NavKeyToString(const ListKey&) { return "ListKey"; }
        std::string NavKeyToString(const DetailKey& k) { return "DetailKey(index=" + std::to_string(k.index) + ")"; }
        std::string NavKeyToString(const ExtraKey& k) { return "ExtraKey(index=" + std::to_string(k.index) + ")"; }

        // @Serializable: the alternative and its index.
        kiln::rt::Saver<NavRoute> NavRouteSaver() {
            return {
                [](const NavRoute& r) {
                    int index = 0;
                    if (const auto* d = std::get_if<DetailKey>(&r)) index = d->index;
                    if (const auto* e = std::get_if<ExtraKey>(&r)) index = e->index;
                    return kiln::rt::SavedValue(kiln::rt::SavedValue::List{ int(r.index()), index });
                },
                [](const kiln::rt::SavedValue& s) -> std::optional<NavRoute> {
                    const auto* l = std::get_if<kiln::rt::SavedValue::List>(&s.v);
                    if (!l || l->size() != 2) return std::nullopt;
                    const auto* kind = std::get_if<std::int64_t>(&(*l)[0].v);
                    const auto* index = std::get_if<std::int64_t>(&(*l)[1].v);
                    if (!kind || !index) return std::nullopt;
                    switch (*kind) {
                        case 0: return NavRoute(HomeKey{});
                        case 1: return NavRoute(ListKey{});
                        case 2: return NavRoute(DetailKey{ int(*index) });
                        case 3: return NavRoute(ExtraKey{ int(*index) });
                    }
                    return std::nullopt;
                },
            };
        }

        void NavListPaneContent(const std::vector<std::string>& items, std::optional<int> selectedIndex, std::function<void(int)> onItemClick) {
            Column(SelectableGroup({}), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&items, selectedIndex, onItemClick] {
                for (int index = 0; index < int(items.size()); ++index)
                    ListCard(items[std::size_t(index)], selectedIndex && index == *selectedIndex, [onItemClick, index] { onItemClick(index); });
            });
        }

        void NavDetailPaneContent(std::optional<std::string> selectedItem, std::function<void()> onShowExtra, std::function<void()> backButton = {}) {
            const std::string title = selectedItem ? *selectedItem : "No item selected";
            const std::string description = selectedItem ? "This is a description about " + *selectedItem + "." : "Select an item from the list.";
            BasicScreen(title, backButton ? backButton : [] {}, m3::MaterialTheme::colorScheme().surfaceContainer, [description, selectedItem, onShowExtra] {
                m3::Text(description);
                if (selectedItem) m3::Button(onShowExtra, {}, {}, [] { m3::Text("Show extra"); });
            });
        }

        void NavExtraPaneContent(const std::string& item, std::function<void()> backButton = {}) {
            BasicScreen(item, backButton ? backButton : [] {}, m3::MaterialTheme::colorScheme().surfaceContainerHighest,
                        [item] { m3::Text("This is extra content about " + item + "."); });
        }
    }

    void ListDetailWithNavigation3Sample() {
        namespace N = kiln::navigation3;
        using Strategy = A::navigation3::ListDetailSceneStrategy<NavRoute>;
        N::NavBackStack<NavRoute>& backStack = N::RememberNavBackStack<NavRoute>(NavRouteSaver(), { HomeKey{} });
        const auto sceneStrategy = A::navigation3::RememberListDetailSceneStrategy<NavRoute>();
        std::optional<int> selectedIndex;
        if (const auto last = backStack.LastOrNull()) {
            if (const auto* d = std::get_if<DetailKey>(&*last)) selectedIndex = d->index;
            if (const auto* e = std::get_if<ExtraKey>(&*last)) selectedIndex = e->index;
        }
        N::NavBackStack<NavRoute>* stack = &backStack;
        N::NavDisplayOptions<NavRoute> options;
        options.sceneStrategies = { sceneStrategy };
        N::NavDisplay(backStack, N::EntryProvider<NavRoute>([stack, selectedIndex](N::EntryProviderScope<NavRoute>& p) {
            p.Entry<HomeKey>([stack](const HomeKey&) {
                Box(FillMax({}, Axis::Both), Alignment::Center(), false, [stack] {
                    m3::Button([stack] { if (!(stack->Last() == NavRoute(ListKey{}))) stack->Add(ListKey{}); }, {}, {}, [] { m3::Text("Go to list"); });
                });
            });
            p.Entry<ListKey>([stack, selectedIndex](const ListKey&) {
                NavListPaneContent(Items(), selectedIndex, [stack](int index) {
                    const NavRoute dest = DetailKey{ index };
                    if (!(stack->Last() == dest)) stack->Add(dest);
                });
            }, Strategy::ListPane({}, [](const A::ThreePaneScaffoldScope&) { NavDetailPaneContent(std::nullopt, [] {}); }));
            p.Entry<DetailKey>([stack, selectedIndex](const DetailKey&) {
                const bool inScene = Here().Read(A::navigation3::LocalListDetailSceneScope).has_value();
                NavDetailPaneContent(selectedIndex ? std::optional<std::string>(Items()[std::size_t(*selectedIndex)]) : std::nullopt,
                                     [stack, selectedIndex] {
                                         const NavRoute dest = ExtraKey{ *selectedIndex };
                                         if (!(stack->Last() == dest)) stack->Add(dest);
                                     },
                                     // Only show the back button in a single-pane context.
                                     inScene ? std::function<void()>() : std::function<void()>([stack] { BackButton([stack] { stack->RemoveLast(); }, true); }));
            }, Strategy::DetailPane());
            p.Entry<ExtraKey>([stack, selectedIndex](const ExtraKey&) {
                const bool inScene = Here().Read(A::navigation3::LocalListDetailSceneScope).has_value();
                NavExtraPaneContent(ExtraItems()[std::size_t(*selectedIndex)],
                                    inScene ? std::function<void()>() : std::function<void()>([stack] { BackButton([stack] { stack->RemoveLast(); }, true); }));
            }, Strategy::ExtraPane());
        }), FillMax({}, Axis::Both), options);
    }


    namespace {
        void RadioButtonRow(bool selected, std::function<void()> onClick, const std::string& text) {
            ClickableOptions click;
            click.role = Role::RadioButton;
            Row(Selectable({}, selected, onClick, click), Arrangement::Start(), AlignmentV::CenterVertically(), [selected, onClick, text] {
                m3::RadioButton(selected, onClick);
                m3::Text(text);
            });
        }
    }

    void ListDetailWithNavigation2Sample() {
        namespace Nav = kiln::navigation;
        const std::string welcomeRoute = "welcome";
        const std::string listDetailRoute = "listdetail";
        const LaunchScope coroutineScope = RememberCoroutineScope();
        // `navController` handles navigation outside the ListDetailPaneScaffold, and
        // `scaffoldNavigator` handles navigation within it.
        Nav::NavHostController& navController = Nav::RememberNavController();
        Navigator& scaffoldNavigator = A::RememberListDetailPaneScaffoldNavigator<NavItemData>(std::nullopt, A::ListDetailPaneScaffoldDefaults::adaptStrategies(),
                                                                                                 true, std::nullopt, NavItemSaver());
        // Back behavior can be customized based on the needs of the app.
        auto& backBehaviorIndex = RememberSaveableState(0);
        static const A::BackNavigationBehavior backBehaviors[] = {
            A::BackNavigationBehavior::PopUntilScaffoldValueChange,
            A::BackNavigationBehavior::PopUntilCurrentDestinationChange,
            A::BackNavigationBehavior::PopUntilContentChange,
            A::BackNavigationBehavior::PopLatest,
        };
        Nav::NavController* nc = &navController;
        Navigator* sn = &scaffoldNavigator;
        auto* indexState = &backBehaviorIndex;

        Nav::NavHostOptions options;
        options.enterTransition = [](Nav::NavTransitionScope&) -> std::optional<EnterTransition> { return SlideInHorizontally([](int w) { return w; }); };
        options.exitTransition = [](Nav::NavTransitionScope&) -> std::optional<ExitTransition> { return SlideOutHorizontally([](int w) { return -w; }); };
        options.popEnterTransition = [](Nav::NavTransitionScope&) -> std::optional<EnterTransition> { return SlideInHorizontally([](int w) { return -w; }); };
        options.popExitTransition = [](Nav::NavTransitionScope&) -> std::optional<ExitTransition> { return SlideOutHorizontally([](int w) { return w; }); };
        Nav::NavHost(navController, welcomeRoute, [=](Nav::NavGraphBuilder& b) {
            b.Composable(welcomeRoute, [nc, indexState, listDetailRoute](const Nav::NavBackStackEntry&) {
                m3::Scaffold(FillMax({}, Axis::Both), {}, [nc, indexState, listDetailRoute](const PaddingValues& paddingValues) {
                    auto& scroll = RememberScrollState();
                    Column(FillMax(Padding(Padding(VerticalScroll({}, scroll), paddingValues), 24_dp), Axis::Both), Arrangement::SpacedBy(16_dp), AlignmentH::Start(),
                           [nc, indexState, listDetailRoute] {
                        TextStyled("How should the scaffold handle back navigation?", m3::MaterialTheme::typography().headlineMedium);
                        const int index = indexState->Get();
                        RadioButtonRow(index == 0, [indexState] { indexState->Set(0); },
                                       "PopUntilScaffoldValueChange - Back navigation forces a change in which pane(s) is/are shown.");
                        RadioButtonRow(index == 1, [indexState] { indexState->Set(1); },
                                       "PopUntilCurrentDestinationChange - Back navigation forces a change in which pane is currently considered \"active\".");
                        RadioButtonRow(index == 2, [indexState] { indexState->Set(2); },
                                       "PopUntilContentChange - Back navigation forces a change in the content of any pane or which pane(s) is/are shown.\n"
                                       "Note: this may result in unintuitive behavior if the device size changes in the middle of the navigation.");
                        RadioButtonRow(index == 3, [indexState] { indexState->Set(3); },
                                       "PopLatest - No special back handling.\n"
                                       "Note: this may result in unintuitive behavior if the device size changes in the middle of the navigation.");
                        m3::Button([nc, listDetailRoute] { nc->Navigate(listDetailRoute); }, {}, {}, [] { m3::Text("Next"); });
                    });
                });
            });
            b.Composable(listDetailRoute, [sn, coroutineScope, indexState](const Nav::NavBackStackEntry&) {
                const A::BackNavigationBehavior behavior = backBehaviors[indexState->Get()];
                const auto destination = sn->CurrentDestination();
                const std::optional<NavItemData> selectedItem = destination ? destination->contentKey : std::nullopt;
                A::NavigableScaffoldOptions o;
                o.defaultBackBehavior = behavior;
                o.extraPane = [sn, selectedItem, coroutineScope, behavior](const ThreePaneScaffoldPaneScope& s) {
                    A::AnimatedPane(s, [sn, selectedItem, coroutineScope, behavior] { ExtraPaneContent(ExtraItems(), selectedItem, *sn, coroutineScope, behavior); });
                };
                A::NavigableListDetailPaneScaffold(*sn,
                    [sn, selectedItem, coroutineScope](const ThreePaneScaffoldPaneScope& s) {
                        A::AnimatedPane(s, s.PreferredWidth({}, 200_dp), {}, [sn, selectedItem, coroutineScope] { ListPaneContent(Items(), selectedItem, *sn, coroutineScope); });
                    },
                    [sn, selectedItem, coroutineScope, behavior](const ThreePaneScaffoldPaneScope& s) {
                        A::AnimatedPane(s, [sn, selectedItem, coroutineScope, behavior] { DetailPaneContent(Items(), selectedItem, *sn, coroutineScope, true, behavior); });
                    },
                    {}, o);
            });
        }, {}, options);
    }

}
