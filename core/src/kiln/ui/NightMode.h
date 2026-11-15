#pragma once

// The app's own light / dark choice (AppCompatDelegate.setDefaultNightMode): night mode for the
// app apart from the system's, as an in-app theme setting picks it.
//
//   ui::SetDefaultNightMode(ui::NightMode::Yes);            // always dark
//   ui::SetDefaultNightMode(ui::NightMode::FollowSystem);   // back to the system's
//
// The configuration's night bits (Configuration::IsNightModeActive, IsSystemInDarkTheme, and
// every resource that reads them) follow it, in the running activity and in those started
// after. As in AppCompat it is not stored: an app keeps its setting and sets it as it starts.

#include <kiln/Function.h>

namespace kiln::ui {

    // AppCompatDelegate.MODE_NIGHT_*.
    enum class NightMode {
        FollowSystem = -1,
        // Dark between sunset and sunrise: from the last known location when the app may read
        // it, else from 22:00 to 6:00 (TwilightManager).
        AutoTime = 0,
        No = 1,
        Yes = 2,
        // Dark while battery saver is on.
        AutoBattery = 3,
    };

    void SetDefaultNightMode(NightMode mode);
    NightMode GetDefaultNightMode();

    // The platform's (Android installs it). Without one the mode is kept in the process and
    // applied to the app's configuration (AutoBattery is then No: there is no battery saver).
    struct NightModeHost {
        kiln::Function<void(NightMode)> set;
    };
    void SetNightModeHost(NightModeHost host);

    // Without a platform host: the uiMode a configuration from the system should carry, and
    // listeners for changes (each app window re-applies its configuration).
    int ApplyNightMode(int systemUiMode);
    int AddNightModeListener(kiln::Function<void()> listener);
    void RemoveNightModeListener(int id);

    // TwilightManager's fallback without a location: night from 22:00 to 6:00 local time.
    bool IsNightByHour(int hourOfDay);

}
