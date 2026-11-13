// FloatingToolbarSamples.kt
#include "FloatingToolbarSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Focus.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/AppBarRow.h>
#include <kiln/material3/FloatingToolbar.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>

namespace catalog::samples {
    namespace FD = m3::FloatingToolbarDefaults;
    using FD::ScreenOffset;

    namespace {
        const std::string description = "Localized description";

        void TipIconButton(const ImageVectorPtr* icon, Modifier modifier = {}) {
            PlainTooltipAbove(description, [icon, modifier] {
                m3::IconButton([] { /* doSomething() */ }, modifier, {}, [icon] { m3::Icon(*icon, description); });
            });
        }
        void LeadingContent() {
            TipIconButton(&Icons::Filled::Check());
            TipIconButton(&Icons::Filled::Edit());
        }
        void TrailingContent() {
            TipIconButton(&Icons::Filled::Download());
            TipIconButton(&Icons::Filled::Favorite());
        }
        // The toolbars' main action: a filled add button, 64 dp along the toolbar.
        void AddButton(bool horizontal) {
            PlainTooltipAbove(description, [horizontal] {
                m3::FilledIconButton([] { /* doSomething() */ }, horizontal ? Width({}, 64_dp) : Height({}, 64_dp), {},
                                     [] { m3::Icon(Icons::Filled::Add(), description); });
            });
        }
        // The five overflow items.
        void OverflowItems(m3::AppBarScope& scope) {
            static const ImageVectorPtr* const icons[5] = { &Icons::Filled::Download(), &Icons::Filled::Favorite(), &Icons::Filled::Add(),
                                                            &Icons::Filled::Person(), &Icons::Filled::ArrowUpward() };
            static const char* const labels[5] = { "Download", "Favorite", "Add", "Person", "ArrowUpward" };
            for (int i = 0; i < 5; ++i) {
                const ImageVectorPtr* icon = icons[i];
                scope.ClickableItem([] { /* doSomething() */ }, [icon] { m3::Icon(*icon, description); }, labels[i]);
            }
        }

        LazyListOptions Spaced(const PaddingValues& contentPadding) {
            LazyListOptions o;
            o.contentPadding = contentPadding;
            o.arrangement = Arrangement::SpacedBy(8_dp);
            return o;
        }
        void Numbers(const Modifier& modifier, const PaddingValues& innerPadding) {
            LazyColumn(modifier, RememberLazyListState(), [](LazyListScope& scope) {
                scope.Items(76, [](int i) {
                    TextStyled(std::to_string(i), m3::MaterialTheme::typography().bodyLarge, Padding(FillMax({}, Axis::Horizontal), 16_dp, 0_dp));
                });
            }, Spaced(innerPadding));
        }

        Modifier BottomCenter() { return ZIndex(OffsetMod(AlignInBox({}, Alignment::BottomCenter()), 0_dp, Dp(-ScreenOffset.value)), 1); }
        Modifier CenterEnd() { return ZIndex(OffsetMod(AlignInBox({}, Alignment::CenterEnd()), Dp(-ScreenOffset.value), 0_dp), 1); }
        Modifier BottomEnd() {
            return ZIndex(OffsetMod(AlignInBox({}, Alignment::BottomEnd()), Dp(-ScreenOffset.value), Dp(-ScreenOffset.value)), 1);
        }

        // The toolbar-only samples: expandable (collapses as the list scrolls), overflowing (an
        // AppBarRow / AppBarColumn of five items) or scrollable (exits as the list scrolls).
        enum class Kind { Expandable, Overflowing, Scrollable };
        void ToolbarSample(bool horizontal, Kind kind) {
            auto& expanded = RememberSaveableState(true);
            std::shared_ptr<m3::FloatingToolbarScrollBehavior> behavior;
            if (kind == Kind::Scrollable)
                behavior = FD::exitAlwaysScrollBehavior(horizontal ? m3::FloatingToolbarExitDirection::Bottom : m3::FloatingToolbarExitDirection::End);
            m3::ScaffoldOptions s;
            m3::Scaffold(behavior ? NestedScroll({}, behavior->NestedScrollConnection()) : Modifier{}, s,
                         [&expanded, horizontal, kind, behavior](const PaddingValues& innerPadding) {
                Box(Padding({}, innerPadding), Alignment::TopStart(), false, [&expanded, horizontal, kind, behavior, innerPadding] {
                    m3::FloatingToolbarOptions o;
                    o.leadingContent = LeadingContent;
                    if (kind == Kind::Overflowing) {
                        o.trailingContent = [horizontal] {
                            if (horizontal) m3::AppBarRow({}, OverflowItems);
                            else m3::AppBarColumn({}, OverflowItems);
                        };
                    } else {
                        o.trailingContent = TrailingContent;
                    }
                    o.scrollBehavior = behavior;
                    const bool e = kind == Kind::Expandable ? expanded.Get() : true;
                    if (horizontal) m3::HorizontalFloatingToolbar(e, BottomCenter(), o, [] { AddButton(true); });
                    else m3::VerticalFloatingToolbar(e, CenterEnd(), o, [] { AddButton(false); });
                    const Modifier listModifier = kind == Kind::Expandable
                                                      ? FD::FloatingToolbarVerticalNestedScroll({}, e, [&expanded] { expanded.Set(true); },
                                                                                                [&expanded] { expanded.Set(false); })
                                                      : Modifier{};
                    Numbers(listModifier, innerPadding);
                });
            });
        }

