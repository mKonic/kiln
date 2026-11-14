#include "kiln/speech/SpeechRecognizer.h"

#include "kiln/Log.h"

#include <map>

namespace kiln::speech {

    namespace {
        struct Registry {
            RecognizerHost host;
            int next = 1;
            std::map<int, SpeechRecognizer*> live;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    const char* ToString(RecognitionError e) {
        switch (e) {
            case RecognitionError::NetworkTimeout: return "network timeout";
            case RecognitionError::Network: return "network error";
            case RecognitionError::Audio: return "audio recording error";
            case RecognitionError::Server: return "server error";
            case RecognitionError::Client: return "client error";
            case RecognitionError::SpeechTimeout: return "no speech heard";
            case RecognitionError::NoMatch: return "nothing recognised";
            case RecognitionError::RecognizerBusy: return "recognizer busy";
            case RecognitionError::InsufficientPermissions: return "no microphone permission";
            case RecognitionError::TooManyRequests: return "too many requests";
            case RecognitionError::ServerDisconnected: return "server disconnected";
            case RecognitionError::LanguageNotSupported: return "language not supported";
            case RecognitionError::LanguageUnavailable: return "language unavailable";
            case RecognitionError::CannotCheckSupport: return "cannot check support";
            case RecognitionError::CannotListenToDownloadEvents: return "cannot listen to download events";
            case RecognitionError::NotAvailable: return "no speech recognition on this device";
        }
        return "unknown error";
    }

    struct SpeechRecognizer::Impl {
        int id = 0;
        bool onDevice = false;
        bool created = false;   // the platform's recognizer exists
        RecognizerState state = RecognizerState::Idle;
        RecognitionListener listener;
    };

    void SetRecognizerHost(RecognizerHost host) { R().host = std::move(host); }

    void OnRecognitionEvent(int id, RecognitionEvent event, float value, const RecognitionResult& result) {
        auto it = R().live.find(id);
        if (it != R().live.end()) it->second->OnEvent(int(event), value, result);
    }

    SpeechRecognizer::SpeechRecognizer(bool onDevice) : m_Impl(std::make_unique<Impl>()) {
        m_Impl->id = R().next++;
        m_Impl->onDevice = onDevice;
        R().live[m_Impl->id] = this;
    }

    SpeechRecognizer::~SpeechRecognizer() {
        R().live.erase(m_Impl->id);
        if (m_Impl->created && R().host.destroy) R().host.destroy(m_Impl->id);
    }

    void SpeechRecognizer::SetListener(RecognitionListener listener) { m_Impl->listener = std::move(listener); }
    RecognizerState SpeechRecognizer::State() const { return m_Impl->state; }

    void SpeechRecognizer::Start(RecognitionOptions options) {
        Impl& m = *m_Impl;
        const RecognizerHost& h = R().host;
        // Created on first use, so a recognizer kept in a static costs nothing until it listens.
        if (!m.created) m.created = h.create && h.create(m.id, m.onDevice);
        if (!m.created || !h.start) {
            m.state = RecognizerState::Idle;
            if (m.listener.onError) m.listener.onError(RecognitionError::NotAvailable);
            return;
        }
        if (m.state != RecognizerState::Idle && h.cancel) h.cancel(m.id);
        if (options.maxResults < 1) options.maxResults = 1;
        m.state = RecognizerState::Starting;
        h.start(m.id, options);
    }

    void SpeechRecognizer::Stop() {
        Impl& m = *m_Impl;
        if (m.state == RecognizerState::Idle || !m.created) return;
        // The service still reports what it heard; until then the session is processing.
        m.state = RecognizerState::Processing;
        if (R().host.stop) R().host.stop(m.id);
    }

    void SpeechRecognizer::Cancel() {
        Impl& m = *m_Impl;
        if (m.state == RecognizerState::Idle || !m.created) return;
        m.state = RecognizerState::Idle;
        if (R().host.cancel) R().host.cancel(m.id);
    }

    bool SpeechRecognizer::IsAvailable() { return R().host.available && R().host.available(false); }
    bool SpeechRecognizer::IsOnDeviceAvailable() { return R().host.available && R().host.available(true); }

    void SpeechRecognizer::OnEvent(int event, float value, const RecognitionResult& result) {
        Impl& m = *m_Impl;
        // A cancelled session's late callbacks (the service may still answer once).
        if (m.state == RecognizerState::Idle) return;
        RecognitionListener& l = m.listener;
        switch (RecognitionEvent(event)) {
            case RecognitionEvent::Ready:
                m.state = RecognizerState::Listening;
                if (l.onReadyForSpeech) l.onReadyForSpeech();
                break;
            case RecognitionEvent::Beginning:
                m.state = RecognizerState::Hearing;
                if (l.onBeginningOfSpeech) l.onBeginningOfSpeech();
                break;
            case RecognitionEvent::Rms:
                if (l.onRmsChanged) l.onRmsChanged(value);
                break;
            case RecognitionEvent::End:
                m.state = RecognizerState::Processing;
                if (l.onEndOfSpeech) l.onEndOfSpeech();
                break;
            case RecognitionEvent::Partial:
                if (l.onPartialResults && !result.candidates.empty()) l.onPartialResults(result);
                break;
            case RecognitionEvent::Results:
                m.state = RecognizerState::Idle;
                if (l.onResults) l.onResults(result);
                break;
            case RecognitionEvent::Error:
                m.state = RecognizerState::Idle;
                log::Info("kiln", "speech recognition: {}", ToString(RecognitionError(int(value))));
                if (l.onError) l.onError(RecognitionError(int(value)));
                break;
        }
    }

}
