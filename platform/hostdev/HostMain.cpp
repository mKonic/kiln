// Linux development host: a GLFW window driving kiln::App. Not a product target; it exists so
// the core and components run without a device (architecture section 2).
#include "HostText.h"

#include "kiln/App.h"

#include <hearth/Log.h>
#include <hearth/surface/GlfwSurface.h>

#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <execinfo.h>
#include <memory>
#include <cstdio>
#include <cstdlib>

namespace {
    kiln::App* g_App = nullptr;
    bool g_MouseDown = false;
    double g_MouseX = 0, g_MouseY = 0;
    float g_ContentScale = 1;

    void SendMouse(kiln::ui::PointerEventType type, kiln::ui::Offset scroll = {}) {
        kiln::ui::RawPointerEvent ev;
        ev.uptimeMillis = std::int64_t(glfwGetTime() * 1000);
        ev.type = type;
        ev.buttons = g_MouseDown ? 1u : 0u;
        kiln::ui::RawPointer p;
        p.id = 0;
        p.position = { float(g_MouseX) * g_ContentScale, float(g_MouseY) * g_ContentScale };
        p.down = g_MouseDown;
        p.type = kiln::ui::PointerType::Mouse;
        p.scrollDelta = scroll;
        ev.pointers.push_back(p);
        g_App->ProcessPointer(ev);
    }

    bool g_FrameRequested = true;
    // One empty event until the loop wakes: on Wayland each is a wl_display.sync, and a burst
    // of them (a frame posting hundreds of tasks) overflows the socket, which GLFW reports as
    // the window closing.
    std::atomic<bool> g_WakePending{ false };
    void Wake() { if (!g_WakePending.exchange(true)) glfwPostEmptyEvent(); }
    void RequestFrame(void*) { g_FrameRequested = true; Wake(); }
}

namespace kiln::hostdev { void InstallImageDecoder(); void InstallAssetReader(); }

