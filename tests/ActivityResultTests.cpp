#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/activity/ActivityResult.h"
#include "kiln/ui/LayoutNode.h"

#include <set>

using namespace kiln;
using namespace kiln::foundation;

TEST(ActivityResultLaunchersStartParseAndAnswerAtOnce) {
    ui::Owner owner;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<std::string> registered;
    std::vector<std::pair<std::string, ui::Intent>> launched;
    std::set<std::string> granted{ "android.permission.CAMERA" };
    ui::ActivityResultHost host;
    host.registerKey = [&](const std::string& k) { registered.push_back(k); };
    host.unregister = [&](const std::string&) {};
    host.launch = [&](const std::string& k, const ui::Intent& i) { launched.push_back({ k, i }); return true; };
    host.checkSelfPermission = [&](const std::string& p) { return granted.count(p) != 0; };
    owner.ActivityResults().SetHost(host);
    std::optional<std::string> picked;
    std::map<std::string, bool> permissions;
    std::optional<ActivityResultLauncher<std::vector<std::string>, std::optional<std::string>>> open;
    std::optional<ActivityResultLauncher<std::vector<std::string>, std::map<std::string, bool>>> ask;
    comp.SetContent([&] {
        rt::Composition::Current()->Provide(ui::LocalActivityResultRegistry, &owner.ActivityResults(), [&] {
            open = RememberLauncherForActivityResult<std::vector<std::string>, std::optional<std::string>>(
                std::make_shared<contracts::OpenDocument>(), [&](const std::optional<std::string>& u) { picked = u; }, rt::Key(1));
            ask = RememberLauncherForActivityResult<std::vector<std::string>, std::map<std::string, bool>>(
                std::make_shared<contracts::RequestMultiplePermissions>(), [&](const std::map<std::string, bool>& m) { permissions = m; }, rt::Key(2));
        });
    });
    comp.Recompose();
    CHECK_EQ(int(registered.size()), 2);
    // A document picker: the intent the contract describes, the URI it returns.
    open->Launch({ "application/pdf" });
    CHECK_EQ(int(launched.size()), 1);
    if (!launched.empty()) {
        CHECK(launched[0].second.action == "android.intent.action.OPEN_DOCUMENT");
        CHECK(launched[0].second.type == std::optional<std::string>("*/*"));
        ui::Intent data;
        data.data = "content://docs/1";
        owner.ActivityResults().DispatchActivityResult(launched[0].first, { ui::kResultOk, data });
    }
    CHECK(picked == std::optional<std::string>("content://docs/1"));
    // Permissions already held: answered without asking (posted).
    ask->Launch({ "android.permission.CAMERA" });
    CHECK_EQ(int(launched.size()), 1);
    rt::Dispatcher::Main().RunPending();
    CHECK(permissions.size() == 1 && permissions["android.permission.CAMERA"]);
    // Not held: the request intent, and the grant results parsed.
    ask->Launch({ "android.permission.RECORD_AUDIO" });
    CHECK_EQ(int(launched.size()), 2);
    if (launched.size() == 2) {
        CHECK(launched[1].second.action == "androidx.activity.result.contract.action.REQUEST_PERMISSIONS");
        ui::Intent data;
        data.extras.Put("androidx.activity.result.contract.extra.PERMISSIONS", std::vector<std::string>{ "android.permission.RECORD_AUDIO" });
        data.extras.Put("androidx.activity.result.contract.extra.PERMISSION_GRANT_RESULTS", std::vector<std::int32_t>{ -1 });
        owner.ActivityResults().DispatchActivityResult(launched[1].first, { ui::kResultOk, data });
    }
    CHECK(permissions.size() == 1 && !permissions["android.permission.RECORD_AUDIO"]);
}

// ContextCompat.checkSelfPermission: the host's answer; every permission without a host.
TEST(CheckSelfPermissionAsksTheHost) {
    CHECK(ui::CheckSelfPermission("android.permission.CAMERA"));
    ui::SetPermissionCheck([](const std::string& p) { return p == "android.permission.CAMERA"; });
    CHECK(ui::CheckSelfPermission("android.permission.CAMERA") && !ui::CheckSelfPermission("android.permission.RECORD_AUDIO"));
    ui::SetPermissionCheck({});
}

