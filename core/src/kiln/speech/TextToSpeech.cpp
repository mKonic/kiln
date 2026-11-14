#include "kiln/speech/TextToSpeech.h"

#include "kiln/Log.h"

#include <deque>
#include <map>

namespace kiln::speech {

    namespace {
        struct Registry {
            TtsHost host;
            int next = 1;
            std::map<int, TextToSpeech*> live;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    struct TextToSpeech::Impl {
        int id = 0;
        TtsStatus status = TtsStatus::Initializing;
        kiln::Function<void(TtsStatus)> onStatus;
        UtteranceListener listener;
        float rate = 1.0f, pitch = 1.0f;
        bool rateOrPitchSet = false;
        std::optional<media::AudioAttributes> attributes;
        std::string language;   // asked for before the engine was ready
        std::string voice;
        int generated = 0;
        // Asked for before the engine was ready: sent in order once it is.
        struct Request {
            bool silence = false;
            std::string text;
            long long durationMillis = 0;
            UtteranceOptions options;
        };
        std::deque<Request> waiting;

        bool Send(const Request& r) {
            const TtsHost& h = R().host;
            int result = -1;
            if (r.silence) {
                if (h.silence) result = h.silence(id, r.durationMillis, r.options.id, int(r.options.queue));
            } else if (h.speak) {
                result = h.speak(id, r.text, r.options.id, int(r.options.queue), r.options.volume, r.options.pan);
            }
            return result == 0;
        }
    };

    void SetTtsHost(TtsHost host) { R().host = std::move(host); }

    void OnTtsStatus(int id, bool success) {
        auto it = R().live.find(id);
        if (it != R().live.end()) it->second->OnStatus(success ? TtsStatus::Ready : TtsStatus::Failed);
    }
    void OnTtsUtterance(int id, int event, const std::string& utteranceId, int a, int b) {
        auto it = R().live.find(id);
        if (it == R().live.end()) return;
        TextToSpeech* t = it->second;
        switch (TtsEvent(event)) {
            case TtsEvent::Start: t->OnStart(utteranceId); break;
            case TtsEvent::Done: t->OnDone(utteranceId); break;
            case TtsEvent::Error: t->OnError(utteranceId, TtsError(a)); break;
            case TtsEvent::Stop: t->OnStop(utteranceId, a != 0); break;
            case TtsEvent::RangeStart: t->OnRangeStart(utteranceId, a, b); break;
        }
    }

    TextToSpeech::TextToSpeech(std::string engine) : m_Impl(std::make_unique<Impl>()) {
        Registry& r = R();
        m_Impl->id = r.next++;
        r.live[m_Impl->id] = this;
        if (r.host.create) {
            r.host.create(m_Impl->id, engine);
        } else {
            m_Impl->status = TtsStatus::Failed;
        }
    }

    TextToSpeech::~TextToSpeech() {
        R().live.erase(m_Impl->id);
        if (R().host.shutdown && m_Impl->status != TtsStatus::Failed) R().host.shutdown(m_Impl->id);
    }

    TtsStatus TextToSpeech::Status() const { return m_Impl->status; }
    void TextToSpeech::SetOnStatusChanged(kiln::Function<void(TtsStatus)> onStatus) { m_Impl->onStatus = std::move(onStatus); }
    void TextToSpeech::SetListener(UtteranceListener listener) { m_Impl->listener = std::move(listener); }

    bool TextToSpeech::Speak(std::string text, UtteranceOptions options) {
        Impl& m = *m_Impl;
        if (m.status == TtsStatus::Failed) return false;
        if (options.id.empty()) options.id = "kiln-" + std::to_string(++m.generated);
        Impl::Request request{ .text = std::move(text), .options = std::move(options) };
        if (m.status == TtsStatus::Initializing) {
            if (request.options.queue == QueueMode::Flush) m.waiting.clear();
            m.waiting.push_back(std::move(request));
            return true;
        }
        return m.Send(request);
    }

    bool TextToSpeech::Silence(long long durationMillis, UtteranceOptions options) {
        Impl& m = *m_Impl;
        if (m.status == TtsStatus::Failed) return false;
        if (options.id.empty()) options.id = "kiln-" + std::to_string(++m.generated);
        Impl::Request request{ .silence = true, .durationMillis = durationMillis, .options = std::move(options) };
        if (m.status == TtsStatus::Initializing) {
            if (request.options.queue == QueueMode::Flush) m.waiting.clear();
            m.waiting.push_back(std::move(request));
            return true;
        }
        return m.Send(request);
    }

    void TextToSpeech::Stop() {
        m_Impl->waiting.clear();
        if (m_Impl->status == TtsStatus::Ready && R().host.stop) R().host.stop(m_Impl->id);
    }

    bool TextToSpeech::IsSpeaking() const {
        if (m_Impl->status != TtsStatus::Ready) return !m_Impl->waiting.empty();
        return R().host.isSpeaking && R().host.isSpeaking(m_Impl->id);
    }

