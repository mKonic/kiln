// Custom Tabs' intent (CustomTabsIntent.Builder.build) and the shortcuts' host calls.
#include "Check.h"

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
