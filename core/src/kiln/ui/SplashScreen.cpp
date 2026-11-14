#include "kiln/ui/SplashScreen.h"

#include <atomic>

namespace kiln::ui {

    namespace {
        std::atomic<bool> g_Keep{ false };
    }
    void KeepSplashScreenOnScreen(bool keep) { g_Keep = keep; }
    bool SplashScreenKeptOnScreen() { return g_Keep; }

}
