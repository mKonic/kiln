// JNI entry points for dev.kiln.KilnNative and the Choreographer-driven frame loop.
#include "AndroidText.h"

namespace kiln::android { void InstallImageDecoder(); void InstallAssetReader(JNIEnv* env, jobject assets); }

#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/text/AnnotatedString.h"
#include "kiln/ui/Back.h"
#include "kiln/ui/Accessibility.h"
#include <array>
#include <cstring>

#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

#include "kiln/App.h"
#include "kiln/ui/Trackpad.h"

#include <hearth/Log.h>
#include <hearth/surface/AndroidSurface.h>

#include <android/api-level.h>
#include <android/choreographer.h>
#include <android/looper.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <chrono>
#include <algorithm>
#include <numeric>
#include <sys/system_properties.h>
#include <memory>
#include <string>

namespace {

    struct Host {
        std::unique_ptr<kiln::android::AndroidText> text;
        std::unique_ptr<kiln::App> app;
        kiln_app handle{};
        hearth::AndroidSurface surface;
        ANativeWindow* window = nullptr;
        bool visible = false;
        bool callbackPosted = false;
        int eventFd = -1, timerFd = -1;
        JavaVM* vm = nullptr;
        jobject view = nullptr;           // global ref to the KilnView
        jmethodID requestFocus = nullptr;
    };
    Host* g_Host = nullptr;
    std::shared_ptr<void> g_LocalDragState;   // the local state of a drag this app started

    void ArmTimer();
    void InstallTextInputHost(JNIEnv* env);
    // A String[] (null entries kept) as UTF-8.
    std::vector<std::optional<std::string>> StringsFromJava(JNIEnv* env, jobjectArray a) {
        std::vector<std::optional<std::string>> out;
        const jsize n = env->GetArrayLength(a);
        for (jsize i = 0; i < n; ++i) {
            auto js = static_cast<jstring>(env->GetObjectArrayElement(a, i));
            if (!js) { out.emplace_back(); continue; }
            const char* c = env->GetStringUTFChars(js, nullptr);
            out.emplace_back(std::string(c));
            env->ReleaseStringUTFChars(js, c);
            env->DeleteLocalRef(js);
        }
        return out;
    }
    JNIEnv* MainEnv();
    void SurfaceDestroyed(JNIEnv*, jclass);

    jstring JavaString(JNIEnv* env, const std::u16string& s) {
        return env->NewString(reinterpret_cast<const jchar*>(s.data()), jsize(s.size()));
    }
    jobjectArray JavaStrings(JNIEnv* env, const std::vector<std::string>& v) {
        jclass sc = env->FindClass("java/lang/String");
        jobjectArray a = env->NewObjectArray(jsize(v.size()), sc, nullptr);
        env->DeleteLocalRef(sc);
        for (std::size_t i = 0; i < v.size(); ++i) {
            jstring js = JavaString(env, kiln::foundation::ToUtf16(v[i]));
            env->SetObjectArrayElement(a, jsize(i), js);
            env->DeleteLocalRef(js);
        }
        return a;
    }

    // The text toolbar's entries as the parallel arrays KilnView takes.
    struct ToolbarArrays {
        JNIEnv* env;
        jintArray kinds, ids, groups, classifications, indexes;
        jobjectArray labels;
        ToolbarArrays(JNIEnv* e, const std::vector<kiln::ui::TextToolbarEntry>& entries) : env(e) {
            const jsize n = jsize(entries.size());
            std::vector<jint> k(entries.size()), i(entries.size()), g(entries.size()), c(entries.size()), x(entries.size());
            std::vector<std::string> l(entries.size());
            for (std::size_t j = 0; j < entries.size(); ++j) {
                const auto& en = entries[j];
                k[j] = en.kind == kiln::ui::TextToolbarEntry::Kind::Classification ? 1 : 0;
                i[j] = jint(en.id);
                g[j] = en.group;
                c[j] = en.classification;
                x[j] = en.index;
                l[j] = en.label;
            }
            auto make = [&](const std::vector<jint>& v) {
                jintArray a = e->NewIntArray(n);
                e->SetIntArrayRegion(a, 0, n, v.data());
                return a;
            };
            kinds = make(k); ids = make(i); groups = make(g); classifications = make(c); indexes = make(x);
            labels = JavaStrings(e, l);
        }
        ~ToolbarArrays() {
            for (jobject o : { (jobject)kinds, (jobject)ids, (jobject)groups, (jobject)classifications, (jobject)indexes, (jobject)labels }) env->DeleteLocalRef(o);
        }
    };

    // Span styles in the flat layout KilnClipSpans takes (its INTS / FLOATS per span, field ids
    // as AndroidClipboardManager's: colour 1 ... shadow 12).
    struct ClipSpans {
        static constexpr int kInts = 12, kFloats = 8;
        std::vector<jint> ints;
        std::vector<jfloat> floats;
        std::vector<std::string> strings;
        static jint Synthesis(kiln::text::FontSynthesis s) {
            using S = kiln::text::FontSynthesis;   // FONT_SYNTHESIS_NONE 0, ALL 1, WEIGHT 2, STYLE 3
            return s == S::All ? 1 : s == S::Weight ? 2 : s == S::Style ? 3 : 0;
        }
        static kiln::text::FontSynthesis SynthesisOf(jint v) {
            using S = kiln::text::FontSynthesis;
            return v == 1 ? S::All : v == 2 ? S::Weight : v == 3 ? S::Style : S::None;
        }
        static jint UnitType(const kiln::text::TextUnit& u) { return u.IsSp() ? 1 : u.IsEm() ? 2 : 0; }
        static ClipSpans From(const kiln::text::AnnotatedString& text) {
            ClipSpans out;
            for (const auto& r : text.SpanStyles()) {
                const auto& st = r.item;
                std::array<jint, kInts> in{};
                std::array<jfloat, kFloats> fl{};
                int mask = 0;
                auto set = [&](int id) { mask |= 1 << id; };
                in[0] = r.start;
                in[1] = r.end;
                if (st.color.Specified()) { set(1); in[9] = jint(st.color.argb); }
                if (st.fontSize.IsSpecified()) { set(2); in[3] = UnitType(st.fontSize); fl[0] = st.fontSize.value; }
                if (st.fontWeight) { set(3); in[5] = st.fontWeight->weight; }
                if (st.fontStyle) { set(4); in[6] = *st.fontStyle == kiln::text::FontStyle::Italic ? 1 : 0; }
                if (st.fontSynthesis) { set(5); in[7] = Synthesis(*st.fontSynthesis); }
                if (st.fontFeatureSettings) set(6);
                if (st.letterSpacing.IsSpecified()) { set(7); in[4] = UnitType(st.letterSpacing); fl[1] = st.letterSpacing.value; }
                if (st.baselineShift) { set(8); fl[2] = st.baselineShift->multiplier; }
                if (st.textGeometricTransform) { set(9); fl[3] = st.textGeometricTransform->scaleX; fl[4] = st.textGeometricTransform->skewX; }
                if (st.background.Specified()) { set(10); in[10] = jint(st.background.argb); }
                if (st.textDecoration) { set(11); in[8] = jint(*st.textDecoration); }
                if (st.shadow) {
                    set(12);
                    in[11] = jint(st.shadow->color.argb);
                    fl[5] = st.shadow->offset.x;
                    fl[6] = st.shadow->offset.y;
                    fl[7] = st.shadow->blurRadius;
                }
                in[2] = mask;
                out.ints.insert(out.ints.end(), in.begin(), in.end());
                out.floats.insert(out.floats.end(), fl.begin(), fl.end());
                out.strings.push_back(st.fontFeatureSettings.value_or(""));
            }
            return out;
        }
        kiln::text::AnnotatedString To(std::string text) const {
            kiln::text::AnnotatedStringBuilder b;
            b.Append(text);
            const std::size_t n = std::min({ ints.size() / kInts, floats.size() / kFloats, strings.size() });
            for (std::size_t i = 0; i < n; ++i) {
                const jint* in = &ints[i * kInts];
                const jfloat* fl = &floats[i * kFloats];
                const int mask = in[2];
                auto has = [&](int id) { return (mask & (1 << id)) != 0; };
                auto unit = [](jint type, float v) {
                    kiln::text::TextUnit u;
                    if (type == 1) { u.type = kiln::text::TextUnit::Type::Sp; u.value = v; }
                    else if (type == 2) { u.type = kiln::text::TextUnit::Type::Em; u.value = v; }
                    return u;
                };
                kiln::text::SpanStyle st;
                if (has(1)) st.color = kiln::ui::Color(std::uint32_t(in[9]));
                if (has(2)) st.fontSize = unit(in[3], fl[0]);
                if (has(3)) st.fontWeight = kiln::text::FontWeight{ in[5] };
                if (has(4)) st.fontStyle = in[6] == 1 ? kiln::text::FontStyle::Italic : kiln::text::FontStyle::Normal;
                if (has(5)) st.fontSynthesis = SynthesisOf(in[7]);
                if (has(6)) st.fontFeatureSettings = strings[i];
                if (has(7)) st.letterSpacing = unit(in[4], fl[1]);
                if (has(8)) st.baselineShift = kiln::text::BaselineShift{ fl[2] };
                if (has(9)) st.textGeometricTransform = kiln::text::TextGeometricTransform{ fl[3], fl[4] };
                if (has(10)) st.background = kiln::ui::Color(std::uint32_t(in[10]));
                if (has(11)) st.textDecoration = kiln::text::TextDecoration(in[8] & 3);
                if (has(12)) st.shadow = kiln::text::Shadow{ kiln::ui::Color(std::uint32_t(in[11])), { fl[5], fl[6] }, fl[7] };
                b.AddStyle(std::move(st), in[0], in[1]);
            }
            return b.ToAnnotatedString();
        }
    };

    // Classifier requests waiting for KilnNative.classifierResult.
    struct ClassifierReply {
        bool hasRange = false;
        int start = 0, end = 0;
        std::optional<kiln::ui::TextClassificationInfo> info;
    };
    struct PendingClassifier {
        static std::map<int, std::function<void(const ClassifierReply&)>>& Map() {
            static std::map<int, std::function<void(const ClassifierReply&)>> m;
            return m;
        }
        static int Add(std::function<void(const ClassifierReply&)> fn) {
            static int last = 0;
            Map()[++last] = std::move(fn);
            return last;
        }
    };

    // NestedScrollInteropConnection's View side: KilnView's NestedScrollingChildHelper port.
    struct ViewNestedScrollParent final : kiln::ui::HostNestedScrollParent {
        jmethodID start = nullptr, pre = nullptr, scroll = nullptr, preFling = nullptr, fling = nullptr, stop = nullptr;
        bool Ready() const { return g_Host && g_Host->view && start; }
        static void Copy(JNIEnv* env, jintArray a, int consumed[2]) {
            if (!a) return;
            jint v[2] = { 0, 0 };
            if (env->GetArrayLength(a) >= 2) env->GetIntArrayRegion(a, 0, 2, v);
            consumed[0] = v[0]; consumed[1] = v[1];
            env->DeleteLocalRef(a);
        }
        bool Start(int axes, bool touch) override {
            return Ready() && MainEnv()->CallBooleanMethod(g_Host->view, start, jint(axes), jboolean(touch));
        }
        void PreScroll(int dx, int dy, bool touch, int consumed[2]) override {
            if (!Ready()) return;
            JNIEnv* env = MainEnv();
            Copy(env, static_cast<jintArray>(env->CallObjectMethod(g_Host->view, pre, jint(dx), jint(dy), jboolean(touch))), consumed);
        }
        void Scroll(int dxc, int dyc, int dxu, int dyu, bool touch, int consumed[2]) override {
            if (!Ready()) return;
            JNIEnv* env = MainEnv();
            Copy(env, static_cast<jintArray>(env->CallObjectMethod(g_Host->view, scroll, jint(dxc), jint(dyc), jint(dxu), jint(dyu), jboolean(touch))), consumed);
        }
        bool PreFling(float vx, float vy) override { return Ready() && MainEnv()->CallBooleanMethod(g_Host->view, preFling, vx, vy); }
        bool Fling(float vx, float vy, bool consumed) override {
            return Ready() && MainEnv()->CallBooleanMethod(g_Host->view, fling, vx, vy, jboolean(consumed));
        }
        void StopAll() override { if (Ready()) MainEnv()->CallVoidMethod(g_Host->view, stop); }
    };
    ViewNestedScrollParent g_NestedScrollParent;

    // `setprop debug.kiln.frametimes 1`: logs the CPU time of each frame's work (recompose,
    // layout, draw, submit), summarised every 120 frames, for measuring before optimising.
    struct FrameStats {
        bool enabled = false;
        std::vector<double> ms;
        void Add(double v) {
            ms.push_back(v);
            if (ms.size() < 120) return;
            std::sort(ms.begin(), ms.end());
            const double sum = std::accumulate(ms.begin(), ms.end(), 0.0);
            __android_log_print(ANDROID_LOG_INFO, "kiln", "frames: n=%zu mean=%.2fms p50=%.2fms p90=%.2fms p99=%.2fms max=%.2fms over16=%td",
                                ms.size(), sum / double(ms.size()), ms[ms.size() / 2], ms[ms.size() * 9 / 10], ms[ms.size() * 99 / 100], ms.back(),
                                std::count_if(ms.begin(), ms.end(), [](double v) { return v > 16.6; }));
            ms.clear();
        }
    };
    FrameStats g_FrameStats;

    void OnVsync(int64_t frameTimeNanos, void*) {
        if (!g_Host) return;
        g_Host->callbackPosted = false;
        if (g_Host->visible && g_Host->window) {
            const auto t0 = std::chrono::steady_clock::now();
            g_Host->app->Frame(double(frameTimeNanos) * 1e-9);
            if (g_FrameStats.enabled)
                g_FrameStats.Add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        ArmTimer();
    }

    void PostFrame(void*) {
        if (!g_Host || g_Host->callbackPosted || !g_Host->visible) return;
        g_Host->callbackPosted = true;
        AChoreographer_postFrameCallback64(AChoreographer_getInstance(), &OnVsync, nullptr);
    }

    // Arms the timerfd for the dispatcher's next timer (or disarms it).
    void ArmTimer() {
        itimerspec spec{};
        if (auto next = g_Host->app->Dispatcher().NextTimer()) {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                *next - kiln::rt::Dispatcher::Clock::now()).count();
            if (ns < 1) ns = 1;
            spec.it_value.tv_sec = ns / 1000000000;
            spec.it_value.tv_nsec = ns % 1000000000;
        }
        timerfd_settime(g_Host->timerFd, 0, &spec, nullptr);
    }

    int OnLooperWake(int fd, int, void*) {
        std::uint64_t value = 0;
        (void)!read(fd, &value, sizeof(value));
        g_Host->app->RunIdle();
        ArmTimer();
        return 1;   // keep the callback
    }

    void Wake() {
        const std::uint64_t one = 1;
        (void)!write(g_Host->eventFd, &one, sizeof(one));
    }

    void LogToLogcat(hearth::LogLevel level, std::string_view message, void*) {
        int prio = ANDROID_LOG_INFO;
        switch (level) {
            case hearth::LogLevel::Trace: prio = ANDROID_LOG_VERBOSE; break;
            case hearth::LogLevel::Warn:  prio = ANDROID_LOG_WARN; break;
            case hearth::LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
            default: break;
        }
        __android_log_print(prio, "kiln", "%.*s", int(message.size()), message.data());
    }

