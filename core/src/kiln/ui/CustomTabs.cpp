#include "kiln/ui/CustomTabs.h"

#include "kiln/ui/Configuration.h"
#include "kiln/ui/Share.h"

#include <memory>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        // CustomTabsIntent's keys.
        constexpr const char* kExtraSession = "android.support.customtabs.extra.SESSION";
        constexpr const char* kExtraEnableInstantApps = "android.support.customtabs.extra.EXTRA_ENABLE_INSTANT_APPS";
        constexpr const char* kExtraToolbarColor = "android.support.customtabs.extra.TOOLBAR_COLOR";
        constexpr const char* kExtraSecondaryToolbarColor = "android.support.customtabs.extra.SECONDARY_TOOLBAR_COLOR";
        constexpr const char* kExtraNavigationBarColor = "androidx.browser.customtabs.extra.NAVIGATION_BAR_COLOR";
        constexpr const char* kExtraNavigationBarDividerColor = "androidx.browser.customtabs.extra.NAVIGATION_BAR_DIVIDER_COLOR";
        constexpr const char* kExtraColorScheme = "androidx.browser.customtabs.extra.COLOR_SCHEME";
        constexpr const char* kExtraColorSchemeParams = "androidx.browser.customtabs.extra.COLOR_SCHEME_PARAMS";
        constexpr const char* kExtraTitleVisibility = "android.support.customtabs.extra.TITLE_VISIBILITY";
        constexpr const char* kExtraUrlBarHiding = "android.support.customtabs.extra.ENABLE_URLBAR_HIDING";
        constexpr const char* kExtraShareState = "androidx.browser.customtabs.extra.SHARE_STATE";
        constexpr const char* kExtraDefaultShareMenuItem = "android.support.customtabs.extra.SHARE_MENU_ITEM";
        constexpr const char* kExtraCloseButtonIcon = "android.support.customtabs.extra.CLOSE_BUTTON_ICON";
        constexpr const char* kExtraMenuItems = "android.support.customtabs.extra.MENU_ITEMS";
        constexpr const char* kExtraActionButtonBundle = "android.support.customtabs.extra.ACTION_BUTTON_BUNDLE";
        constexpr const char* kExtraTintActionButton = "android.support.customtabs.extra.TINT_ACTION_BUTTON";
        constexpr const char* kExtraCustomContentActions = "androidx.browser.customtabs.extra.CUSTOM_CONTENT_ACTIONS";
        constexpr const char* kExtraSwipeUpGesture = "androidx.browser.customtabs.extra.SECONDARY_TOOLBAR_SWIPE_UP_GESTURE";
        constexpr const char* kExtraExitAnimationBundle = "android.support.customtabs.extra.EXIT_ANIMATION_BUNDLE";
        constexpr const char* kExtraInitialActivityHeightPx = "androidx.browser.customtabs.extra.INITIAL_ACTIVITY_HEIGHT_PX";
        constexpr const char* kExtraActivityHeightResizeBehavior = "androidx.browser.customtabs.extra.ACTIVITY_HEIGHT_RESIZE_BEHAVIOR";
        constexpr const char* kExtraInitialActivityWidthPx = "androidx.browser.customtabs.extra.INITIAL_ACTIVITY_WIDTH_PX";
        constexpr const char* kExtraSideSheetMaximization = "androidx.browser.customtabs.extra.ACTIVITY_SIDE_SHEET_ENABLE_MAXIMIZATION";
        constexpr const char* kExtraSideSheetBreakpointDp = "androidx.browser.customtabs.extra.ACTIVITY_SIDE_SHEET_BREAKPOINT_DP";
        constexpr const char* kExtraSideSheetPosition = "androidx.browser.customtabs.extra.ACTIVITY_SIDE_SHEET_POSITION";
        constexpr const char* kExtraSideSheetDecorationType = "androidx.browser.customtabs.extra.ACTIVITY_SIDE_SHEET_DECORATION_TYPE";
        constexpr const char* kExtraSideSheetRoundedCorners = "androidx.browser.customtabs.extra.ACTIVITY_SIDE_SHEET_ROUNDED_CORNERS_POSITION";
        constexpr const char* kExtraToolbarCornerRadiusDp = "androidx.browser.customtabs.extra.TOOLBAR_CORNER_RADIUS_DP";
        constexpr const char* kExtraCloseButtonPosition = "androidx.browser.customtabs.extra.CLOSE_BUTTON_POSITION";
        constexpr const char* kExtraOpenInBrowserState = "androidx.browser.customtabs.extra.OPEN_IN_BROWSER_STATE";
        constexpr const char* kExtraDisableBookmarksButton = "org.chromium.chrome.browser.customtabs.EXTRA_DISABLE_STAR_BUTTON";
        constexpr const char* kExtraDisableDownloadButton = "org.chromium.chrome.browser.customtabs.EXTRA_DISABLE_DOWNLOAD_BUTTON";
        constexpr const char* kExtraSendToExternalHandler = "android.support.customtabs.extra.SEND_TO_EXTERNAL_HANDLER";
        constexpr const char* kExtraInitialNavigationCanLeave = "androidx.browser.customtabs.extra.INITIAL_NAVIGATION_CAN_LEAVE_BROWSER";
        constexpr const char* kExtraTranslateLanguageTag = "androidx.browser.customtabs.extra.TRANSLATE_LANGUAGE_TAG";
        constexpr const char* kExtraDisableBackgroundInteraction = "androidx.browser.customtabs.extra.DISABLE_BACKGROUND_INTERACTION";
        constexpr const char* kExtraEphemeralBrowsing = "androidx.browser.customtabs.extra.ENABLE_EPHEMERAL_BROWSING";
        constexpr const char* kExtraCloseButtonEnabled = "androidx.browser.customtabs.extra.CLOSE_BUTTON_ENABLED";
        constexpr const char* kExtraHeaders = "com.android.browser.headers";   // Browser.EXTRA_HEADERS
        constexpr const char* kKeyId = "android.support.customtabs.customaction.ID";
        constexpr const char* kKeyIcon = "android.support.customtabs.customaction.ICON";
        constexpr const char* kKeyDescription = "android.support.customtabs.customaction.DESCRIPTION";
        constexpr const char* kKeyPendingIntent = "android.support.customtabs.customaction.PENDING_INTENT";
        constexpr const char* kKeyMenuItemTitle = "android.support.customtabs.customaction.MENU_ITEM_TITLE";
        constexpr const char* kKeyContentId = "androidx.browser.customtabs.customcontentaction.ID";
        constexpr const char* kKeyContentLabel = "androidx.browser.customtabs.customcontentaction.LABEL";
        constexpr const char* kKeyContentPendingIntent = "androidx.browser.customtabs.customcontentaction.PENDING_INTENT";
        constexpr const char* kKeyContentTargetType = "androidx.browser.customtabs.customcontentaction.TARGET_TYPE";

        void Check(bool ok, const char* message) {
            if (!ok) throw std::invalid_argument(message);
        }
        BundleBitmap Bitmap(const CustomTabIcon& icon) { return { icon.encoded, icon.drawable }; }
        // CustomTabColorSchemeParams.toBundle
        void PutColors(Bundle& b, const CustomTabColorSchemeParams& p) {
            if (p.toolbarColor) b.Put(kExtraToolbarColor, std::int32_t(p.toolbarColor->argb));
            if (p.secondaryToolbarColor) b.Put(kExtraSecondaryToolbarColor, std::int32_t(p.secondaryToolbarColor->argb));
            if (p.navigationBarColor) b.Put(kExtraNavigationBarColor, std::int32_t(p.navigationBarColor->argb));
            if (p.navigationBarDividerColor) b.Put(kExtraNavigationBarDividerColor, std::int32_t(p.navigationBarDividerColor->argb));
        }
    }

    Intent CustomTabsIntent(const std::string& url, const CustomTabsOptions& o) {
        using namespace customtabs;
        Intent i;
        i.action = kActionView;
        i.data = url;
        Bundle& x = i.extras;
        if (o.urlBarHiding) x.Put(kExtraUrlBarHiding, *o.urlBarHiding);
        if (o.closeButtonIcon) x.Put(kExtraCloseButtonIcon, Bitmap(*o.closeButtonIcon));
        if (o.showTitle) x.Put(kExtraTitleVisibility, std::int32_t(1));
        Check(o.shareState >= 0 && o.shareState <= 2, "Invalid value for the shareState argument");
        if (o.shareState == kShareStateOn) x.Put(kExtraDefaultShareMenuItem, true);
        else if (o.shareState == kShareStateOff) x.Put(kExtraDefaultShareMenuItem, false);
        if (o.actionButton) {
            auto b = std::make_shared<Bundle>();
            b->Put(kKeyId, std::int32_t(0));   // TOOLBAR_ACTION_BUTTON_ID
            b->Put(kKeyIcon, Bitmap(o.actionButton->icon));
            b->Put(kKeyDescription, o.actionButton->description);
            b->Put(kKeyPendingIntent, BundlePendingIntent{ o.actionButton->deepLink });
            x.Put(kExtraActionButtonBundle, BundleNested{ b });
            x.Put(kExtraTintActionButton, o.actionButton->tint);
        }
        if (o.secondaryToolbarSwipeUpDeepLink) x.Put(kExtraSwipeUpGesture, BundlePendingIntent{ *o.secondaryToolbarSwipeUpDeepLink });
        if (!o.exitEnterAnimation.empty() || !o.exitExitAnimation.empty())
            x.Put(kExtraExitAnimationBundle, BundleAnimationOptions{ o.exitEnterAnimation, o.exitExitAnimation });
        Check(o.colorScheme >= 0 && o.colorScheme <= 2, "Invalid value for the colorScheme argument");
        if (o.colorScheme != kColorSchemeSystem) x.Put(kExtraColorScheme, std::int32_t(o.colorScheme));
        if (o.initialActivityHeightPx) {
            Check(*o.initialActivityHeightPx > 0, "Invalid value for the initialHeightPx argument");
            Check(o.activityHeightResizeBehavior >= 0 && o.activityHeightResizeBehavior <= 2, "Invalid value for the activityHeightResizeBehavior argument");
            x.Put(kExtraInitialActivityHeightPx, std::int32_t(*o.initialActivityHeightPx));
            x.Put(kExtraActivityHeightResizeBehavior, std::int32_t(o.activityHeightResizeBehavior));
        }
        if (o.initialActivityWidthPx) {
            Check(*o.initialActivityWidthPx > 0, "Invalid value for the initialWidthPx argument");
            x.Put(kExtraInitialActivityWidthPx, std::int32_t(*o.initialActivityWidthPx));
        }
        if (o.activitySideSheetBreakpointDp) {
            Check(*o.activitySideSheetBreakpointDp > 0, "Invalid value for the initialWidthPx argument");
            x.Put(kExtraSideSheetBreakpointDp, std::int32_t(*o.activitySideSheetBreakpointDp));
        }
        if (o.activitySideSheetMaximization) x.Put(kExtraSideSheetMaximization, *o.activitySideSheetMaximization);
        if (o.activitySideSheetPosition) {
            Check(*o.activitySideSheetPosition >= 0 && *o.activitySideSheetPosition <= 2, "Invalid value for the sideSheetPosition argument");
            x.Put(kExtraSideSheetPosition, std::int32_t(*o.activitySideSheetPosition));
        }
        if (o.activitySideSheetDecorationType) {
            Check(*o.activitySideSheetDecorationType >= 0 && *o.activitySideSheetDecorationType <= 3, "Invalid value for the decorationType argument");
            x.Put(kExtraSideSheetDecorationType, std::int32_t(*o.activitySideSheetDecorationType));
        }
        if (o.activitySideSheetRoundedCornersPosition) {
            Check(*o.activitySideSheetRoundedCornersPosition >= 0 && *o.activitySideSheetRoundedCornersPosition <= 2, "Invalid value for the roundedCornersPosition argument");
            x.Put(kExtraSideSheetRoundedCorners, std::int32_t(*o.activitySideSheetRoundedCornersPosition));
        }
        if (o.toolbarCornerRadiusDp) {
            Check(*o.toolbarCornerRadiusDp >= 0 && *o.toolbarCornerRadiusDp <= kMaxToolbarCornerRadiusDp, "Invalid value for the cornerRadiusDp argument");
            x.Put(kExtraToolbarCornerRadiusDp, std::int32_t(*o.toolbarCornerRadiusDp));
        }
        if (o.closeButtonPosition) {
            Check(*o.closeButtonPosition >= 0 && *o.closeButtonPosition <= 2, "Invalid value for the position argument");
            x.Put(kExtraCloseButtonPosition, std::int32_t(*o.closeButtonPosition));
        }
        if (o.openInBrowserButtonState) {
            Check(*o.openInBrowserButtonState >= 0 && *o.openInBrowserButtonState <= 2, "Invalid value for the openInBrowserState argument.");
            x.Put(kExtraOpenInBrowserState, std::int32_t(*o.openInBrowserButtonState));
        }
        if (o.bookmarksButton) x.Put(kExtraDisableBookmarksButton, !*o.bookmarksButton);
        if (o.downloadButton) x.Put(kExtraDisableDownloadButton, !*o.downloadButton);
        if (o.sendToExternalDefaultHandler) x.Put(kExtraSendToExternalHandler, *o.sendToExternalDefaultHandler);
        if (o.initialNavigationAllowedToLeaveBrowser) x.Put(kExtraInitialNavigationCanLeave, *o.initialNavigationAllowedToLeaveBrowser);
        if (!o.translateLocale.empty()) x.Put(kExtraTranslateLanguageTag, o.translateLocale);
        if (o.backgroundInteraction) x.Put(kExtraDisableBackgroundInteraction, !*o.backgroundInteraction);
        if (o.ephemeralBrowsing) x.Put(kExtraEphemeralBrowsing, *o.ephemeralBrowsing);
        if (o.closeButton) x.Put(kExtraCloseButtonEnabled, *o.closeButton);

        // build()
        x.Put(kExtraSession, BundleNullBinder{});   // present, even without a session
        if (!o.menuItems.empty()) {
            BundleList list;
            for (const auto& m : o.menuItems) {
                auto b = std::make_shared<Bundle>();
                b->Put(kKeyMenuItemTitle, m.label);
                b->Put(kKeyPendingIntent, BundlePendingIntent{ m.deepLink });
                list.bundles.push_back(b);
            }
            x.Put(kExtraMenuItems, std::move(list));
        }
        x.Put(kExtraEnableInstantApps, o.instantApps);
        PutColors(x, { o.toolbarColor, o.secondaryToolbarColor, o.navigationBarColor, o.navigationBarDividerColor });
        if (!o.colorSchemeParams.empty()) {
            BundleSparse sparse;
            for (const auto& [scheme, params] : o.colorSchemeParams) {
                Check(scheme == kColorSchemeLight || scheme == kColorSchemeDark, "Invalid colorScheme");
                auto b = std::make_shared<Bundle>();
                PutColors(*b, params);
                sparse.items.emplace_back(scheme, b);
            }
            x.Put(kExtraColorSchemeParams, std::move(sparse));
        }
        x.Put(kExtraShareState, std::int32_t(o.shareState));
        if (!o.customContentActions.empty()) {
            BundleList list;
            for (std::size_t a = 0; a < o.customContentActions.size(); ++a) {
                const auto& c = o.customContentActions[a];
                for (std::size_t p = 0; p < a; ++p)
                    if (o.customContentActions[p].id == c.id) throw std::invalid_argument("CustomContentAction with ID " + std::to_string(c.id) + " already exists.");
                auto b = std::make_shared<Bundle>();
                b->Put(kKeyContentId, std::int32_t(c.id));
                b->Put(kKeyContentLabel, c.label);
                b->Put(kKeyContentPendingIntent, BundlePendingIntent{ c.deepLink });
                b->Put(kKeyContentTargetType, std::int32_t(c.targetType));
                list.bundles.push_back(b);
            }
            x.Put(kExtraCustomContentActions, std::move(list));
        }
        // setCurrentLocaleAsDefaultAcceptLanguage
        const auto locales = PlatformLocaleList();
        if (!locales.empty() && !locales.front().empty()) {
            auto headers = std::make_shared<Bundle>();
            headers->Put("Accept-Language", locales.front());
            x.Put(kExtraHeaders, BundleNested{ headers });
        }
        return i;
    }

    ActivityOptions CustomTabsActivityOptions(const CustomTabsOptions& o) {
        ActivityOptions a;
        a.enterAnimation = o.startEnterAnimation;
        a.exitAnimation = o.startExitAnimation;
        a.shareIdentity = o.shareIdentity;   // setShareIdentityEnabled (API 34)
        // setAllowPassThroughOnTouchOutside (API 36): background interaction, enabled unless off.
        a.allowPassThroughOnTouchOutside = o.backgroundInteraction.value_or(true);
        return a;
    }

}
