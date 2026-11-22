// Vibration, sensors, location and the biometric prompt over fake hosts: the checks, the flows
// and the cancellation each API promises.
#include "Check.h"

#include "kiln/lifecycle/Lifecycle.h"
#include "kiln/security/KeyStore.h"
#include "kiln/ui/Biometric.h"

#include <map>
#include "kiln/ui/Location.h"
#include "kiln/ui/Sensors.h"
#include "kiln/ui/Vibration.h"

#include <map>

using namespace kiln;
using namespace kiln::ui;

TEST(VibrationChecksPatternsAndUsesTheHost) {
    SetVibrationHost({});
    CHECK(!Vibrate(VibrationEffect::OneShot(50)) && !HasVibrator());
    std::vector<VibrationEffect> played;
    int cancels = 0;
    SetVibrationHost({ .vibrate = [&](const VibrationEffect& e) { played.push_back(e); return true; }, .cancel = [&] { ++cancels; },
                       .hasVibrator = [] { return true; } });
    CHECK(Vibrate(VibrationEffect::Waveform({ 0, 40, 60, 40 }, { 0, 255, 0, 128 }, 1)));
    CHECK(!Vibrate(VibrationEffect::Waveform({ 0, 40 }, { 255 })));          // amplitudes do not match
    CHECK(!Vibrate(VibrationEffect::Waveform({ 0, 40 }, {}, 2)));            // repeat past the end
    CHECK(Vibrate(VibrationEffect::Compose({ { VibrationEffect::PrimitiveClick, 0.5f, 0 }, { VibrationEffect::PrimitiveTick, 1, 20 } })));
    CancelVibration();
    CHECK(played.size() == 2 && played[1].primitives.size() == 2 && cancels == 1 && HasVibrator());
    SetVibrationHost({});
}

TEST(SensorEventsFlowWhileCollected) {
    std::map<int, kiln::Function<void(const SensorEvent&)>> listeners;
    int next = 0;
    SetSensorHost({
        .has = [](SensorType t) { return t == SensorType::Accelerometer; },
        .start = [&](SensorType t, int, kiln::Function<void(const SensorEvent&)> l) { if (t != SensorType::Accelerometer) return -1; listeners[next] = std::move(l); return next++; },
        .stop = [&](int id) { listeners.erase(id); },
    });
    CHECK(HasSensor(SensorType::Accelerometer) && !HasSensor(SensorType::Light));
    std::vector<float> seen;
    {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), SensorEvents(SensorType::Accelerometer).Collect(
            kiln::Function<void(const SensorEvent&)>([&](const SensorEvent& e) { seen.push_back(e.values[0]); })));
        CHECK(listeners.size() == 1);
        for (float x : { 1.0f, 2.0f }) { SensorEvent e; e.values[0] = x; e.valueCount = 3; listeners.begin()->second(e); }
        CHECK(seen == (std::vector<float>{ 1, 2 }));
    }
    CHECK(listeners.empty());   // collection ended: unsubscribed
    // No such sensor: the flow ends at once.
    bool ended = false;
    rt::Job none = rt::Launch(rt::Dispatcher::Main(), [](bool& ended) -> rt::Task<> {
        co_await SensorEvents(SensorType::Light).Collect(kiln::Function<void(const SensorEvent&)>([](const SensorEvent&) {}));
        ended = true;
    }(ended));
    CHECK(ended);
    SetSensorHost({});
}

