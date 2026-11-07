// JNI entry points for dev.kiln.KilnNative and the Choreographer-driven frame loop.
#include "AndroidText.h"

namespace kiln::android { void InstallImageDecoder(); void InstallAssetReader(JNIEnv* env, jobject assets); }

#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/text/AnnotatedString.h"
#include "kiln/ui/Back.h"
#include "kiln/ui/Accessibility.h"
#include <cstring>

#include <climits>
#include <cmath>
#include <functional>
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

    void ArmTimer();
    void InstallTextInputHost(JNIEnv* env);
    JNIEnv* MainEnv();
    void SurfaceDestroyed(JNIEnv*, jclass);

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
            static jmethodID haptic = env->GetMethodID(vc, "performHaptic", "(I)V");
            static jmethodID showBar = env->GetMethodID(vc, "showTextToolbar", "(IIIII)V");
            static jmethodID hideBar = env->GetMethodID(vc, "hideTextToolbar", "()V");
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
            owner.SetClipboardHost(std::move(clip));
            owner.SetHapticHost([](int constant) { MainEnv()->CallVoidMethod(g_Host->view, haptic, jint(constant)); });
            kiln::ui::Owner::TextToolbarHost bar;
            bar.show = [](kiln::ui::Rect r, unsigned items) {
                MainEnv()->CallVoidMethod(g_Host->view, showBar, jint(std::floor(r.left)), jint(std::floor(r.top)), jint(std::ceil(r.right)),
                                          jint(std::ceil(r.bottom)), jint(items));
            };
            bar.hide = [] { MainEnv()->CallVoidMethod(g_Host->view, hideBar); };
            owner.SetTextToolbarHost(std::move(bar));
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
        if (!g_Host) return;
        g_Host->app->GetOwner().OnTextToolbarItem(unsigned(item));
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
        static jmethodID start = env->GetMethodID(cls, "startInput", "(IIZLjava/lang/String;)V");
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
            e->CallVoidMethod(g_Host->view, start, type, opts, jboolean(o.singleLine), jt);
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
                              jfloat maxFling, jfloat minFling) {
        if (!g_Host) return;
        auto& c = g_Host->app->GetOwner().Config();
        c.longPressTimeoutMillis = longPress;
        c.doubleTapTimeoutMillis = doubleTap;
        c.touchSlop = touchSlop;
        c.maximumFlingVelocity = maxFling;
        c.minimumFlingVelocity = minFling;
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
        { "setAnimationScale", "(F)V", reinterpret_cast<void*>(&SetAnimationScale) },
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
        { "setViewConfiguration", "(JJFFF)V", reinterpret_cast<void*>(&SetViewConfiguration) },
        { "surfaceCreated", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(&SurfaceCreated) },
        { "surfaceChanged", "(II)V", reinterpret_cast<void*>(&SurfaceChanged) },
        { "surfaceDestroyed", "()V", reinterpret_cast<void*>(&SurfaceDestroyed) },
        { "destroy", "()V", reinterpret_cast<void*>(&Destroy) },
        { "saveState", "()[B", reinterpret_cast<void*>(&SaveState) },
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
