// NavigationSuiteScaffoldSamples.kt
#include "NavigationSuiteScaffoldSamples.h"
#include "Common.h"

#include <kiln/material3/Button.h>
#include <kiln/material3/adaptive/NavigationSuiteScaffold.h>

#include <memory>
#include <optional>
#include <string>

namespace catalog::samples {
    namespace NS = m3::adaptive;

    namespace {
        // The three items, a filled heart on the selected one.
        std::function<void()> NavItems(kiln::rt::State<int>& selectedItem, std::optional<NS::NavigationSuiteType> type) {
            return [&selectedItem, type] {
                static const char* const navItems[3] = { "Songs", "Artists", "Playlists" };
                for (int index = 0; index < 3; ++index) {
                    NS::NavigationSuiteItemOptions o;
                    o.navigationSuiteType = type;
                    const bool selected = selectedItem.Get() == index;
                    NS::NavigationSuiteItem(
                        selected, [&selectedItem, index] { selectedItem.Set(index); },
                        [selected] { m3::Icon(selected ? Icons::Filled::Favorite() : Icons::Outlined::FavoriteBorder(), std::nullopt); },
                        [index] { m3::Text(navItems[index]); }, {}, o);
                }
            };
        }

        // Screen content.
        std::function<void()> Screen(NS::NavigationSuiteType navSuiteType, std::shared_ptr<NS::NavigationSuiteScaffoldState> state, LaunchScope scope) {
            return [navSuiteType, state, scope] {
                Column(FillMax({}, Axis::Horizontal), Arrangement::Top(), AlignmentH::CenterHorizontally(), [navSuiteType, state, scope] {
                    m3::TextOptions t;
                    t.textAlign = kiln::text::TextAlign::Center;
                    m3::Text(std::string("Current NavigationSuiteType: ") + NS::ToString(navSuiteType) + "\nVisibility: " +
                                 (state->CurrentValue() == NS::NavigationSuiteScaffoldValue::Visible ? "Visible" : "Hidden"),
                             Padding({}, 16_dp), t);
                    m3::Button([state, scope] { scope.Launch(state->Toggle()); }, {}, {}, [] { m3::Text("Hide/show navigation component"); });
                });
            };
        }
    }

    void NavigationSuiteScaffoldSample() {
        auto& selectedItem = RememberState(0);
        const NS::NavigationSuiteType navSuiteType = NS::NavigationSuiteScaffoldDefaults::navigationSuiteType(NS::CurrentWindowAdaptiveInfoV2());
        const auto state = NS::RememberNavigationSuiteScaffoldState();
        const LaunchScope scope = RememberCoroutineScope();
        NS::NavigationSuiteScaffoldOptions o;
        o.state = state;
        NS::NavigationSuiteScaffold(NavItems(selectedItem, std::nullopt), {}, o, Screen(navSuiteType, state, scope));
    }

    void NavigationSuiteScaffoldCustomConfigSample() {
        using W = NS::WindowSizeClass::WidthClass;
        auto& selectedItem = RememberState(0);
        // Custom configuration that shows a wide navigation rail in small/medium width screens, an
        // expanded wide navigation rail in expanded width screens, and a short navigation bar in small
        // height screens.
        const NS::WindowSizeClass size = NS::CurrentWindowAdaptiveInfoV2().windowSizeClass;
        NS::NavigationSuiteType navSuiteType;
        if (size.WindowWidthSizeClass() == W::Compact || size.WindowWidthSizeClass() == W::Medium) {
            navSuiteType = NS::NavigationSuiteType::WideNavigationRailCollapsed;
        } else if (size.WindowHeightSizeClass() == NS::WindowSizeClass::HeightClass::Compact) {
            navSuiteType = NS::NavigationSuiteType::ShortNavigationBarMedium;
        } else if (size.WindowWidthSizeClass() == W::Expanded) {
            navSuiteType = NS::NavigationSuiteType::WideNavigationRailExpanded;
        } else {
            navSuiteType = NS::NavigationSuiteScaffoldDefaults::navigationSuiteType(NS::CurrentWindowAdaptiveInfoV2());
        }
        const auto state = NS::RememberNavigationSuiteScaffoldState();
        const LaunchScope scope = RememberCoroutineScope();
        NS::NavigationSuiteScaffoldOptions o;
        o.navigationSuiteType = navSuiteType;
        o.state = state;
        o.navigationItemVerticalArrangement = Arrangement::Center();
        NS::NavigationSuiteScaffold(NavItems(selectedItem, navSuiteType), {}, o, Screen(navSuiteType, state, scope));
    }
}
