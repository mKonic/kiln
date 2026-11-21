#pragma once

// Custom Tabs (androidx.browser's CustomTabsIntent, without the library: the intent and its
// extras): a web page in the user's browser, inside the app's task, with the app's colours.
//
//   ui::LaunchCustomTab("https://example.com/help", { .toolbarColor = ui::Color(0xFF6750A4) });
//
// Buttons and menu items the app adds open the app at a deep link (as notification actions do);
// the browser fills in the page's URL as the intent's data when the link has none.

#include "kiln/ui/ActivityIntent.h"
#include "kiln/ui/Geometry.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    // CustomTabColorSchemeParams: the colours for one colour scheme.
    struct CustomTabColorSchemeParams {
        std::optional<Color> toolbarColor, secondaryToolbarColor, navigationBarColor, navigationBarDividerColor;
        bool operator==(const CustomTabColorSchemeParams&) const = default;
    };

    // CustomTabsIntent's constants.
    namespace customtabs {
        inline constexpr int kColorSchemeSystem = 0, kColorSchemeLight = 1, kColorSchemeDark = 2;
        inline constexpr int kShareStateDefault = 0, kShareStateOn = 1, kShareStateOff = 2;
        inline constexpr int kOpenInBrowserStateDefault = 0, kOpenInBrowserStateOn = 1, kOpenInBrowserStateOff = 2;
        inline constexpr int kActivityHeightDefault = 0, kActivityHeightAdjustable = 1, kActivityHeightFixed = 2;
        inline constexpr int kSideSheetPositionDefault = 0, kSideSheetPositionStart = 1, kSideSheetPositionEnd = 2;
        inline constexpr int kSideSheetDecorationDefault = 0, kSideSheetDecorationNone = 1, kSideSheetDecorationShadow = 2, kSideSheetDecorationDivider = 3;
        inline constexpr int kSideSheetRoundedCornersDefault = 0, kSideSheetRoundedCornersNone = 1, kSideSheetRoundedCornersTop = 2;
        inline constexpr int kCloseButtonPositionDefault = 0, kCloseButtonPositionStart = 1, kCloseButtonPositionEnd = 2;
        inline constexpr int kContentTargetTypeImage = 1, kContentTargetTypeLink = 2;
        inline constexpr int kMaxToolbarCornerRadiusDp = 16;
    }

    // An icon: encoded image bytes (PNG, JPEG, WebP) or a drawable resource's name.
    struct CustomTabIcon {
        std::vector<std::uint8_t> encoded;
        std::string drawable;
        bool operator==(const CustomTabIcon&) const = default;
    };
    // addMenuItem: a menu entry that opens the app at `deepLink`.
    struct CustomTabMenuItem {
        std::string label, deepLink;
        bool operator==(const CustomTabMenuItem&) const = default;
    };
    // setActionButton: the toolbar's button.
    struct CustomTabActionButton {
        CustomTabIcon icon;
        std::string description, deepLink;
        bool tint = false;
        bool operator==(const CustomTabActionButton&) const = default;
    };
    // CustomContentAction: an item in the context menu of an image or a link (targetType).
    struct CustomContentAction {
        int id = 0;
        std::string label, deepLink;
        int targetType = customtabs::kContentTargetTypeImage;
        bool operator==(const CustomContentAction&) const = default;
    };

    struct CustomTabsOptions {
        // The default colour scheme's colours (setToolbarColor, ...; setDefaultColorSchemeParams).
        std::optional<Color> toolbarColor, navigationBarColor, secondaryToolbarColor, navigationBarDividerColor;
        // setColorSchemeParams: colours for kColorSchemeLight / kColorSchemeDark.
        std::map<int, CustomTabColorSchemeParams> colorSchemeParams;
        // setColorScheme: kColorSchemeSystem, Light or Dark.
        int colorScheme = customtabs::kColorSchemeSystem;
        // The page's title in the toolbar (SHOW_PAGE_TITLE), not only its URL.
        bool showTitle = false;
        // The toolbar hides when the page scrolls down.
        std::optional<bool> urlBarHiding;
        // setShareState: kShareStateDefault, On or Off (the share item in the menu).
        int shareState = customtabs::kShareStateDefault;
        bool instantApps = true;
        std::optional<CustomTabIcon> closeButtonIcon;
        std::vector<CustomTabMenuItem> menuItems;
        std::optional<CustomTabActionButton> actionButton;
        std::vector<CustomContentAction> customContentActions;
        // setSecondaryToolbarSwipeUpGesture: opens the app at this deep link.
        std::optional<std::string> secondaryToolbarSwipeUpDeepLink;
        // setStartAnimations / setExitAnimations: anim resources by name.
        std::string startEnterAnimation, startExitAnimation, exitEnterAnimation, exitExitAnimation;
        // setInitialActivityHeightPx (a partial tab) with its resize behaviour, and the width (a
        // side sheet on large screens).
        std::optional<int> initialActivityHeightPx, initialActivityWidthPx, activitySideSheetBreakpointDp;
        int activityHeightResizeBehavior = customtabs::kActivityHeightDefault;
        std::optional<bool> activitySideSheetMaximization;
        std::optional<int> activitySideSheetPosition, activitySideSheetDecorationType, activitySideSheetRoundedCornersPosition;
        std::optional<int> toolbarCornerRadiusDp, closeButtonPosition, openInBrowserButtonState;
        std::optional<bool> bookmarksButton, downloadButton, sendToExternalDefaultHandler, initialNavigationAllowedToLeaveBrowser;
        std::optional<bool> backgroundInteraction, ephemeralBrowsing, closeButton;
        // setTranslateLocale: a BCP 47 tag to translate the page to.
        std::string translateLocale;
        // setShareIdentityEnabled: the browser learns which app opened the tab (API 34).
        bool shareIdentity = false;
    };

    // CustomTabsIntent.Builder(...).build().intent with the URL as its data. Throws
    // std::invalid_argument for a value out of range, as the builder's setters do.
    Intent CustomTabsIntent(const std::string& url, const CustomTabsOptions& options = {});
    // The start animation, share identity and pass-through options build() puts in its
    // startAnimationBundle.
    ActivityOptions CustomTabsActivityOptions(const CustomTabsOptions& options);
    // launchUrl: a browser without Custom Tabs opens the URL as a plain link.
    inline bool LaunchCustomTab(const std::string& url, const CustomTabsOptions& options = {}) {
        return StartActivity(CustomTabsIntent(url, options), CustomTabsActivityOptions(options));
    }

}
