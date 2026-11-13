// NavigationRailSamples.kt
#include "NavigationRailSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/NavigationRail.h>
#include <kiln/material3/Tooltip.h>
#include <kiln/material3/WideNavigationRail.h>

namespace catalog::samples {
    using RailValue = m3::WideNavigationRailValue;
    using RailState = std::shared_ptr<m3::WideNavigationRailState>;

    namespace {
        const char* const items[3] = { "Home", "Search", "Settings" };
        const ImageVectorPtr& SelectedIcon(int i) {
            static const ImageVectorPtr* const icons[3] = { &Icons::Filled::Home(), &Icons::Filled::Favorite(), &Icons::Filled::Star() };
            return *icons[i];
        }
        const ImageVectorPtr& UnselectedIcon(int i) {
            static const ImageVectorPtr* const icons[3] = { &Icons::Outlined::Home(), &Icons::Outlined::FavoriteBorder(), &Icons::Outlined::StarBorder() };
            return *icons[i];
        }
        const char* const note = "Note: This demo is best shown in portrait mode, as landscape mode may result in a compact height in certain "
                                 "devices. For any compact screen dimensions, use a Navigation Bar instead.";

        // The menu / menu-open button that expands and collapses the rail, with its tooltip.
        kiln::Function<void()> Header(RailState state, LaunchScope scope) {
            return [state, scope] {
                const bool expanding = state->TargetValue() == RailValue::Expanded;
                const std::string headerDescription = expanding ? "Collapse rail" : "Expand rail";
                m3::TooltipBox(m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                               [headerDescription](const m3::TooltipScope& s) {
                                   m3::PlainTooltip(s, Semantics({}, [headerDescription](SemanticsConfiguration& c) {
                                                        c.liveRegion = LiveRegionMode::Assertive;
                                                        c.paneTitle = headerDescription;
                                                    }),
                                                    {}, [headerDescription] { m3::Text(headerDescription); });
                               },
                               m3::RememberTooltipState(), {}, {}, [state, scope, expanding, headerDescription] {
                                   const bool expanded = state->CurrentValue() == RailValue::Expanded;
                                   m3::IconButton(
                                       [state, scope] {
                                           if (state->TargetValue() == RailValue::Expanded) scope.Launch(state->Collapse());
                                           else scope.Launch(state->Expand());
                                       },
                                       Semantics(Padding({}, PaddingValues{ 24_dp, 0_dp, 0_dp, 0_dp }),
                                                 [expanded](SemanticsConfiguration& c) { c.stateDescription = expanded ? "Expanded" : "Collapsed"; }),
                                       {}, [expanding, headerDescription] {
                                           m3::Icon(expanding ? Icons::AutoMirrored::Filled::MenuOpen() : Icons::Filled::Menu(), headerDescription);
                                       });
                               });
            };
        }

        // The three items; `describe` gives the icons their labels as descriptions.
        void RailItems(kiln::rt::State<int>& selectedItem, bool railExpanded, bool describe, kiln::Function<void()> afterClick = {}) {
            for (int index = 0; index < 3; ++index) {
                const bool selected = selectedItem.Get() == index;
                m3::WideNavigationRailItem(
                    selected, [&selectedItem, index, afterClick] {
                        selectedItem.Set(index);
                        if (afterClick) afterClick();
                    },
                    [selected, index, describe] {
                        m3::Icon(selected ? SelectedIcon(index) : UnselectedIcon(index),
                                 describe ? std::optional<std::string>(items[index]) : std::nullopt);
                    },
                    [index] { m3::Text(items[index]); }, railExpanded, {}, {});
            }
        }

        void ClassicRail(bool bottomAligned) {
            auto& selectedItem = RememberSaveableState(0);
            m3::NavigationRail({}, {}, [&selectedItem, bottomAligned] {
                if (bottomAligned) Spacer(Weight({}, 1));
                for (int index = 0; index < 3; ++index) {
                    const bool selected = selectedItem.Get() == index;
                    m3::NavigationRailItemOptions o;
                    o.label = [index] { m3::Text(items[index]); };
                    m3::NavigationRailItem(selected, [&selectedItem, index] { selectedItem.Set(index); },
                                           [selected, index] { m3::Icon(selected ? SelectedIcon(index) : UnselectedIcon(index), std::string(items[index])); },
                                           {}, o);
                }
            });
        }
    }

