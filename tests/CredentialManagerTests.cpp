// Credential Manager calls: the request bundles androidx.credentials builds (CreatePasswordRequest,
// CreatePublicKeyCredentialRequest, DisplayInfo, GetCredentialRequest's metadata), and the
// host's answers as results, exceptions and cancellation.
#include "Check.h"

#include "kiln/ui/CredentialManager.h"

#include <stdexcept>

using namespace kiln;
using namespace kiln::ui;

namespace {
    template<typename F>
    bool Throws(F&& f) {
        try { f(); } catch (const std::invalid_argument&) { return true; }
        return false;
    }
    bool BoolAt(const Bundle& b, const char* key, bool want) {
        const BundleValue* v = b.Get(key);
        return v && *v == BundleValue(want);
    }
}

TEST(CreatePasswordRequestBuildsAndroidxBundles) {
    const CreateCredentialRequest r = CreatePasswordRequest("ada", "s3cret", std::nullopt, true, false, "com.example/.Provider");
    CHECK(r.type == credentials::kTypePasswordCredential && !r.isSystemProviderRequired && !r.origin);
    CHECK(*r.credentialData.GetString(credentials::kBundleKeyId) == "ada" && *r.credentialData.GetString(credentials::kBundleKeyPassword) == "s3cret");
    CHECK(BoolAt(r.credentialData, credentials::kBundleKeyIsAutoSelectAllowed, false) &&
          BoolAt(r.credentialData, credentials::kBundleKeyPreferImmediatelyAvailableCredentials, true));
    // The query phase sees no credential data, only auto-select.
    CHECK(r.candidateQueryData.entries.size() == 1 && BoolAt(r.candidateQueryData, credentials::kBundleKeyIsAutoSelectAllowed, false));
    // The display info is nested in the final data: the id as the user, no display name.
    const Bundle data = r.FinalCredentialData();
    const Bundle* info = data.GetBundle(credentials::kBundleKeyRequestDisplayInfo);
    CHECK(info && *info->GetString(credentials::kBundleKeyUserId) == "ada" && !info->Get(credentials::kBundleKeyUserDisplayName) &&
          *info->GetString(credentials::kBundleKeyDefaultProvider) == "com.example/.Provider");
    CHECK(!r.credentialData.Get(credentials::kBundleKeyRequestDisplayInfo));
    CHECK(Throws([] { CreatePasswordRequest("ada", ""); }) && Throws([] { CreatePasswordRequest("", "pw"); }));
}

TEST(CreatePublicKeyCredentialRequestReadsTheUserFromJson) {
    const std::string json = R"({"rp":{"id":"example.com","name":"Example"},"user":{"id":"dXNlcg","name":"ada@example.com","displayName":"Ada é"},"challenge":"YQ"})";
    const CreateCredentialRequest r = CreatePublicKeyCredentialRequest(json, std::vector<std::uint8_t>{ 1, 2 }, false, std::nullopt, true, true);
    CHECK(r.type == credentials::kTypePublicKeyCredential && r.displayInfo.userId == "ada@example.com" &&
          r.displayInfo.userDisplayName == std::optional<std::string>("Ada \xC3\xA9"));
    for (const Bundle* b : { &r.credentialData, &r.candidateQueryData }) {
        CHECK(*b->GetString(credentials::kBundleKeySubtype) == credentials::kSubtypeCreatePublicKeyCredentialRequest);
        CHECK(*b->GetString(credentials::kBundleKeyRequestJson) == json);
        CHECK(*b->Get(credentials::kBundleKeyClientDataHash) == BundleValue(std::optional<std::vector<std::uint8_t>>(std::vector<std::uint8_t>{ 1, 2 })));
        CHECK(BoolAt(*b, credentials::kBundleKeyIsAutoSelectAllowed, true) && BoolAt(*b, credentials::kBundleKeyIsConditionalRequest, true));
    }
    // displayName: null is none.
    const auto noName = CreatePublicKeyCredentialRequest(R"({"user":{"name":"bob","displayName":null}})");
    CHECK(noName.displayInfo.userId == "bob" && !noName.displayInfo.userDisplayName && !noName.credentialData.Get(credentials::kBundleKeyIsConditionalRequest));
    CHECK(Throws([] { CreatePublicKeyCredentialRequest(R"({"rp":{}})"); }));
    CHECK(Throws([] { CreatePublicKeyCredentialRequest("not json"); }));
    CHECK(Throws([] { CreatePublicKeyCredentialRequest(R"({"user":{"name":"a"}} trailing)"); }));
    CHECK(Throws([] { CreatePublicKeyCredentialRequest(""); }));
}

