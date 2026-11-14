// Speech: text to speech's binding, the requests that wait for it, and the recognizer's session.
#include "Check.h"

#include "kiln/speech/SpeechRecognizer.h"
#include "kiln/speech/TextToSpeech.h"

using namespace kiln;
using namespace kiln::speech;

namespace {
    struct FakeTts {
        std::vector<std::string> log;
        int created = 0, lastId = 0;
        bool speaking = false;
        FakeTts() {
            SetTtsHost({
                .create = [this](int id, const std::string& engine) { ++created; lastId = id; log.push_back("create " + engine); },
                .shutdown = [this](int) { log.push_back("shutdown"); },
                .speak = [this](int, const std::string& text, const std::string& uid, int queue, float, float) {
                    log.push_back((queue ? "add " : "flush ") + text + " " + uid);
                    return 0;
                },
                .silence = [this](int, long long ms, const std::string&, int) { log.push_back("silence " + std::to_string(ms)); return 0; },
                .stop = [this](int) { log.push_back("stop"); },
                .isSpeaking = [this](int) { return speaking; },
                .setRateAndPitch = [this](int, float rate, float pitch) { log.push_back("rate " + std::to_string(int(rate * 10)) + " " + std::to_string(int(pitch * 10))); },
                .language = [this](int, const std::string& tag, bool set) {
                    log.push_back((set ? "set " : "is ") + tag);
                    return tag == "de-DE" ? 1 : -2;
                },
            });
        }
        ~FakeTts() { SetTtsHost({}); }
    };
}

TEST(TextToSpeechSendsWhatWaitedForTheEngine) {
    FakeTts fake;
    std::vector<std::string> done, errors;
    {
        TextToSpeech tts("com.example.tts");
        CHECK(tts.Status() == TtsStatus::Initializing && fake.created == 1);
        tts.SetListener({ .onDone = [&](const std::string& id) { done.push_back(id); },
                          .onError = [&](const std::string& id, TtsError) { errors.push_back(id); } });
        TtsStatus seen = TtsStatus::Initializing;
        tts.SetOnStatusChanged([&](TtsStatus s) { seen = s; });
        // Asked for while binding: kept, a Flush dropping what came before it.
        CHECK(tts.Speak("one") && tts.Speak("two", { .queue = QueueMode::Flush, .id = "b" }) && tts.Silence(300));
        tts.SetSpeechRate(1.5f);
        tts.SetLanguage("de-DE");
        CHECK(tts.IsSpeaking() && fake.log.size() == 1);
        OnTtsStatus(fake.lastId, true);
        CHECK(seen == TtsStatus::Ready && tts.Status() == TtsStatus::Ready);
        CHECK((fake.log == std::vector<std::string>{ "create com.example.tts", "rate 15 10", "set de-DE", "flush two b", "silence 300" }));
        // Ready: straight through, with made-up ids.
        CHECK(tts.Speak("three"));
        CHECK(fake.log.back() == "add three kiln-3");
        CHECK(tts.IsLanguageAvailable("fr-FR") == LanguageAvailability::NotSupported);
        CHECK(tts.SetLanguage("de-DE") == LanguageAvailability::Country);
        OnTtsUtterance(fake.lastId, int(TtsEvent::Done), "b", 0, 0);
        OnTtsUtterance(fake.lastId, int(TtsEvent::Error), "kiln-3", int(TtsError::Synthesis), 0);
        CHECK(done == std::vector<std::string>{ "b" } && errors == std::vector<std::string>{ "kiln-3" });
        tts.Stop();
        CHECK(fake.log.back() == "stop");
    }
    CHECK(fake.log.back() == "shutdown");
    // Callbacks for a destroyed instance go nowhere.
    OnTtsUtterance(fake.lastId, int(TtsEvent::Done), "late", 0, 0);
    CHECK(done.size() == 1);
}

TEST(TextToSpeechWithoutAnEngine) {
    FakeTts fake;
    std::vector<std::string> errors;
    TextToSpeech tts;
    tts.SetListener({ .onError = [&](const std::string& id, TtsError e) { if (e == TtsError::Service) errors.push_back(id); } });
    CHECK(tts.Speak("lost", { .id = "x" }));
    OnTtsStatus(fake.lastId, false);
    CHECK(tts.Status() == TtsStatus::Failed && errors == std::vector<std::string>{ "x" } && !tts.Speak("no"));
    SetTtsHost({});
    TextToSpeech none;
    CHECK(none.Status() == TtsStatus::Failed && !none.Speak("no") && none.Voices().empty());
}