        // The FAB samples: four actions beside a vibrant FAB, over the lorem ipsum.
        void FabActions(bool focusWhenExpandedOnly, bool expanded, bool favoriteTooltip = true) {
            const Modifier m = focusWhenExpandedOnly ? FocusPropertiesMod({}, [expanded](FocusProperties& p) { p.canFocus = expanded; }) : Modifier{};
            TipIconButton(&Icons::Filled::Person(), m);
            TipIconButton(&Icons::Filled::Edit(), m);
            if (favoriteTooltip) TipIconButton(&Icons::Filled::Favorite(), m);
            else m3::IconButton([] { /* doSomething() */ }, m, {}, [] { m3::Icon(Icons::Filled::Favorite(), description); });
            TipIconButton(&Icons::Filled::MoreVert(), m);
        }
        kiln::Function<void()> VibrantFab(kiln::Function<void()> onClick) {
            return [onClick] {
                PlainTooltipAbove(description, [onClick] {
                    FD::VibrantFloatingActionButton(onClick, {}, {}, [] { m3::Icon(Icons::Filled::Add(), description); });
                });
            };
        }
        void Lorem(Modifier modifier) {
            Column(VerticalScroll(modifier, RememberScrollState()), [] { m3::Text(LoremIpsum()); });
        }
        Modifier LoremPadding() { return Padding(FillMax({}, Axis::Horizontal), 16_dp, 0_dp); }

        // Expandable FAB samples: the FAB toggles the toolbar and scrolling the text does too.
        void ExpandableWithFab(bool horizontal) {
            auto& expanded = RememberSaveableState(true);
            const auto vibrantColors = FD::vibrantFloatingToolbarColors();
            m3::Scaffold({}, {}, [&expanded, vibrantColors, horizontal](const PaddingValues& innerPadding) {
                Box(Padding({}, innerPadding), Alignment::TopStart(), false, [&expanded, vibrantColors, horizontal] {
                    const bool e = expanded.Get();
                    m3::FloatingToolbarWithFabOptions o;
                    o.colors = vibrantColors;
                    const auto fab = VibrantFab([&expanded] { expanded.Set(!expanded.Peek()); });
                    if (horizontal) m3::HorizontalFloatingToolbar(e, fab, BottomEnd(), o, [e] { FabActions(true, e); });
                    else m3::VerticalFloatingToolbar(e, fab, BottomEnd(), o, [e] { FabActions(true, e); });
                    Lorem(FD::FloatingToolbarVerticalNestedScroll(LoremPadding(), e, [&expanded] { expanded.Set(true); },
                                                                  [&expanded] { expanded.Set(false); }));
                });
            });
        }

        // Centered FAB samples: always expanded; the toolbar exits as the text scrolls.
        void CenteredWithFab(bool horizontal) {
            auto behavior = FD::exitAlwaysScrollBehavior(horizontal ? m3::FloatingToolbarExitDirection::Bottom : m3::FloatingToolbarExitDirection::End);
            const auto vibrantColors = FD::vibrantFloatingToolbarColors();
            m3::Scaffold(NestedScroll({}, behavior->NestedScrollConnection()), {}, [behavior, vibrantColors, horizontal](const PaddingValues& innerPadding) {
                Box(Padding({}, innerPadding), Alignment::TopStart(), false, [behavior, vibrantColors, horizontal] {
                    m3::FloatingToolbarWithFabOptions o;
                    o.colors = vibrantColors;
                    o.scrollBehavior = behavior;
                    const auto fab = VibrantFab([] { /* doSomething() */ });
                    if (horizontal) m3::HorizontalFloatingToolbar(true, fab, BottomCenter(), o, [] { FabActions(false, true); });
                    else m3::VerticalFloatingToolbar(true, fab, CenterEnd(), o, [] { FabActions(false, true, false); });
                    Lorem(LoremPadding());
                });
            });
        }
    }

    void ExpandableHorizontalFloatingToolbarSample() { ToolbarSample(true, Kind::Expandable); }
    void OverflowingHorizontalFloatingToolbarSample() { ToolbarSample(true, Kind::Overflowing); }
    void ScrollableHorizontalFloatingToolbarSample() { ToolbarSample(true, Kind::Scrollable); }
    void ExpandableVerticalFloatingToolbarSample() { ToolbarSample(false, Kind::Expandable); }
    void OverflowingVerticalFloatingToolbarSample() { ToolbarSample(false, Kind::Overflowing); }
    void ScrollableVerticalFloatingToolbarSample() { ToolbarSample(false, Kind::Scrollable); }
    void HorizontalFloatingToolbarWithFabSample() { ExpandableWithFab(true); }
    void CenteredHorizontalFloatingToolbarWithFabSample() { CenteredWithFab(true); }
    void VerticalFloatingToolbarWithFabSample() { ExpandableWithFab(false); }
    void CenteredVerticalFloatingToolbarWithFabSample() { CenteredWithFab(false); }

    void HorizontalFloatingToolbarAsScaffoldFabSample() {
        auto& expanded = RememberSaveableState(true);
        const auto vibrantColors = FD::vibrantFloatingToolbarColors();
        m3::ScaffoldOptions s;
        s.floatingActionButton = [&expanded, vibrantColors] {
            const bool e = expanded.Get();
            m3::FloatingToolbarWithFabOptions o;
            o.colors = vibrantColors;
            m3::HorizontalFloatingToolbar(e, VibrantFab([&expanded] { expanded.Set(!expanded.Peek()); }), {}, o, [e] { FabActions(true, e); });
        };
        s.floatingActionButtonPosition = m3::FabPosition::End;
        m3::Scaffold({}, s, [&expanded](const PaddingValues& innerPadding) {
            Box(Padding({}, innerPadding), Alignment::TopStart(), false, [&expanded] {
                const bool e = expanded.Get();
                Lorem(FD::FloatingToolbarVerticalNestedScroll(LoremPadding(), e, [&expanded] { expanded.Set(true); },
                                                              [&expanded] { expanded.Set(false); }));
            });
        });
    }
}