    void WideNavigationRailResponsiveSample() {
        auto& selectedItem = RememberSaveableState(0);
        auto state = m3::RememberWideNavigationRailState();
        const LaunchScope scope = RememberCoroutineScope();
        Row(FillMax({}, Axis::Horizontal), [&selectedItem, state, scope] {
            m3::WideNavigationRailOptions o;
            o.state = state;
            o.header = Header(state, scope);
            m3::WideNavigationRail({}, o, [&selectedItem, state] { RailItems(selectedItem, state->TargetValue() == RailValue::Expanded, false); });
            const std::string textString = state->CurrentValue() == RailValue::Expanded ? "Expanded" : "Collapsed";
            Column({}, [state, textString] {
                m3::Text(std::string("Is animating: ") + (state->IsAnimating() ? "true" : "false"), Padding({}, 16_dp));
                m3::Text("The rail is " + textString + ".", Padding({}, 16_dp));
                m3::Text(note, Padding({}, 16_dp));
            });
        });
    }

    void ModalWideNavigationRailSample() {
        auto& selectedItem = RememberSaveableState(0);
        auto state = m3::RememberWideNavigationRailState();
        const LaunchScope scope = RememberCoroutineScope();
        Row(FillMax({}, Axis::Horizontal), [&selectedItem, state, scope] {
            m3::ModalWideNavigationRailOptions o;
            o.state = state;
            o.expandedHeaderTopPadding = 64_dp;
            o.header = Header(state, scope);
            m3::ModalWideNavigationRail({}, o, [&selectedItem, state] { RailItems(selectedItem, state->TargetValue() == RailValue::Expanded, true); });
            const std::string textString = state->CurrentValue() == RailValue::Expanded ? "Expanded" : "Collapsed";
            Column({}, [textString] {
                m3::Text("The rail is " + textString + ".", Padding({}, 16_dp));
                m3::Text(note, Padding({}, 16_dp));
            });
        });
    }

    void DismissibleModalWideNavigationRailSample() {
        auto& selectedItem = RememberSaveableState(0);
        auto state = m3::RememberWideNavigationRailState();
        const LaunchScope scope = RememberCoroutineScope();
        Row(FillMax({}, Axis::Both), [&selectedItem, state, scope] {
            m3::ModalWideNavigationRailOptions o;
            o.state = state;
            o.hideOnCollapse = true;
            m3::ModalWideNavigationRail({}, o, [&selectedItem, state, scope] { RailItems(selectedItem, true, false, [state, scope] { scope.Launch(state->Collapse()); }); });
            Column(FillMax({}, Axis::Horizontal), Arrangement::Top(), AlignmentH::CenterHorizontally(), [&selectedItem, state, scope] {
                const std::string currentPage = items[selectedItem.Get()];
                m3::Button([state, scope] { scope.Launch(state->Expand()); }, Padding({}, 32_dp), {}, [currentPage] {
                    m3::TextOptions t;
                    t.textAlign = kiln::text::TextAlign::Center;
                    m3::Text(currentPage + " Page\nOpen modal rail", {}, t);
                });
            });
        });
    }

    void WideNavigationRailCollapsedSample() {
        auto& selectedItem = RememberSaveableState(0);
        m3::WideNavigationRail({}, {}, [&selectedItem] { RailItems(selectedItem, false, false); });
    }

    void WideNavigationRailExpandedSample() {
        auto& selectedItem = RememberSaveableState(0);
        m3::WideNavigationRailOptions o;
        o.state = m3::RememberWideNavigationRailState(RailValue::Expanded);
        m3::WideNavigationRail({}, o, [&selectedItem] { RailItems(selectedItem, true, false); });
    }

    void WideNavigationRailArrangementsSample() {
        auto& selectedItem = RememberSaveableState(0);
        auto state = m3::RememberWideNavigationRailState();
        const LaunchScope scope = RememberCoroutineScope();
        auto& isArrangementCenter = RememberSaveableState(true);
        Row(FillMax({}, Axis::Horizontal), [&selectedItem, &isArrangementCenter, state, scope] {
            m3::WideNavigationRailOptions o;
            o.state = state;
            o.arrangement = isArrangementCenter.Get() ? Arrangement::Center() : Arrangement::Bottom();
            o.header = Header(state, scope);
            m3::WideNavigationRail({}, o, [&selectedItem, state] { RailItems(selectedItem, state->TargetValue() == RailValue::Expanded, false); });
            const std::string changeToString = isArrangementCenter.Get() ? "Bottom" : "Center";
            Column(Weight({}, 1), Arrangement::Top(), AlignmentH::CenterHorizontally(), [&isArrangementCenter, changeToString] {
                m3::Text("Change arrangement to:", Padding({}, 16_dp));
                m3::Button([&isArrangementCenter] { isArrangementCenter.Set(!isArrangementCenter.Peek()); }, Padding({}, 4_dp), {},
                           [changeToString] { m3::Text(changeToString); });
                m3::Text(note, Padding({}, 16_dp));
            });
        });
    }

    void NavigationRailSample() { ClassicRail(false); }
    void NavigationRailBottomAlignSample() { ClassicRail(true); }
}
