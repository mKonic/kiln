// AppBarSamples.kt
#include "AppBarSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/AppBarRow.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/adaptive/WindowAdaptiveInfo.h>

#include <memory>
#include <vector>

namespace catalog::samples {
    namespace TD = m3::TopAppBarDefaults;
    namespace BD = m3::BottomAppBarDefaults;
    using ScrollBehavior = std::shared_ptr<m3::TopAppBarScrollBehavior>;

    namespace {

        // Text(text, maxLines = 1, overflow = TextOverflow.Ellipsis)
        kiln::Function<void()> OneLine(std::string text, const Modifier& modifier = {}) {
            return [text, modifier] {
                m3::TextOptions o;
                o.maxLines = 1;
                o.overflow = kiln::text::TextOverflow::Ellipsis;
                m3::Text(text, modifier, o);
            };
        }

        // An IconButton with a plain tooltip above it.
        void TipButton(const std::string& tip, const ImageVectorPtr* icon, const std::string& description) {
            PlainTooltipAbove(tip, [icon, description] {
                m3::IconButton([] { /* doSomething() */ }, {}, {}, [icon, description] { m3::Icon(*icon, description); });
            });
        }
        void MenuButton() { TipButton("Menu", &Icons::Filled::Menu(), "Menu"); }
        void FavoriteButton() { TipButton("Add to favorites", &Icons::Filled::Favorite(), "Add to favorites"); }

        // The samples' content: 0..75 in bodyLarge, 8 dp apart.
        void NumberItems(LazyListScope& scope) {
            scope.Items(76, [](int i) {
                TextStyled(std::to_string(i), m3::MaterialTheme::typography().bodyLarge, Padding(FillMax({}, Axis::Horizontal), 16_dp, 0_dp));
            });
        }
        LazyListOptions Spaced(PaddingValues contentPadding = {}) {
            LazyListOptions o;
            o.arrangement = Arrangement::SpacedBy(8_dp);
            o.contentPadding = contentPadding;
            return o;
        }
        // LazyColumn(modifier = Modifier.padding(innerPadding), ...)
        void NumberList(const PaddingValues& innerPadding, LazyListState* state = nullptr) {
            LazyListState& s = state ? *state : RememberLazyListState();
            LazyColumn(Padding({}, innerPadding), s, NumberItems, Spaced());
        }
        // LazyColumn(contentPadding = innerPadding, ...)
        void PaddedNumberList(const PaddingValues& innerPadding) {
            LazyColumn({}, RememberLazyListState(), NumberItems, Spaced(innerPadding));
        }

        Modifier Nested(const ScrollBehavior& b) { return NestedScroll({}, b->NestedScrollConnection()); }
        Modifier Nested(const std::shared_ptr<m3::BottomAppBarScrollBehavior>& b) { return NestedScroll({}, b->NestedScrollConnection()); }

        // A small top bar sample over the number list.
        void SmallBar(bool centered, const char* title, bool subtitle, bool alignCenter, ScrollBehavior behavior, bool favorite = true) {
            m3::ScaffoldOptions s;
            s.topBar = [=] {
                m3::TopAppBarOptions o;
                if (subtitle) o.subtitle = OneLine("Subtitle");
                if (alignCenter) o.titleHorizontalAlignment = AlignmentH::CenterHorizontally();
                o.navigationIcon = MenuButton;
                if (favorite) o.actions = FavoriteButton;
                o.scrollBehavior = behavior;
                if (centered) m3::CenterAlignedTopAppBar(OneLine(title), {}, o);
                else m3::TopAppBar(OneLine(title), {}, o);
            };
            m3::Scaffold(behavior ? Nested(behavior) : Modifier{}, s, [](const PaddingValues& p) { NumberList(p); });
        }