TEST(LocationAnswersOnceAndCancels) {
    std::map<int, kiln::Function<void(std::optional<Location>)>> pending;
    std::vector<int> cancelled;
    int next = 0;
    SetLocationHost({
        .lastKnown = [] { return std::optional(Location{ .latitude = 1, .longitude = 2, .provider = "network" }); },
        .current = [&](const std::string&, kiln::Function<void(std::optional<Location>)> done) { pending[next] = std::move(done); return next++; },
        .cancel = [&](int id) { cancelled.push_back(id); },
        .enabled = [] { return true; },
    });
    CHECK(LastKnownLocation() && LastKnownLocation()->latitude == 1 && IsLocationEnabled());
    std::optional<Location> got;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
    CHECK(!job.Done() && pending.size() == 1);
    pending[0](Location{ .latitude = 45.5, .longitude = -73.6, .accuracy = 12.0f, .provider = "gps" });
    CHECK(job.Done() && got && got->latitude == 45.5 && got->accuracy == std::optional(12.0f) && cancelled.empty());
    // A task cancelled before the fix cancels the request.
    {
        rt::Job waiting = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
        CHECK(!waiting.Done());
    }
    CHECK(cancelled == std::vector<int>{ 1 });
    SetLocationHost({});
    rt::Job off = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
    CHECK(off.Done() && !got);
}

TEST(BiometricPromptResultsAndCancellation) {
    CHECK(CanAuthenticate() == BIOMETRIC_ERROR_UNSUPPORTED && !GetBiometricStrings(kBiometricStrong));
    kiln::Function<void(const BiometricEvent&)> answer;
    std::vector<int> cancelled;
    BiometricPromptInfo shown;
    std::optional<CryptoObject> shownCrypto;
    SetBiometricHost({
        .canAuthenticate = [](int a) { return a == kBiometricStrong ? BIOMETRIC_ERROR_NONE_ENROLLED : BIOMETRIC_SUCCESS; },
        .strings = [](int) { return std::optional<BiometricStrings>(BiometricStrings{ "Use fingerprint", "Touch the sensor", "Fingerprint unlock" }); },
        .authenticate = [&](const BiometricPromptInfo& i, const std::optional<CryptoObject>& c, kiln::Function<void(const BiometricEvent&)> done) {
            shown = i;
            shownCrypto = c;
            answer = std::move(done);
            return 7;
        },
        .cancel = [&](int id) { cancelled.push_back(id); },
    });
    CHECK(CanAuthenticate() == 0 && CanAuthenticate(kBiometricStrong) == 11 && GetBiometricStrings(kBiometricWeak)->buttonLabel == std::string("Use fingerprint"));
    BiometricResult r;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](BiometricResult& r) -> rt::Task<> {
        r = co_await Authenticate({ .title = "Unlock" }, CryptoObject::Encrypt("notes", { 1, 2, 3 }));
    }(r));
    CHECK(shown.title == "Unlock" && shownCrypto && shownCrypto->keyAlias == "notes" && shownCrypto->input == (std::vector<std::uint8_t>{ 1, 2, 3 }) && !job.Done());
    answer({ BiometricEvent::Kind::Failed });   // a rejected finger: still open
    CHECK(!job.Done());
    BiometricEvent ok{ BiometricEvent::Kind::Succeeded };
    ok.authenticationType = AUTHENTICATION_RESULT_TYPE_BIOMETRIC;
    ok.crypto = CryptoObject::Encrypt("notes", {});
    ok.crypto->output = { 9, 9 };
    ok.crypto->outputIv = { 4 };
    answer(ok);
    CHECK(job.Done() && r.succeeded && r.authenticationType == 2 && r.crypto && r.crypto->output == (std::vector<std::uint8_t>{ 9, 9 }) && cancelled.empty());
    {
        rt::Job open = rt::Launch(rt::Dispatcher::Main(), [](BiometricResult& r) -> rt::Task<> { r = co_await Authenticate({ .title = "Again" }); }(r));
    }
    CHECK(cancelled == std::vector<int>{ 7 });

    // BiometricPrompt: failed attempts, then an error; cancelAuthentication.
    int failed = 0, error = 0;
    BiometricPrompt prompt({ .onAuthenticationError = [&](int code, const std::string&) { error = code; },
                             .onAuthenticationFailed = [&] { ++failed; } });
    prompt.Authenticate({ .title = "P" });
    answer({ BiometricEvent::Kind::Failed });
    answer({ BiometricEvent::Kind::Failed });
    prompt.CancelAuthentication();
    CHECK(cancelled.back() == 7);
    answer({ BiometricEvent::Kind::Error, ERROR_CANCELED, "Cancelled" });
    CHECK(failed == 2 && error == ERROR_CANCELED);

    PromptVerticalListContentView list;
    list.SetDescription("d").AddListItem(PromptContentItemPlainText("a")).AddListItem(PromptContentItemBulletedText("b"), 0);
    CHECK(list.Build().items.size() == 2 && list.Build().items[0].kind == PromptContentItem::Kind::BulletedText);
    bool threw = false;
    try { list.AddListItem(PromptContentItemPlainText("c"), 5); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    BiometricPromptInfo legacy;
    CHECK(!legacy.IsDeviceCredentialAllowed() && legacy.SetDeviceCredentialAllowed(true).IsDeviceCredentialAllowed());
    SetBiometricHost({});
}

