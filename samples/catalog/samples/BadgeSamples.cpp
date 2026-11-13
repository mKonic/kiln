// BadgeSamples.kt
#include "BadgeSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/material3/Badge.h>
#include <kiln/material3/NavigationBar.h>

namespace catalog::samples {
    void NavigationBarItemWithBadge() {
        m3::NavigationBar({}, {}, [] {
            const auto item = [](kiln::Function<void()> badge) {
                m3::NavigationBarItem(false, [] {}, [badge] {
                    m3::BadgedBox(badge, {}, [] { m3::Icon(Icons::Filled::Star(), std::string("Favorite")); });
                }, {}, {});
            };
            item([] { m3::Badge(ContentDescription({}, "New notification")); });
            for (const char* badgeNumber : { "8", "999+" }) {
                item([badgeNumber] {
                    m3::Badge({}, std::nullopt, std::nullopt, [badgeNumber] {
                        m3::Text(badgeNumber, ContentDescription({}, std::string(badgeNumber) + " new notifications"));
                    });
                });
            }
        });
    }
}