    void Create(JNIEnv* env, jclass, jobject view, jobject text, jobject assets, jfloat density, jfloat fontScale, jbyteArray saved) {
        hearth::SetLogSink(&LogToLogcat);
        kiln::android::InstallImageDecoder();
        kiln::android::InstallAssetReader(env, assets);
        g_Host = new Host();
        g_Host->text = std::make_unique<kiln::android::AndroidText>(env, text);
        kiln::foundation::SetUpperCaseFunction([](std::u16string_view t, const std::string& tag) {
            return g_Host && g_Host->text ? g_Host->text->UpperCase(t, tag) : std::u16string(t);
        });
        {
            jclass vc = env->GetObjectClass(view);
            g_NestedScrollParent.start = env->GetMethodID(vc, "nsStart", "(IZ)Z");
            g_NestedScrollParent.pre = env->GetMethodID(vc, "nsPreScroll", "(IIZ)[I");
            g_NestedScrollParent.scroll = env->GetMethodID(vc, "nsScroll", "(IIIIZ)[I");
            g_NestedScrollParent.preFling = env->GetMethodID(vc, "nsPreFling", "(FF)Z");
            g_NestedScrollParent.fling = env->GetMethodID(vc, "nsFling", "(FFZ)Z");
            g_NestedScrollParent.stop = env->GetMethodID(vc, "nsStop", "()V");
            env->DeleteLocalRef(vc);
        }
        kiln::HostServices services{ g_Host->text.get(), &PostFrame, nullptr };
        services.nestedScrollParent = &g_NestedScrollParent;
        g_Host->app = std::make_unique<kiln::App>(services);
        g_Host->app->SetDensity(density, fontScale);
        g_Host->handle.impl = g_Host->app.get();
        env->GetJavaVM(&g_Host->vm);
        g_Host->view = env->NewGlobalRef(view);
        jclass viewClass = env->GetObjectClass(view);
        g_Host->requestFocus = env->GetMethodID(viewClass, "requestOwnerFocus", "()Z");
        env->DeleteLocalRef(viewClass);
        InstallTextInputHost(env);
        {
            jclass vc = env->GetObjectClass(view);
            static jmethodID onA11y = env->GetMethodID(vc, "onAccessibilityChange", "(Z)V");
            static jmethodID touchExploration = env->GetMethodID(vc, "isTouchExplorationOn", "()Z");
            static jmethodID fromTool = env->GetMethodID(vc, "isRequestFromAccessibilityTool", "()Z");
            static jmethodID requestRect = env->GetMethodID(vc, "requestRectangle", "(IIII)V");
            static jmethodID invalidate = env->GetMethodID(vc, "invalidate", "()V");
            static jmethodID focusFromTouch = env->GetMethodID(vc, "requestFocusFromTouch", "()Z");
            env->DeleteLocalRef(vc);
            auto& owner = g_Host->app->GetOwner();
            owner.SetOnAccessibilityChange([](bool layout) { MainEnv()->CallVoidMethod(g_Host->view, onA11y, jboolean(layout)); });
            kiln::ui::AccessibilityHost host;
            host.touchExplorationEnabled = [] { return bool(MainEnv()->CallBooleanMethod(g_Host->view, touchExploration)); };
            host.requestFromAccessibilityTool = [] { return bool(MainEnv()->CallBooleanMethod(g_Host->view, fromTool)); };
            host.requestRectangleOnScreen = [](const kiln::ui::IntRect& r) {
                MainEnv()->CallVoidMethod(g_Host->view, requestRect, jint(r.left), jint(r.top), jint(r.right), jint(r.bottom));
                return true;
            };
            host.invalidate = [] { MainEnv()->CallVoidMethod(g_Host->view, invalidate); };
            host.requestFocusFromTouch = [] { MainEnv()->CallBooleanMethod(g_Host->view, focusFromTouch); };
            host.uptimeMillis = [] {
                return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            };
            owner.Accessibility().SetHost(std::move(host));
            // Autofill (PlatformAutofillManagerImpl): the view's AutofillManager.
            {
                jclass avc = env->GetObjectClass(view);
                static jmethodID afEntered = env->GetMethodID(avc, "autofillNotifyEntered", "(IIIII)V");
                static jmethodID afExited = env->GetMethodID(avc, "autofillNotifyExited", "(I)V");
                static jmethodID afValue = env->GetMethodID(avc, "autofillNotifyValue", "(IILjava/lang/String;IJ)V");
                static jmethodID afVisible = env->GetMethodID(avc, "autofillNotifyVisibility", "(IZ)V");
                static jmethodID afCommit = env->GetMethodID(avc, "autofillCommit", "()V");
                static jmethodID afCancel = env->GetMethodID(avc, "autofillCancel", "()V");
                static jmethodID afRequest = env->GetMethodID(avc, "autofillRequest", "(IIIII)V");
                env->DeleteLocalRef(avc);
                kiln::ui::AutofillHost af;
                af.notifyViewEntered = [](int id, kiln::ui::IntRect r) {
                    MainEnv()->CallVoidMethod(g_Host->view, afEntered, jint(id), jint(r.left), jint(r.top), jint(r.right), jint(r.bottom));
                };
                af.notifyViewExited = [](int id) { MainEnv()->CallVoidMethod(g_Host->view, afExited, jint(id)); };
                af.notifyValueChanged = [](int id, const kiln::ui::FillableData& d) {
                    JNIEnv* e = MainEnv();
                    jstring js = d.kind == kiln::ui::FillableData::Kind::Text ? JavaString(e, kiln::foundation::ToUtf16(d.text)) : nullptr;
                    const jint i = d.kind == kiln::ui::FillableData::Kind::Toggle ? jint(d.toggle) : jint(d.listIndex);
                    e->CallVoidMethod(g_Host->view, afValue, jint(id), jint(d.kind), js, i, jlong(d.dateMillis));
                    if (js) e->DeleteLocalRef(js);
                };
                af.notifyViewVisibilityChanged = [](int id, bool v) { MainEnv()->CallVoidMethod(g_Host->view, afVisible, jint(id), jboolean(v)); };
                af.commit = [] { MainEnv()->CallVoidMethod(g_Host->view, afCommit); };
                af.cancel = [] { MainEnv()->CallVoidMethod(g_Host->view, afCancel); };
                af.requestAutofill = [](int id, kiln::ui::IntRect r) {
                    MainEnv()->CallVoidMethod(g_Host->view, afRequest, jint(id), jint(r.left), jint(r.top), jint(r.right), jint(r.bottom));
                };
                af.credentialRequestsSupported = android_get_device_api_level() >= 34;
                jclass svc = env->GetObjectClass(view);
                static jmethodID keepOn = env->GetMethodID(svc, "setKeepScreenOnFromNative", "(Z)V");
                static jmethodID sensitive = env->GetMethodID(svc, "setContentSensitiveFromNative", "(Z)V");
                env->DeleteLocalRef(svc);
                static jmethodID dragStart = env->GetMethodID(env->GetObjectClass(view), "startDragFromNative",
                                                              "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;III[B)Z");
                static jmethodID dragPermissions = env->GetMethodID(env->GetObjectClass(view), "requestDragPermissionsFromNative", "()Z");
                (void)dragPermissions;
                kiln::ui::DragAndDropHost dnd;
                dnd.startDrag = [](const kiln::ui::DragAndDropTransferData& d, int w, int h, std::vector<std::uint8_t> rgba) {
                    JNIEnv* e = MainEnv();
                    std::vector<std::string> items;
                    for (const auto& it : d.clip.items) {
                        items.push_back(it.text.value_or(""));
                        items.push_back(it.htmlText.value_or(""));
                        items.push_back(it.uri.value_or(""));
                    }
                    jobjectArray jitems = JavaStrings(e, items);
                    // Absent parts are null.
                    for (std::size_t i = 0; i < d.clip.items.size(); ++i) {
                        const auto& it = d.clip.items[i];
                        if (!it.text) e->SetObjectArrayElement(jitems, jsize(i * 3), nullptr);
                        if (!it.htmlText) e->SetObjectArrayElement(jitems, jsize(i * 3 + 1), nullptr);
                        if (!it.uri) e->SetObjectArrayElement(jitems, jsize(i * 3 + 2), nullptr);
                    }
                    std::vector<std::string> mimes = d.metadata.mimeTypes;
                    if (mimes.empty()) mimes.push_back(d.clip.items.empty() || d.clip.items[0].text ? "text/plain" : "*/*");
                    jobjectArray jmimes = JavaStrings(e, mimes);
                    jstring label = JavaString(e, kiln::foundation::ToUtf16(d.metadata.label));
                    jbyteArray px = nullptr;
                    if (!rgba.empty()) {
                        px = e->NewByteArray(jsize(rgba.size()));
                        e->SetByteArrayRegion(px, 0, jsize(rgba.size()), reinterpret_cast<const jbyte*>(rgba.data()));
                    }
                    g_LocalDragState = d.localState;
                    const bool ok = e->CallBooleanMethod(g_Host->view, dragStart, label, jmimes, jitems, jint(d.flags), jint(w), jint(h), px);
                    for (jobject o : { (jobject)jitems, (jobject)jmimes, (jobject)label, (jobject)px }) if (o) e->DeleteLocalRef(o);
                    return ok;
                };
                owner.DragAndDrop().SetHost(std::move(dnd));
                owner.SetScreenHost([](bool on) { MainEnv()->CallVoidMethod(g_Host->view, keepOn, jboolean(on)); },
                                    [](bool s) { MainEnv()->CallVoidMethod(g_Host->view, sensitive, jboolean(s)); });
                owner.SetAutofillHost(std::move(af));
            }
            static jmethodID setBack = env->GetMethodID(env->GetObjectClass(view), "setBackEnabled", "(Z)V");
            auto& back = kiln::ui::BackDispatcher::Main();
            back.onHasEnabledChanged = [](bool on) { MainEnv()->CallVoidMethod(g_Host->view, setBack, jboolean(on)); };
            if (back.HasEnabledCallbacks()) back.onHasEnabledChanged(true);
        }
        {
            jclass vc = env->GetObjectClass(view);
            static jmethodID requestRect = env->GetMethodID(vc, "requestRectangle", "(IIII)V");
            env->DeleteLocalRef(vc);
            g_Host->app->GetOwner().SetRequestRectangleOnScreen([](kiln::ui::Rect r) {
                MainEnv()->CallVoidMethod(g_Host->view, requestRect, jint(std::floor(r.left)), jint(std::floor(r.top)),
                                          jint(std::ceil(r.right)), jint(std::ceil(r.bottom)));
            });
        }
        {
            jclass vc = env->GetObjectClass(view);
            static jmethodID playSound = env->GetMethodID(vc, "playSound", "(I)V");
            env->DeleteLocalRef(vc);
            g_Host->app->GetOwner().SetPlaySoundEffect([](int constant) { MainEnv()->CallVoidMethod(g_Host->view, playSound, jint(constant)); });
        }
        {
            jclass vc = env->GetObjectClass(view);
            static jmethodID setIcon = env->GetMethodID(vc, "setPointerIconType", "(I)V");
            static jmethodID openUri = env->GetMethodID(vc, "openUri", "(Ljava/lang/String;)Z");
            env->DeleteLocalRef(vc);
            g_Host->app->GetOwner().SetPointerIconHost([](int type) { MainEnv()->CallVoidMethod(g_Host->view, setIcon, jint(type)); });
            g_Host->app->GetOwner().SetUriHandler([](const std::string& uri) {
                JNIEnv* e = MainEnv();
                jstring s = e->NewStringUTF(uri.c_str());   // URIs are ASCII (percent-encoded)
                const bool ok = e->CallBooleanMethod(g_Host->view, openUri, s);
                e->DeleteLocalRef(s);
                return ok;
            });
        }
        {
            // Clipboard, haptics and the floating text toolbar.
            jclass vc = env->GetObjectClass(view);
            static jmethodID setClip = env->GetMethodID(vc, "setClipboardText", "(Ljava/lang/String;)V");
            static jmethodID getClip = env->GetMethodID(vc, "getClipboardText", "()Ljava/lang/String;");
            static jmethodID hasClip = env->GetMethodID(vc, "clipboardHasText", "()Z");
            static jmethodID clipAll = env->GetMethodID(vc, "getClipboardClip", "()[Ljava/lang/String;");
            static jmethodID clipMeta = env->GetMethodID(vc, "getClipboardMetadata", "()[Ljava/lang/String;");
            static jmethodID setStyled = env->GetMethodID(vc, "setClipboardStyledText", "(Ljava/lang/String;[I[F[Ljava/lang/String;)V");
            static jmethodID getStyled = env->GetMethodID(vc, "getClipboardStyledText", "()[Ljava/lang/Object;");
            static jmethodID readContent = env->GetMethodID(vc, "readContent", "(Ljava/lang/String;)[B");
            static jmethodID releasePermission = env->GetMethodID(vc, "releaseContentPermission", "(I)V");
            static jmethodID haptic = env->GetMethodID(vc, "performHaptic", "(I)V");
            static jmethodID showBar = env->GetMethodID(vc, "showTextToolbar", "(IIII[I[I[Ljava/lang/String;[I[I[I)Z");
            static jmethodID updateBar = env->GetMethodID(vc, "updateTextToolbar", "([I[I[Ljava/lang/String;[I[I[I)V");
            static jmethodID moveBar = env->GetMethodID(vc, "moveTextToolbar", "(IIII)V");
            static jmethodID hideBar = env->GetMethodID(vc, "hideTextToolbar", "()V");
            static jmethodID tcCreate = env->GetMethodID(vc, "createClassifierSession", "(I)I");
            static jmethodID tcDestroy = env->GetMethodID(vc, "destroyClassifierSession", "(I)V");
            static jmethodID tcSuggest = env->GetMethodID(vc, "suggestSelection", "(IILjava/lang/String;II[Ljava/lang/String;)V");
            static jmethodID tcClassify = env->GetMethodID(vc, "classifyText", "(IILjava/lang/String;II[Ljava/lang/String;)V");
            static jmethodID tcRelease = env->GetMethodID(vc, "releaseClassification", "(I)V");
            static jmethodID ptList = env->GetMethodID(vc, "processTextActivities", "()[Ljava/lang/String;");
            static jmethodID ptStart = env->GetMethodID(vc, "processText", "(IZLjava/lang/String;)V");
            static jmethodID luCan = env->GetMethodID(vc, "canLookUp", "(Ljava/lang/String;)Z");
            static jmethodID luStart = env->GetMethodID(vc, "lookUp", "(Ljava/lang/String;)V");
            env->DeleteLocalRef(vc);
            auto& owner = g_Host->app->GetOwner();
            kiln::ui::Owner::ClipboardHost clip;
            clip.setText = [](const std::string& text) {
                JNIEnv* e = MainEnv();
                const std::u16string u = kiln::foundation::ToUtf16(text);
                jstring s = e->NewString(reinterpret_cast<const jchar*>(u.data()), jsize(u.size()));
                e->CallVoidMethod(g_Host->view, setClip, s);
                e->DeleteLocalRef(s);
            };
            clip.setAnnotatedText = [](const kiln::text::AnnotatedString& text) {
                JNIEnv* e = MainEnv();
                const ClipSpans spans = ClipSpans::From(text);
                jstring js = JavaString(e, kiln::foundation::ToUtf16(text.Text()));
                jintArray ji = e->NewIntArray(jsize(spans.ints.size()));
                e->SetIntArrayRegion(ji, 0, jsize(spans.ints.size()), spans.ints.data());
                jfloatArray jf = e->NewFloatArray(jsize(spans.floats.size()));
                e->SetFloatArrayRegion(jf, 0, jsize(spans.floats.size()), spans.floats.data());
                jobjectArray jstrs = JavaStrings(e, spans.strings);
                e->CallVoidMethod(g_Host->view, setStyled, js, ji, jf, jstrs);
                for (jobject o : { (jobject)js, (jobject)ji, (jobject)jf, (jobject)jstrs }) e->DeleteLocalRef(o);
            };
            clip.getAnnotatedText = []() -> std::optional<kiln::text::AnnotatedString> {
                JNIEnv* e = MainEnv();
                auto r = static_cast<jobjectArray>(e->CallObjectMethod(g_Host->view, getStyled));
                if (!r) return std::nullopt;
                auto js = static_cast<jstring>(e->GetObjectArrayElement(r, 0));
                auto ji = static_cast<jintArray>(e->GetObjectArrayElement(r, 1));
                auto jf = static_cast<jfloatArray>(e->GetObjectArrayElement(r, 2));
                auto jstrs = static_cast<jobjectArray>(e->GetObjectArrayElement(r, 3));
                std::u16string u(std::size_t(e->GetStringLength(js)), u'\0');
                e->GetStringRegion(js, 0, jsize(u.size()), reinterpret_cast<jchar*>(u.data()));
                ClipSpans spans;
                spans.ints.resize(std::size_t(e->GetArrayLength(ji)));
                e->GetIntArrayRegion(ji, 0, jsize(spans.ints.size()), spans.ints.data());
                spans.floats.resize(std::size_t(e->GetArrayLength(jf)));
                e->GetFloatArrayRegion(jf, 0, jsize(spans.floats.size()), spans.floats.data());
                for (auto& str : StringsFromJava(e, jstrs)) spans.strings.push_back(str.value_or(""));
                for (jobject o : { (jobject)r, (jobject)js, (jobject)ji, (jobject)jf, (jobject)jstrs }) e->DeleteLocalRef(o);
                return spans.To(kiln::foundation::ToUtf8(u));
            };
            clip.getText = []() -> std::optional<std::string> {
                JNIEnv* e = MainEnv();
                auto s = static_cast<jstring>(e->CallObjectMethod(g_Host->view, getClip));
                if (!s) return std::nullopt;
                std::u16string u(std::size_t(e->GetStringLength(s)), u'\0');
                e->GetStringRegion(s, 0, jsize(u.size()), reinterpret_cast<jchar*>(u.data()));
                std::string out = kiln::foundation::ToUtf8(u);
                e->DeleteLocalRef(s);
                return out;
            };
            clip.hasText = [] { return bool(MainEnv()->CallBooleanMethod(g_Host->view, hasClip)); };
            clip.getClip = []() -> std::optional<std::pair<kiln::ui::ClipEntry, kiln::ui::ClipMetadata>> {
                JNIEnv* e = MainEnv();
                auto a = static_cast<jobjectArray>(e->CallObjectMethod(g_Host->view, clipAll));
                if (!a) return std::nullopt;
                const std::vector<std::optional<std::string>> v = StringsFromJava(e, a);
                e->DeleteLocalRef(a);
                kiln::ui::ClipMetadata meta;
                meta.label = v[0].value_or("");
                const int types = std::stoi(v[1].value_or("0"));
                for (int i = 0; i < types; ++i) meta.mimeTypes.push_back(v[std::size_t(2 + i)].value_or(""));
                kiln::ui::ClipEntry entry;
                for (std::size_t k = std::size_t(2 + types); k + 2 < v.size(); k += 3) entry.items.push_back({ v[k], v[k + 1], v[k + 2] });
                return std::pair{ std::move(entry), std::move(meta) };
            };
            clip.getMetadata = []() -> std::optional<kiln::ui::ClipMetadata> {
                JNIEnv* e = MainEnv();
                auto a = static_cast<jobjectArray>(e->CallObjectMethod(g_Host->view, clipMeta));
                if (!a) return std::nullopt;
                const std::vector<std::optional<std::string>> v = StringsFromJava(e, a);
                e->DeleteLocalRef(a);
                kiln::ui::ClipMetadata meta;
                meta.label = v[0].value_or("");
                for (std::size_t i = 1; i < v.size(); ++i) meta.mimeTypes.push_back(v[i].value_or(""));
                return meta;
            };
            kiln::ui::Owner::ContentHost content;
            content.read = [](const std::string& uri) -> std::optional<std::vector<std::uint8_t>> {
                JNIEnv* e = MainEnv();
                jstring s = e->NewStringUTF(uri.c_str());
                auto bytes = static_cast<jbyteArray>(e->CallObjectMethod(g_Host->view, readContent, s));
                e->DeleteLocalRef(s);
                if (!bytes) return std::nullopt;
                std::vector<std::uint8_t> out(static_cast<std::size_t>(e->GetArrayLength(bytes)));
                e->GetByteArrayRegion(bytes, 0, jsize(out.size()), reinterpret_cast<jbyte*>(out.data()));
                e->DeleteLocalRef(bytes);
                return out;
            };
            content.releasePermission = [](int handle) { MainEnv()->CallVoidMethod(g_Host->view, releasePermission, jint(handle)); };
            owner.SetContentHost(std::move(content));
            owner.SetClipboardHost(std::move(clip));
            owner.SetHapticHost([](int constant) { MainEnv()->CallVoidMethod(g_Host->view, haptic, jint(constant)); });
            {
                jclass mc = env->GetObjectClass(view);
                static jmethodID mCreate = env->GetMethodID(mc, "createMagnifier", "(ZFFFFFZ)I");
                static jmethodID mShow = env->GetMethodID(mc, "showMagnifier", "(IFFFFF)V");
                static jmethodID mUpdate = env->GetMethodID(mc, "updateMagnifierContent", "(I)V");
                static jmethodID mDismiss = env->GetMethodID(mc, "dismissMagnifier", "(I)V");
                static jmethodID mDestroy = env->GetMethodID(mc, "destroyMagnifier", "(I)V");
                static jmethodID mSize = env->GetMethodID(mc, "magnifierSize", "(I)[I");
                env->DeleteLocalRef(mc);
                kiln::ui::Owner::MagnifierHost mag;
                mag.create = [](const kiln::ui::Owner::MagnifierConfig& c) {
                    return int(MainEnv()->CallIntMethod(g_Host->view, mCreate, jboolean(c.useTextDefault), c.widthPx, c.heightPx, c.cornerRadiusPx,
                                                        c.elevationPx, c.initialZoom, jboolean(c.clippingEnabled)));
                };
                mag.show = [](int h, kiln::ui::Offset src, kiln::ui::Offset center, float zoom) {
                    MainEnv()->CallVoidMethod(g_Host->view, mShow, jint(h), src.x, src.y, center.x, center.y, zoom);
                };
                mag.updateContent = [](int h) { MainEnv()->CallVoidMethod(g_Host->view, mUpdate, jint(h)); };
                mag.dismiss = [](int h) { MainEnv()->CallVoidMethod(g_Host->view, mDismiss, jint(h)); };
                mag.destroy = [](int h) { MainEnv()->CallVoidMethod(g_Host->view, mDestroy, jint(h)); };
                mag.size = [](int h) -> kiln::ui::IntSize {
                    JNIEnv* e = MainEnv();
                    auto a = static_cast<jintArray>(e->CallObjectMethod(g_Host->view, mSize, jint(h)));
                    jint v[2] = { 0, 0 };
                    e->GetIntArrayRegion(a, 0, 2, v);
                    e->DeleteLocalRef(a);
                    return { int(v[0]), int(v[1]) };
                };
                owner.SetMagnifierHost(std::move(mag));
            }
            kiln::ui::Owner::TextToolbarHost bar;
            bar.show = [](kiln::ui::Rect r, const std::vector<kiln::ui::TextToolbarEntry>& entries) {
                JNIEnv* e = MainEnv();
                ToolbarArrays a(e, entries);
                return bool(e->CallBooleanMethod(g_Host->view, showBar, jint(std::floor(r.left)), jint(std::floor(r.top)), jint(std::ceil(r.right)),
                                                 jint(std::ceil(r.bottom)), a.kinds, a.ids, a.labels, a.groups, a.classifications, a.indexes));
            };
            bar.update = [](const std::vector<kiln::ui::TextToolbarEntry>& entries) {
                JNIEnv* e = MainEnv();
                ToolbarArrays a(e, entries);
                e->CallVoidMethod(g_Host->view, updateBar, a.kinds, a.ids, a.labels, a.groups, a.classifications, a.indexes);
            };
            bar.move = [](kiln::ui::Rect r) {
                MainEnv()->CallVoidMethod(g_Host->view, moveBar, jint(std::floor(r.left)), jint(std::floor(r.top)), jint(std::ceil(r.right)),
                                          jint(std::ceil(r.bottom)));
            };
            bar.hide = [] { MainEnv()->CallVoidMethod(g_Host->view, hideBar); };
            owner.SetTextToolbarHost(std::move(bar));
            kiln::ui::TextClassifierHost tc;
            tc.createSession = [](kiln::ui::SelectedTextType t) {
                return int(MainEnv()->CallIntMethod(g_Host->view, tcCreate, jint(t == kiln::ui::SelectedTextType::EditableText ? 0 : 1)));
            };
            tc.destroySession = [](int id) { MainEnv()->CallVoidMethod(g_Host->view, tcDestroy, jint(id)); };
            tc.suggestSelection = [](int session, const std::u16string& text, int start, int end, const std::vector<std::string>& locales, auto done) {
                const int request = PendingClassifier::Add([done = std::move(done)](const ClassifierReply& r) {
                    done(r.hasRange ? std::optional(std::pair{ r.start, r.end }) : std::nullopt, r.info);
                });
                JNIEnv* e = MainEnv();
                jstring js = JavaString(e, text);
                jobjectArray jl = JavaStrings(e, locales);
                e->CallVoidMethod(g_Host->view, tcSuggest, jint(session), jint(request), js, jint(start), jint(end), jl);
                e->DeleteLocalRef(js);
                e->DeleteLocalRef(jl);
            };
            tc.classifyText = [](int session, const std::u16string& text, int start, int end, const std::vector<std::string>& locales, auto done) {
                const int request = PendingClassifier::Add([done = std::move(done)](const ClassifierReply& r) { done(r.info); });
                JNIEnv* e = MainEnv();
                jstring js = JavaString(e, text);
                jobjectArray jl = JavaStrings(e, locales);
                e->CallVoidMethod(g_Host->view, tcClassify, jint(session), jint(request), js, jint(start), jint(end), jl);
                e->DeleteLocalRef(js);
                e->DeleteLocalRef(jl);
            };
            tc.releaseClassification = [](int handle) { MainEnv()->CallVoidMethod(g_Host->view, tcRelease, jint(handle)); };
            tc.processTextActivities = [] {
                JNIEnv* e = MainEnv();
                auto a = static_cast<jobjectArray>(e->CallObjectMethod(g_Host->view, ptList));
                std::vector<std::string> out;
                if (!a) return out;
                for (auto& l : StringsFromJava(e, a)) out.push_back(l.value_or(""));
                e->DeleteLocalRef(a);
                return out;
            };
            tc.processText = [](int index, bool editable, const std::u16string& selected) {
                JNIEnv* e = MainEnv();
                jstring js = JavaString(e, selected);
                e->CallVoidMethod(g_Host->view, ptStart, jint(index), jboolean(editable), js);
                e->DeleteLocalRef(js);
            };
            tc.canLookUp = [](const std::u16string& selected) {
                JNIEnv* e = MainEnv();
                jstring js = JavaString(e, selected);
                const bool can = e->CallBooleanMethod(g_Host->view, luCan, js);
                e->DeleteLocalRef(js);
                return can;
            };
            tc.lookUp = [](const std::u16string& selected) {
                JNIEnv* e = MainEnv();
                jstring js = JavaString(e, selected);
                e->CallVoidMethod(g_Host->view, luStart, js);
                e->DeleteLocalRef(js);
            };
            owner.SetTextClassifierHost(std::move(tc));
            static jmethodID requestControl = env->GetMethodID(env->GetObjectClass(view), "requestInsetsControl", "(I)Z");
            static jmethodID setControlled = env->GetMethodID(env->GetObjectClass(view), "setControlledInsets", "(IIIIFF)V");
            static jmethodID finishControl = env->GetMethodID(env->GetObjectClass(view), "finishInsetsControl", "(Z)V");
            kiln::ui::InsetsControlHost control;
            control.request = [](kiln::ui::InsetsType t) { return bool(MainEnv()->CallBooleanMethod(g_Host->view, requestControl, jint(t))); };
            control.setInsetsAndAlpha = [](const kiln::ui::InsetsValue& v, float alpha, float progress) {
                MainEnv()->CallVoidMethod(g_Host->view, setControlled, jint(v.left), jint(v.top), jint(v.right), jint(v.bottom), jfloat(alpha), jfloat(progress));
            };
            control.finish = [](bool shown) { MainEnv()->CallVoidMethod(g_Host->view, finishControl, jboolean(shown)); };
            owner.Insets().SetControlHost(std::move(control));
            static jmethodID barsAppearance = env->GetMethodID(env->GetObjectClass(view), "setSystemBarsAppearance", "(ZZ)V");
            owner.SetSystemBarsAppearanceHost([](bool lightStatus, bool lightNav) {
                MainEnv()->CallVoidMethod(g_Host->view, barsAppearance, jboolean(lightStatus), jboolean(lightNav));
            });
        }
        {
            jclass kcm = env->FindClass("android/view/KeyCharacterMap");
            static jclass kcmClass = reinterpret_cast<jclass>(env->NewGlobalRef(kcm));
            static jmethodID getDeadChar = env->GetStaticMethodID(kcm, "getDeadChar", "(II)I");
            env->DeleteLocalRef(kcm);
            g_Host->app->GetOwner().SetDeadCharCombiner([](int accent, int c) {
                return int(MainEnv()->CallStaticIntMethod(kcmClass, getDeadChar, jint(accent), jint(c)));
            });
        }
        // Keys and the IME reach a View only while it holds platform focus.
        g_Host->app->GetOwner().SetRequestHostFocus([] {
            JNIEnv* e = nullptr;
            g_Host->vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6);
            return e && e->CallBooleanMethod(g_Host->view, g_Host->requestFocus);
        });

