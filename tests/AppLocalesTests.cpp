// Per-app language: the platform's LocaleManager when there is one, else the app's
// configuration carries the chosen locales.
#include "Check.h"
#include "FakeText.h"

#include "kiln/App.h"
#include "kiln/ui/AppLocales.h"

using namespace kiln;

TEST(AppLocalesOverrideTheConfigurationWithoutAPlatform) {
    test::FakeText text;
    App app({ .text = &text });
    ui::Configuration system;
    system.locales = { "en-US", "fr-FR" };
    app.SetConfiguration(system);
    CHECK(app.GetConfiguration().locales == system.locales && ui::GetApplicationLocales().empty());
    ui::SetApplicationLocales({ "de-DE" });
    CHECK(app.GetConfiguration().locales == std::vector<std::string>{ "de-DE" });
    CHECK(ui::GetApplicationLocales() == std::vector<std::string>{ "de-DE" });
    // A later system change keeps the app's choice; clearing it follows the system again.
    system.locales = { "es-ES" };
    app.SetConfiguration(system);
    CHECK(app.GetConfiguration().locales == std::vector<std::string>{ "de-DE" });
    ui::SetApplicationLocales({});
    CHECK(app.GetConfiguration().locales == std::vector<std::string>{ "es-ES" });
}

TEST(AppLocalesGoToThePlatform) {
    std::vector<std::string> stored;
    ui::SetAppLocalesHost({ .set = [&](const std::vector<std::string>& t) { stored = t; }, .get = [&] { return stored; } });
    test::FakeText text;
    App app({ .text = &text });
    ui::Configuration system;
    system.locales = { "en-US" };
    app.SetConfiguration(system);
    ui::SetApplicationLocales({ "ja-JP", "en-US" });
    // The platform applies it (a configuration change follows); kiln does not override.
    CHECK(stored == (std::vector<std::string>{ "ja-JP", "en-US" }) && ui::GetApplicationLocales() == stored);
    CHECK(app.GetConfiguration().locales == std::vector<std::string>{ "en-US" });
    ui::SetAppLocalesHost({});
}
