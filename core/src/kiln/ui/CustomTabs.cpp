#include "kiln/ui/CustomTabs.h"

#include "kiln/ui/Share.h"

namespace kiln::ui {

    namespace {
        // CustomTabsIntent's keys.
        constexpr const char* kExtraSession = "android.support.customtabs.extra.SESSION";
        constexpr const char* kExtraEnableInstantApps = "android.support.customtabs.extra.EXTRA_ENABLE_INSTANT_APPS";
        constexpr const char* kExtraToolbarColor = "android.support.customtabs.extra.TOOLBAR_COLOR";
        constexpr const char* kExtraNavigationBarColor = "androidx.browser.customtabs.extra.NAVIGATION_BAR_COLOR";
        constexpr const char* kExtraColorScheme = "androidx.browser.customtabs.extra.COLOR_SCHEME";
        constexpr const char* kExtraTitleVisibility = "android.support.customtabs.extra.TITLE_VISIBILITY";
        constexpr const char* kExtraUrlBarHiding = "android.support.customtabs.extra.ENABLE_URLBAR_HIDING";
        constexpr const char* kExtraShareState = "androidx.browser.customtabs.extra.SHARE_STATE";
    }

    Intent CustomTabsIntent(const std::string& url, const CustomTabsOptions& o) {
        Intent i;
        i.action = kActionView;
        i.data = url;
        // build(): EXTRA_SESSION must be present, even without a session.
        i.extras.Put(kExtraSession, BundleNullBinder{});
        if (o.colorScheme != 0) i.extras.Put(kExtraColorScheme, std::int32_t(o.colorScheme));
        if (o.showTitle) i.extras.Put(kExtraTitleVisibility, std::int32_t(1));
        if (o.urlBarHiding) i.extras.Put(kExtraUrlBarHiding, *o.urlBarHiding);
        i.extras.Put(kExtraEnableInstantApps, o.instantApps);
        // The default colour scheme's params, as extras (CustomTabColorSchemeParams.toBundle).
        if (o.toolbarColor) i.extras.Put(kExtraToolbarColor, std::int32_t(o.toolbarColor->argb));
        if (o.navigationBarColor) i.extras.Put(kExtraNavigationBarColor, std::int32_t(o.navigationBarColor->argb));
        i.extras.Put(kExtraShareState, std::int32_t(o.shareState));
        return i;
    }

}