        // Posted work and timers wake the main looper (AndroidUiDispatcher's Handler half).
        g_Host->eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        g_Host->timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        ALooper* looper = ALooper_forThread();
        ALooper_addFd(looper, g_Host->eventFd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &OnLooperWake, nullptr);
        ALooper_addFd(looper, g_Host->timerFd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &OnLooperWake, nullptr);
        g_Host->app->Dispatcher().wake = [] { Wake(); };

        if (saved) {
            std::vector<std::uint8_t> bytes(std::size_t(env->GetArrayLength(saved)));
            env->GetByteArrayRegion(saved, 0, jsize(bytes.size()), reinterpret_cast<jbyte*>(bytes.data()));
            g_Host->app->RestoreState(bytes);
        }
        {
            // LocalBringIntoViewSpec's platform default: a TV (FEATURE_LEANBACK) pivots.
            jclass vc = env->GetObjectClass(view);
            jmethodID isTv = env->GetMethodID(vc, "isTelevision", "()Z");
            env->DeleteLocalRef(vc);
            g_Host->app->SetTelevision(isTv && env->CallBooleanMethod(view, isTv));
        }
        {
            char v[PROP_VALUE_MAX] = {};
            g_FrameStats.enabled = __system_property_get("debug.kiln.frametimes", v) > 0 && v[0] == '1';
        }
        kiln_main(&g_Host->handle);
    }

    void SetVisible(JNIEnv*, jclass, jboolean visible) {
        if (!g_Host) return;
        g_Host->visible = visible;
        if (visible && g_Host->app->FramePending()) {
            g_Host->callbackPosted = false;
            PostFrame(nullptr);
        }
    }

    void FontsChanged(JNIEnv*, jclass) {
        if (!g_Host) return;
        g_Host->app->GetOwner().OnFontsChanged();
        g_Host->app->RequestFrame();
    }

    void TextToolbarItem(JNIEnv*, jclass, jint item) {
        if (!g_Host || item < 0) return;
        g_Host->app->GetOwner().OnTextToolbarItem(std::size_t(item));
        g_Host->app->RequestFrame();
    }

    void ClassifierResult(JNIEnv* env, jclass, jint request, jboolean hasRange, jint start, jint end, jint handle, jobjectArray actions,
                          jboolean legacy) {
        auto& map = PendingClassifier::Map();
        auto it = map.find(request);
        if (it == map.end()) return;
        auto fn = std::move(it->second);
        map.erase(it);
        ClassifierReply r;
        r.hasRange = hasRange;
        r.start = start;
        r.end = end;
        if (handle != 0) {
            kiln::ui::TextClassificationInfo info;
            info.handle = handle;
            if (actions) for (auto& a : StringsFromJava(env, actions)) info.actions.push_back(a.value_or(""));
            info.legacyAssistItem = legacy;
            r.info = std::move(info);
        }
        if (!g_Host) return;
        fn(r);
        g_Host->app->RequestFrame();
    }

    void TextToolbarDestroyed(JNIEnv*, jclass) {
        if (!g_Host) return;
        g_Host->app->GetOwner().OnTextToolbarDestroyed();
    }

    void SetFontWeightAdjustment(JNIEnv*, jclass, jint adjustment) {
        if (!g_Host) return;
        g_Host->app->SetFontWeightAdjustment(adjustment);
    }

    void SetPasswordVisibility(JNIEnv*, jclass, jint bits) {
        if (!g_Host) return;
        g_Host->app->GetOwner().SetPasswordVisibility(bits);
        g_Host->app->RequestFrame();
    }

    void SetAnimationScale(JNIEnv*, jclass, jfloat scale) {
        if (!g_Host) return;
        g_Host->app->SetAnimationScale(scale);
    }

    void SetLayoutDirection(JNIEnv*, jclass, jboolean rtl) {
        if (!g_Host) return;
        g_Host->app->SetLayoutDirection(rtl ? kiln::ui::LayoutDirection::Rtl : kiln::ui::LayoutDirection::Ltr);
    }

    void SetDensity(JNIEnv*, jclass, jfloat density, jfloat fontScale) {
        if (!g_Host) return;
        g_Host->app->SetDensity(density, fontScale);
        g_Host->app->RequestFrame();
    }

    // MotionEvent -> RawPointerEvent (MotionEventAdapter, spec 02 req. 9). Positions arrive in
    // view pixels, which are surface pixels here (the SurfaceView fills the KilnView).
    void TouchEvent(JNIEnv* env, jclass, jint action, jint actionIndex, jlong eventTime, jintArray ids,
                    jfloatArray xy, jintArray tools, jint buttons, jint metaState, jint classification, jfloat hscroll, jfloat vscroll,
                    jlongArray historyTimes, jfloatArray historyXy, jfloatArray pan) {
        if (!g_Host) return;
        enum { DOWN = 0, UP = 1, MOVE = 2, CANCEL = 3, POINTER_DOWN = 5, POINTER_UP = 6, HOVER_MOVE = 7,
               SCROLL = 8, HOVER_ENTER = 9, HOVER_EXIT = 10 };
        kiln::ui::RawPointerEvent ev;
        ev.uptimeMillis = eventTime;
        ev.buttons = std::uint32_t(buttons);
        ev.metaState = std::uint32_t(metaState);
        ev.classification = classification;
        ev.cancel = action == CANCEL;
        const jsize n = env->GetArrayLength(ids);
        jint* id = env->GetIntArrayElements(ids, nullptr);
        jfloat* p = env->GetFloatArrayElements(xy, nullptr);
        jint* tool = env->GetIntArrayElements(tools, nullptr);
        for (jsize i = 0; i < n; ++i) {
            kiln::ui::RawPointer rp;
            rp.id = id[i];
            rp.position = { p[2 * i], p[2 * i + 1] };
            // TOOL_TYPE_FINGER 1, STYLUS 2, MOUSE 3, ERASER 4
            rp.type = tool[i] == 1 ? kiln::ui::PointerType::Touch : tool[i] == 2 ? kiln::ui::PointerType::Stylus
                    : tool[i] == 3 ? kiln::ui::PointerType::Mouse : tool[i] == 4 ? kiln::ui::PointerType::Eraser
                                   : kiln::ui::PointerType::Unknown;
            const bool lifting = (action == UP || action == POINTER_UP) && i == actionIndex;
            const bool hover = action == HOVER_MOVE || action == HOVER_ENTER || action == HOVER_EXIT || action == SCROLL;
            // A wheel scroll with a button held is a drag in progress (MotionEventAdapter).
            rp.down = !lifting && (!hover || (action == SCROLL && buttons != 0)) && action != CANCEL;
            // Compose flips the scroll axes: positive AXIS_VSCROLL scrolls content up (-y).
            if (action == SCROLL) rp.scrollDelta = { -hscroll, -vscroll };
            ev.pointers.push_back(rp);
        }
        // History: times[h], xy[(h * n + i) * 2] for pointer i.
        if (historyTimes) {
            const jsize hn = env->GetArrayLength(historyTimes);
            jlong* ht = env->GetLongArrayElements(historyTimes, nullptr);
            jfloat* hx = env->GetFloatArrayElements(historyXy, nullptr);
            for (jsize h = 0; h < hn; ++h)
                for (jsize i = 0; i < n; ++i)
                    ev.pointers[std::size_t(i)].historical.push_back({ std::int64_t(ht[h]), { hx[(h * n + i) * 2], hx[(h * n + i) * 2 + 1] } });
            env->ReleaseLongArrayElements(historyTimes, ht, JNI_ABORT);
            env->ReleaseFloatArrayElements(historyXy, hx, JNI_ABORT);
        }
        env->ReleaseIntArrayElements(ids, id, JNI_ABORT);
        env->ReleaseFloatArrayElements(xy, p, JNI_ABORT);
        env->ReleaseIntArrayElements(tools, tool, JNI_ABORT);
        // Trackpad swipes and pinches (Android 14+) become one unpressed pointer at the cursor.
        static kiln::ui::TrackpadGestureTracker trackpad;
        if (pan) {
            const jsize pn = env->GetArrayLength(pan);
            jfloat* pv = env->GetFloatArrayElements(pan, nullptr);
            trackpad.Process(ev, action, classification, pv, int(pn));
            env->ReleaseFloatArrayElements(pan, pv, JNI_ABORT);
        } else {
            trackpad.Process(ev, action, classification, nullptr, 0);
        }
        g_Host->app->ProcessPointer(ev);
        ArmTimer();
    }

    jboolean KeyEvent(JNIEnv*, jclass, jint action, jint keyCode, jint metaState, jint unicode, jint repeat, jlong time, jint source) {
        if (!g_Host) return {};
        kiln::ui::KeyEvent e;
        e.keyCode = keyCode;
        e.type = action == 0 ? kiln::ui::KeyEventType::KeyDown : action == 1 ? kiln::ui::KeyEventType::KeyUp
                                                                           : kiln::ui::KeyEventType::Unknown;
        e.metaState = std::uint32_t(metaState);
        e.unicode = std::uint32_t(unicode);
        e.repeatCount = repeat;
        e.uptimeMillis = time;
        e.fromSoftKeyboard = source & 1;
        e.fromHardwareSource = source & 2;
        e.fromKeyboardSource = source & 4;
        const bool handled = g_Host->app->ProcessKey(e);
        ArmTimer();
        return handled;
    }

    jboolean KeyEventPreIme(JNIEnv*, jclass, jint action, jint keyCode, jint metaState, jint unicode, jint repeat, jlong time, jint source) {
        if (!g_Host) return {};
        kiln::ui::KeyEvent e;
        e.keyCode = keyCode;
        e.type = action == 0 ? kiln::ui::KeyEventType::KeyDown : action == 1 ? kiln::ui::KeyEventType::KeyUp
                                                                           : kiln::ui::KeyEventType::Unknown;
        e.metaState = std::uint32_t(metaState);
        e.unicode = std::uint32_t(unicode);
        e.repeatCount = repeat;
        e.uptimeMillis = time;
        e.fromSoftKeyboard = source & 1;
        e.fromHardwareSource = source & 2;
        e.fromKeyboardSource = source & 4;
        const bool handled = g_Host->app->ProcessKeyPreIme(e);
        ArmTimer();
        return handled;
    }

    // AndroidComposeView.handleRotaryEvent: pixels already scaled by the view configuration.
    jboolean Rotary(JNIEnv*, jclass, jfloat vertical, jfloat horizontal, jlong time, jint deviceId) {
        if (!g_Host) return {};
        const bool handled = g_Host->app->ProcessRotary({ vertical, horizontal, time, deviceId });
        ArmTimer();
        return handled;
    }

    // Touch-navigation pads (AndroidComposeView.dispatchGenericMotionEvent): the MotionEvent's
    // pointers, pressures and batched samples, plus the pad's primary axis (0 none, 1 x, 2 y).
    jboolean IndirectPointer(JNIEnv* env, jclass, jint action, jint actionIndex, jlong eventTime, jintArray ids,
                             jfloatArray xy, jfloatArray pressures, jlongArray historyTimes, jfloatArray historyXy, jint axis) {
        if (!g_Host) return {};
        using kiln::ui::RawIndirectPointerEvent;
        using A = RawIndirectPointerEvent::Action;
        RawIndirectPointerEvent ev;
        // MotionEvent ACTION_DOWN 0, UP 1, MOVE 2, CANCEL 3, POINTER_DOWN 5, POINTER_UP 6
        switch (action) {
            case 0: ev.action = A::Down; break;
            case 1: ev.action = A::Up; break;
            case 2: ev.action = A::Move; break;
            case 3: ev.action = A::Cancel; break;
            case 5: ev.action = A::PointerDown; break;
            case 6: ev.action = A::PointerUp; break;
            default: ev.action = A::Other; break;
        }
        ev.actionIndex = actionIndex;
        ev.uptimeMillis = eventTime;
        ev.axis = kiln::ui::IndirectPointerEventPrimaryDirectionalMotionAxis(axis);
        const jsize n = env->GetArrayLength(ids);
        jint* id = env->GetIntArrayElements(ids, nullptr);
        jfloat* p = env->GetFloatArrayElements(xy, nullptr);
        jfloat* pr = env->GetFloatArrayElements(pressures, nullptr);
        for (jsize i = 0; i < n; ++i)
            ev.pointers.push_back({ id[i], { p[2 * i], p[2 * i + 1] }, pr[i], {} });
        if (historyTimes) {
            const jsize hn = env->GetArrayLength(historyTimes);
            jlong* ht = env->GetLongArrayElements(historyTimes, nullptr);
            jfloat* hx = env->GetFloatArrayElements(historyXy, nullptr);
            for (jsize h = 0; h < hn; ++h)
                for (jsize i = 0; i < n; ++i)
                    ev.pointers[std::size_t(i)].historical.push_back({ std::int64_t(ht[h]), { hx[(h * n + i) * 2], hx[(h * n + i) * 2 + 1] }, {} });
            env->ReleaseLongArrayElements(historyTimes, ht, JNI_ABORT);
            env->ReleaseFloatArrayElements(historyXy, hx, JNI_ABORT);
        }
        env->ReleaseIntArrayElements(ids, id, JNI_ABORT);
        env->ReleaseFloatArrayElements(xy, p, JNI_ABORT);
        env->ReleaseFloatArrayElements(pressures, pr, JNI_ABORT);
        const bool consumed = g_Host->app->ProcessIndirectPointer(ev);
        ArmTimer();
        return consumed;
    }

    // View focus integration (AndroidComposeView getFocusedRect / focusSearch / requestFocus).
    jfloatArray FocusedRect(JNIEnv* env, jclass) {
        if (!g_Host) return nullptr;
        const auto r = g_Host->app->FocusedRect();
        if (!r) return nullptr;
        jfloatArray out = env->NewFloatArray(4);
        const jfloat v[4] = { r->left, r->top, r->right, r->bottom };
        env->SetFloatArrayRegion(out, 0, 4, v);
        return out;
    }

    jboolean HasFocusableContent(JNIEnv*, jclass) {
        return g_Host && g_Host->app->HasFocusableContent();
    }

    // [result (-1 cancelled, 0 none, 1 found), left, top, right, bottom of the target]
    jfloatArray FocusSearch(JNIEnv* env, jclass, jint direction, jfloat l, jfloat t, jfloat r, jfloat b) {
        jfloat v[5] = { 0, 0, 0, 0, 0 };
        if (g_Host) {
            kiln::ui::Rect target{};
            v[0] = jfloat(g_Host->app->HostFocusSearch(direction, { l, t, r, b }, target));
            v[1] = target.left; v[2] = target.top; v[3] = target.right; v[4] = target.bottom;
        }
        jfloatArray out = env->NewFloatArray(5);
        env->SetFloatArrayRegion(out, 0, 5, v);
        return out;
    }

    jboolean IsBetterFocusCandidate(JNIEnv* env, jclass, jint direction, jfloatArray proposed, jfloatArray current, jfloatArray focused) {
        if (!g_Host) return {};
        auto rect = [&](jfloatArray a) {
            jfloat v[4];
            env->GetFloatArrayRegion(a, 0, 4, v);
            return kiln::ui::Rect{ v[0], v[1], v[2], v[3] };
        };
        return g_Host->app->IsBetterFocusCandidate(direction, rect(proposed), rect(current), rect(focused));
    }

    jboolean HostRequestFocus(JNIEnv*, jclass, jint direction, jboolean hasRect, jfloat l, jfloat t, jfloat r, jfloat b,
                              jboolean hostHasFocus) {
        if (!g_Host) return {};
        const kiln::ui::Rect prev{ l, t, r, b };
        const bool done = g_Host->app->HostRequestFocus(direction, hasRect ? &prev : nullptr, hostHasFocus);
        ArmTimer();
        return done;
    }

    void IndirectFling(JNIEnv*, jclass, jboolean next) {
        if (!g_Host) return;
        g_Host->app->MoveFocusFromIndirectFling(next);
        ArmTimer();
    }

    void SetWindowInsets(JNIEnv* env, jclass, jintArray data) {
        if (!g_Host) return;
        const jsize n = env->GetArrayLength(data);
        std::vector<jint> v(static_cast<std::size_t>(n));
        env->GetIntArrayRegion(data, 0, n, v.data());
        auto& h = g_Host->app->GetOwner().Insets();
        constexpr std::size_t kTypes = std::size_t(kiln::ui::InsetsType::Count);
        if (v.size() < kTypes * 9 + 1) return;
        for (std::size_t i = 0; i < kTypes; ++i) {
            const jint* p = v.data() + i * 9;
            const auto type = kiln::ui::InsetsType(i);
            h.Set(type, { p[0], p[1], p[2], p[3] });
            h.SetIgnoringVisibility(type, { p[4], p[5], p[6], p[7] });
            h.SetVisible(type, p[8] != 0);
        }
        std::vector<kiln::ui::InsetsRect> cutouts;
        const std::size_t count = std::size_t(v[kTypes * 9]);
        for (std::size_t i = 0; i < count && kTypes * 9 + 1 + i * 4 + 3 < v.size(); ++i) {
            const jint* p = v.data() + kTypes * 9 + 1 + i * 4;
            cutouts.push_back({ p[0], p[1], p[2], p[3] });
        }
        h.SetCutoutRects(std::move(cutouts));
        h.SetHasInsets(true);
        g_Host->app->RequestFrame();
    }
    void InsetsControlReady(JNIEnv* env, jclass, jintArray data) {
        if (!g_Host || env->GetArrayLength(data) < 12) return;
        jint v[12];
        env->GetIntArrayRegion(data, 0, 12, v);
        g_Host->app->GetOwner().Insets().OnControlReady({ v[0], v[1], v[2], v[3] }, { v[4], v[5], v[6], v[7] }, { v[8], v[9], v[10], v[11] });
        g_Host->app->RequestFrame();
    }
    void InsetsControlEnded(JNIEnv*, jclass) {
        if (!g_Host) return;
        g_Host->app->GetOwner().Insets().OnControlEnded();
        g_Host->app->RequestFrame();
    }
    void SetInsetsAnimation(JNIEnv* env, jclass, jint type, jboolean running, jintArray bounds, jfloat fraction, jfloat alpha, jlong duration) {
        if (!g_Host || type < 0 || type >= jint(kiln::ui::InsetsType::Count)) return;
        auto& h = g_Host->app->GetOwner().Insets();
        const auto t = kiln::ui::InsetsType(type);
        if (!running) { h.SetAnimation(t, std::nullopt); g_Host->app->RequestFrame(); return; }
        kiln::ui::InsetsAnimation a = h.Animation(t).value_or(kiln::ui::InsetsAnimation{});
        if (bounds && env->GetArrayLength(bounds) >= 8) {
            jint b[8];
            env->GetIntArrayRegion(bounds, 0, 8, b);
            a.source = { b[0], b[1], b[2], b[3] };
            a.target = { b[4], b[5], b[6], b[7] };
        }
        a.fraction = fraction;
        a.alpha = alpha;
        a.durationMillis = duration;
        h.SetAnimation(t, a);
        g_Host->app->RequestFrame();
    }

    void HostFocusChanged(JNIEnv*, jclass, jboolean focused) {
        if (!g_Host) return;
        g_Host->app->GetOwner().OnHostFocusChanged(focused);
    }

    void WindowFocusChanged(JNIEnv*, jclass, jboolean focused) {
        if (!g_Host) return;
        g_Host->app->GetOwner().SetWindowFocused(focused);
    }

    // --- text input --------------------------------------------------------------------------

    // EditorInfo.update(imeOptions) (TextInputServiceAndroid:528-682).
    void MapEditorInfo(const kiln::ui::ImeOptions& o, int& inputType, int& imeOptions) {
        // EditorInfo.update (Compose's EditorInfo.android.kt).
        using A = kiln::ui::ImeAction;
        switch (o.imeAction) {
            case A::Default: imeOptions = o.singleLine ? 6 : 0; break;   // DONE / UNSPECIFIED
            case A::None: imeOptions = 1; break;
            case A::Go: imeOptions = 2; break;
            case A::Search: imeOptions = 3; break;
            case A::Send: imeOptions = 4; break;
            case A::Next: imeOptions = 5; break;
            case A::Done: imeOptions = 6; break;
            case A::Previous: imeOptions = 7; break;
        }
        using K = kiln::ui::KeyboardType;
        constexpr int TEXT = 1, NUMBER = 2, PHONE = 3, DATETIME = 4;
        constexpr int NUMBER_SIGNED = 0x1000, NUMBER_DECIMAL = 0x2000, NUMBER_PASSWORD = 0x10;
        switch (o.keyboardType) {
            case K::Text: inputType = TEXT; break;
            case K::Ascii: inputType = TEXT; imeOptions |= int(0x80000000u); break;   // IME_FLAG_FORCE_ASCII
            case K::Number: inputType = NUMBER; break;
            case K::Phone: inputType = PHONE; break;
            case K::Uri: inputType = TEXT | 0x10; break;
            case K::Email: inputType = TEXT | 0x20; break;
            case K::Password: inputType = TEXT | 0x80; break;
            case K::NumberPassword: inputType = NUMBER | NUMBER_PASSWORD; break;
            case K::Decimal: inputType = NUMBER | NUMBER_DECIMAL; break;
            case K::PasswordVisible: inputType = TEXT | 0x90; break;
            case K::PostalAddress: inputType = TEXT | 0x70; break;
            case K::PersonName: inputType = TEXT | 0x60; break;
            case K::EmailSubject: inputType = TEXT | 0x30; break;
            case K::ShortMessage: inputType = TEXT | 0x40; break;
            case K::LongMessage: inputType = TEXT | 0x50; break;
            case K::Filter: inputType = TEXT | 0xB0; break;
            case K::Phonetic: inputType = TEXT | 0xC0; break;
            case K::DateTime: inputType = DATETIME; break;
            case K::Date: inputType = DATETIME | 0x10; break;
            case K::Time: inputType = DATETIME | 0x20; break;
            case K::NumberSigned: inputType = NUMBER | NUMBER_SIGNED; break;
            case K::DecimalSigned: inputType = NUMBER | NUMBER_DECIMAL | NUMBER_SIGNED; break;
            case K::DecimalPassword: inputType = NUMBER | NUMBER_PASSWORD | NUMBER_DECIMAL; break;
            case K::NumberPasswordSigned: inputType = NUMBER | NUMBER_PASSWORD | NUMBER_SIGNED; break;
            case K::DecimalPasswordSigned: inputType = NUMBER | NUMBER_PASSWORD | NUMBER_DECIMAL | NUMBER_SIGNED; break;
        }
        if (!o.singleLine && (inputType & 0xF) == TEXT) {
            inputType |= 0x20000;                                       // TYPE_TEXT_FLAG_MULTI_LINE
            if (o.imeAction == A::Default) imeOptions |= 0x40000000;    // IME_FLAG_NO_ENTER_ACTION
        }
        if ((inputType & 0xF) == TEXT) {
            using C = kiln::ui::Capitalization;
            if (o.capitalization == C::Characters) inputType |= 0x1000;
            else if (o.capitalization == C::Words) inputType |= 0x2000;
            else if (o.capitalization == C::Sentences) inputType |= 0x4000;
            if (o.autoCorrect) inputType |= 0x8000;
            if (android_get_device_api_level() >= 37) inputType |= 0x200000;   // TYPE_TEXT_FLAG_ENABLE_TEXT_SUGGESTION_SELECTED
        }
        imeOptions |= 0x2000000;                                        // IME_FLAG_NO_FULLSCREEN
    }

    JNIEnv* MainEnv() {
        JNIEnv* e = nullptr;
        g_Host->vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6);
        return e;
    }

    void InstallTextInputHost(JNIEnv* env) {
        jclass cls = env->GetObjectClass(g_Host->view);
        static jmethodID start = env->GetMethodID(cls, "startInput", "(IIZLjava/lang/String;ZZ)V");
        static jmethodID handwriting = env->GetMethodID(cls, "startStylusHandwriting", "()V");
        static jmethodID stop = env->GetMethodID(cls, "stopInput", "()V");
        static jmethodID update = env->GetMethodID(cls, "imeUpdate", "(IIIIZ)V");
        static jmethodID anchor = env->GetMethodID(cls, "cursorAnchorInfo", "([FLjava/lang/String;)V");
        static jmethodID restart = env->GetMethodID(cls, "restartInputFromNative", "()V");
        static jmethodID show = env->GetMethodID(cls, "showKeyboard", "()V");
        static jmethodID hide = env->GetMethodID(cls, "hideKeyboard", "()V");
        env->DeleteLocalRef(cls);
        kiln::ui::TextInputHost host;
        host.start = [](const kiln::ui::ImeOptions& o, const kiln::ui::TextInputSnapshot&) {
            int type = 0, opts = 0;
            MapEditorInfo(o, type, opts);
            std::string tags;
            for (const auto& t : o.hintLocales) { if (!tags.empty()) tags += ','; tags += t; }
            JNIEnv* e = MainEnv();
            jstring jt = e->NewStringUTF(tags.c_str());
            e->CallVoidMethod(g_Host->view, start, type, opts, jboolean(o.singleLine), jt, jboolean(o.acceptsContent), jboolean(o.stylusHandwriting));
            e->DeleteLocalRef(jt);
        };
        host.stop = [] { MainEnv()->CallVoidMethod(g_Host->view, stop); };
        host.update = [](const kiln::ui::TextInputSnapshot& s, bool selectionChanged) {
            MainEnv()->CallVoidMethod(g_Host->view, update, s.selStart, s.selEnd, s.compStart, s.compEnd, jboolean(selectionChanged));
        };
        host.cursorAnchorInfo = [](const kiln::ui::CursorAnchorInfo& c) {
            std::vector<jfloat> d(c.matrix.begin(), c.matrix.end());
            d.insert(d.end(), { float(c.selStart), float(c.selEnd), c.hasInsertionMarker ? 1.0f : 0.0f, c.markerX, c.markerTop,
                                c.markerBaseline, c.markerBottom, float(c.markerFlags), float(c.composingStart),
                                float(c.characterBounds.size()) });
            for (const auto& b : c.characterBounds) d.insert(d.end(), { float(b.index), b.left, b.top, b.right, b.bottom, float(b.flags) });
            d.insert(d.end(), { c.hasEditorBounds ? 1.0f : 0.0f, c.editorBounds[0], c.editorBounds[1], c.editorBounds[2], c.editorBounds[3],
                                float(c.visibleLineBounds.size()) });
            for (const auto& l : c.visibleLineBounds) d.insert(d.end(), l.begin(), l.end());
            JNIEnv* e = MainEnv();
            jfloatArray a = e->NewFloatArray(jsize(d.size()));
            e->SetFloatArrayRegion(a, 0, jsize(d.size()), d.data());
            jstring t = e->NewString(reinterpret_cast<const jchar*>(c.composingText.data()), jsize(c.composingText.size()));
            e->CallVoidMethod(g_Host->view, anchor, a, t);
            e->DeleteLocalRef(a);
            e->DeleteLocalRef(t);
        };
        host.restartInput = [] { MainEnv()->CallVoidMethod(g_Host->view, restart); };
        host.showKeyboard = [] { MainEnv()->CallVoidMethod(g_Host->view, show); };
        host.hideKeyboard = [] { MainEnv()->CallVoidMethod(g_Host->view, hide); };
        host.startStylusHandwriting = [] { MainEnv()->CallVoidMethod(g_Host->view, handwriting); };
        kiln::ui::SetStylusHandwritingSupported(android_get_device_api_level() >= 34);
        g_Host->app->GetOwner().SetTextInputHost(host);
    }

    kiln::ui::TextInputSession* Session() { return g_Host ? g_Host->app->GetOwner().ActiveTextInput() : nullptr; }

    std::u16string FromJava(JNIEnv* env, jstring s) {
        const jsize n = env->GetStringLength(s);
        std::u16string out(std::size_t(n), u'\0');
        env->GetStringRegion(s, 0, n, reinterpret_cast<jchar*>(out.data()));
        return out;
    }
    // Every IME edit is followed by the frame that shows it.
    void AfterIme() { g_Host->app->RequestFrame(); }

    jboolean ImeBeginBatchEdit(JNIEnv*, jclass) { auto* s = Session(); return s ? s->BeginBatchEdit() : false; }
    jboolean ImeEndBatchEdit(JNIEnv*, jclass) {
        auto* s = Session();
        if (!s) return false;
        const bool more = s->EndBatchEdit();
        AfterIme();
        return more;
    }
    void ImeCommitText(JNIEnv* env, jclass, jstring t, jint c, jboolean suggestion) {
        if (auto* s = Session()) { s->CommitText(FromJava(env, t), c, suggestion); AfterIme(); }
    }
    // The composing spans from KilnInputConnection.annotations (kind, start, end, value quads).
    void ImeSetComposingText(JNIEnv* env, jclass, jstring t, jint c, jintArray spans, jobjectArray families, jboolean suggestion) {
        auto* s = Session();
        if (!s) return;
        std::vector<kiln::text::Range<kiln::text::SpanStyle>> annotations;
        if (spans) {
            const jsize n = env->GetArrayLength(spans);
            std::vector<jint> v(static_cast<std::size_t>(n));
            env->GetIntArrayRegion(spans, 0, n, v.data());
            for (jsize i = 0; i + 3 < n; i += 4) {
                kiln::text::SpanStyle st;
                const int value = v[std::size_t(i + 3)];
                switch (v[std::size_t(i)]) {
                    case 1: st.background = kiln::ui::Color(std::uint32_t(value)); break;
                    case 2: st.color = kiln::ui::Color(std::uint32_t(value)); break;
                    case 3: st.textDecoration = kiln::text::TextDecoration::LineThrough; break;
                    case 4: st.fontWeight = kiln::text::FontWeight::Bold(); break;
                    case 5: st.fontStyle = kiln::text::FontStyle::Italic; break;
                    case 6: st.fontWeight = kiln::text::FontWeight::Bold(); st.fontStyle = kiln::text::FontStyle::Italic; break;
                    case 7: st.textDecoration = kiln::text::TextDecoration::Underline; break;
                    case 8: {
                        auto* js = static_cast<jstring>(env->GetObjectArrayElement(families, value));
                        const char* chars = env->GetStringUTFChars(js, nullptr);
                        const std::string name(chars);
                        env->ReleaseStringUTFChars(js, chars);
                        env->DeleteLocalRef(js);
                        using F = kiln::text::FontFamily;
                        if (name == "cursive") st.fontFamily = F::Cursive();
                        else if (name == "monospace") st.fontFamily = F::Monospace();
                        else if (name == "sans-serif") st.fontFamily = F::SansSerif();
                        else if (name == "serif") st.fontFamily = F::Serif();
                        else st.fontFamily = F{ F::Kind::SystemName, name, {} };
                        break;
                    }
                    default: continue;
                }
                annotations.push_back({ st, v[std::size_t(i + 1)], v[std::size_t(i + 2)], {} });
            }
        }
        s->SetComposingText(FromJava(env, t), c, spans ? &annotations : nullptr, suggestion);
        AfterIme();
    }
    void ImeRequestCursorUpdates(JNIEnv*, jclass, jint mode) { if (auto* s = Session()) s->RequestCursorUpdates(mode); }
    kiln::ui::HandwritingGesture GestureFromJava(JNIEnv* env, jfloatArray a) {
        jfloat d[14] = {};
        env->GetFloatArrayRegion(a, 0, 14, d);
        using G = kiln::ui::HandwritingGesture;
        G g;
        g.type = G::Type(std::clamp(int(d[0]), 0, 6));
        g.granularity = d[1] != 0 ? G::Granularity::Word : G::Granularity::Character;
        g.area = { d[2], d[3], d[4], d[5] };
        g.endArea = { d[6], d[7], d[8], d[9] };
        g.point = { d[10], d[11] };
        g.endPoint = { d[12], d[13] };
        return g;
    }
    jint ImePerformHandwritingGesture(JNIEnv* env, jclass, jfloatArray data, jstring insert, jstring fallback) {
        auto* s = Session();
        if (!s) return kiln::ui::HandwritingGesture::kResultFailed;
        kiln::ui::HandwritingGesture g = GestureFromJava(env, data);
        if (insert) g.textToInsert = FromJava(env, insert);
        if (fallback) g.fallbackText = FromJava(env, fallback);
        const int r = s->PerformHandwritingGesture(g);
        AfterIme();
        return r;
    }
    jboolean ImePreviewHandwritingGesture(JNIEnv* env, jclass, jfloatArray data) {
        auto* s = Session();
        if (!s) return false;
        const bool shown = s->PreviewHandwritingGesture(GestureFromJava(env, data));
        AfterIme();
        return shown;
    }
    void ImeCancelHandwritingPreview(JNIEnv*, jclass) { if (auto* s = Session()) { s->CancelHandwritingPreview(); AfterIme(); } }
    jboolean ImeCommitContent(JNIEnv* env, jclass, jstring uri, jobjectArray types, jstring label, jstring link, jint permission) {
        auto* s = Session();
        if (!s) return false;
        kiln::ui::ClipMetadata meta;
        meta.label = kiln::foundation::ToUtf8(FromJava(env, label));
        for (const auto& t : StringsFromJava(env, types)) meta.mimeTypes.push_back(t.value_or(""));
        kiln::ui::ClipEntry entry{ { kiln::ui::ClipItem{ std::nullopt, std::nullopt, kiln::foundation::ToUtf8(FromJava(env, uri)) } } };
        std::optional<std::string> linkUri;
        if (link) linkUri = kiln::foundation::ToUtf8(FromJava(env, link));
        const bool taken = s->CommitContent(entry, meta, linkUri, permission);
        AfterIme();
        return taken;
    }
    void ImeSetComposingRegion(JNIEnv*, jclass, jint a, jint b) { if (auto* s = Session()) { s->SetComposingRegion(a, b); AfterIme(); } }
    void ImeFinishComposingText(JNIEnv*, jclass) { if (auto* s = Session()) { s->FinishComposingText(); AfterIme(); } }
    void ImeDeleteSurroundingText(JNIEnv*, jclass, jint b, jint a, jboolean cp) {
        if (!g_Host) return;
        if (auto* s = Session()) { if (cp) s->DeleteSurroundingTextInCodePoints(b, a); else s->DeleteSurroundingText(b, a); AfterIme(); }
    }
    void ImeSetSelection(JNIEnv*, jclass, jint a, jint b) { if (auto* s = Session()) { s->SetSelection(a, b); AfterIme(); } }
    void ImePerformEditorAction(JNIEnv*, jclass, jint action) {
        if (!g_Host) return;
        using A = kiln::ui::ImeAction;
        const A map[] = { A::Default, A::None, A::Go, A::Search, A::Send, A::Next, A::Done, A::Previous };
        if (auto* s = Session()) { s->PerformImeAction(action >= 0 && action < 8 ? map[action] : A::Default); AfterIme(); }
    }
    jstring ImeGetText(JNIEnv* env, jclass) {
        if (!g_Host) return {};
        auto* s = Session();
        const std::u16string t = s ? s->Snapshot().text : std::u16string();
        return env->NewString(reinterpret_cast<const jchar*>(t.data()), jsize(t.size()));
    }
    jintArray ImeGetSelection(JNIEnv* env, jclass) {
        if (!g_Host) return {};
        jint v[4] = { 0, 0, -1, -1 };
        if (auto* s = Session()) { auto snap = s->Snapshot(); v[0] = snap.selStart; v[1] = snap.selEnd; v[2] = snap.compStart; v[3] = snap.compEnd; }
        jintArray a = env->NewIntArray(4);
        env->SetIntArrayRegion(a, 0, 4, v);
        return a;
    }

    jbyteArray SaveState(JNIEnv* env, jclass) {
        if (!g_Host) return {};
        const std::vector<std::uint8_t> bytes = g_Host->app->SaveState();
        jbyteArray a = env->NewByteArray(jsize(bytes.size()));
        env->SetByteArrayRegion(a, 0, jsize(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
        return a;
    }

    // --- back ------------------------------------------------------------------------------

    void AfterInput() {
        kiln::rt::Dispatcher::Main().RunPending();
        g_Host->app->RequestFrame();
    }
    void BackEventJni(JNIEnv*, jclass, jboolean started, jfloat x, jfloat y, jfloat progress, jint edge, jlong frameTime) {
        if (!g_Host) return;
        const kiln::ui::BackEvent e{ x, y, progress, kiln::ui::SwipeEdge(edge), frameTime };
        auto& d = kiln::ui::BackDispatcher::Main();
        if (started) d.Started(e); else d.Progressed(e);
        AfterInput();
    }
    void BackCancelled(JNIEnv*, jclass) {
        if (!g_Host) return;
        kiln::ui::BackDispatcher::Main().Cancelled();
        AfterInput();
    }
    void BackInvoked(JNIEnv*, jclass) {
        if (!g_Host) return;
        kiln::ui::BackDispatcher::Main().Completed();
        AfterInput();
    }

    // --- accessibility (spec 01: the delegate's node info, actions and events) ------------------

    // Strings of the last call that returned an int[] referring to them.
    std::vector<std::optional<std::string>> g_A11yStrings;
    int A11yString(const std::optional<std::string>& s) {
        if (!s) return -1;
        g_A11yStrings.push_back(s);
        return int(g_A11yStrings.size()) - 1;
    }
    jintArray IntArray(JNIEnv* env, const std::vector<jint>& d) {
        jintArray a = env->NewIntArray(jsize(d.size()));
        env->SetIntArrayRegion(a, 0, jsize(d.size()), d.data());
        return a;
    }
    jint FloatBits(float f) { jint i; std::memcpy(&i, &f, sizeof(i)); return i; }
    kiln::ui::AccessibilityDelegate& A11y() { return g_Host->app->GetOwner().Accessibility(); }

    void A11ySetEnabled(JNIEnv* env, jclass, jboolean enabled, jstring locale) {
        if (!g_Host) return;
        A11y().SetStrings(kiln::ui::a11y::Strings::ForLocale(kiln::foundation::ToUtf8(FromJava(env, locale))));
        A11y().SetEnabled(enabled);
    }

    // Node info: see KilnAccessibility.createAccessibilityNodeInfo for the layout.
    jintArray A11yNodeInfo(JNIEnv* env, jclass, jint id) {
        g_A11yStrings.clear();
        std::vector<jint> d;
        if (!g_Host) return IntArray(env, d);
        const auto info = A11y().CreateNodeInfo(id);
        if (!info) { d.push_back(0); return IntArray(env, d); }
        d.push_back(1);
        d.push_back(info->parent);
        d.insert(d.end(), { info->boundsInRoot.left, info->boundsInRoot.top, info->boundsInRoot.right, info->boundsInRoot.bottom });
        int f = 0;
        auto bit = [&](bool on, int b) { if (on) f |= b; };
        bit(info->importantForAccessibility, 1); bit(info->accessibilityFocused, 2); bit(info->checkable, 4);
        bit(info->selected, 8); bit(info->heading, 16); bit(info->textEntryKey, 32); bit(info->password, 64);
        bit(info->editable, 128); bit(info->enabled, 256); bit(info->focusable, 512); bit(info->focused, 1024);
        bit(info->visibleToUser, 2048); bit(info->clickable, 4096); bit(info->longClickable, 8192);
        bit(info->scrollable, 16384); bit(info->screenReaderFocusable, 32768); bit(info->contentInvalid, 65536);
        bit(info->dataSensitive, 131072); bit(info->checked.has_value(), 262144); bit(info->checked.value_or(false), 524288);
        d.push_back(f);
        d.insert(d.end(), { info->drawingOrder, info->maxTextLength, info->liveRegion, info->movementGranularities });
        d.push_back(info->textSelection ? info->textSelection->first : INT_MIN);
        d.push_back(info->textSelection ? info->textSelection->second : INT_MIN);
        d.push_back(info->traversalBefore);
        d.push_back(info->traversalAfter);
        d.push_back(info->collectionInfo ? info->collectionInfo->first : INT_MIN);
        d.push_back(info->collectionInfo ? info->collectionInfo->second : INT_MIN);
        const auto& item = info->collectionItemInfo;
        d.push_back(item ? item->rowIndex : INT_MIN);
        d.insert(d.end(), { item ? item->rowSpan : 0, item ? item->columnIndex : 0, item ? item->columnSpan : 0, item && item->selected ? 1 : 0 });
        d.push_back(info->rangeInfo ? 1 : 0);
        d.push_back(FloatBits(info->rangeInfo ? info->rangeInfo->min : 0));
        d.push_back(FloatBits(info->rangeInfo ? info->rangeInfo->max : 0));
        d.push_back(FloatBits(info->rangeInfo ? info->rangeInfo->current : 0));
        // Strings 0-8: className, roleDescription, text, contentDescription, stateDescription,
        // hintText, error, paneTitle, viewIdResourceName.
        g_A11yStrings = { info->className, info->roleDescription, info->text, info->contentDescription, info->stateDescription,
                          info->hintText, info->error, info->paneTitle, info->viewIdResourceName };
        d.push_back(int(info->children.size()));
        d.insert(d.end(), info->children.begin(), info->children.end());
        d.push_back(int(info->actions.size()));
        for (const auto& a : info->actions) { d.push_back(a.id); d.push_back(A11yString(a.label)); }
        d.push_back(int(info->textSpans.size()));
        for (std::size_t i = 0; i < info->textSpans.size(); ++i) {
            const auto& s = info->textSpans[i];
            d.insert(d.end(), { jint(s.kind), s.start, s.end, A11yString(s.value) });
        }
        d.push_back(int(info->extraDataKeys.size()));
        for (const auto& k : info->extraDataKeys) d.push_back(A11yString(k));
        return IntArray(env, d);
    }

    jobjectArray A11yStrings(JNIEnv* env, jclass) {
        jclass str = env->FindClass("java/lang/String");
        jobjectArray out = env->NewObjectArray(jsize(g_A11yStrings.size()), str, nullptr);
        for (std::size_t i = 0; i < g_A11yStrings.size(); ++i) {
            if (!g_A11yStrings[i]) continue;
            const std::u16string u = kiln::foundation::ToUtf16(*g_A11yStrings[i]);
            jstring js = env->NewString(reinterpret_cast<const jchar*>(u.data()), jsize(u.size()));
            env->SetObjectArrayElement(out, jsize(i), js);
            env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(str);
        return out;
    }

    // Events: per event type, id, contentChangeTypes, flags (1 password, 2 sensitive), fromIndex,
    // toIndex, itemCount, addedCount, removedCount, scrollX, maxScrollX, scrollY, maxScrollY,
    // deltaX, deltaY, action, granularity (INT_MIN: unset), className, contentDescription,
    // beforeText (string indices), text count, text indices.
    jintArray A11yTakeEvents(JNIEnv* env, jclass) {
        g_A11yStrings.clear();
        std::vector<jint> d;
        if (!g_Host) return IntArray(env, d);
        auto opt = [](const std::optional<int>& v) { return v ? jint(*v) : jint(INT_MIN); };
        for (const auto& e : A11y().TakeEvents()) {
            d.insert(d.end(), { e.type, e.id, opt(e.contentChangeTypes), (e.password ? 1 : 0) | (e.dataSensitive ? 2 : 0),
                                opt(e.fromIndex), opt(e.toIndex), opt(e.itemCount), opt(e.addedCount), opt(e.removedCount),
                                opt(e.scrollX), opt(e.maxScrollX), opt(e.scrollY), opt(e.maxScrollY), e.scrollDeltaX, e.scrollDeltaY,
                                opt(e.action), opt(e.movementGranularity) });
            d.push_back(A11yString(e.className));
            d.push_back(A11yString(e.contentDescription));
            d.push_back(A11yString(e.beforeText));
            d.push_back(int(e.text.size()));
            for (const auto& t : e.text) d.push_back(A11yString(t));
        }
        return IntArray(env, d);
    }

    jboolean A11yPerformAction(JNIEnv* env, jclass, jint id, jint action, jint granularity, jboolean extend, jint selStart,
                               jint selEnd, jstring text, jboolean hasProgress, jfloat progress) {
        if (!g_Host) return false;
        kiln::ui::AccessibilityActionArgs args;
        if (granularity != INT_MIN) args.granularity = granularity;
        args.extendSelection = extend;
        if (selStart != INT_MIN) args.selectionStart = selStart;
        if (selEnd != INT_MIN) args.selectionEnd = selEnd;
        if (text) args.text = kiln::foundation::ToUtf8(FromJava(env, text));
        if (hasProgress) args.progress = progress;
        const bool ok = A11y().PerformAction(id, action, args);
        g_Host->app->RequestFrame();
        return ok;
    }

    // Extra data: [kind, ...] — 0 none; 1 int: value; 2 string: (strings[0]); 3 character
    // locations: count, (present, l, t, r, b) each; 4 shape: type (INT_MIN unset), has rect,
    // l, t, r, b, corner count, corners (float bits), region count, (l, t, r, b) each. Pixels in
    // root coordinates.
    jintArray A11yExtraData(JNIEnv* env, jclass, jint id, jstring key, jint start, jint length) {
        g_A11yStrings.clear();
        std::vector<jint> d;
        if (!g_Host) return IntArray(env, d);
        kiln::ui::AccessibilityActionArgs args;
        if (start != INT_MIN) args.characterStart = start;
        if (length != INT_MIN) args.characterLength = length;
        const auto x = A11y().ExtraData(id, kiln::foundation::ToUtf8(FromJava(env, key)), args);
        if (x.intValue) { d = { 1, *x.intValue }; }
        else if (x.stringValue) { d = { 2 }; A11yString(x.stringValue); }
        else if (!x.rects.empty()) {
            d = { 3, jint(x.rects.size()) };
            for (const auto& r : x.rects)
                d.insert(d.end(), { r ? 1 : 0, FloatBits(r ? r->left : 0), FloatBits(r ? r->top : 0), FloatBits(r ? r->right : 0), FloatBits(r ? r->bottom : 0) });
        } else if (x.shapeType || x.shapeRect || !x.shapeCorners.empty() || !x.shapeRegion.empty()) {
            d = { 4, x.shapeType ? *x.shapeType : INT_MIN, x.shapeRect ? 1 : 0 };
            const auto r = x.shapeRect.value_or(kiln::ui::Rect{});
            d.insert(d.end(), { jint(std::floor(r.left)), jint(std::floor(r.top)), jint(std::ceil(r.right)), jint(std::ceil(r.bottom)) });
            d.push_back(int(x.shapeCorners.size()));
            for (float c : x.shapeCorners) d.push_back(FloatBits(c));
            d.push_back(int(x.shapeRegion.size()));
            for (const auto& q : x.shapeRegion) d.insert(d.end(), { jint(std::floor(q.left)), jint(std::floor(q.top)), jint(std::ceil(q.right)), jint(std::ceil(q.bottom)) });
        } else {
            d = { 0 };
        }
        return IntArray(env, d);
    }

    void A11ySpanClick(JNIEnv*, jclass, jint id, jint index) {
        if (!g_Host) return;
        if (const auto* n = A11y().Find(id)) {
            int k = 0;
            for (const auto& s : n->config.textSpans) {
                if (s.textIndex != 0) continue;
                if (k++ == index) { if (s.onClick) s.onClick(); break; }
            }
        }
        g_Host->app->RequestFrame();
    }

    jboolean A11yHover(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
        return g_Host && A11y().DispatchHover(action, x, y);
    }
    jint A11yFindFocus(JNIEnv*, jclass, jboolean accessibility) {
        return g_Host ? A11y().FindFocus(accessibility) : jint(INT_MIN);
    }
    void A11yCheckSemantics(JNIEnv*, jclass) { if (g_Host) A11y().CheckForSemanticsChanges(); }

    // A Bundle as {String[] keys, int[] kinds, Object[] values}: kind 0 String, 1 boolean and 2 int
    // (their text), 3 String[], 4 byte[] (null allowed).
    jobjectArray JavaBundle(JNIEnv* env, const kiln::ui::Bundle& b) {
        jclass oc = env->FindClass("java/lang/Object");
        jclass sc = env->FindClass("java/lang/String");
        const jsize n = jsize(b.entries.size());
        jobjectArray keys = env->NewObjectArray(n, sc, nullptr);
        jobjectArray values = env->NewObjectArray(n, oc, nullptr);
        std::vector<jint> kinds;
        for (jsize i = 0; i < n; ++i) {
            const auto& [k, v] = b.entries[std::size_t(i)];
            jstring jk = JavaString(env, kiln::foundation::ToUtf16(k));
            env->SetObjectArrayElement(keys, i, jk);
            env->DeleteLocalRef(jk);
            jobject jv = nullptr;
            if (const auto* str = std::get_if<std::string>(&v)) { kinds.push_back(0); jv = JavaString(env, kiln::foundation::ToUtf16(*str)); }
            else if (const auto* bo = std::get_if<bool>(&v)) { kinds.push_back(1); jv = env->NewStringUTF(*bo ? "true" : "false"); }
            else if (const auto* in = std::get_if<std::int32_t>(&v)) { kinds.push_back(2); jv = env->NewStringUTF(std::to_string(*in).c_str()); }
            else if (const auto* list = std::get_if<std::vector<std::string>>(&v)) { kinds.push_back(3); jv = JavaStrings(env, *list); }
            else if (const auto* bytes = std::get_if<std::optional<std::vector<std::uint8_t>>>(&v)) {
                kinds.push_back(4);
                if (*bytes) {
                    jbyteArray a = env->NewByteArray(jsize((*bytes)->size()));
                    env->SetByteArrayRegion(a, 0, jsize((*bytes)->size()), reinterpret_cast<const jbyte*>((*bytes)->data()));
                    jv = a;
                }
            }
            if (jv) {
                env->SetObjectArrayElement(values, i, jv);
                env->DeleteLocalRef(jv);
            }
        }
        jintArray jkinds = env->NewIntArray(n);
        env->SetIntArrayRegion(jkinds, 0, n, kinds.data());
        jobjectArray out = env->NewObjectArray(3, oc, nullptr);
        env->SetObjectArrayElement(out, 0, keys);
        env->SetObjectArrayElement(out, 1, jkinds);
        env->SetObjectArrayElement(out, 2, values);
        for (jobject o : { (jobject)keys, (jobject)jkinds, (jobject)values, (jobject)oc, (jobject)sc }) env->DeleteLocalRef(o);
        return out;
    }

    kiln::ui::Bundle BundleFromJava(JNIEnv* env, jobjectArray b) {
        kiln::ui::Bundle out;
        if (!b) return out;
        auto keys = static_cast<jobjectArray>(env->GetObjectArrayElement(b, 0));
        auto kinds = static_cast<jintArray>(env->GetObjectArrayElement(b, 1));
        auto values = static_cast<jobjectArray>(env->GetObjectArrayElement(b, 2));
        const auto ks = StringsFromJava(env, keys);
        std::vector<jint> kd(std::size_t(env->GetArrayLength(kinds)));
        env->GetIntArrayRegion(kinds, 0, jsize(kd.size()), kd.data());
        auto text = [&](jobject o) {
            const char* c = env->GetStringUTFChars(static_cast<jstring>(o), nullptr);
            std::string r(c);
            env->ReleaseStringUTFChars(static_cast<jstring>(o), c);
            return r;
        };
        for (std::size_t i = 0; i < ks.size() && i < kd.size(); ++i) {
            if (!ks[i]) continue;
            jobject v = env->GetObjectArrayElement(values, jsize(i));
            switch (kd[i]) {
                case 0: if (v) out.Put(*ks[i], text(v)); break;
                case 1: if (v) out.Put(*ks[i], text(v) == "true"); break;
                case 2: if (v) out.Put(*ks[i], std::int32_t(std::stol(text(v)))); break;
                case 3: {
                    std::vector<std::string> list;
                    if (v) for (auto& e : StringsFromJava(env, static_cast<jobjectArray>(v))) list.push_back(e.value_or(""));
                    out.Put(*ks[i], std::move(list));
                    break;
                }
                case 4: {
                    std::optional<std::vector<std::uint8_t>> bytes;
                    if (v) {
                        bytes.emplace(std::size_t(env->GetArrayLength(static_cast<jbyteArray>(v))));
                        env->GetByteArrayRegion(static_cast<jbyteArray>(v), 0, jsize(bytes->size()), reinterpret_cast<jbyte*>(bytes->data()));
                    }
                    out.Put(*ks[i], std::move(bytes));
                    break;
                }
                default: break;
            }
            if (v) env->DeleteLocalRef(v);
        }
        for (jobject o : { (jobject)keys, (jobject)kinds, (jobject)values }) env->DeleteLocalRef(o);
        return out;
    }

    // A GetCredentialRequest as {data bundle, Object[] options}; an option is {type, request
    // bundle, candidate query bundle, "true" / "false" for isSystemProviderRequired}.
    jobjectArray JavaCredentialRequest(JNIEnv* env, const kiln::ui::CredentialRequestData& r) {
        jclass oc = env->FindClass("java/lang/Object");
        jobjectArray options = env->NewObjectArray(jsize(r.options.size()), oc, nullptr);
        for (std::size_t i = 0; i < r.options.size(); ++i) {
            const auto& o = r.options[i];
            jobjectArray jo = env->NewObjectArray(4, oc, nullptr);
            jstring type = JavaString(env, kiln::foundation::ToUtf16(o.type));
            jobjectArray rq = JavaBundle(env, o.requestData), cq = JavaBundle(env, o.candidateQueryData);
            jstring sys = env->NewStringUTF(o.isSystemProviderRequired ? "true" : "false");
            env->SetObjectArrayElement(jo, 0, type);
            env->SetObjectArrayElement(jo, 1, rq);
            env->SetObjectArrayElement(jo, 2, cq);
            env->SetObjectArrayElement(jo, 3, sys);
            for (jobject x : { (jobject)type, (jobject)rq, (jobject)cq, (jobject)sys }) env->DeleteLocalRef(x);
            env->SetObjectArrayElement(options, jsize(i), jo);
            env->DeleteLocalRef(jo);
        }
        jobjectArray out = env->NewObjectArray(2, oc, nullptr);
        jobjectArray data = JavaBundle(env, r.data);
        env->SetObjectArrayElement(out, 0, data);
        env->SetObjectArrayElement(out, 1, options);
        for (jobject x : { (jobject)data, (jobject)options, (jobject)oc }) env->DeleteLocalRef(x);
        return out;
    }

    // populateViewStructure as flat arrays: 11 ints per node (id, parent index, autofill type or
    // -1, value kind or -1, toggle / list value, left, top, right, bottom, flags, max length or
    // -1), 5 strings (value text, hints joined by newlines, content description, text, class
    // name) and the date value.
    jobjectArray AutofillStructure(JNIEnv* env, jclass) {
        if (!g_Host || !g_Host->app->GetOwner().Autofill()) return nullptr;
        const auto nodes = g_Host->app->GetOwner().Autofill()->Structure();
        std::vector<jint> ints;
        std::vector<jlong> dates;
        std::vector<std::optional<std::string>> strs;
        for (const auto& n : nodes) {
            int flags = 0;
            if (n.focused) flags |= 1 | (*n.focused ? 2 : 0);
            if (n.enabled) flags |= 4;
            if (n.clickable) flags |= 8;
            if (n.longClickable) flags |= 16;
            if (n.focusable) flags |= 32;
            if (n.selected) flags |= 64 | (*n.selected ? 128 : 0);
            if (n.checkable) flags |= 256;
            if (n.checked) flags |= 512;
            if (n.sensitive) flags |= 1024;
            if (n.passwordInputType) flags |= 2048;
            const auto& v = n.value;
            ints.insert(ints.end(), { n.id, n.parent, n.autofillType.value_or(-1), v ? jint(v->kind) : -1,
                                      v ? (v->kind == kiln::ui::FillableData::Kind::Toggle ? jint(v->toggle) : jint(v->listIndex)) : 0,
                                      n.bounds.left, n.bounds.top, n.bounds.right, n.bounds.bottom, flags, n.maxTextLength.value_or(-1) });
            dates.push_back(v ? jlong(v->dateMillis) : 0);
            std::optional<std::string> hints;
            for (const auto& hnt : n.hints) hints = hints ? *hints + "\n" + hnt : hnt;
            strs.push_back(v && v->kind == kiln::ui::FillableData::Kind::Text ? std::optional(v->text) : std::nullopt);
            strs.push_back(hints);
            strs.push_back(n.contentDescription);
            strs.push_back(n.text);
            strs.push_back(n.className);
        }
        jclass oc = env->FindClass("java/lang/Object");
        jobjectArray out = env->NewObjectArray(4, oc, nullptr);
        // The credential requests, one per node (null without one).
        jobjectArray requests = env->NewObjectArray(jsize(nodes.size()), oc, nullptr);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i].credentialRequest) continue;
            jobjectArray r = JavaCredentialRequest(env, *nodes[i].credentialRequest);
            env->SetObjectArrayElement(requests, jsize(i), r);
            env->DeleteLocalRef(r);
        }
        env->SetObjectArrayElement(out, 3, requests);
        env->DeleteLocalRef(requests);
        env->DeleteLocalRef(oc);
        jintArray ji = env->NewIntArray(jsize(ints.size()));
        env->SetIntArrayRegion(ji, 0, jsize(ints.size()), ints.data());
        jclass sc = env->FindClass("java/lang/String");
        jobjectArray js = env->NewObjectArray(jsize(strs.size()), sc, nullptr);
        env->DeleteLocalRef(sc);
        for (std::size_t i = 0; i < strs.size(); ++i) {
            if (!strs[i]) continue;
            jstring t = JavaString(env, kiln::foundation::ToUtf16(*strs[i]));
            env->SetObjectArrayElement(js, jsize(i), t);
            env->DeleteLocalRef(t);
        }
        jlongArray jl = env->NewLongArray(jsize(dates.size()));
        env->SetLongArrayRegion(jl, 0, jsize(dates.size()), dates.data());
        env->SetObjectArrayElement(out, 0, ji);
        env->SetObjectArrayElement(out, 1, js);
        env->SetObjectArrayElement(out, 2, jl);
        for (jobject o : { (jobject)ji, (jobject)js, (jobject)jl }) env->DeleteLocalRef(o);
        return out;
    }

    // Scroll capture: the requests in flight.
    std::map<int, kiln::rt::Job> g_ScrollCaptureJobs;
    kiln::ui::ScrollCapture::Candidate g_ScrollCaptureCandidate;

    jintArray ScrollCaptureSearch(JNIEnv* env, jclass) {
        if (!g_Host) return nullptr;
        const auto c = g_Host->app->GetOwner().ScrollCaptureManager().Search();
        if (!c) return nullptr;
        g_ScrollCaptureCandidate = *c;
        const jint v[5] = { c->id, c->viewportBounds.left, c->viewportBounds.top, c->viewportBounds.right, c->viewportBounds.bottom };
        jintArray a = env->NewIntArray(5);
        env->SetIntArrayRegion(a, 0, 5, v);
        return a;
    }

    void ScrollCaptureStart(JNIEnv*, jclass, jint id, jint l, jint t, jint r, jint b) {
        if (!g_Host) return;
        kiln::ui::ScrollCapture::Candidate c{ id, g_ScrollCaptureCandidate.depth, { l, t, r, b } };
        g_Host->app->GetOwner().ScrollCaptureManager().Start(c);
        g_Host->app->RequestFrame();
    }

    kiln::rt::Task<> ScrollCaptureImageTask(int request, kiln::ui::IntRect area) {
        const auto image = co_await g_Host->app->GetOwner().ScrollCaptureManager().ImageRequest(area);
        static jmethodID result = MainEnv()->GetMethodID(MainEnv()->GetObjectClass(g_Host->view), "scrollCaptureImageResult", "(IIIIIII[B)V");
        JNIEnv* e = MainEnv();
        jbyteArray px = nullptr;
        if (!image.rgba.empty()) {
            px = e->NewByteArray(jsize(image.rgba.size()));
            e->SetByteArrayRegion(px, 0, jsize(image.rgba.size()), reinterpret_cast<const jbyte*>(image.rgba.data()));
        }
        e->CallVoidMethod(g_Host->view, result, jint(request), jint(image.rect.left), jint(image.rect.top), jint(image.rect.right),
                          jint(image.rect.bottom), jint(image.width), jint(image.height), px);
        if (px) e->DeleteLocalRef(px);
        kiln::rt::Dispatcher::Main().Post([request] { g_ScrollCaptureJobs.erase(request); });
    }

    void ScrollCaptureImageRequest(JNIEnv*, jclass, jint request, jint l, jint t, jint r, jint b) {
        if (!g_Host) return;
        g_ScrollCaptureJobs[request] = kiln::rt::Launch(kiln::rt::Dispatcher::Main(), ScrollCaptureImageTask(request, { l, t, r, b }));
        g_Host->app->RequestFrame();
    }

    void ScrollCaptureCancel(JNIEnv*, jclass, jint request) { g_ScrollCaptureJobs.erase(request); }

    kiln::rt::Task<> ScrollCaptureEndTask(int request) {
        co_await g_Host->app->GetOwner().ScrollCaptureManager().End();
        static jmethodID ended = MainEnv()->GetMethodID(MainEnv()->GetObjectClass(g_Host->view), "scrollCaptureEnded", "(I)V");
        MainEnv()->CallVoidMethod(g_Host->view, ended, jint(request));
        kiln::rt::Dispatcher::Main().Post([request] { g_ScrollCaptureJobs.erase(request); });
    }

    void ScrollCaptureEnd(JNIEnv*, jclass, jint request) {
        if (!g_Host) return;
        // NonCancellable: the end runs to completion even if a request is cancelled.
        g_ScrollCaptureJobs[request] = kiln::rt::Launch(kiln::rt::Dispatcher::Main(), ScrollCaptureEndTask(request));
        g_Host->app->RequestFrame();
    }

    // OnDragListener.onDrag.
    jboolean DragEvent(JNIEnv* env, jclass, jint action, jfloat x, jfloat y, jobjectArray mimes, jstring label, jobjectArray items, jboolean local,
                       jboolean result) {
        if (!g_Host || action < 1 || action > 6) return JNI_FALSE;
        kiln::ui::DragAndDropEvent e;
        e.action = kiln::ui::DragAndDropEvent::Action(action);
        e.positionInRoot = { x, y };
        for (auto& m : StringsFromJava(env, mimes)) if (m) e.mimeTypes.push_back(*m);
        if (items) {
            const auto parts = StringsFromJava(env, items);
            kiln::ui::ClipEntry clip;
            for (std::size_t i = 0; i + 2 < parts.size(); i += 3) clip.items.push_back({ parts[i], parts[i + 1], parts[i + 2] });
            e.clip = std::move(clip);
            kiln::ui::ClipMetadata meta;
            if (label) {
                const char* c = env->GetStringUTFChars(label, nullptr);
                meta.label = c;
                env->ReleaseStringUTFChars(label, c);
            }
            meta.mimeTypes = e.mimeTypes;
            e.metadata = std::move(meta);
        }
        if (local) e.localState = g_LocalDragState;
        e.result = result;
        static jmethodID permissions = env->GetMethodID(env->GetObjectClass(g_Host->view), "requestDragPermissionsFromNative", "()Z");
        e.requestPermissions = [] { return bool(MainEnv()->CallBooleanMethod(g_Host->view, permissions)); };
        const bool handled = g_Host->app->GetOwner().DragAndDrop().OnDrag(e);
        if (e.action == kiln::ui::DragAndDropEvent::Action::Ended) g_LocalDragState.reset();
        g_Host->app->RequestFrame();
        return jboolean(handled);
    }

    // A pending credential request's outcome.
    void CredentialResult(JNIEnv* env, jclass, jint id, jstring type, jobjectArray data) {
        if (!g_Host || !g_Host->app->GetOwner().Autofill()) return;
        kiln::ui::Credential c;
        if (type) {
            const char* t = env->GetStringUTFChars(type, nullptr);
            c.type = t;
            env->ReleaseStringUTFChars(type, t);
        }
        c.data = BundleFromJava(env, data);
        g_Host->app->GetOwner().Autofill()->OnCredentialResult(id, c);
        g_Host->app->RequestFrame();
    }

    void CredentialError(JNIEnv* env, jclass, jint id, jstring type, jstring message) {
        if (!g_Host || !g_Host->app->GetOwner().Autofill()) return;
        auto str = [&](jstring s) {
            if (!s) return std::string();
            const char* c = env->GetStringUTFChars(s, nullptr);
            std::string r(c);
            env->ReleaseStringUTFChars(s, c);
            return r;
        };
        g_Host->app->GetOwner().Autofill()->OnCredentialError(id, str(type), str(message));
        g_Host->app->RequestFrame();
    }

    // performAutofill: one value (kind: 0 text, 1 toggle, 2 list, 3 date).
    void AutofillPerform(JNIEnv* env, jclass, jint id, jint kind, jstring text, jint intValue, jlong date) {
        if (!g_Host || !g_Host->app->GetOwner().Autofill()) return;
        kiln::ui::FillableData d;
        d.kind = kiln::ui::FillableData::Kind(std::clamp(int(kind), 0, 3));
        if (text) {
            std::u16string u(std::size_t(env->GetStringLength(text)), u'\0');
            env->GetStringRegion(text, 0, jsize(u.size()), reinterpret_cast<jchar*>(u.data()));
            d.text = kiln::foundation::ToUtf8(u);
        }
        d.toggle = intValue != 0;
        d.listIndex = intValue;
        d.dateMillis = date;
        g_Host->app->GetOwner().Autofill()->PerformAutofill(id, d);
        g_Host->app->RequestFrame();
    }
    jboolean A11yFlushBounds(JNIEnv*, jclass) {
        if (!g_Host) return false;
        A11y().FlushBoundsUpdates();
        return A11y().HasPendingBoundsUpdates();
    }
    jboolean A11yCanScroll(JNIEnv*, jclass, jboolean vertical, jint direction, jfloat x, jfloat y) {
        return g_Host && A11y().CanScroll(vertical, direction, { x, y });
    }

    // --- content capture and translation ------------------------------------------------------

    kiln::ui::ContentCaptureManager& Capture() { return A11y().ContentCapture(); }

    void CcStart(JNIEnv*, jclass) {
        if (!g_Host) return;
        Capture().SetClock([] {
            return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        });
        Capture().SetEnabled(true);
        Capture().OnStart();
    }
    void CcStop(JNIEnv*, jclass) { if (g_Host) Capture().OnStop(); }
    void CcCheck(JNIEnv*, jclass) { if (g_Host) Capture().CheckForChanges(); }
    void CcFlush(JNIEnv*, jclass) { if (g_Host) Capture().FlushBuffered(); }

    // Per event: kind (0 appear, 1 disappear, 2 text, 3 flush), id, parent, index, has text
    // size, text size (float bits), left, top, width, height, timestamp (high, low), className,
    // text, contentDescription, testTag (string indices).
    jintArray CcTakeEvents(JNIEnv* env, jclass) {
        g_A11yStrings.clear();
        std::vector<jint> d;
        if (!g_Host) return IntArray(env, d);
        for (const auto& e : Capture().TakeEvents()) {
            d.insert(d.end(), { jint(e.kind), e.id, e.parent, e.index, e.textSizePx ? 1 : 0, FloatBits(e.textSizePx.value_or(0)),
                                e.left, e.top, e.width, e.height, jint(e.timestamp >> 32), jint(e.timestamp & 0xffffffff) });
            d.push_back(A11yString(e.className));
            d.push_back(A11yString(e.text));
            d.push_back(A11yString(e.contentDescription));
            d.push_back(A11yString(e.testTag));
        }
        return IntArray(env, d);
    }

    // The text to translate per id (null: none).
    jobjectArray CcTranslationRequests(JNIEnv* env, jclass, jintArray ids) {
        std::vector<int> v(std::size_t(env->GetArrayLength(ids)));
        env->GetIntArrayRegion(ids, 0, jsize(v.size()), v.data());
        g_A11yStrings.assign(v.size(), std::nullopt);
        if (g_Host)
            for (const auto& [id, text] : Capture().TranslationRequests(v))
                for (std::size_t i = 0; i < v.size(); ++i) if (v[i] == id) g_A11yStrings[i] = text;
        return A11yStrings(env, nullptr);
    }
    void CcTranslationResponses(JNIEnv* env, jclass, jintArray ids, jobjectArray texts) {
        if (!g_Host) return;
        std::vector<int> v(std::size_t(env->GetArrayLength(ids)));
        env->GetIntArrayRegion(ids, 0, jsize(v.size()), v.data());
        std::vector<std::pair<int, std::string>> r;
        for (std::size_t i = 0; i < v.size(); ++i) {
            auto t = static_cast<jstring>(env->GetObjectArrayElement(texts, jsize(i)));
            if (!t) continue;
            r.emplace_back(v[i], kiln::foundation::ToUtf8(FromJava(env, t)));
            env->DeleteLocalRef(t);
        }
        Capture().TranslationResponses(r);
        g_Host->app->RequestFrame();
    }
    // 0 show, 1 hide, 2 clear.
    void CcTranslation(JNIEnv*, jclass, jint mode) {
        if (!g_Host) return;
        if (mode == 0) Capture().OnShowTranslation();
        else if (mode == 1) Capture().OnHideTranslation();
        else Capture().OnClearTranslation();
        g_Host->app->RequestFrame();
    }

    void SetViewConfiguration(JNIEnv*, jclass, jlong longPress, jlong doubleTap, jfloat touchSlop,
                              jfloat maxFling, jfloat minFling, jfloat handwritingSlop, jfloat handwritingLineMargin) {
        if (!g_Host) return;
        auto& c = g_Host->app->GetOwner().Config();
        c.longPressTimeoutMillis = longPress;
        c.doubleTapTimeoutMillis = doubleTap;
        c.touchSlop = touchSlop;
        c.maximumFlingVelocity = maxFling;
        c.minimumFlingVelocity = minFling;
        c.handwritingSlop = handwritingSlop;
        c.handwritingGestureLineMargin = handwritingLineMargin;
    }

    void SurfaceCreated(JNIEnv* env, jclass, jobject surface) {
        if (!g_Host) return;
        g_Host->window = ANativeWindow_fromSurface(env, surface);
        g_Host->surface.Reset(g_Host->window);
        g_Host->app->SurfaceCreated(g_Host->surface);
    }

    void SurfaceChanged(JNIEnv*, jclass, jint width, jint height) {
        if (!g_Host) return;
        g_Host->app->SurfaceResized(std::uint32_t(width), std::uint32_t(height));
    }

    // Activity destroyed: dispose the composition and everything bound to the old view (Compose
    // disposes its composition with the activity; a new activity gets a fresh kiln_main).
    void Destroy(JNIEnv* env, jclass) {
        if (!g_Host) return;
        ALooper* looper = ALooper_forThread();
        ALooper_removeFd(looper, g_Host->eventFd);
        ALooper_removeFd(looper, g_Host->timerFd);
        g_Host->visible = false;
        g_A11yStrings.clear();
        if (g_Host->window) { SurfaceDestroyed(env, nullptr); }
        g_Host->app.reset();
        kiln::ui::BackDispatcher::Main().onHasEnabledChanged = nullptr;
        auto& d = kiln::rt::Dispatcher::Main();
        d.wake = nullptr;
        d.requestFrame = nullptr;
        g_Host->text.reset();
        close(g_Host->eventFd);
        close(g_Host->timerFd);
        env->DeleteGlobalRef(g_Host->view);
        delete g_Host;
        g_Host = nullptr;
    }

    void SurfaceDestroyed(JNIEnv*, jclass) {
        if (!g_Host) return;
        g_Host->app->SurfaceDestroyed();
        g_Host->surface.Reset(nullptr);
        if (g_Host->window) ANativeWindow_release(g_Host->window);
        g_Host->window = nullptr;
    }

}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = env->FindClass("dev/kiln/KilnNative");
    if (!cls) return JNI_ERR;
    static const JNINativeMethod methods[] = {
        { "create", "(Ldev/kiln/KilnView;Ldev/kiln/KilnText;Landroid/content/res/AssetManager;FF[B)V", reinterpret_cast<void*>(&Create) },
        { "setVisible", "(Z)V", reinterpret_cast<void*>(&SetVisible) },
        { "setDensity", "(FF)V", reinterpret_cast<void*>(&SetDensity) },
        { "setFontWeightAdjustment", "(I)V", reinterpret_cast<void*>(&SetFontWeightAdjustment) },
        { "fontsChanged", "()V", reinterpret_cast<void*>(&FontsChanged) },
        { "textToolbarItem", "(I)V", reinterpret_cast<void*>(&TextToolbarItem) },
        { "textToolbarDestroyed", "()V", reinterpret_cast<void*>(&TextToolbarDestroyed) },
        { "classifierResult", "(IZIII[Ljava/lang/String;Z)V", reinterpret_cast<void*>(&ClassifierResult) },
        { "setAnimationScale", "(F)V", reinterpret_cast<void*>(&SetAnimationScale) },
        { "setPasswordVisibility", "(I)V", reinterpret_cast<void*>(&SetPasswordVisibility) },
        { "setLayoutDirection", "(Z)V", reinterpret_cast<void*>(&SetLayoutDirection) },
        { "touchEvent", "(IIJ[I[F[IIIIFF[J[F[F)V", reinterpret_cast<void*>(&TouchEvent) },
        { "keyEvent", "(IIIIIJI)Z", reinterpret_cast<void*>(&KeyEvent) },
        { "keyEventPreIme", "(IIIIIJI)Z", reinterpret_cast<void*>(&KeyEventPreIme) },
        { "rotary", "(FFJI)Z", reinterpret_cast<void*>(&Rotary) },
        { "indirectPointer", "(IIJ[I[F[F[J[FI)Z", reinterpret_cast<void*>(&IndirectPointer) },
        { "indirectFling", "(Z)V", reinterpret_cast<void*>(&IndirectFling) },
        { "focusedRect", "()[F", reinterpret_cast<void*>(&FocusedRect) },
        { "hasFocusableContent", "()Z", reinterpret_cast<void*>(&HasFocusableContent) },
        { "focusSearch", "(IFFFF)[F", reinterpret_cast<void*>(&FocusSearch) },
        { "isBetterFocusCandidate", "(I[F[F[F)Z", reinterpret_cast<void*>(&IsBetterFocusCandidate) },
        { "hostRequestFocus", "(IZFFFFZ)Z", reinterpret_cast<void*>(&HostRequestFocus) },
        { "hostFocusChanged", "(Z)V", reinterpret_cast<void*>(&HostFocusChanged) },
        { "windowFocusChanged", "(Z)V", reinterpret_cast<void*>(&WindowFocusChanged) },
        { "setWindowInsets", "([I)V", reinterpret_cast<void*>(&SetWindowInsets) },
        { "insetsControlReady", "([I)V", reinterpret_cast<void*>(&InsetsControlReady) },
        { "insetsControlEnded", "()V", reinterpret_cast<void*>(&InsetsControlEnded) },
        { "setInsetsAnimation", "(IZ[IFFJ)V", reinterpret_cast<void*>(&SetInsetsAnimation) },
        { "imeBeginBatchEdit", "()Z", reinterpret_cast<void*>(&ImeBeginBatchEdit) },
        { "imeEndBatchEdit", "()Z", reinterpret_cast<void*>(&ImeEndBatchEdit) },
        { "imeCommitText", "(Ljava/lang/String;IZ)V", reinterpret_cast<void*>(&ImeCommitText) },
        { "imeSetComposingText", "(Ljava/lang/String;I[I[Ljava/lang/String;Z)V", reinterpret_cast<void*>(&ImeSetComposingText) },
        { "imeRequestCursorUpdates", "(I)V", reinterpret_cast<void*>(&ImeRequestCursorUpdates) },
        { "imePerformHandwritingGesture", "([FLjava/lang/String;Ljava/lang/String;)I", reinterpret_cast<void*>(&ImePerformHandwritingGesture) },
        { "imePreviewHandwritingGesture", "([F)Z", reinterpret_cast<void*>(&ImePreviewHandwritingGesture) },
        { "imeCancelHandwritingPreview", "()V", reinterpret_cast<void*>(&ImeCancelHandwritingPreview) },
        { "imeCommitContent", "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Z", reinterpret_cast<void*>(&ImeCommitContent) },
        { "imeSetComposingRegion", "(II)V", reinterpret_cast<void*>(&ImeSetComposingRegion) },
        { "imeFinishComposingText", "()V", reinterpret_cast<void*>(&ImeFinishComposingText) },
        { "imeDeleteSurroundingText", "(IIZ)V", reinterpret_cast<void*>(&ImeDeleteSurroundingText) },
        { "imeSetSelection", "(II)V", reinterpret_cast<void*>(&ImeSetSelection) },
        { "imePerformEditorAction", "(I)V", reinterpret_cast<void*>(&ImePerformEditorAction) },
        { "imeGetText", "()Ljava/lang/String;", reinterpret_cast<void*>(&ImeGetText) },
        { "imeGetSelection", "()[I", reinterpret_cast<void*>(&ImeGetSelection) },
        { "backEvent", "(ZFFFIJ)V", reinterpret_cast<void*>(&BackEventJni) },
        { "backCancelled", "()V", reinterpret_cast<void*>(&BackCancelled) },
        { "backInvoked", "()V", reinterpret_cast<void*>(&BackInvoked) },
        { "a11ySetEnabled", "(ZLjava/lang/String;)V", reinterpret_cast<void*>(&A11ySetEnabled) },
        { "a11yNodeInfo", "(I)[I", reinterpret_cast<void*>(&A11yNodeInfo) },
        { "a11yStrings", "()[Ljava/lang/String;", reinterpret_cast<void*>(&A11yStrings) },
        { "a11yTakeEvents", "()[I", reinterpret_cast<void*>(&A11yTakeEvents) },
        { "a11yPerformAction", "(IIIZIILjava/lang/String;ZF)Z", reinterpret_cast<void*>(&A11yPerformAction) },
        { "a11yExtraData", "(ILjava/lang/String;II)[I", reinterpret_cast<void*>(&A11yExtraData) },
        { "a11ySpanClick", "(II)V", reinterpret_cast<void*>(&A11ySpanClick) },
        { "a11yHover", "(IFF)Z", reinterpret_cast<void*>(&A11yHover) },
        { "a11yFindFocus", "(Z)I", reinterpret_cast<void*>(&A11yFindFocus) },
        { "a11yCheckSemantics", "()V", reinterpret_cast<void*>(&A11yCheckSemantics) },
        { "autofillStructure", "()[Ljava/lang/Object;", reinterpret_cast<void*>(&AutofillStructure) },
        { "autofillPerform", "(IILjava/lang/String;IJ)V", reinterpret_cast<void*>(&AutofillPerform) },
        { "scrollCaptureSearch", "()[I", reinterpret_cast<void*>(&ScrollCaptureSearch) },
        { "scrollCaptureStart", "(IIIII)V", reinterpret_cast<void*>(&ScrollCaptureStart) },
        { "scrollCaptureImageRequest", "(IIIII)V", reinterpret_cast<void*>(&ScrollCaptureImageRequest) },
        { "scrollCaptureCancel", "(I)V", reinterpret_cast<void*>(&ScrollCaptureCancel) },
        { "scrollCaptureEnd", "(I)V", reinterpret_cast<void*>(&ScrollCaptureEnd) },
        { "dragEvent", "(IFF[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;ZZ)Z", reinterpret_cast<void*>(&DragEvent) },
        { "credentialResult", "(ILjava/lang/String;[Ljava/lang/Object;)V", reinterpret_cast<void*>(&CredentialResult) },
        { "credentialError", "(ILjava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(&CredentialError) },
        { "a11yFlushBounds", "()Z", reinterpret_cast<void*>(&A11yFlushBounds) },
        { "a11yCanScroll", "(ZIFF)Z", reinterpret_cast<void*>(&A11yCanScroll) },
        { "ccStart", "()V", reinterpret_cast<void*>(&CcStart) },
        { "ccStop", "()V", reinterpret_cast<void*>(&CcStop) },
        { "ccCheck", "()V", reinterpret_cast<void*>(&CcCheck) },
        { "ccFlush", "()V", reinterpret_cast<void*>(&CcFlush) },
        { "ccTakeEvents", "()[I", reinterpret_cast<void*>(&CcTakeEvents) },
        { "ccTranslationRequests", "([I)[Ljava/lang/String;", reinterpret_cast<void*>(&CcTranslationRequests) },
        { "ccTranslationResponses", "([I[Ljava/lang/String;)V", reinterpret_cast<void*>(&CcTranslationResponses) },
        { "ccTranslation", "(I)V", reinterpret_cast<void*>(&CcTranslation) },
        { "setViewConfiguration", "(JJFFFFF)V", reinterpret_cast<void*>(&SetViewConfiguration) },
        { "surfaceCreated", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(&SurfaceCreated) },
        { "surfaceChanged", "(II)V", reinterpret_cast<void*>(&SurfaceChanged) },
        { "surfaceDestroyed", "()V", reinterpret_cast<void*>(&SurfaceDestroyed) },
        { "destroy", "()V", reinterpret_cast<void*>(&Destroy) },
        { "saveState", "()[B", reinterpret_cast<void*>(&SaveState) },
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
