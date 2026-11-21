// Sharing: ShareCompat-style send intents, Intent.createChooser's grant migration, and the
// activity host.
#include "Check.h"

#include "kiln/ui/Share.h"

using namespace kiln;
using namespace kiln::ui;

TEST(ShareBuildsSendIntentsAndAChooser) {
    const Intent text = ShareIntent({ .text = "hello", .subject = "Hi" });
    CHECK(text.action == kActionSend && text.type == std::optional<std::string>("text/plain") && text.flags == 0 && text.clipUris.empty());
    CHECK(text.extras.GetString(kExtraText) && *text.extras.GetString(kExtraText) == "hello" && *text.extras.GetString(kExtraSubject) == "Hi");

    const Intent one = ShareIntent({ .streams = { "content://a/1" }, .mimeType = "image/png" });
    CHECK(one.action == kActionSend && one.type == std::optional<std::string>("image/png"));
    CHECK(one.extras.Get(kExtraStream) && *one.extras.Get(kExtraStream) == BundleValue(BundleUri{ "content://a/1" }));
    CHECK((one.flags & kFlagGrantReadUriPermission) && one.clipUris == std::vector<std::string>{ "content://a/1" });

    const Intent many = ShareIntent({ .streams = { "content://a/1", "content://a/2" } });
    CHECK(many.action == kActionSendMultiple && many.type == std::optional<std::string>("*/*"));
    CHECK(*many.extras.Get(kExtraStream) == BundleValue(BundleUriList{ { "content://a/1", "content://a/2" } }));

    // createChooser: the target nested, the title, and the target's grants moved over.
    const Intent chooser = CreateChooser(many, "Send");
    CHECK(chooser.action == kActionChooser && *chooser.extras.GetString(kExtraTitle) == "Send");
    const auto* nested = std::get_if<BundleIntent>(chooser.extras.Get(kExtraIntent));
    CHECK(nested && nested->intent && *nested->intent == many);
    CHECK((chooser.flags & kFlagGrantReadUriPermission) && chooser.clipUris == many.clipUris);
    // A target granting its data, not clip data: the data becomes the chooser's clip.
    Intent view;
    view.action = kActionView;
    view.data = "content://b/1";
    view.flags = kFlagGrantReadUriPermission;
    CHECK(CreateChooser(view).clipUris == std::vector<std::string>{ "content://b/1" });
    // Without grant flags nothing moves.
    CHECK(CreateChooser(text).clipUris.empty() && CreateChooser(text).flags == 0);
}

TEST(StartActivityGoesThroughTheActivityHost) {
    SetActivityHost({});
    CHECK(!Share({ .text = "x" }) && !ShouldShowRequestPermissionRationale("android.permission.CAMERA"));
    std::vector<Intent> started;
    SetActivityHost({
        .startActivity = [&](const Intent& i, const ActivityOptions&) { started.push_back(i); return true; },
        .shouldShowRequestPermissionRationale = [](const std::string& p) { return p == "android.permission.CAMERA"; },
    });
    CHECK(Share({ .text = "x" }, "Title") && OpenUri("https://example.com"));
    CHECK(started.size() == 2 && started[0].action == kActionChooser && started[1].action == kActionView &&
          started[1].data == std::optional<std::string>("https://example.com"));
    CHECK(ShouldShowRequestPermissionRationale("android.permission.CAMERA") && !ShouldShowRequestPermissionRationale("android.permission.RECORD_AUDIO"));
    SetActivityHost({});
}
