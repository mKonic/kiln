#pragma once

// The splash screen (API 31's SplashScreen, androidx core-splashscreen's
// setKeepOnScreenCondition): kiln keeps it up until the content's first frame is on screen,
// and longer while the app asks. Its look comes from kiln_add_app's SPLASH_* options.
//
//   ui::KeepSplashScreenOnScreen(true);                       // in kiln_main
//   ... once the first data is loaded:
//   ui::KeepSplashScreenOnScreen(false);

namespace kiln::ui {

    // While true, the splash screen stays (at most until the app's content draws without it,
    // which the system ends after a few seconds). Main thread.
    void KeepSplashScreenOnScreen(bool keep);
    bool SplashScreenKeptOnScreen();

}
