// Custom Tabs' intent (CustomTabsIntent.Builder.build) and the shortcuts' host calls.
#include "Check.h"

#include "kiln/ui/Configuration.h"
#include "kiln/ui/CustomTabs.h"
#include "kiln/ui/Shortcuts.h"

using namespace kiln;
using namespace kiln::ui;

TEST(CustomTabsIntentCarriesTheBuildersExtras) {
    const Intent plain = CustomTabsIntent("https://example.com");
    CHECK(plain.action == "android.intent.action.VIEW" && plain.data == std::optional<std::string>("https://example.com"));
    // EXTRA_SESSION is always there, with no binder; instant apps on; share state default.
    CHECK(plain.extras.Get("android.support.customtabs.extra.SESSION") &&
          std::holds_alternative<BundleNullBinder>(*plain.extras.Get("android.support.customtabs.extra.SESSION")));
    CHECK(*plain.extras.Get("android.support.customtabs.extra.EXTRA_ENABLE_INSTANT_APPS") == BundleValue(true));
    CHECK(*plain.extras.Get("androidx.browser.customtabs.extra.SHARE_STATE") == BundleValue(std::int32_t(0)));
    CHECK(!plain.extras.Get("android.support.customtabs.extra.TOOLBAR_COLOR") && !plain.extras.Get("android.support.customtabs.extra.TITLE_VISIBILITY"));

    const Intent styled = CustomTabsIntent("https://example.com", { .toolbarColor = Color(0xFF6750A4u), .colorScheme = 2, .showTitle = true,
                                                                    .urlBarHiding = true, .shareState = 2 });
    CHECK(*styled.extras.Get("android.support.customtabs.extra.TOOLBAR_COLOR") == BundleValue(std::int32_t(0xFF6750A4u)));
    CHECK(*styled.extras.Get("androidx.browser.customtabs.extra.COLOR_SCHEME") == BundleValue(std::int32_t(2)));
    CHECK(*styled.extras.Get("android.support.customtabs.extra.TITLE_VISIBILITY") == BundleValue(std::int32_t(1)));
    CHECK(*styled.extras.Get("android.support.customtabs.extra.ENABLE_URLBAR_HIDING") == BundleValue(true));
    CHECK(*styled.extras.Get("androidx.browser.customtabs.extra.SHARE_STATE") == BundleValue(std::int32_t(2)));
}

TEST(ShortcutsGoThroughTheHost) {
    SetShortcutHost({});
    CHECK(!SetDynamicShortcuts({ { .id = "a" } }) && DynamicShortcutIds().empty());
    std::vector<std::string> ids;
    std::vector<ShortcutOp> ops;
    SetShortcutHost([&](ShortcutOp op, const std::vector<Shortcut>& s, const std::vector<std::string>& r) -> std::vector<std::string> {
        ops.push_back(op);
        switch (op) {
            case ShortcutOp::Set: ids.clear(); for (const auto& x : s) ids.push_back(x.id); return { "ok" };
            case ShortcutOp::Push: ids.push_back(s.at(0).id); return { "ok" };
            case ShortcutOp::Remove: for (const auto& id : r) std::erase(ids, id); return { "ok" };
            case ShortcutOp::List: return ids;
            default: return {};
        }
    });
    CHECK(SetDynamicShortcuts({ { .id = "new", .shortLabel = "New", .data = "notes://new" }, { .id = "search", .shortLabel = "Search" } }));
    CHECK(PushDynamicShortcut({ .id = "recent", .shortLabel = "Recent" }));
    RemoveDynamicShortcuts({ "search" });
    CHECK(DynamicShortcutIds() == (std::vector<std::string>{ "new", "recent" }));
    CHECK(!RequestPinShortcut({ .id = "new" }));   // the host refused
    ReportShortcutUsed("new");
    CHECK(ops.back() == ShortcutOp::ReportUsed);
    SetShortcutHost({});
}

TEST(ConversationShortcutsAreLongLivedWithALocus) {
    std::vector<Shortcut> seen;
    std::vector<std::string> removed;
    SetShortcutHost([&](ShortcutOp op, const std::vector<Shortcut>& s, const std::vector<std::string>& r) -> std::vector<std::string> {
        if (op == ShortcutOp::RemoveLongLived) removed = r;
        seen = s;
        return { "ok" };
    });
    // ShortcutInfoCompat.Builder.setIsConversation: long-lived, the id as the locus.
    CHECK(PushDynamicShortcut({ .id = "chat-u1", .shortLabel = "Ada", .categories = { "com.example.SHARE_TARGET" },
                                .persons = { { .name = "Ada", .key = "u1" } }, .isConversation = true }));
    CHECK(seen.size() == 1 && seen[0].longLived && seen[0].locusId == "chat-u1" && seen[0].persons.size() == 1 &&
          seen[0].categories == std::vector<std::string>{ "com.example.SHARE_TARGET" });
    // An explicit locus stays; a plain shortcut is neither.
    CHECK(PushDynamicShortcut({ .id = "chat-u2", .shortLabel = "Bob", .isConversation = true, .locusId = "screen-2" }) && seen[0].locusId == "screen-2");
    CHECK(PushDynamicShortcut({ .id = "new", .shortLabel = "New" }) && !seen[0].longLived && seen[0].locusId.empty());
    RemoveLongLivedShortcuts({ "chat-u1" });
    CHECK(removed == std::vector<std::string>{ "chat-u1" });
    SetShortcutHost({});
}