int main() {
    kiln::hostdev::InstallImageDecoder();
    kiln::hostdev::InstallAssetReader();
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // KILN_WINDOW=WxH: the window size (default 412x915).
    int winW = 412, winH = 915;
    if (const char* ws = std::getenv("KILN_WINDOW")) std::sscanf(ws, "%dx%d", &winW, &winH);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "kiln", nullptr, nullptr);

    float sx = 1, sy = 1;
    glfwGetWindowContentScale(window, &sx, &sy);
    // A phone-like density on the desktop: KILN_DENSITY overrides.
    const float density = std::getenv("KILN_DENSITY") ? float(std::atof(std::getenv("KILN_DENSITY"))) : sx;

    auto text = kiln::hostdev::CreateHostText(density, 1.0f);
    // Owned here so it (and its swapchain) goes before the window's Wayland surface.
    auto appOwner = std::make_unique<kiln::App>(kiln::HostServices{ text.get(), &RequestFrame, nullptr });
    kiln::App& app = *appOwner;
    app.SetDensity(density, 1.0f);
    app.Dispatcher().wake = [] { Wake(); };
    kiln_app handle{ &app };
    kiln_main(&handle);

    g_App = &app;
    g_ContentScale = sx;
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        g_MouseX = x; g_MouseY = y;
        SendMouse(kiln::ui::PointerEventType::Move);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        g_MouseDown = action == GLFW_PRESS;
        SendMouse(g_MouseDown ? kiln::ui::PointerEventType::Press : kiln::ui::PointerEventType::Release);
    });
    glfwSetScrollCallback(window, [](GLFWwindow*, double dx, double dy) {
        SendMouse(kiln::ui::PointerEventType::Scroll, { float(-dx), float(-dy) });
    });

    glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int mods) {
        namespace k = kiln::ui::keys;
        int code = 0;
        switch (key) {
            case GLFW_KEY_TAB: code = k::Tab; break;
            case GLFW_KEY_ENTER: code = k::Enter; break;
            case GLFW_KEY_KP_ENTER: code = k::NumpadEnter; break;
            case GLFW_KEY_SPACE: code = k::Space; break;
            case GLFW_KEY_ESCAPE: code = k::Escape; break;
            case GLFW_KEY_LEFT: code = k::DpadLeft; break;
            case GLFW_KEY_RIGHT: code = k::DpadRight; break;
            case GLFW_KEY_UP: code = k::DpadUp; break;
            case GLFW_KEY_DOWN: code = k::DpadDown; break;
            case GLFW_KEY_PAGE_UP: code = k::PageUp; break;
            case GLFW_KEY_PAGE_DOWN: code = k::PageDown; break;
            case GLFW_KEY_BACKSPACE: code = k::Del; break;
            default: return;
        }
        kiln::ui::KeyEvent e;
        e.keyCode = code;
        e.type = action == GLFW_RELEASE ? kiln::ui::KeyEventType::KeyUp : kiln::ui::KeyEventType::KeyDown;
        e.repeatCount = action == GLFW_REPEAT ? 1 : 0;
        e.metaState = (mods & GLFW_MOD_SHIFT ? kiln::ui::kMetaShift : 0) | (mods & GLFW_MOD_CONTROL ? kiln::ui::kMetaCtrl : 0);
        e.uptimeMillis = std::int64_t(glfwGetTime() * 1000);
        g_App->ProcessKey(e);
    });

    // Typed characters go to the focused text field, as an IME's commitText would.
    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
        kiln::ui::TextInputSession* session = g_App->GetOwner().ActiveTextInput();
        if (!session) return;
        std::u16string text;
        if (codepoint >= 0x10000) {
            codepoint -= 0x10000;
            text += char16_t(0xD800 + (codepoint >> 10));
            text += char16_t(0xDC00 + (codepoint & 0x3FF));
        } else {
            text += char16_t(codepoint);
        }
        session->CommitText(text, 1);
    });

    glfwSetWindowCloseCallback(window, [](GLFWwindow*) { std::fprintf(stderr, "kiln: the window was asked to close at %.3f s\n", glfwGetTime()); });

    hearth::GlfwSurface surface(window);
    app.SurfaceCreated(surface);
    int lastW = 0, lastH = 0;
    // KILN_FIXED_FRAME_MS: frame n gets time n * ms, so animations land on the same frames in
    // every run (golden renders).
    const double fixedFrame = std::getenv("KILN_FIXED_FRAME_MS") ? std::atof(std::getenv("KILN_FIXED_FRAME_MS")) / 1000.0 : 0.0;
    std::int64_t frameCount = 0;
    const bool trace = std::getenv("KILN_TRACE_FRAMES") != nullptr;   // a line per frame (finding a hang)
    // With tracing, SIGUSR1 prints where the main thread is (a hang inside a frame).
    if (trace) std::signal(SIGUSR1, [](int) {
        void* frames[64];
        const int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, 2);
    });
    // With a fixed frame clock the dispatcher runs on virtual time too: each frame advances it by
    // one interval, and when only timers wait it jumps to the next one.
    auto& dispatcher = app.Dispatcher();
    if (fixedFrame > 0) dispatcher.SetVirtualTime(true);
    const auto frameStep = std::chrono::duration_cast<kiln::rt::Dispatcher::Clock::duration>(std::chrono::duration<double>(fixedFrame));
    while (!glfwWindowShouldClose(window)) {
        if (g_FrameRequested || (fixedFrame > 0 && app.FramePending())) glfwPollEvents();
        else if (fixedFrame > 0 && dispatcher.NextTimer()) {
            dispatcher.AdvanceTimeBy(*dispatcher.NextTimer() - dispatcher.Now());
            glfwPollEvents();
        } else if (auto next = app.Dispatcher().NextTimer()) {
            const double wait = std::chrono::duration<double>(*next - app.Dispatcher().Now()).count();
            glfwWaitEventsTimeout(wait > 0 ? wait : 0.0);
        } else glfwWaitEvents();
        g_WakePending = false;
        app.RunIdle();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w != lastW || h != lastH) { lastW = w; lastH = h; app.SurfaceResized(w, h); }
        if (g_FrameRequested || app.FramePending()) {
            g_FrameRequested = false;
            if (trace) std::fprintf(stderr, "kiln: frame %lld at %.3f s\n", static_cast<long long>(frameCount), glfwGetTime());
            if (fixedFrame > 0) {
                dispatcher.AdvanceTimeBy(frameStep);
                ++frameCount;
            }
            app.Frame(fixedFrame > 0 ? std::chrono::duration<double>(dispatcher.Now().time_since_epoch()).count() : glfwGetTime());
        }
    }
    g_App = nullptr;
    appOwner.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
}