// Auth Tab (AuthTabIntentTest, AuthenticateUserResultContract): the intent, the redirect extras,
// the results, and an Auth Tab session through a fake host.
#include "kiln/foundation/activity/AuthTab.h"

TEST(AuthTabIntentContractAndSession) {
    using namespace kiln::foundation;
    AuthTabOptions o;
    o.toolbarColor = kiln::ui::Color(0xFF112233u);
    o.colorSchemeParams[kiln::ui::customtabs::kColorSchemeDark] = { .toolbarColor = kiln::ui::Color(0xFF000000u) };
    o.ephemeralBrowsing = true;
    const kiln::ui::Intent built = AuthTabIntent(o);
    CHECK(*built.extras.Get(authtab::kExtraLaunchAuthTab) == kiln::ui::BundleValue(true));
    CHECK(std::holds_alternative<kiln::ui::BundleNullBinder>(*built.extras.Get("android.support.customtabs.extra.SESSION")));
    CHECK(*built.extras.Get("android.support.customtabs.extra.TOOLBAR_COLOR") == kiln::ui::BundleValue(std::int32_t(0xFF112233u)));
    CHECK(std::get<kiln::ui::BundleSparse>(*built.extras.Get("androidx.browser.customtabs.extra.COLOR_SCHEME_PARAMS")).items.size() == 1);
    CHECK(!built.extras.Get("androidx.browser.customtabs.extra.COLOR_SCHEME"));

    contracts::AuthenticateUser contract;
    const kiln::ui::Intent scheme = contract.CreateIntent(AuthTabRequest::ForScheme(built, "https://auth.example.com/login", "myapp"));
    CHECK(scheme.data == std::optional<std::string>("https://auth.example.com/login") && *scheme.extras.GetString(authtab::kExtraRedirectScheme) == "myapp");
    const kiln::ui::Intent https = contract.CreateIntent(AuthTabRequest::ForHttps(built, "https://auth.example.com/login", "example.com", "/done"));
    CHECK(*https.extras.GetString(authtab::kExtraHttpsRedirectHost) == "example.com" && *https.extras.GetString(authtab::kExtraHttpsRedirectPath) == "/done");
    kiln::ui::Intent back;
    back.data = "myapp://done?code=1";
    const AuthResult ok = contract.ParseResult(authtab::kResultOk, back);
    CHECK(ok.resultCode == authtab::kResultOk && ok.resultUri == std::optional<std::string>("myapp://done?code=1"));
    CHECK(contract.ParseResult(authtab::kResultVerificationFailed, back).resultUri == std::nullopt);
    CHECK(contract.ParseResult(42, back).resultCode == authtab::kResultUnknownCode);

    // A session: its callbacks under the Custom Tabs names, the intent tied to its browser.
    kiln::ui::SetCustomTabsHost({ .newAuthTabSession = [](int, std::optional<int>) { return 77; } });
    kiln::ui::CustomTabsClient client(1, "com.browser");
    int event = 0;
    auto session = client.AttachAuthTabSession(kiln::ui::CreatePendingAuthTabSession(4, { .onNavigationEvent = [&](int e, const kiln::ui::Bundle&) { event = e; } }));
    CHECK(session && session->Id() == 77 && session->SessionId() == std::optional<int>(4));
    kiln::ui::Bundle nav;
    nav.Put("event", std::int32_t(kiln::ui::customtabs::kNavigationStarted));
    kiln::ui::DispatchCustomTabsCallback(77, "onNavigationEvent", nav);
    kiln::rt::Dispatcher::Main().RunPending();
    CHECK(event == kiln::ui::customtabs::kNavigationStarted);
    AuthTabOptions tied;
    tied.session = session;
    const kiln::ui::Intent t = AuthTabIntent(tied);
    CHECK(t.packageName == "com.browser" && *t.extras.Get("android.support.customtabs.extra.SESSION") == kiln::ui::BundleValue(kiln::ui::BundleCustomTabsSession{ 77 }));
    kiln::ui::SetCustomTabsHost({});
}
