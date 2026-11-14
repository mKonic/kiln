#pragma once

// Custom Tabs (androidx.browser's CustomTabsIntent, without the library: the intent and its
// extras): a web page in the user's browser, inside the app's task, with the app's colours.
//
//   ui::LaunchCustomTab("https://example.com/help", { .toolbarColor = ui::Color(0xFF6750A4) });

#include "kiln/ui/ActivityIntent.h"
#include "kiln/ui/Geometry.h"

#include <optional>
#include <string>

namespace kiln::ui {

    struct CustomTabsOptions {
        // CustomTabColorSchemeParams: the toolbar's and navigation bar's colours.
        std::optional<Color> toolbarColor, navigationBarColor;
        // COLOR_SCHEME_SYSTEM (0), LIGHT (1) or DARK (2).
        int colorScheme = 0;
        // The page's title in the toolbar (SHOW_PAGE_TITLE), not only its URL.
        bool showTitle = false;
        // The toolbar hides when the page scrolls down.
        std::optional<bool> urlBarHiding;
        // SHARE_STATE_DEFAULT (0), ON (1) or OFF (2): the share item in the menu.
        int shareState = 0;
        bool instantApps = true;
    };

    // CustomTabsIntent.Builder(...).build().intent with the URL as its data.
    Intent CustomTabsIntent(const std::string& url, const CustomTabsOptions& options = {});
    // launchUrl: a browser without Custom Tabs opens the URL as a plain link.
    inline bool LaunchCustomTab(const std::string& url, const CustomTabsOptions& options = {}) {
        return StartActivity(CustomTabsIntent(url, options));
    }

}
