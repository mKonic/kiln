// JNI entry points for dev.kiln.KilnNative and the Choreographer-driven frame loop.
#include "AndroidText.h"

namespace kiln::android { void InstallImageDecoder(); void InstallAssetReader(JNIEnv* env, jobject assets); }

#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/ui/Back.h"

#include <climits>
#include <functional>
#include <unordered_map>

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
    JNIEnv* MainEnv();
    void SurfaceDestroyed(JNIEnv*, jclass);

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

    void Create(JNIEnv* env, jclass, jobject view, jobject text, jobject assets, jfloat density, jfloat fontScale, jbyteArray saved) {
        hearth::SetLogSink(&LogToLogcat);
        kiln::android::InstallImageDecoder();
        kiln::android::InstallAssetReader(env, assets);
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
        {
            jclass vc = env->GetObjectClass(view);
            static jmethodID onSem = env->GetMethodID(vc, "onSemanticsChanged", "()V");
            env->DeleteLocalRef(vc);
            g_Host->app->GetOwner().SetOnSemanticsChanged([] { MainEnv()->CallVoidMethod(g_Host->view, onSem); });
            static jmethodID setBack = env->GetMethodID(env->GetObjectClass(view), "setBackEnabled", "(Z)V");
            auto& back = kiln::ui::BackDispatcher::Main();
            back.onHasEnabledChanged = [](bool on) { MainEnv()->CallVoidMethod(g_Host->view, setBack, jboolean(on)); };
            if (back.HasEnabledCallbacks()) back.onHasEnabledChanged(true);
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
                    jfloatArray xy, jintArray tools, jint buttons, jint classification, jfloat hscroll, jfloat vscroll,
                    jlongArray historyTimes, jfloatArray historyXy, jfloatArray pan) {
        if (!g_Host) return;
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
        using T = kiln::ui::PointerEventType;
        // Trackpad two-finger swipe (MotionEventAdapter): CLASSIFICATION_TWO_FINGER_SWIPE (3)
        // starts it and holds until the fingers lift; it is one unpressed mouse pointer at the
        // cursor (where the swipe began) carrying the gesture scroll distance, as Pan events.
        static bool panOngoing = false;
        static kiln::ui::Offset panCursor;
        if (classification == 3) panOngoing = true;
        else if (classification == 5) panOngoing = false;   // PINCH
        if (panOngoing && pan && !ev.pointers.empty()) {
            if (action == DOWN) panCursor = ev.pointers[0].position;
            kiln::ui::RawPointer rp = ev.pointers[0];
            rp.position = panCursor;
            rp.down = false;
            rp.type = kiln::ui::PointerType::Mouse;
            const jsize pn = env->GetArrayLength(pan);
            jfloat* pv = env->GetFloatArrayElements(pan, nullptr);
            rp.panOffset = { pv[0], pv[1] };
            for (std::size_t h = 0; h < rp.historical.size() && jsize(3 + 2 * h) < pn; ++h) {
                rp.historical[h].position = panCursor;
                rp.historical[h].panOffset = { pv[2 + 2 * h], pv[3 + 2 * h] };
            }
            env->ReleaseFloatArrayElements(pan, pv, JNI_ABORT);
            ev.pointers = { rp };
            switch (action) {
                case DOWN: case POINTER_DOWN: ev.type = T::PanStart; break;
                case UP: case POINTER_UP: ev.type = T::PanEnd; break;
                case MOVE: ev.type = T::PanMove; break;
                default: ev.type = T::Move; break;
            }
            if (action == UP || action == CANCEL) panOngoing = false;
            g_Host->app->ProcessPointer(ev);
            ArmTimer();
            return;
        }
        if (action == UP || action == CANCEL) panOngoing = false;
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
        if (!g_Host) return {};
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
        if (!g_Host) return;
        if (type < 0 || type >= jint(kiln::ui::InsetsType::Count)) return;
        g_Host->app->GetOwner().Insets().Set(kiln::ui::InsetsType(type), { l, t, r, b });
    }
    void SetImeState(JNIEnv*, jclass, jboolean visible, jboolean animating) {
        if (!g_Host) return;
        auto& h = g_Host->app->GetOwner().Insets();
        h.SetImeVisible(visible);
        h.SetImeAnimating(animating);
    }

    void HostFocusChanged(JNIEnv*, jclass, jboolean focused) {
        if (!g_Host) return;
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
    jfloatArray ImeGetCaret(JNIEnv* env, jclass) {
        if (!g_Host) return {};
        jfloat v[5] = { 0, 0, 0, 0, 0 };
        if (auto* s = Session()) { auto c = s->CaretRect(); v[0] = c.x; v[1] = c.top; v[2] = c.baseline; v[3] = c.bottom; v[4] = c.valid ? 1.0f : 0.0f; }
        jfloatArray a = env->NewFloatArray(5);
        env->SetFloatArrayRegion(a, 0, 5, v);
        return a;
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

    // --- semantics -------------------------------------------------------------------------

    struct SemCache {
        std::uint64_t version = ~0ull;
        kiln::ui::SemanticsNode root;
        std::unordered_map<int, const kiln::ui::SemanticsNode*> byId;
        std::unordered_map<int, int> parent;
    };
    SemCache g_Sem;

    void IndexSem(const kiln::ui::SemanticsNode& n, int parent) {
        g_Sem.byId[n.id] = &n;
        g_Sem.parent[n.id] = parent;
        for (const auto& c : n.children) IndexSem(c, n.id);
    }
    const kiln::ui::SemanticsNode* SemFind(int id) {
        auto& owner = g_Host->app->GetOwner();
        if (g_Sem.version != owner.SemanticsVersion()) {
            g_Sem.version = owner.SemanticsVersion();
            g_Sem.root = owner.BuildSemanticsTree();
            g_Sem.byId.clear();
            g_Sem.parent.clear();
            IndexSem(g_Sem.root, -1);
        }
        auto it = g_Sem.byId.find(id);
        return it == g_Sem.byId.end() ? nullptr : it->second;
    }
    std::string Join(const std::vector<std::string>& v) {
        std::string out;
        for (const auto& s : v) { if (!out.empty()) out += ' '; out += s; }
        return out;
    }

    jintArray SemNode(JNIEnv* env, jclass, jint id) {
        if (!g_Host) return {};
        std::vector<jint> d(11, 0);
        if (const auto* n = SemFind(id)) {
            const auto& c = n->config;
            d[0] = 1;
            d[1] = g_Sem.parent.count(id) ? g_Sem.parent[id] : -1;
            if (c.textSelection) { d[2] = c.textSelection->first; d[3] = c.textSelection->second; }
            int f = 0;
            if (c.onClick) f |= 1;
            if (c.focused) { f |= 2; if (*c.focused) f |= 4; }
            if (c.editableText) f |= 8;
            if (c.heading) f |= 16;
            if (c.disabled) f |= 32;
            if (c.password) f |= 64;
            if (c.selected) { f |= 128; if (*c.selected) f |= 256; }
            if (c.toggleableState) { f |= 512; if (*c.toggleableState == 1) f |= 1024; }
            if (c.onLongClick) f |= 2048;
            d[4] = f;
            d[5] = jint(c.role);
            d[6] = jint(n->boundsInRoot.left); d[7] = jint(n->boundsInRoot.top);
            d[8] = jint(n->boundsInRoot.right); d[9] = jint(n->boundsInRoot.bottom);
            d[10] = jint(n->children.size());
            for (const auto& ch : n->children) d.push_back(ch.id);
            // After the children: scroll flags, collection rows, collection columns.
            int sf = 0;
            auto axis = [&](const std::optional<kiln::ui::ScrollAxisRange>& r, int present, int fwd, int back, int scrollable) {
                if (!r || !c.scrollBy) return;
                sf |= present;
                const float v = r->value ? r->value() : 0, max = r->maxValue ? r->maxValue() : 0;
                if (max > 0) sf |= scrollable;
                // canScrollForward / canScrollBackward (reverseScrolling swaps them).
                if ((v < max && !r->reverseScrolling) || (v > 0 && r->reverseScrolling)) sf |= fwd;
                if ((v > 0 && !r->reverseScrolling) || (v < max && r->reverseScrolling)) sf |= back;
            };
            axis(c.horizontalScrollAxisRange, 1, 4, 8, 256);
            axis(c.verticalScrollAxisRange, 2, 16, 32, 256);
            const auto collection = kiln::ui::ResolveCollectionInfo(*n);
            const auto item = kiln::ui::ResolveCollectionItemInfo(*n, d[1] == -1 ? nullptr : SemFind(d[1]));
            if (collection) sf |= 512;
            if (c.pageUp) sf |= 1024;
            if (c.pageDown) sf |= 2048;
            if (c.pageLeft) sf |= 4096;
            if (c.pageRight) sf |= 8192;
            if (item) sf |= 16384;
            d.push_back(sf);
            d.push_back(collection ? collection->first : 0);
            d.push_back(collection ? collection->second : 0);
            // Then the item's row index, row span, column index, column span.
            d.push_back(item ? item->rowIndex : 0);
            d.push_back(item ? item->rowSpan : 0);
            d.push_back(item ? item->columnIndex : 0);
            d.push_back(item ? item->columnSpan : 0);
        }
        jintArray a = env->NewIntArray(jsize(d.size()));
        env->SetIntArrayRegion(a, 0, jsize(d.size()), d.data());
        return a;
    }

    // Scroll containers that moved since the last call: id, flags (1 x, 2 y), scrollX, maxScrollX,
    // scrollY, maxScrollY, deltaX, deltaY each.
    kiln::ui::ScrollEventTracker g_ScrollEvents;
    jintArray SemScrollEvents(JNIEnv* env, jclass) {
        std::vector<jint> d;
        if (g_Host) {
            if (const auto* root = SemFind(-1)) {
                for (const auto& e : g_ScrollEvents.Collect(*root, g_Host->app->GetOwner())) {
                    d.push_back(e.id);
                    d.push_back((e.x ? 1 : 0) | (e.y ? 2 : 0));
                    d.push_back(e.x ? e.x->first : 0); d.push_back(e.x ? e.x->second : 0);
                    d.push_back(e.y ? e.y->first : 0); d.push_back(e.y ? e.y->second : 0);
                    d.push_back(e.deltaX); d.push_back(e.deltaY);
                }
            }
        }
        jintArray a = env->NewIntArray(jsize(d.size()));
        env->SetIntArrayRegion(a, 0, jsize(d.size()), d.data());
        return a;
    }

    jobjectArray SemStrings(JNIEnv* env, jclass, jint id) {
        if (!g_Host) return {};
        std::string v[6];
        if (const auto* n = SemFind(id)) {
            const auto& c = n->config;
            v[0] = Join(c.text);
            v[1] = Join(c.contentDescription);
            v[2] = c.editableText.value_or("");
            v[3] = c.stateDescription.value_or("");
            v[4] = c.paneTitle.value_or("");
            v[5] = c.onClickLabel.value_or("");
        }
        jclass str = env->FindClass("java/lang/String");
        jobjectArray out = env->NewObjectArray(6, str, nullptr);
        for (int i = 0; i < 6; ++i) {
            const std::u16string u = kiln::foundation::ToUtf16(v[i]);
            jstring js = env->NewString(reinterpret_cast<const jchar*>(u.data()), jsize(u.size()));
            env->SetObjectArrayElement(out, i, js);
            env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(str);
        return out;
    }

    jboolean SemAction(JNIEnv* env, jclass, jint id, jint action, jstring arg) {
        if (!g_Host) return {};
        const auto* n = SemFind(id);
        if (!n) return false;
        const auto& c = n->config;
        bool ok = false;
        switch (action) {
            case 0: ok = c.onClick && c.onClick(); break;
            case 1: ok = c.onLongClick && c.onLongClick(); break;
            case 2: ok = c.requestFocus && c.requestFocus(); break;
            case 3: ok = c.setText && c.setText(kiln::foundation::ToUtf8(FromJava(env, arg))); break;
            case 5: {
                // Page actions: arg u d l r.
                const std::string dir = kiln::foundation::ToUtf8(FromJava(env, arg));
                const char k = dir.empty() ? ' ' : dir[0];
                const auto& f = k == 'u' ? c.pageUp : k == 'd' ? c.pageDown : k == 'l' ? c.pageLeft : c.pageRight;
                ok = f && f();
                break;
            }
            case 4: {
                // Scroll actions: arg is f(orward) b(ackward) u d l r. The amount is the viewport
                // (GetScrollViewportLength, else the node's size), signed like Compose's delegate.
                if (!c.scrollBy) break;
                const std::string dir = kiln::foundation::ToUtf8(FromJava(env, arg));
                const char k = dir.empty() ? 'f' : dir[0];
                const bool fwd = k == 'f', back = k == 'b';
                auto canScroll = [](const kiln::ui::ScrollAxisRange& r, float amount) {
                    const float v = r.value ? r.value() : 0, max = r.maxValue ? r.maxValue() : 0;
                    return (amount < 0 && v > 0) || (amount > 0 && v < max);
                };
                const auto& b = n->boundsInRoot;
                if (const auto& r = c.horizontalScrollAxisRange; r && (k == 'l' || k == 'r' || fwd || back)) {
                    float amount = c.scrollViewportLength ? c.scrollViewportLength() : b.right - b.left;
                    if (k == 'l' || back) amount = -amount;
                    if (r->reverseScrolling) amount = -amount;
                    if (canScroll(*r, amount)) {
                        // A pageable node pages instead (PageLeft / PageRight).
                        if (c.pageLeft || c.pageRight) ok = amount > 0 ? (c.pageRight && c.pageRight()) : (c.pageLeft && c.pageLeft());
                        else ok = c.scrollBy(amount, 0);
                        break;
                    }
                }
                if (const auto& r = c.verticalScrollAxisRange; r && (k == 'u' || k == 'd' || fwd || back)) {
                    float amount = c.scrollViewportLength ? c.scrollViewportLength() : b.bottom - b.top;
                    if (k == 'u' || back) amount = -amount;
                    if (r->reverseScrolling) amount = -amount;
                    if (canScroll(*r, amount)) {
                        if (c.pageUp || c.pageDown) ok = amount > 0 ? (c.pageDown && c.pageDown()) : (c.pageUp && c.pageUp());
                        else ok = c.scrollBy(0, amount);
                    }
                }
                break;
            }
            default: break;
        }
        g_Host->app->RequestFrame();
        return ok;
    }

    jint SemHitTest(JNIEnv*, jclass, jfloat x, jfloat y) {
        if (!g_Host) return {};
        SemFind(-1);
        // Deepest node whose bounds contain the point, last child first (drawn on top).
        std::function<int(const kiln::ui::SemanticsNode&)> hit = [&](const kiln::ui::SemanticsNode& n) -> int {
            for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
                if (!it->boundsInRoot.Contains({ x, y })) continue;
                const int deeper = hit(*it);
                return deeper != INT_MIN ? deeper : it->id;
            }
            return INT_MIN;
        };
        return hit(g_Sem.root);
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
        g_Sem = {};
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
        { "setAnimationScale", "(F)V", reinterpret_cast<void*>(&SetAnimationScale) },
        { "setLayoutDirection", "(Z)V", reinterpret_cast<void*>(&SetLayoutDirection) },
        { "touchEvent", "(IIJ[I[F[IIIFF[J[F[F)V", reinterpret_cast<void*>(&TouchEvent) },
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
        { "backEvent", "(ZFFFIJ)V", reinterpret_cast<void*>(&BackEventJni) },
        { "backCancelled", "()V", reinterpret_cast<void*>(&BackCancelled) },
        { "backInvoked", "()V", reinterpret_cast<void*>(&BackInvoked) },
        { "semNode", "(I)[I", reinterpret_cast<void*>(&SemNode) },
        { "semScrollEvents", "()[I", reinterpret_cast<void*>(&SemScrollEvents) },
        { "semStrings", "(I)[Ljava/lang/String;", reinterpret_cast<void*>(&SemStrings) },
        { "semAction", "(IILjava/lang/String;)Z", reinterpret_cast<void*>(&SemAction) },
        { "semHitTest", "(FF)I", reinterpret_cast<void*>(&SemHitTest) },
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