// The rest of CustomTabsIntent.Builder: menu items and the action button (PendingIntents that
// open the app), colour scheme params as a SparseArray, partial tabs, toolbar options, content
// actions, Accept-Language, and the start options.
TEST(CustomTabsIntentCarriesTheRestOfTheBuilder) {
    SetPlatformLocaleList({ "de-DE" });
    CustomTabsOptions o;
    o.menuItems = { { "Report", "app://report" } };
    o.actionButton = CustomTabActionButton{ { {}, "ic_bookmark" }, "Bookmark", "app://bookmark", true };
    o.colorSchemeParams[customtabs::kColorSchemeDark] = { .toolbarColor = Color(0xFF000000u) };
    o.navigationBarDividerColor = Color(0xFF111111u);
    o.initialActivityHeightPx = 600;
    o.activityHeightResizeBehavior = customtabs::kActivityHeightFixed;
    o.toolbarCornerRadiusDp = 8;
    o.bookmarksButton = false;
    o.backgroundInteraction = false;
    o.customContentActions = { { 1, "Save image", "app://save", customtabs::kContentTargetTypeImage } };
    o.exitEnterAnimation = "slide_in_left";
    o.startEnterAnimation = "slide_in_right";
    o.shareState = customtabs::kShareStateOn;
    o.shareIdentity = true;
    const Intent i = CustomTabsIntent("https://example.com", o);
    const auto* menu = std::get_if<BundleList>(i.extras.Get("android.support.customtabs.extra.MENU_ITEMS"));
    CHECK(menu && menu->bundles.size() == 1 && *menu->bundles[0]->GetString("android.support.customtabs.customaction.MENU_ITEM_TITLE") == "Report");
    CHECK(*menu->bundles[0]->Get("android.support.customtabs.customaction.PENDING_INTENT") == BundleValue(BundlePendingIntent{ "app://report" }));
    const Bundle* action = i.extras.GetBundle("android.support.customtabs.extra.ACTION_BUTTON_BUNDLE");
    CHECK(action && *action->Get("android.support.customtabs.customaction.ICON") == BundleValue(BundleBitmap{ {}, "ic_bookmark" }));
    CHECK(*i.extras.Get("android.support.customtabs.extra.TINT_ACTION_BUTTON") == BundleValue(true));
    const auto* schemes = std::get_if<BundleSparse>(i.extras.Get("androidx.browser.customtabs.extra.COLOR_SCHEME_PARAMS"));
    CHECK(schemes && schemes->items.size() == 1 && schemes->items[0].first == 2 &&
          *schemes->items[0].second->Get("android.support.customtabs.extra.TOOLBAR_COLOR") == BundleValue(std::int32_t(0xFF000000u)));
    CHECK(*i.extras.Get("androidx.browser.customtabs.extra.NAVIGATION_BAR_DIVIDER_COLOR") == BundleValue(std::int32_t(0xFF111111u)));
    CHECK(*i.extras.Get("androidx.browser.customtabs.extra.INITIAL_ACTIVITY_HEIGHT_PX") == BundleValue(std::int32_t(600)));
    CHECK(*i.extras.Get("androidx.browser.customtabs.extra.ACTIVITY_HEIGHT_RESIZE_BEHAVIOR") == BundleValue(std::int32_t(2)));
    CHECK(*i.extras.Get("androidx.browser.customtabs.extra.TOOLBAR_CORNER_RADIUS_DP") == BundleValue(std::int32_t(8)));
    CHECK(*i.extras.Get("org.chromium.chrome.browser.customtabs.EXTRA_DISABLE_STAR_BUTTON") == BundleValue(true));
    CHECK(*i.extras.Get("androidx.browser.customtabs.extra.DISABLE_BACKGROUND_INTERACTION") == BundleValue(true));
    CHECK(*i.extras.Get("android.support.customtabs.extra.SHARE_MENU_ITEM") == BundleValue(true));
    CHECK(*i.extras.Get("android.support.customtabs.extra.EXIT_ANIMATION_BUNDLE") == BundleValue(BundleAnimationOptions{ "slide_in_left", "" }));
    const auto* content = std::get_if<BundleList>(i.extras.Get("androidx.browser.customtabs.extra.CUSTOM_CONTENT_ACTIONS"));
    CHECK(content && content->bundles.size() == 1 && *content->bundles[0]->Get("androidx.browser.customtabs.customcontentaction.TARGET_TYPE") == BundleValue(std::int32_t(1)));
    const Bundle* headers = i.extras.GetBundle("com.android.browser.headers");
    CHECK(headers && *headers->GetString("Accept-Language") == "de-DE");
    const ActivityOptions start = CustomTabsActivityOptions(o);
    CHECK(start.enterAnimation == "slide_in_right" && start.shareIdentity == true && start.allowPassThroughOnTouchOutside == false);
    // The setters' range checks.
    auto throws = [](CustomTabsOptions bad) {
        try { CustomTabsIntent("https://example.com", bad); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(throws({ .colorScheme = 3 }));
    CustomTabsOptions radius;
    radius.toolbarCornerRadiusDp = 17;
    CHECK(throws(radius));
    CustomTabsOptions dup;
    dup.customContentActions = { { 1, "a", "x" }, { 1, "b", "y" } };
    CHECK(throws(dup));
    SetPlatformLocaleList({ "en-US" });
}
