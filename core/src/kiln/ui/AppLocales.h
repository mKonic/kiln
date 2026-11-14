#pragma once

// Per-app language (AppCompatDelegate.setApplicationLocales / getApplicationLocales): the app's
// own language, apart from the system's, as the in-app language picker sets it.
//
//   ui::SetApplicationLocales({ "de-DE" });   // the app in German
//   ui::SetApplicationLocales({});            // back to the system's languages
//
// On Android 13 and later LocaleManager keeps it (and the system's per-app language settings
// show it); before, it is stored by the app and applied to its activities, as AppCompat does.
// The configuration's locales (and every resource and format that reads them) change with it.

#include <kiln/Function.h>

#include <string>
#include <vector>

namespace kiln::ui {

    // BCP 47 language tags, most preferred first; empty: the system's.
    void SetApplicationLocales(std::vector<std::string> languageTags);
    std::vector<std::string> GetApplicationLocales();

    // The platform's (Android installs it). Without one the choice is kept in the process and
    // applied to the app's configuration.
    struct AppLocalesHost {
        kiln::Function<void(const std::vector<std::string>& tags)> set;
        kiln::Function<std::vector<std::string>()> get;
    };
    void SetAppLocalesHost(AppLocalesHost host);

    // Without a platform host: the locales a configuration from the system should carry, and
    // listeners for changes (each app window re-applies its configuration).
    std::vector<std::string> ApplyApplicationLocales(std::vector<std::string> systemLocales);
    int AddApplicationLocalesListener(kiln::Function<void()> listener);
    void RemoveApplicationLocalesListener(int id);

}