TEST(BiometricRequestsMapToThePrompt) {
    using Req = AuthenticationRequest;
    // No fallback: a Cancel button that cancels.
    BiometricPromptInfo p = Req::BiometricRequest("Sign in", {}).ToPromptInfo(false);
    CHECK(p.allowedAuthenticators == kBiometricWeak && p.negativeButton == BiometricPromptInfo::NegativeButton::Cancel && p.negativeButtonText.empty());
    // One custom option: the negative button chooses it.
    p = Req::BiometricRequest("Sign in", { Fallback::CustomOption("Use password", Fallback::ICON_TYPE_PASSWORD) }).ToPromptInfo(false);
    CHECK(p.negativeButtonText == "Use password" && p.negativeButton == BiometricPromptInfo::NegativeButton::Fallback && p.fallbackOptions.size() == 1);
    // The device credential: allowed beside the biometric.
    std::optional<CryptoObject> crypto;
    p = Req::BiometricRequest("Pay", { Fallback::DeviceCredential() }, [](Req::Biometric& b) {
            b.minStrength = Req::Biometric::Strength::Class3(CryptoObject::Sign("pay", { 1 }));
            b.subtitle = "Confirm";
            b.content = Req::BodyContent::VerticalList("Order", { PromptContentItemPlainText("1 item") });
            b.isConfirmationRequired = false;
        }).ToPromptInfo(false, &crypto);
    CHECK(p.allowedAuthenticators == (kBiometricStrong | kDeviceCredential) && p.subtitle == "Confirm" && !p.confirmationRequired);
    CHECK(p.contentView && p.contentView->kind == PromptContentView::Kind::VerticalList && p.contentView->items.size() == 1);
    CHECK(crypto && crypto->kind == CryptoObject::Kind::Signature);
    // Several fallbacks: listed where the platform can (36.1), else the first alone.
    Req many = Req::BiometricRequest("x", { Fallback::CustomOption("A"), Fallback::CustomOption("B"), Fallback::DeviceCredential() });
    p = many.ToPromptInfo(true);
    CHECK(p.fallbackOptions.size() == 2 && (p.allowedAuthenticators & kDeviceCredential) && p.negativeButtonText.empty());
    p = many.ToPromptInfo(false);
    CHECK(p.negativeButtonText == "A" && !(p.allowedAuthenticators & kDeviceCredential));
    // The credential alone.
    p = Req::CredentialRequest("PIN", [](Req::Credential& c) { c.content = Req::BodyContent::PlainText("Why"); }).ToPromptInfo(false);
    CHECK(p.allowedAuthenticators == kDeviceCredential && p.description == "Why");
    bool threw = false;
    try { Req::BiometricRequest("x", { Fallback::DeviceCredential(), Fallback::DeviceCredential() }); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw && Req::Biometric::GetMaxFallbackOptions() == 4);
    threw = false;
    try { Req::BiometricRequest("x", std::vector<Fallback>(5, Fallback::CustomOption("o"))); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(BiometricLauncherWaitsForTheLifecycleAndReportsResults) {
    kiln::Function<void(const BiometricEvent&)> answer;
    BiometricPromptInfo shown;
    int prompts = 0;
    SetBiometricHost({
        .authenticate = [&](const BiometricPromptInfo& i, const std::optional<CryptoObject>&, kiln::Function<void(const BiometricEvent&)> done) {
            shown = i;
            answer = std::move(done);
            ++prompts;
            return 3;
        },
        .cancel = [](int) {},
    });
    struct Owner : lifecycle::LifecycleOwner {
        std::unique_ptr<lifecycle::LifecycleRegistry> registry = lifecycle::LifecycleRegistry::CreateUnsafe(*this);
        lifecycle::Lifecycle& GetLifecycle() override { return *registry; }
    } owner;
    owner.registry->HandleLifecycleEvent(lifecycle::Lifecycle::Event::OnCreate);
    std::vector<std::string> results;
    int attempts = 0;
    auto launcher = RegisterForAuthenticationResult(owner, {
        .onAuthResult = [&](const AuthenticationResult& r) {
            if (r.IsSuccess()) results.push_back("success " + std::to_string(r.success()->authType));
            else if (r.IsError()) results.push_back("error " + std::to_string(r.error()->errorCode));
            else results.push_back("fallback " + r.customFallbackSelected()->fallback.text);
        },
        .onAuthAttemptFailed = [&] { ++attempts; },
    });
    launcher->Launch(AuthenticationRequest::BiometricRequest("Wait", { Fallback::CustomOption("Password") }));
    CHECK(prompts == 0);   // created, not started: pending
    owner.registry->HandleLifecycleEvent(lifecycle::Lifecycle::Event::OnStart);
    CHECK(prompts == 1 && shown.title == "Wait");
    answer({ BiometricEvent::Kind::Failed });
    answer({ BiometricEvent::Kind::FallbackSelected, 0, "Password", 0, 0 });
    launcher->Launch(AuthenticationRequest::CredentialRequest("PIN"));
    answer({ BiometricEvent::Kind::Succeeded, 0, {}, AUTHENTICATION_RESULT_TYPE_DEVICE_CREDENTIAL });
    launcher->Launch(AuthenticationRequest::CredentialRequest("PIN"));
    answer({ BiometricEvent::Kind::Error, ERROR_USER_CANCELED, "x" });
    CHECK(attempts == 1 && results == (std::vector<std::string>{ "fallback Password", "success 1", "error 10" }));
    owner.registry->HandleLifecycleEvent(lifecycle::Lifecycle::Event::OnStop);
    owner.registry->HandleLifecycleEvent(lifecycle::Lifecycle::Event::OnDestroy);
    launcher->Launch(AuthenticationRequest::CredentialRequest("Gone"));
    CHECK(prompts == 3);   // ignored once destroyed
    SetBiometricHost({});
}

TEST(KeyStoreKeysThroughTheHost) {
    CHECK(!security::GenerateKey("k", {}) && !security::ContainsKey("k") && !security::GetPublicKey("k"));
    std::map<std::string, security::KeyGenSpec> keys;
    security::SetKeyStoreHost({
        .generate = [&](const std::string& a, const security::KeyGenSpec& s) { keys[a] = s; return true; },
        .remove = [&](const std::string& a) { return keys.erase(a) > 0; },
        .contains = [&](const std::string& a) { return keys.contains(a); },
        .publicKey = [&](const std::string& a) -> std::optional<std::vector<std::uint8_t>> {
            if (!keys.contains(a) || keys[a].algorithm == security::KeyGenSpec::Algorithm::AES) return std::nullopt;
            return std::vector<std::uint8_t>{ 0x30 };
        },
    });
    CHECK(security::GenerateKey("sign", { .algorithm = security::KeyGenSpec::Algorithm::EC, .purposes = security::PURPOSE_SIGN, .blockModes = {},
                                          .encryptionPaddings = {}, .digests = { "SHA-256" }, .userAuthenticationRequired = true }));
    CHECK(security::ContainsKey("sign") && keys["sign"].userAuthenticationRequired && security::GetPublicKey("sign"));
    CHECK(security::DeleteKey("sign") && !security::DeleteKey("sign"));
    security::SetKeyStoreHost({});
}
