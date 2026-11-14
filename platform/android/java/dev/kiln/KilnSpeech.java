package dev.kiln;

import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;
import android.util.Log;
import android.util.SparseArray;

import java.util.ArrayList;
import java.util.Locale;
import java.util.Set;

/**
 * kiln::speech's platform side: TextToSpeech engines and SpeechRecognizers by id. Callbacks reach
 * native code on the main thread (UtteranceProgressListener's come on a binder thread).
 */
final class KilnSpeech {
    // speech::TtsEvent
    private static final int START = 0, DONE = 1, ERROR = 2, STOP = 3, RANGE = 4;
    // speech::RecognitionEvent
    private static final int R_READY = 0, R_BEGIN = 1, R_RMS = 2, R_END = 3, R_PARTIAL = 4, R_RESULTS = 5, R_ERROR = 6;

    private static Context sContext;
    private static final Handler sMain = new Handler(Looper.getMainLooper());
    private static final SparseArray<TextToSpeech> sTts = new SparseArray<>();
    private static final SparseArray<SpeechRecognizer> sRecognizers = new SparseArray<>();

    static native void ttsStatus(int id, boolean success);
    static native void ttsUtterance(int id, int event, String utteranceId, int a, int b);
    static native void recognition(int id, int event, float value, String[] candidates, float[] confidences);

    private KilnSpeech() {}

    static void attach(Context context) {
        if (sContext == null) sContext = context.getApplicationContext();
    }

    // --- text to speech ------------------------------------------------------------------------

    static void ttsCreate(int id, String engine) {
        if (sContext == null) {
            ttsStatus(id, false);
            return;
        }
        // The listener may run before the constructor returns: look the engine up by id there.
        TextToSpeech.OnInitListener onInit = status -> sMain.post(() -> {
            TextToSpeech t = sTts.get(id);
            if (t != null && status == TextToSpeech.SUCCESS) {
                t.setOnUtteranceProgressListener(new Progress(id));
                ttsStatus(id, true);
            } else {
                if (t != null) t.shutdown();
                sTts.remove(id);
                ttsStatus(id, false);
            }
        });
        TextToSpeech t = engine == null || engine.isEmpty() ? new TextToSpeech(sContext, onInit)
                                                            : new TextToSpeech(sContext, onInit, engine);
        sTts.put(id, t);
    }

    private static final class Progress extends UtteranceProgressListener {
        private final int id;
        Progress(int id) { this.id = id; }
        private void post(int event, String u, int a, int b) { sMain.post(() -> { if (sTts.get(id) != null) ttsUtterance(id, event, u, a, b); }); }
        @Override public void onStart(String u) { post(START, u, 0, 0); }
        @Override public void onDone(String u) { post(DONE, u, 0, 0); }
        @Override @Deprecated public void onError(String u) { post(ERROR, u, TextToSpeech.ERROR, 0); }
        @Override public void onError(String u, int code) { post(ERROR, u, code, 0); }
        @Override public void onStop(String u, boolean interrupted) { post(STOP, u, interrupted ? 1 : 0, 0); }
        @Override public void onRangeStart(String u, int start, int end, int frame) { post(RANGE, u, start, end); }
    }

    static void ttsShutdown(int id) {
        TextToSpeech t = sTts.get(id);
        sTts.remove(id);
        if (t != null) t.shutdown();
    }

