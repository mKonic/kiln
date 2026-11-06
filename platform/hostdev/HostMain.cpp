// Linux development host: a GLFW window driving kiln::App. Not a product target; it exists so
// the core and components run without a device (architecture section 2).
#include "HostText.h"

#include "kiln/App.h"

#include <hearth/Log.h>
#include <hearth/surface/GlfwSurface.h>

#include <GLFW/glfw3.h>

#include <chrono>
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
    void RequestFrame(void*) { g_FrameRequested = true; glfwPostEmptyEvent(); }
}

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(412, 915, "kiln", nullptr, nullptr);

    float sx = 1, sy = 1;
    glfwGetWindowContentScale(window, &sx, &sy);
    // A phone-like density on the desktop: KILN_DENSITY overrides.
    const float density = std::getenv("KILN_DENSITY") ? float(std::atof(std::getenv("KILN_DENSITY"))) : sx;

    auto text = kiln::hostdev::CreateHostText(density, 1.0f);
    kiln::App app({ text.get(), &RequestFrame, nullptr });
    app.SetDensity(density, 1.0f);
    app.Dispatcher().wake = [] { glfwPostEmptyEvent(); };
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

    hearth::GlfwSurface surface(window);
    app.SurfaceCreated(surface);
    int lastW = 0, lastH = 0;
    while (!glfwWindowShouldClose(window)) {
        if (g_FrameRequested) glfwPollEvents();
        else if (auto next = app.Dispatcher().NextTimer()) {
            const double wait = std::chrono::duration<double>(*next - kiln::rt::Dispatcher::Clock::now()).count();
            glfwWaitEventsTimeout(wait > 0 ? wait : 0.0);
        } else glfwWaitEvents();
        app.RunIdle();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w != lastW || h != lastH) { lastW = w; lastH = h; app.SurfaceResized(w, h); }
        if (g_FrameRequested || app.FramePending()) {
            g_FrameRequested = false;
            app.Frame(glfwGetTime());
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
}
