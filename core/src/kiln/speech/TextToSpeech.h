#pragma once

// Speaking text aloud (android.speech.tts.TextToSpeech): the device's speech engine reads text in
// a language and voice, queued or interrupting, with progress per utterance.
//
//   static speech::TextToSpeech tts;   // binds to the engine; speech asked for meanwhile waits
//   tts.Speak("Turn left in 200 metres", { .queue = speech::QueueMode::Flush });
//
// Apps that target Android 11 and later need the engine visible to them: kiln's manifests declare
// <queries><intent><action android:name="android.intent.action.TTS_SERVICE"/></intent></queries>.

#include <kiln/Function.h>
#include <kiln/media/AudioFocus.h>

#include <memory>
#include <string>
#include <vector>

namespace kiln::speech {

    enum class QueueMode { Flush = 0, Add = 1 };   // QUEUE_FLUSH drops what is queued and stops the current speech

    enum class TtsStatus {
        Initializing,   // binding to the engine
        Ready,
        Failed,         // no engine (or it would not start); Speak returns false
    };

    // TextToSpeech's errors (ERROR, ERROR_SYNTHESIS ...).
    enum class TtsError {
        Generic = -1, Synthesis = -3, Service = -4, Output = -5, Network = -6, NetworkTimeout = -7,
        InvalidRequest = -8, NotInstalledYet = -9,
    };

    // setLanguage / isLanguageAvailable.
    enum class LanguageAvailability {
        CountryVariant = 2, Country = 1, Language = 0,
        MissingData = -1,    // the engine supports it but its data is not installed
        NotSupported = -2,
    };

    struct Voice {
        std::string name;
        std::string locale;   // BCP 47
        int quality = 300;    // Voice.QUALITY_*: 100 very low ... 500 very high
        int latency = 300;    // Voice.LATENCY_*: 100 very low ... 500 very high
        bool requiresNetwork = false;
        bool notInstalled = false;   // its data must be downloaded first
        bool operator==(const Voice&) const = default;
    };

    struct UtteranceOptions {
        QueueMode queue = QueueMode::Add;
        std::string id;          // reported to the listener; empty: one is made up
        float volume = 1.0f;     // 0 .. 1
        float pan = 0.0f;        // -1 (left) .. 1 (right)
    };

    // UtteranceProgressListener, on the main thread.
    struct UtteranceListener {
        kiln::Function<void(const std::string& id)> onStart;
        kiln::Function<void(const std::string& id)> onDone;
        kiln::Function<void(const std::string& id, TtsError error)> onError;
        // Stopped by Stop() or by a Flush; interrupted when it had started speaking.
        kiln::Function<void(const std::string& id, bool interrupted)> onStop;
        // The engine is about to speak text[start, end) (UTF-16 offsets, as the platform has them).
        kiln::Function<void(const std::string& id, int start, int end)> onRangeStart;
    };

    class TextToSpeech {
    public:
        // Binds to the engine: the user's default, or the package named (com.google.android.tts).
        explicit TextToSpeech(std::string engine = {});
        ~TextToSpeech();   // shutdown
        TextToSpeech(const TextToSpeech&) = delete;
        TextToSpeech& operator=(const TextToSpeech&) = delete;

        TtsStatus Status() const;
        void SetOnStatusChanged(kiln::Function<void(TtsStatus)> onStatus);
        void SetListener(UtteranceListener listener);

        // Queues the text. Before the engine is ready the request waits and is sent once it is
        // (dropped, with onError, if it fails). False when the engine failed or refused it.
        bool Speak(std::string text, UtteranceOptions options = {});
        // Silence in the queue (playSilentUtterance).
        bool Silence(long long durationMillis, UtteranceOptions options = {});
        // Stops speaking and empties the queue.
        void Stop();
        bool IsSpeaking() const;

        // 1 is normal; 2 twice as fast, 0.5 half. Kept for later utterances, also across the engine
        // binding.
        void SetSpeechRate(float rate);
        void SetPitch(float pitch);
        void SetAudioAttributes(media::AudioAttributes attributes);   // default: the media stream
        // A BCP 47 tag (en-GB). What the engine can do with it; NotSupported until it is ready.
        LanguageAvailability SetLanguage(const std::string& languageTag);
        LanguageAvailability IsLanguageAvailable(const std::string& languageTag) const;
        std::vector<std::string> AvailableLanguages() const;
        std::vector<Voice> Voices() const;
        bool SetVoice(const std::string& name);
        std::string CurrentEngine() const;
        // The engine's default language, from the user's settings.
        std::string DefaultLanguage() const;

        // From the platform host.
        void OnStatus(TtsStatus status);
        void OnStart(const std::string& id);
        void OnDone(const std::string& id);
        void OnError(const std::string& id, TtsError error);
        void OnStop(const std::string& id, bool interrupted);
        void OnRangeStart(const std::string& id, int start, int end);

        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
    };

    // An engine binding of the platform's, by id; its callbacks go to OnTts* below. Without a host
    // (the desktop, tests) there is no engine: Status() is Failed.
    struct TtsHost {
        kiln::Function<void(int id, const std::string& engine)> create;   // answers with OnTtsStatus
        kiln::Function<void(int id)> shutdown;
        // Returns TextToSpeech's SUCCESS (0) or ERROR (-1).
        kiln::Function<int(int id, const std::string& text, const std::string& utteranceId, int queue, float volume, float pan)> speak;
        kiln::Function<int(int id, long long durationMillis, const std::string& utteranceId, int queue)> silence;
        kiln::Function<void(int id)> stop;
        kiln::Function<bool(int id)> isSpeaking;
        kiln::Function<void(int id, float rate, float pitch)> setRateAndPitch;
        kiln::Function<void(int id, media::AudioAttributes attributes)> setAudioAttributes;
        kiln::Function<int(int id, const std::string& tag, bool set)> language;   // setLanguage / isLanguageAvailable
        kiln::Function<std::vector<std::string>(int id)> languages;
        kiln::Function<std::vector<Voice>(int id)> voices;
        kiln::Function<bool(int id, const std::string& name)> setVoice;
        kiln::Function<std::string(int id)> currentEngine;
        kiln::Function<std::string(int id)> defaultLanguage;
    };
    void SetTtsHost(TtsHost host);

    void OnTtsStatus(int id, bool success);
    void OnTtsUtterance(int id, int event, const std::string& utteranceId, int a, int b);   // event: TtsEvent
    enum class TtsEvent { Start, Done, Error, Stop, RangeStart };

}