TEST(GetCredentialRequestMetadata) {
    const GetCredentialRequest r{ .credentialOptions = { GetPasswordOption() }, .preferImmediatelyAvailableCredentials = true };
    const Bundle m = r.MetadataBundle();
    CHECK(BoolAt(m, credentials::kBundleKeyPreferIdentityDocUi, false) && BoolAt(m, credentials::kBundleKeyPreferImmediatelyAvailableCredentials, true));
}

TEST(CredentialManagerCallsGoThroughTheHost) {
    // Without a host: the unsupported exceptions.
    SetCredentialManagerHost({});
    std::string error;
    {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](std::string& error) -> rt::Task<> {
            try { co_await GetCredential({}); } catch (const GetCredentialException& e) { error = e.Type(); }
        }(error));
        CHECK(job.Done() && error == credentials::kGetTypeUnsupported);
    }

    kiln::Function<void(CredentialManagerHost::Outcome)> answer;
    GetCredentialRequest seen;
    CreateCredentialRequest created;
    std::vector<int> cancelled;
    SetCredentialManagerHost({
        .get = [&](const GetCredentialRequest& r, auto done) { seen = r; answer = std::move(done); return 3; },
        .create = [&](const CreateCredentialRequest& r, auto done) { created = r; answer = std::move(done); return 4; },
        .clear = [&](auto done) { answer = std::move(done); return 5; },
        .cancel = [&](int id) { cancelled.push_back(id); },
    });

    Credential got;
    rt::Job get = rt::Launch(rt::Dispatcher::Main(), [](Credential& got) -> rt::Task<> {
        got = co_await GetCredential({ .credentialOptions = { GetPasswordOption({ "ada" }) } });
    }(got));
    CHECK(!get.Done() && seen.credentialOptions.size() == 1 && seen.credentialOptions[0].type == credentials::kTypePasswordCredential);
    Bundle data;
    data.Put(credentials::kBundleKeyId, std::string("ada"));
    data.Put(credentials::kBundleKeyPassword, std::string("pw"));
    answer({ true, credentials::kTypePasswordCredential, data, "" });
    CHECK(get.Done() && got.type == credentials::kTypePasswordCredential && *got.data.GetString(credentials::kBundleKeyPassword) == "pw");

    // An error answer: the exception carries the platform's type and message.
    std::string type, message;
    rt::Job create = rt::Launch(rt::Dispatcher::Main(), [](std::string& type, std::string& message) -> rt::Task<> {
        try { co_await CreateCredential(CreatePasswordRequest("ada", "pw")); }
        catch (const CreateCredentialException& e) { type = e.Type(); message = e.what(); }
    }(type, message));
    CHECK(created.type == credentials::kTypePasswordCredential && created.FinalCredentialData().GetBundle(credentials::kBundleKeyRequestDisplayInfo));
    answer({ false, credentials::kCreateTypeUserCanceled, {}, "dismissed" });
    CHECK(create.Done() && type == credentials::kCreateTypeUserCanceled && message == "dismissed");

    bool cleared = false;
    rt::Job clear = rt::Launch(rt::Dispatcher::Main(), [](bool& cleared) -> rt::Task<> { co_await ClearCredentialState(); cleared = true; }(cleared));
    answer({ true, "", {}, "" });
    CHECK(clear.Done() && cleared && cancelled.empty());

    // Cancelling the task cancels the platform call.
    {
        rt::Job open = rt::Launch(rt::Dispatcher::Main(), [](Credential& got) -> rt::Task<> { got = co_await GetCredential({}); }(got));
    }
    CHECK(cancelled == std::vector<int>{ 3 });
    SetCredentialManagerHost({});
}
