// Linux development host: a GLFW window driving kiln::App. Not a product target; it exists so
// the core and components run without a device (architecture section 2).
#include "HostText.h"

#include "kiln/App.h"

#include <hearth/Log.h>
#include <hearth/surface/GlfwSurface.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

namespace {
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
    kiln_app handle{ &app };
    kiln_main(&handle);

    hearth::GlfwSurface surface(window);
    app.SurfaceCreated(surface);
    int lastW = 0, lastH = 0;
    while (!glfwWindowShouldClose(window)) {
        if (g_FrameRequested) glfwPollEvents(); else glfwWaitEvents();
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
