// NavigationBarSamples.kt
#include "NavigationBarSamples.h"
#include "Common.h"

#include <kiln/material3/NavigationBar.h>
#include <kiln/material3/ShortNavigationBar.h>

namespace catalog::samples {
    namespace {
        const char* const items[3] = { "Songs", "Artists", "Playlists" };
        const ImageVectorPtr& SelectedIcon(int i) {
            static const ImageVectorPtr* const icons[3] = { &Icons::Filled::Home(), &Icons::Filled::Favorite(), &Icons::Filled::Star() };
            return *icons[i];
        }
        const ImageVectorPtr& UnselectedIcon(int i) {
            static const ImageVectorPtr* const icons[3] = { &Icons::Outlined::Home(), &Icons::Outlined::FavoriteBorder(), &Icons::Outlined::StarBorder() };
            return *icons[i];
        }

        void ShortBar(m3::ShortNavigationBarArrangement arrangement, m3::NavigationItemIconPosition iconPosition) {
            auto& selectedItem = RememberSaveableState(0);
            m3::ShortNavigationBarOptions bar;
            bar.arrangement = arrangement;
            m3::ShortNavigationBar({}, bar, [&selectedItem, iconPosition] {
                for (int index = 0; index < 3; ++index) {
                    const bool selected = selectedItem.Get() == index;
                    m3::ShortNavigationBarItemOptions o;
                    o.iconPosition = iconPosition;
                    m3::ShortNavigationBarItem(selected, [&selectedItem, index] { selectedItem.Set(index); },
                                               [selected, index] { m3::Icon(selected ? SelectedIcon(index) : UnselectedIcon(index), std::nullopt); },
                                               [index] { m3::Text(items[index]); }, {}, o);
                }
            });
        }
    }

    void ShortNavigationBarSample() { ShortBar(m3::ShortNavigationBarArrangement::EqualWeight, m3::NavigationItemIconPosition::Top); }

    void ShortNavigationBarWithHorizontalItemsSample() {
        Column({}, [] {
            m3::Text("Note: this is configuration is better displayed in medium screen sizes.", Padding({}, 16_dp));
            Spacer(Height({}, 32_dp));
            ShortBar(m3::ShortNavigationBarArrangement::Centered, m3::NavigationItemIconPosition::Start);
        });
    }

    void NavigationBarSample() {
        auto& selectedItem = RememberSaveableState(0);
        m3::NavigationBar({}, {}, [&selectedItem] {
            for (int index = 0; index < 3; ++index) {
                const bool selected = selectedItem.Get() == index;
                m3::NavigationBarItemOptions o;
                o.label = [index] { m3::Text(items[index]); };
                m3::NavigationBarItem(selected, [&selectedItem, index] { selectedItem.Set(index); },
                                      [selected, index] { m3::Icon(selected ? SelectedIcon(index) : UnselectedIcon(index), std::string(items[index])); }, {}, o);
            }
        });
    }
}
