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