    void TextToSpeech::SetSpeechRate(float rate) {
        m_Impl->rate = rate > 0 ? rate : 1.0f;
        m_Impl->rateOrPitchSet = true;
        if (m_Impl->status == TtsStatus::Ready && R().host.setRateAndPitch) R().host.setRateAndPitch(m_Impl->id, m_Impl->rate, m_Impl->pitch);
    }

    void TextToSpeech::SetPitch(float pitch) {
        m_Impl->pitch = pitch > 0 ? pitch : 1.0f;
        m_Impl->rateOrPitchSet = true;
        if (m_Impl->status == TtsStatus::Ready && R().host.setRateAndPitch) R().host.setRateAndPitch(m_Impl->id, m_Impl->rate, m_Impl->pitch);
    }

    void TextToSpeech::SetAudioAttributes(media::AudioAttributes attributes) {
        m_Impl->attributes = attributes;
        if (m_Impl->status == TtsStatus::Ready && R().host.setAudioAttributes) R().host.setAudioAttributes(m_Impl->id, attributes);
    }

    LanguageAvailability TextToSpeech::SetLanguage(const std::string& tag) {
        if (m_Impl->status != TtsStatus::Ready) {
            m_Impl->language = tag;
            return LanguageAvailability::NotSupported;
        }
        if (!R().host.language) return LanguageAvailability::NotSupported;
        return LanguageAvailability(R().host.language(m_Impl->id, tag, true));
    }

    LanguageAvailability TextToSpeech::IsLanguageAvailable(const std::string& tag) const {
        if (m_Impl->status != TtsStatus::Ready || !R().host.language) return LanguageAvailability::NotSupported;
        return LanguageAvailability(R().host.language(m_Impl->id, tag, false));
    }

    std::vector<std::string> TextToSpeech::AvailableLanguages() const {
        if (m_Impl->status != TtsStatus::Ready || !R().host.languages) return {};
        return R().host.languages(m_Impl->id);
    }

    std::vector<Voice> TextToSpeech::Voices() const {
        if (m_Impl->status != TtsStatus::Ready || !R().host.voices) return {};
        return R().host.voices(m_Impl->id);
    }

    bool TextToSpeech::SetVoice(const std::string& name) {
        if (m_Impl->status == TtsStatus::Failed) return false;
        if (m_Impl->status == TtsStatus::Initializing) {
            m_Impl->voice = name;
            return true;
        }
        return R().host.setVoice && R().host.setVoice(m_Impl->id, name);
    }

    std::string TextToSpeech::CurrentEngine() const {
        if (m_Impl->status != TtsStatus::Ready || !R().host.currentEngine) return {};
        return R().host.currentEngine(m_Impl->id);
    }

    std::string TextToSpeech::DefaultLanguage() const {
        if (m_Impl->status != TtsStatus::Ready || !R().host.defaultLanguage) return {};
        return R().host.defaultLanguage(m_Impl->id);
    }

    void TextToSpeech::OnStatus(TtsStatus status) {
        Impl& m = *m_Impl;
        if (m.status != TtsStatus::Initializing) return;
        m.status = status;
        const TtsHost& h = R().host;
        if (status == TtsStatus::Ready) {
            // What was set while binding, then what was asked to be said.
            if (m.rateOrPitchSet && h.setRateAndPitch) h.setRateAndPitch(m.id, m.rate, m.pitch);
            if (m.attributes && h.setAudioAttributes) h.setAudioAttributes(m.id, *m.attributes);
            if (!m.language.empty() && h.language) {
                auto a = LanguageAvailability(h.language(m.id, m.language, true));
                if (a < LanguageAvailability::Language) log::Warn("kiln", "text to speech: language {} is not available ({})", m.language, int(a));
            }
            if (!m.voice.empty() && h.setVoice && !h.setVoice(m.id, m.voice)) log::Warn("kiln", "text to speech: no voice {}", m.voice);
            std::deque<Impl::Request> waiting = std::move(m.waiting);
            m.waiting.clear();
            for (const auto& r : waiting)
                if (!m.Send(r) && m.listener.onError) m.listener.onError(r.options.id, TtsError::Generic);
        } else {
            log::Warn("kiln", "text to speech: no engine");
            std::deque<Impl::Request> waiting = std::move(m.waiting);
            m.waiting.clear();
            for (const auto& r : waiting)
                if (m.listener.onError) m.listener.onError(r.options.id, TtsError::Service);
        }
        if (m.onStatus) m.onStatus(status);
    }

    void TextToSpeech::OnStart(const std::string& id) {
        if (m_Impl->listener.onStart) m_Impl->listener.onStart(id);
    }
    void TextToSpeech::OnDone(const std::string& id) {
        if (m_Impl->listener.onDone) m_Impl->listener.onDone(id);
    }
    void TextToSpeech::OnError(const std::string& id, TtsError error) {
        if (m_Impl->listener.onError) m_Impl->listener.onError(id, error);
    }
    void TextToSpeech::OnStop(const std::string& id, bool interrupted) {
        if (m_Impl->listener.onStop) m_Impl->listener.onStop(id, interrupted);
    }
    void TextToSpeech::OnRangeStart(const std::string& id, int start, int end) {
        if (m_Impl->listener.onRangeStart) m_Impl->listener.onRangeStart(id, start, end);
    }

}