        using TwoRowsFn = void (*)(kiln::Function<void()>, const Modifier&, const m3::TwoRowsTopAppBarOptions&);
        void TwoRowsBar(TwoRowsFn bar, const char* title, bool subtitle, bool alignCenter, bool tooltips, ScrollBehavior behavior) {
            m3::ScaffoldOptions s;
            s.topBar = [=] {
                m3::TwoRowsTopAppBarOptions o;
                if (subtitle) o.subtitle = OneLine("Subtitle");
                if (alignCenter) o.titleHorizontalAlignment = AlignmentH::CenterHorizontally();
                if (tooltips) {
                    o.navigationIcon = MenuButton;
                    o.actions = FavoriteButton;
                } else {
                    o.navigationIcon = [] { m3::IconButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Menu(), std::string("Menu")); }); };
                    o.actions = [] { m3::IconButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Favorite(), std::string("Add to favorites")); }); };
                }
                o.scrollBehavior = behavior;
                bar(OneLine(title), {}, o);
            };
            m3::Scaffold(behavior ? Nested(behavior) : Modifier{}, s, [](const PaddingValues& p) { NumberList(p); });
        }

        // A FAB with an "Add" tooltip, in the bottom app bar's colours.
        void AddFab(const Modifier& modifier) {
            PlainTooltipAbove("Add", [modifier] {
                m3::FloatingActionButtonOptions o;
                o.containerColor = BD::bottomAppBarFabColor();
                o.elevation = m3::FloatingActionButtonDefaults::bottomAppBarFabElevation();
                m3::FloatingActionButton([] { /* do something */ }, modifier, o, [] { m3::Icon(Icons::Filled::Add(), std::string("Add")); });
            });
        }
        void CheckAndEdit() {
            TipButton("Check", &Icons::Filled::Check(), "Check");
            TipButton("Edit", &Icons::Filled::Edit(), "Edit");
        }

        // The flexible bottom bar samples: five buttons, the middle one filled and 56 dp wide.
        void FlexibleBar(Arrangement arrangement, bool noHorizontalPadding, bool vibrant) {
            auto behavior = BD::exitAlwaysScrollBehavior();
            static const ImageVectorPtr* const icons[5] = { &Icons::AutoMirrored::Filled::ArrowBack(), &Icons::AutoMirrored::Filled::ArrowForward(),
                                                            &Icons::Filled::Add(), &Icons::Filled::Check(), &Icons::Filled::Edit() };
            static const char* const buttons[5] = { "Back", "Forward", "Add", "Check", "Edit" };
            m3::ScaffoldOptions s;
            s.bottomBar = [=] {
                m3::FlexibleBottomAppBarOptions o;
                o.horizontalArrangement = arrangement;
                if (noHorizontalPadding) o.contentPadding = PaddingValues{ 0_dp, 0_dp, 0_dp, 0_dp };
                o.scrollBehavior = behavior;
                if (vibrant) o.containerColor = m3::MaterialTheme::colorScheme().primaryContainer;
                m3::FlexibleBottomAppBar({}, o, [] {
                    for (int index = 0; index < 5; ++index) {
                        const std::string button = buttons[index];
                        const ImageVectorPtr* icon = icons[index];
                        PlainTooltipAbove(button, [index, button, icon] {
                            if (index == 2) {
                                m3::FilledIconButton([] {}, Width({}, 56_dp), {}, [icon, button] { m3::Icon(*icon, button); });
                            } else {
                                m3::IconButton([] {}, {}, {}, [icon, button] { m3::Icon(*icon, button); });
                            }
                        });
                    }
                });
            };
            m3::Scaffold(Nested(behavior), s, [](const PaddingValues& p) { PaddedNumberList(p); });
        }
    }

    void SimpleTopAppBar() { SmallBar(false, "Simple TopAppBar", false, false, nullptr); }

    void SimpleTopAppBarWithAdaptiveActions() {
        const auto sizeClass = m3::adaptive::CurrentWindowAdaptiveInfo().windowSizeClass;
        // Material guidelines state 3 items max in compact, and 5 items max elsewhere.
        const int maxItemCount = sizeClass.minWidthDp >= m3::adaptive::WindowSizeClass::WIDTH_DP_MEDIUM_LOWER_BOUND ? 5 : 3;
        static const ImageVectorPtr* const icons[5] = { &Icons::Filled::Attachment(), &Icons::Filled::Edit(), &Icons::Outlined::Star(),
                                                        &Icons::Filled::Snooze(), &Icons::Outlined::MarkEmailUnread() };
        static const char* const items[5] = { "Attachment", "Edit", "Star", "Snooze", "Mark unread" };
        m3::ScaffoldOptions s;
        s.topBar = [maxItemCount] {
            m3::TopAppBarOptions o;
            o.navigationIcon = MenuButton;
            o.actions = [maxItemCount] {
                m3::AppBarRow({}, [](m3::AppBarMenuState& menu) {
                    PlainTooltipAbove("Overflow", [&menu] {
                        m3::IconButton([&menu] { menu.Show(); }, {}, {}, [] { m3::Icon(Icons::Filled::MoreVert(), std::string("Overflow")); });
                    });
                }, maxItemCount, [](m3::AppBarScope& scope) {
                    for (int index = 0; index < 5; ++index) {
                        const std::string item = items[index];
                        const ImageVectorPtr* icon = icons[index];
                        scope.ClickableItem([] {}, [icon, item] { m3::Icon(*icon, item); }, item);
                    }
                });
            };
            m3::TopAppBar(OneLine("Simple TopAppBar"), {}, o);
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { NumberList(p); });
    }

    void SimpleTopAppBarWithSubtitle() { SmallBar(false, "Simple TopAppBar", true, false, TD::pinnedScrollBehavior()); }
    void SimpleCenterAlignedTopAppBar() { SmallBar(true, "Centered TopAppBar", false, false, nullptr); }
    void SimpleTopAppBarWithSubtitleAndCenterAligned() { SmallBar(false, "Simple TopAppBar", true, true, TD::pinnedScrollBehavior()); }

    void PinnedTopAppBar() {
        auto behavior = TD::pinnedScrollBehavior();
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TopAppBarOptions o;
            o.navigationIcon = MenuButton;
            o.actions = [] {
                FavoriteButton();
                TipButton("Add to starred", &Icons::Filled::Star(), "Star");
            };
            o.scrollBehavior = behavior;
            m3::TopAppBar(OneLine("TopAppBar"), {}, o);
        };
        m3::Scaffold(Nested(behavior), s, [](const PaddingValues& p) { NumberList(p); });
    }

    void PinnedTopAppBarWithPreScrolledLazyColumn() {
        LazyListState& lazyListState = RememberLazyListState(30);
        auto behavior = TD::pinnedScrollBehavior(lazyListState);
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TopAppBarOptions o;
            o.navigationIcon = MenuButton;
            o.scrollBehavior = behavior;
            m3::TopAppBar(OneLine("TopAppBar"), {}, o);
        };
        m3::Scaffold(Nested(behavior), s, [&lazyListState](const PaddingValues& p) { NumberList(p, &lazyListState); });
    }

    void PinnedTopAppBarWithReversedLazyGrid() {
        LazyGridState& lazyGridState = RememberLazyGridState();
        auto behavior = TD::pinnedScrollBehavior(lazyGridState);
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TopAppBarOptions o;
            o.navigationIcon = MenuButton;
            o.scrollBehavior = behavior;
            m3::TopAppBar(OneLine("TopAppBar"), {}, o);
        };
        m3::Scaffold(Nested(behavior), s, [&lazyGridState](const PaddingValues& p) {
            LazyGridOptions o;
            o.reverseLayout = true;
            LazyVerticalGrid(Padding({}, p), GridCells::Adaptive(100_dp), lazyGridState, [](LazyGridScope& scope) {
                scope.Items(76, [](int i) {
                    Box(Height(FillMax({}, Axis::Horizontal), 50_dp), Alignment::TopStart(), false, [i] {
                        TextStyled(std::to_string(i), m3::MaterialTheme::typography().bodyLarge, Padding(FillMax({}, Axis::Horizontal), 16_dp, 0_dp));
                    });
                });
            }, o);
        });
    }

    void EnterAlwaysTopAppBar() {
        auto behavior = TD::enterAlwaysScrollBehavior();
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TopAppBarOptions o;
            o.subtitle = OneLine("Subtitle");
            o.navigationIcon = MenuButton;
            o.actions = FavoriteButton;
            o.scrollBehavior = behavior;
            m3::TopAppBar(OneLine("TopAppBar"), {}, o);
        };
        m3::Scaffold(Nested(behavior), s, [](const PaddingValues& p) { NumberList(p); });
    }

    void EnterAlwaysTopAppBarWithReverseScrolling() {
        ScrollState& scrollState = RememberScrollState();
        auto behavior = TD::enterAlwaysScrollBehavior(scrollState);
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TopAppBarOptions o;
            o.navigationIcon = MenuButton;
            o.scrollBehavior = behavior;
            m3::TopAppBar(OneLine("TopAppBar"), {}, o);
        };
        m3::Scaffold(Nested(behavior), s, [&scrollState](const PaddingValues& p) {
            Column(VerticalScroll(Padding(FillMax({}, Axis::Both), p), scrollState, true, true), Arrangement::Bottom(), AlignmentH::Start(), [] {
                for (int i = 0; i <= 75; ++i) {
                    Box(Padding(FillMax({}, Axis::Both), 16_dp, 0_dp), Alignment::TopStart(), false,
                        [i] { TextStyled(std::to_string(i), m3::MaterialTheme::typography().bodyLarge); }, kiln::rt::Key(i));
                }
            });
        });
    }

    void ExitUntilCollapsedMediumTopAppBar() {
        TwoRowsBar(m3::MediumTopAppBar, "Medium TopAppBar", false, false, true, TD::exitUntilCollapsedScrollBehavior());
    }
    void ExitUntilCollapsedMediumFlexibleTopAppBar() {
        TwoRowsBar(m3::MediumFlexibleTopAppBar, "Medium TopAppBar", true, false, true, TD::exitUntilCollapsedScrollBehavior());
    }
    void MediumFlexibleTopAppBarWithSubtitleAndCenterAligned() {
        TwoRowsBar(m3::MediumFlexibleTopAppBar, "Medium TopAppBar", true, true, false, nullptr);
    }
    void ExitUntilCollapsedLargeTopAppBar() {
        TwoRowsBar(m3::LargeTopAppBar, "Large TopAppBar", false, false, true, TD::exitUntilCollapsedScrollBehavior());
    }
    void ExitUntilCollapsedLargeFlexibleTopAppBar() {
        TwoRowsBar(m3::LargeFlexibleTopAppBar, "Large TopAppBar", true, false, true, TD::exitUntilCollapsedScrollBehavior());
    }
    void LargeFlexibleTopAppBarWithSubtitleAndCenterAligned() {
        TwoRowsBar(m3::LargeFlexibleTopAppBar, "Large TopAppBar", true, true, false, nullptr);
    }

    void CustomTwoRowsTopAppBar() {
        auto behavior = TD::exitUntilCollapsedScrollBehavior();
        m3::ScaffoldOptions s;
        s.topBar = [behavior] {
            m3::TwoRowsTopAppBarOptions o;
            o.collapsedHeight = 64_dp;
            o.expandedHeight = 156_dp;
            o.navigationIcon = MenuButton;
            o.scrollBehavior = behavior;
            m3::TwoRowsTopAppBar([](bool expanded) { OneLine(expanded ? "Expanded TopAppBar" : "Collapsed TopAppBar")(); }, {},
                                 [](bool expanded) {
                                     if (expanded) OneLine("Expanded Subtitle", Padding({}, PaddingValues{ 0_dp, 0_dp, 0_dp, 24_dp }))();
                                     else OneLine("Collapsed Subtitle")();
                                 },
                                 o);
        };
        m3::Scaffold(Nested(behavior), s, [](const PaddingValues& p) {
            Column(VerticalScroll(Padding(FillMax({}, Axis::Horizontal), p), RememberScrollState()), [] {
                Here().Provide(m3::LocalTextStyle(), m3::MaterialTheme::typography().bodyLarge, [] { m3::Text(LoremIpsum()); });
            });
        });
    }

    void SimpleBottomAppBar() {
        m3::BottomAppBar([] { TipButton("Menu", &Icons::Filled::Menu(), "Menu button"); }, {}, {});
    }

    void BottomAppBarWithFAB() {
        m3::BottomAppBar(CheckAndEdit, {}, [] { AddFab({}); });
    }

    void ExitAlwaysBottomAppBar() {
        auto behavior = BD::exitAlwaysScrollBehavior();
        m3::ScaffoldOptions s;
        s.bottomBar = [behavior] {
            m3::BottomAppBarOptions o;
            o.scrollBehavior = behavior;
            m3::BottomAppBar(CheckAndEdit, {}, {}, o);
        };
        s.floatingActionButton = [] { AddFab(OffsetMod({}, 0_dp, 4_dp)); };
        s.floatingActionButtonPosition = m3::FabPosition::EndOverlay;
        m3::Scaffold(Nested(behavior), s, [](const PaddingValues& p) { PaddedNumberList(p); });
    }

    void ExitAlwaysBottomAppBarSpacedAround() { FlexibleBar(Arrangement::SpaceAround(), true, false); }
    void ExitAlwaysBottomAppBarSpacedBetween() { FlexibleBar(Arrangement::SpaceBetween(), false, false); }
    void ExitAlwaysBottomAppBarSpacedEvenly() { FlexibleBar(Arrangement::SpaceEvenly(), true, false); }
    void ExitAlwaysBottomAppBarFixed() { FlexibleBar(BD::FlexibleFixedHorizontalArrangement(), false, false); }
    void ExitAlwaysBottomAppBarFixedVibrant() { FlexibleBar(BD::FlexibleFixedHorizontalArrangement(), false, true); }

    void BottomAppBarWithOverflow() {
        static const ImageVectorPtr* const icons[6] = { &Icons::AutoMirrored::Filled::ArrowBack(), &Icons::AutoMirrored::Filled::ArrowForward(),
                                                        &Icons::Filled::Add(), &Icons::Filled::Check(), &Icons::Filled::Edit(), &Icons::Filled::Favorite() };
        static const char* const items[6] = { "Back", "Forward", "Add", "Check", "Edit", "Favorite" };
        m3::FlexibleBottomAppBarOptions o;
        o.contentPadding = PaddingValues{ 96_dp, 0_dp, 96_dp, 0_dp };
        o.horizontalArrangement = BD::FlexibleFixedHorizontalArrangement();
        m3::FlexibleBottomAppBar({}, o, [] {
            m3::AppBarRow({}, [](m3::AppBarMenuState& menuState) {
                PlainTooltipAbove("Overflow", [&menuState] {
                    m3::IconButton([&menuState] { if (menuState.IsShowing()) menuState.Dismiss(); else menuState.Show(); }, {}, {},
                                   [] { m3::Icon(Icons::Filled::MoreVert(), std::string("Overflow")); });
                });
            }, INT_MAX, [](m3::AppBarScope& scope) {
                for (int index = 0; index < 6; ++index) {
                    const std::string item = items[index];
                    const ImageVectorPtr* icon = icons[index];
                    scope.ClickableItem([] { /* doSomething() */ }, [icon, item] { m3::Icon(*icon, item); }, item);
                }
            });
        });
    }
}