    static int ttsSpeak(int id, String text, String utteranceId, int queue, float volume, float pan) {
        TextToSpeech t = sTts.get(id);
        if (t == null) return TextToSpeech.ERROR;
        Bundle params = new Bundle();
        if (volume != 1f) params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, volume);
        if (pan != 0f) params.putFloat(TextToSpeech.Engine.KEY_PARAM_PAN, pan);
        return t.speak(text, queue, params, utteranceId);
    }

    static int ttsSilence(int id, long durationMs, String utteranceId, int queue) {
        TextToSpeech t = sTts.get(id);
        return t == null ? TextToSpeech.ERROR : t.playSilentUtterance(durationMs, queue, utteranceId);
    }

    static void ttsStop(int id) {
        TextToSpeech t = sTts.get(id);
        if (t != null) t.stop();
    }

    static boolean ttsIsSpeaking(int id) {
        TextToSpeech t = sTts.get(id);
        return t != null && t.isSpeaking();
    }

    static void ttsRateAndPitch(int id, float rate, float pitch) {
        TextToSpeech t = sTts.get(id);
        if (t == null) return;
        t.setSpeechRate(rate);
        t.setPitch(pitch);
    }

    static void ttsAudioAttributes(int id, int usage, int contentType) {
        TextToSpeech t = sTts.get(id);
        if (t != null) t.setAudioAttributes(new AudioAttributes.Builder().setUsage(usage).setContentType(contentType).build());
    }

    static int ttsLanguage(int id, String tag, boolean set) {
        TextToSpeech t = sTts.get(id);
        if (t == null) return TextToSpeech.LANG_NOT_SUPPORTED;
        Locale locale = Locale.forLanguageTag(tag);
        return set ? t.setLanguage(locale) : t.isLanguageAvailable(locale);
    }

    static String[] ttsLanguages(int id) {
        TextToSpeech t = sTts.get(id);
        Set<Locale> set = t == null ? null : t.getAvailableLanguages();
        if (set == null) return new String[0];
        ArrayList<String> out = new ArrayList<>();
        for (Locale l : set) out.add(l.toLanguageTag());
        return out.toArray(new String[0]);
    }

    // Six strings a voice: name, locale, quality, latency, network, not installed.
    static String[] ttsVoices(int id) {
        TextToSpeech t = sTts.get(id);
        Set<Voice> set = t == null ? null : t.getVoices();
        if (set == null) return new String[0];
        ArrayList<String> out = new ArrayList<>();
        for (Voice v : set) {
            out.add(v.getName());
            out.add(v.getLocale().toLanguageTag());
            out.add(Integer.toString(v.getQuality()));
            out.add(Integer.toString(v.getLatency()));
            out.add(v.isNetworkConnectionRequired() ? "1" : "0");
            out.add(v.getFeatures().contains(TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED) ? "1" : "0");
        }
        return out.toArray(new String[0]);
    }

    static boolean ttsSetVoice(int id, String name) {
        TextToSpeech t = sTts.get(id);
        Set<Voice> set = t == null ? null : t.getVoices();
        if (set == null) return false;
        for (Voice v : set)
            if (v.getName().equals(name)) return t.setVoice(v) == TextToSpeech.SUCCESS;
        return false;
    }

    static String ttsEngine(int id) {
        TextToSpeech t = sTts.get(id);
        String e = t == null ? null : t.getDefaultEngine();
        return e == null ? "" : e;
    }

    static String ttsDefaultLanguage(int id) {
        TextToSpeech t = sTts.get(id);
        Voice v = t == null ? null : t.getDefaultVoice();
        return v == null ? "" : v.getLocale().toLanguageTag();
    }

    // --- speech recognition --------------------------------------------------------------------

    static boolean recognitionAvailable(boolean onDevice) {
        if (sContext == null) return false;
        return onDevice ? SpeechRecognizer.isOnDeviceRecognitionAvailable(sContext) : SpeechRecognizer.isRecognitionAvailable(sContext);
    }

    static boolean recognizerCreate(int id, boolean onDevice) {
        if (!recognitionAvailable(onDevice)) return false;
        try {
            SpeechRecognizer r = onDevice ? SpeechRecognizer.createOnDeviceSpeechRecognizer(sContext)
                                          : SpeechRecognizer.createSpeechRecognizer(sContext);
            r.setRecognitionListener(new Listener(id));
            sRecognizers.put(id, r);
            return true;
        } catch (RuntimeException e) {
            Log.w("kiln", "speech recognition: " + e);
            return false;
        }
    }

    private static final class Listener implements RecognitionListener {
        private final int id;
        Listener(int id) { this.id = id; }
        private void send(int event, float value, Bundle b) {
            String[] candidates = null;
            float[] confidences = null;
            if (b != null) {
                ArrayList<String> list = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                if (list != null) candidates = list.toArray(new String[0]);
                confidences = b.getFloatArray(SpeechRecognizer.CONFIDENCE_SCORES);
            }
            if (sRecognizers.get(id) != null) recognition(id, event, value, candidates, confidences);
        }
        @Override public void onReadyForSpeech(Bundle params) { send(R_READY, 0, null); }
        @Override public void onBeginningOfSpeech() { send(R_BEGIN, 0, null); }
        @Override public void onRmsChanged(float rmsdB) { send(R_RMS, rmsdB, null); }
        @Override public void onBufferReceived(byte[] buffer) {}
        @Override public void onEndOfSpeech() { send(R_END, 0, null); }
        @Override public void onError(int error) { send(R_ERROR, error, null); }
        @Override public void onResults(Bundle results) { send(R_RESULTS, 0, results); }
        @Override public void onPartialResults(Bundle partialResults) { send(R_PARTIAL, 0, partialResults); }
        @Override public void onEvent(int eventType, Bundle params) {}
    }

    static void recognizerStart(int id, String language, boolean partial, int maxResults, boolean preferOffline,
                                boolean webSearch, long silenceMs) {
        SpeechRecognizer r = sRecognizers.get(id);
        if (r == null) return;
        Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                webSearch ? RecognizerIntent.LANGUAGE_MODEL_WEB_SEARCH : RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        if (language != null && !language.isEmpty()) intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, language);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, partial);
        intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, maxResults);
        intent.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, preferOffline);
        intent.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, sContext.getPackageName());
        if (silenceMs > 0) intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, silenceMs);
        r.startListening(intent);
    }

    static void recognizerStop(int id) {
        SpeechRecognizer r = sRecognizers.get(id);
        if (r != null) r.stopListening();
    }

    static void recognizerCancel(int id) {
        SpeechRecognizer r = sRecognizers.get(id);
        if (r != null) r.cancel();
    }

    static void recognizerDestroy(int id) {
        SpeechRecognizer r = sRecognizers.get(id);
        sRecognizers.remove(id);
        if (r != null) r.destroy();
    }
}