TEST(SpeechRecognizerSessions) {
    std::vector<std::string> log;
    bool service = true;
    SetRecognizerHost({
        .create = [&](int, bool onDevice) { log.push_back(onDevice ? "create on-device" : "create"); return service; },
        .destroy = [&](int) { log.push_back("destroy"); },
        .start = [&](int, const RecognitionOptions& o) { log.push_back("start " + o.language + " " + std::to_string(o.maxResults)); },
        .stop = [&](int) { log.push_back("stop"); },
        .cancel = [&](int) { log.push_back("cancel"); },
        .available = [&](bool onDevice) { return service && !onDevice; },
    });
    CHECK(SpeechRecognizer::IsAvailable() && !SpeechRecognizer::IsOnDeviceAvailable());
    std::vector<std::string> heard;
    std::vector<RecognitionError> errors;
    {
        SpeechRecognizer r;
        int id = 0;
        r.SetListener({ .onPartialResults = [&](const RecognitionResult& p) { heard.push_back("~" + p.Best()); },
                        .onResults = [&](const RecognitionResult& p) { heard.push_back(p.Best()); },
                        .onError = [&](RecognitionError e) { errors.push_back(e); } });
        CHECK(log.empty());   // created on first use
        r.Start({ .language = "en-US", .maxResults = 0 });
        CHECK((log == std::vector<std::string>{ "create", "start en-US 1" }) && r.State() == RecognizerState::Starting);
        // The test's recognizer has the first free id; find it by the event reaching it.
        for (id = 1; id < 1000 && r.State() != RecognizerState::Listening; ++id) OnRecognitionEvent(id, RecognitionEvent::Ready, 0, {});
        --id;
        CHECK(r.State() == RecognizerState::Listening);
        OnRecognitionEvent(id, RecognitionEvent::Beginning, 0, {});
        OnRecognitionEvent(id, RecognitionEvent::Partial, 0, { .candidates = { "hello" } });
        OnRecognitionEvent(id, RecognitionEvent::Partial, 0, {});   // empty partials are not reported
        CHECK(r.State() == RecognizerState::Hearing);
        r.Stop();
        CHECK(r.State() == RecognizerState::Processing && log.back() == "stop");
        OnRecognitionEvent(id, RecognitionEvent::Results, 0, { .candidates = { "hello world", "yellow world" }, .confidences = { 0.9f, 0.1f } });
        CHECK(r.State() == RecognizerState::Idle && (heard == std::vector<std::string>{ "~hello", "hello world" }));
        // Starting again while listening cancels the session; a cancelled session's late answer is dropped.
        r.Start();
        r.Start();
        CHECK(log.back() == "start  5" && log[log.size() - 2] == "cancel");
        r.Cancel();
        OnRecognitionEvent(id, RecognitionEvent::Results, 0, { .candidates = { "late" } });
        CHECK(heard.size() == 2 && r.State() == RecognizerState::Idle);
        r.Start();
        OnRecognitionEvent(id, RecognitionEvent::Error, float(int(RecognitionError::NoMatch)), {});
        CHECK((errors == std::vector<RecognitionError>{ RecognitionError::NoMatch }) && r.State() == RecognizerState::Idle);
    }
    CHECK(log.back() == "destroy");
    // No service: reported as an error, nothing to destroy.
    service = false;
    log.clear();
    {
        SpeechRecognizer r(true);
        r.SetListener({ .onError = [&](RecognitionError e) { errors.push_back(e); } });
        r.Start();
        CHECK(errors.back() == RecognitionError::NotAvailable && r.State() == RecognizerState::Idle);
    }
    CHECK((log == std::vector<std::string>{ "create on-device" }));
    SetRecognizerHost({});
    CHECK(!SpeechRecognizer::IsAvailable());
    CHECK(std::string(ToString(RecognitionError::InsufficientPermissions)) == "no microphone permission");
}
