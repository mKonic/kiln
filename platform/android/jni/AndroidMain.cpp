// JNI entry points for dev.kiln.KilnNative and the Choreographer-driven frame loop.
#include "AndroidText.h"

#include "kiln/App.h"

#include <hearth/Log.h>
#include <hearth/surface/AndroidSurface.h>

#include <android/choreographer.h>
#include <android/looper.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <chrono>
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

    void OnVsync(int64_t frameTimeNanos, void*) {
        if (!g_Host) return;
        g_Host->callbackPosted = false;
        if (g_Host->visible && g_Host->window) g_Host->app->Frame(double(frameTimeNanos) * 1e-9);
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

    void Create(JNIEnv* env, jclass, jobject view, jobject text, jfloat density, jfloat fontScale) {
        if (g_Host) {   // Activity recreated: keep the app, swap nothing for M0.
            g_Host->app->SetDensity(density, fontScale);
            return;
        }
        hearth::SetLogSink(&LogToLogcat);
        g_Host = new Host();
        g_Host->text = std::make_unique<kiln::android::AndroidText>(env, text);
        g_Host->app = std::make_unique<kiln::App>(kiln::HostServices{ g_Host->text.get(), &PostFrame, nullptr });
        g_Host->app->SetDensity(density, fontScale);
        g_Host->handle.impl = g_Host->app.get();
        env->GetJavaVM(&g_Host->vm);
        g_Host->view = env->NewGlobalRef(view);
        jclass viewClass = env->GetObjectClass(view);
        g_Host->requestFocus = env->GetMethodID(viewClass, "requestFocus", "()Z");
        env->DeleteLocalRef(viewClass);
        InstallTextInputHost(env);
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

        kiln_main(&g_Host->handle);
    }

    void SetVisible(JNIEnv*, jclass, jboolean visible) {
        g_Host->visible = visible;
        if (visible && g_Host->app->FramePending()) {
            g_Host->callbackPosted = false;
            PostFrame(nullptr);
        }
    }

    void SetAnimationScale(JNIEnv*, jclass, jfloat scale) {
        g_Host->app->SetAnimationScale(scale);
    }

    void SetDensity(JNIEnv*, jclass, jfloat density, jfloat fontScale) {
        g_Host->app->SetDensity(density, fontScale);
        g_Host->app->RequestFrame();
    }

    // MotionEvent -> RawPointerEvent (MotionEventAdapter, spec 02 req. 9). Positions arrive in
    // view pixels, which are surface pixels here (the SurfaceView fills the KilnView).
    void TouchEvent(JNIEnv* env, jclass, jint action, jint actionIndex, jlong eventTime, jintArray ids,
                    jfloatArray xy, jintArray tools, jint buttons, jint classification, jfloat hscroll, jfloat vscroll) {
        enum { DOWN = 0, UP = 1, MOVE = 2, CANCEL = 3, POINTER_DOWN = 5, POINTER_UP = 6, HOVER_MOVE = 7,
               SCROLL = 8, HOVER_ENTER = 9, HOVER_EXIT = 10 };
        kiln::ui::RawPointerEvent ev;
        ev.uptimeMillis = eventTime;
        ev.buttons = std::uint32_t(buttons);
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
            rp.down = !lifting && !hover && action != CANCEL;
            // Compose flips the scroll axes: positive AXIS_VSCROLL scrolls content up (-y).
            if (action == SCROLL) rp.scrollDelta = { -hscroll, -vscroll };
            ev.pointers.push_back(rp);
        }
        env->ReleaseIntArrayElements(ids, id, JNI_ABORT);
        env->ReleaseFloatArrayElements(xy, p, JNI_ABORT);
        env->ReleaseIntArrayElements(tools, tool, JNI_ABORT);
        using T = kiln::ui::PointerEventType;
        switch (action) {
            case DOWN: case POINTER_DOWN: ev.type = T::Press; break;
            case UP: case POINTER_UP: ev.type = T::Release; break;
            case MOVE: case HOVER_MOVE: ev.type = T::Move; break;
            case HOVER_ENTER: ev.type = T::Enter; break;
            case HOVER_EXIT: ev.type = T::Exit; break;
            case SCROLL: ev.type = T::Scroll; break;
            default: break;
        }
        g_Host->app->ProcessPointer(ev);
        ArmTimer();
    }

    jboolean KeyEvent(JNIEnv*, jclass, jint action, jint keyCode, jint metaState, jint unicode, jint repeat, jlong time) {
        kiln::ui::KeyEvent e;
        e.keyCode = keyCode;
        e.type = action == 0 ? kiln::ui::KeyEventType::KeyDown : action == 1 ? kiln::ui::KeyEventType::KeyUp
                                                                           : kiln::ui::KeyEventType::Unknown;
        e.metaState = std::uint32_t(metaState);
        e.unicode = std::uint32_t(unicode);
        e.repeatCount = repeat;
        e.uptimeMillis = time;
        const bool handled = g_Host->app->ProcessKey(e);
        ArmTimer();
        return handled;
    }

    void SetInsets(JNIEnv*, jclass, jint type, jint l, jint t, jint r, jint b) {
        if (type < 0 || type >= jint(kiln::ui::InsetsType::Count)) return;
        g_Host->app->GetOwner().Insets().Set(kiln::ui::InsetsType(type), { l, t, r, b });
    }
    void SetImeState(JNIEnv*, jclass, jboolean visible, jboolean animating) {
        auto& h = g_Host->app->GetOwner().Insets();
        h.SetImeVisible(visible);
        h.SetImeAnimating(animating);
    }

    void HostFocusChanged(JNIEnv*, jclass, jboolean focused) {
        g_Host->app->GetOwner().OnHostFocusChanged(focused);
    }

    // --- text input --------------------------------------------------------------------------

    // EditorInfo.update(imeOptions) (TextInputServiceAndroid:528-682).
    void MapEditorInfo(const kiln::ui::ImeOptions& o, int& inputType, int& imeOptions) {
        using K = kiln::ui::KeyboardType;
        constexpr int TEXT = 1, NUMBER = 2, PHONE = 3;
        switch (o.keyboardType) {
            case K::Text: inputType = TEXT; break;
            case K::Ascii: inputType = TEXT; imeOptions |= int(0x80000000u); break;   // IME_FLAG_FORCE_ASCII
            case K::Number: inputType = NUMBER; break;
            case K::Phone: inputType = PHONE; break;
            case K::Uri: inputType = TEXT | 0x10; break;
            case K::Email: inputType = TEXT | 0x20; break;
            case K::Password: inputType = TEXT | 0x80; break;
            case K::NumberPassword: inputType = NUMBER | 0x10; break;
            case K::Decimal: inputType = NUMBER | 0x2000; break;
            case K::PasswordVisible: inputType = TEXT | 0x90; break;
        }
        using A = kiln::ui::ImeAction;
        int action = 0;
        switch (o.imeAction) {
            case A::Default: action = o.singleLine ? 6 : 0; break;   // DONE / UNSPECIFIED
            case A::None: action = 1; break;
            case A::Go: action = 2; break;
            case A::Search: action = 3; break;
            case A::Send: action = 4; break;
            case A::Next: action = 5; break;
            case A::Done: action = 6; break;
            case A::Previous: action = 7; break;
        }
        imeOptions |= action;
        if ((inputType & 0xF) == TEXT) {
            if (!o.singleLine) {
                inputType |= 0x20000;                                   // TYPE_TEXT_FLAG_MULTI_LINE
                if (o.imeAction == A::Default) imeOptions |= 0x40000000; // IME_FLAG_NO_ENTER_ACTION
            }
            using C = kiln::ui::Capitalization;
            if (o.capitalization == C::Characters) inputType |= 0x1000;
            else if (o.capitalization == C::Words) inputType |= 0x2000;
            else if (o.capitalization == C::Sentences) inputType |= 0x4000;
            if (o.autoCorrect) inputType |= 0x8000;
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
        static jmethodID start = env->GetMethodID(cls, "startInput", "(IIZ)V");
        static jmethodID stop = env->GetMethodID(cls, "stopInput", "()V");
        static jmethodID update = env->GetMethodID(cls, "updateSelection", "(IIII)V");
        static jmethodID restart = env->GetMethodID(cls, "restartInputFromNative", "()V");
        env->DeleteLocalRef(cls);
        kiln::ui::TextInputHost host;
        host.start = [](const kiln::ui::ImeOptions& o, const kiln::ui::TextInputSnapshot&) {
            int type = 0, opts = 0;
            MapEditorInfo(o, type, opts);
            MainEnv()->CallVoidMethod(g_Host->view, start, type, opts, jboolean(o.singleLine));
        };
        host.stop = [] { MainEnv()->CallVoidMethod(g_Host->view, stop); };
        host.updateSelection = [](const kiln::ui::TextInputSnapshot& s) {
            MainEnv()->CallVoidMethod(g_Host->view, update, s.selStart, s.selEnd, s.compStart, s.compEnd);
        };
        host.restartInput = [] { MainEnv()->CallVoidMethod(g_Host->view, restart); };
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

    void ImeCommitText(JNIEnv* env, jclass, jstring t, jint c) { if (auto* s = Session()) { s->CommitText(FromJava(env, t), c); AfterIme(); } }
    void ImeSetComposingText(JNIEnv* env, jclass, jstring t, jint c) { if (auto* s = Session()) { s->SetComposingText(FromJava(env, t), c); AfterIme(); } }
    void ImeSetComposingRegion(JNIEnv*, jclass, jint a, jint b) { if (auto* s = Session()) { s->SetComposingRegion(a, b); AfterIme(); } }
    void ImeFinishComposingText(JNIEnv*, jclass) { if (auto* s = Session()) { s->FinishComposingText(); AfterIme(); } }
    void ImeDeleteSurroundingText(JNIEnv*, jclass, jint b, jint a, jboolean cp) {
        if (auto* s = Session()) { if (cp) s->DeleteSurroundingTextInCodePoints(b, a); else s->DeleteSurroundingText(b, a); AfterIme(); }
    }
    void ImeSetSelection(JNIEnv*, jclass, jint a, jint b) { if (auto* s = Session()) { s->SetSelection(a, b); AfterIme(); } }
    void ImePerformEditorAction(JNIEnv*, jclass, jint action) {
        using A = kiln::ui::ImeAction;
        const A map[] = { A::Default, A::None, A::Go, A::Search, A::Send, A::Next, A::Done, A::Previous };
        if (auto* s = Session()) { s->PerformImeAction(action >= 0 && action < 8 ? map[action] : A::Default); AfterIme(); }
    }
    jstring ImeGetText(JNIEnv* env, jclass) {
        auto* s = Session();
        const std::u16string t = s ? s->Snapshot().text : std::u16string();
        return env->NewString(reinterpret_cast<const jchar*>(t.data()), jsize(t.size()));
    }
    jfloatArray ImeGetCaret(JNIEnv* env, jclass) {
        jfloat v[5] = { 0, 0, 0, 0, 0 };
        if (auto* s = Session()) { auto c = s->CaretRect(); v[0] = c.x; v[1] = c.top; v[2] = c.baseline; v[3] = c.bottom; v[4] = c.valid ? 1.0f : 0.0f; }
        jfloatArray a = env->NewFloatArray(5);
        env->SetFloatArrayRegion(a, 0, 5, v);
        return a;
    }

    jintArray ImeGetSelection(JNIEnv* env, jclass) {
        jint v[4] = { 0, 0, -1, -1 };
        if (auto* s = Session()) { auto snap = s->Snapshot(); v[0] = snap.selStart; v[1] = snap.selEnd; v[2] = snap.compStart; v[3] = snap.compEnd; }
        jintArray a = env->NewIntArray(4);
        env->SetIntArrayRegion(a, 0, 4, v);
        return a;
    }

    void SetViewConfiguration(JNIEnv*, jclass, jlong longPress, jlong doubleTap, jfloat touchSlop,
                              jfloat maxFling, jfloat minFling) {
        auto& c = g_Host->app->GetOwner().Config();
        c.longPressTimeoutMillis = longPress;
        c.doubleTapTimeoutMillis = doubleTap;
        c.touchSlop = touchSlop;
        c.maximumFlingVelocity = maxFling;
        c.minimumFlingVelocity = minFling;
    }

    void SurfaceCreated(JNIEnv* env, jclass, jobject surface) {
        g_Host->window = ANativeWindow_fromSurface(env, surface);
        g_Host->surface.Reset(g_Host->window);
        g_Host->app->SurfaceCreated(g_Host->surface);
    }

    void SurfaceChanged(JNIEnv*, jclass, jint width, jint height) {
        g_Host->app->SurfaceResized(std::uint32_t(width), std::uint32_t(height));
    }

    void SurfaceDestroyed(JNIEnv*, jclass) {
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
        { "create", "(Ldev/kiln/KilnView;Ldev/kiln/KilnText;FF)V", reinterpret_cast<void*>(&Create) },
        { "setVisible", "(Z)V", reinterpret_cast<void*>(&SetVisible) },
        { "setDensity", "(FF)V", reinterpret_cast<void*>(&SetDensity) },
        { "setAnimationScale", "(F)V", reinterpret_cast<void*>(&SetAnimationScale) },
        { "touchEvent", "(IIJ[I[F[IIIFF)V", reinterpret_cast<void*>(&TouchEvent) },
        { "keyEvent", "(IIIIIJ)Z", reinterpret_cast<void*>(&KeyEvent) },
        { "hostFocusChanged", "(Z)V", reinterpret_cast<void*>(&HostFocusChanged) },
        { "setInsets", "(IIIII)V", reinterpret_cast<void*>(&SetInsets) },
        { "setImeState", "(ZZ)V", reinterpret_cast<void*>(&SetImeState) },
        { "imeCommitText", "(Ljava/lang/String;I)V", reinterpret_cast<void*>(&ImeCommitText) },
        { "imeSetComposingText", "(Ljava/lang/String;I)V", reinterpret_cast<void*>(&ImeSetComposingText) },
        { "imeSetComposingRegion", "(II)V", reinterpret_cast<void*>(&ImeSetComposingRegion) },
        { "imeFinishComposingText", "()V", reinterpret_cast<void*>(&ImeFinishComposingText) },
        { "imeDeleteSurroundingText", "(IIZ)V", reinterpret_cast<void*>(&ImeDeleteSurroundingText) },
        { "imeSetSelection", "(II)V", reinterpret_cast<void*>(&ImeSetSelection) },
        { "imePerformEditorAction", "(I)V", reinterpret_cast<void*>(&ImePerformEditorAction) },
        { "imeGetText", "()Ljava/lang/String;", reinterpret_cast<void*>(&ImeGetText) },
        { "imeGetSelection", "()[I", reinterpret_cast<void*>(&ImeGetSelection) },
        { "imeGetCaret", "()[F", reinterpret_cast<void*>(&ImeGetCaret) },
        { "setViewConfiguration", "(JJFFF)V", reinterpret_cast<void*>(&SetViewConfiguration) },
        { "surfaceCreated", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(&SurfaceCreated) },
        { "surfaceChanged", "(II)V", reinterpret_cast<void*>(&SurfaceChanged) },
        { "surfaceDestroyed", "()V", reinterpret_cast<void*>(&SurfaceDestroyed) },
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
