#pragma once

// Speech to text (android.speech.SpeechRecognizer with a RecognizerIntent): listens to the
// microphone and reports what was said, partial results while the user speaks and the final
// candidates when they stop.
//
//   static speech::SpeechRecognizer recognizer;
//   recognizer.SetListener({ .onResults = [](const speech::RecognitionResult& r) { query = r.Best(); } });
//   recognizer.Start({ .language = "en-US" });
//
// Needs android.permission.RECORD_AUDIO (request it with contracts::RequestPermission). Apps that
// target Android 11 and later need the service visible to them: kiln's manifests declare
// <queries><intent><action android:name="android.speech.RecognitionService"/></intent></queries>.
// A recognizer is used from the main thread, like the platform's.

#include <kiln/Function.h>

#include <memory>
#include <string>
#include <vector>

namespace kiln::speech {

    // SpeechRecognizer.ERROR_*.
    enum class RecognitionError {
        NetworkTimeout = 1, Network = 2, Audio = 3, Server = 4, Client = 5,
        SpeechTimeout = 6,   // no speech heard
        NoMatch = 7,         // nothing recognised
        RecognizerBusy = 8,
        InsufficientPermissions = 9,   // RECORD_AUDIO not granted
        TooManyRequests = 10, ServerDisconnected = 11, LanguageNotSupported = 12, LanguageUnavailable = 13,
        CannotCheckSupport = 14, CannotListenToDownloadEvents = 15,
        NotAvailable = 100,  // kiln's: no recognition service on the device
    };
    const char* ToString(RecognitionError error);

    struct RecognitionOptions {
        std::string language;          // BCP 47 (EXTRA_LANGUAGE); empty: the device's
        bool partialResults = true;    // EXTRA_PARTIAL_RESULTS
        int maxResults = 5;            // EXTRA_MAX_RESULTS
        bool preferOffline = false;    // EXTRA_PREFER_OFFLINE
        bool webSearch = false;        // LANGUAGE_MODEL_WEB_SEARCH, else FREE_FORM
        // Silence that ends the speech (EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS); 0: the service's.
        long long completeSilenceMillis = 0;
    };

    struct RecognitionResult {
        std::vector<std::string> candidates;   // most likely first (RESULTS_RECOGNITION)
        std::vector<float> confidences;        // 0 .. 1 for each candidate, when the service gives them (CONFIDENCE_SCORES)
        std::string Best() const { return candidates.empty() ? std::string() : candidates.front(); }
        bool operator==(const RecognitionResult&) const = default;
    };

    // RecognitionListener, on the main thread.
    struct RecognitionListener {
        kiln::Function<void()> onReadyForSpeech;
        kiln::Function<void()> onBeginningOfSpeech;
        kiln::Function<void(float rmsDb)> onRmsChanged;   // the input's loudness, for a level meter
        kiln::Function<void()> onEndOfSpeech;
        kiln::Function<void(const RecognitionResult&)> onPartialResults;
        kiln::Function<void(const RecognitionResult&)> onResults;
        kiln::Function<void(RecognitionError)> onError;
    };

    enum class RecognizerState {
        Idle,
        Starting,    // asked to listen, waiting for the service
        Listening,   // ready for speech
        Hearing,     // the user is speaking
        Processing,  // the speech ended; waiting for the results
    };

    class SpeechRecognizer {
    public:
        // onDevice: the device's own recognizer (createOnDeviceSpeechRecognizer, Android 12+), which
        // works without a network.
        explicit SpeechRecognizer(bool onDevice = false);
        ~SpeechRecognizer();   // destroy
        SpeechRecognizer(const SpeechRecognizer&) = delete;
        SpeechRecognizer& operator=(const SpeechRecognizer&) = delete;

        void SetListener(RecognitionListener listener);
        // Starts listening. Results, or an error, end it. Starting again while listening cancels
        // the session first.
        void Start(RecognitionOptions options = {});
        // Stops listening: the speech so far is recognised and reported.
        void Stop();
        // Ends the session without results.
        void Cancel();
        RecognizerState State() const;

        // A recognition service is installed (isRecognitionAvailable).
        static bool IsAvailable();
        static bool IsOnDeviceAvailable();

        // From the platform host (RecognitionListener's calls).
        void OnEvent(int event, float value, const RecognitionResult& result);

        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
    };

    enum class RecognitionEvent { Ready, Beginning, Rms, End, Partial, Results, Error };

    // The platform's recognizers, by id; their callbacks go to OnRecognitionEvent. Without a host
    // (the desktop, tests) there is no service: Start reports NotAvailable.
    struct RecognizerHost {
        kiln::Function<bool(int id, bool onDevice)> create;   // false: no service
        kiln::Function<void(int id)> destroy;
        kiln::Function<void(int id, const RecognitionOptions& options)> start;
        kiln::Function<void(int id)> stop;
        kiln::Function<void(int id)> cancel;
        kiln::Function<bool(bool onDevice)> available;
    };
    void SetRecognizerHost(RecognizerHost host);
    // value: the RMS for Rms, the error for Error.
    void OnRecognitionEvent(int id, RecognitionEvent event, float value, const RecognitionResult& result);

}
