// The app's own night mode (AppCompatDelegate.setDefaultNightMode): the platform's host when there
// is one, else the app's configuration carries the chosen night bits.
#include "Check.h"
#include "FakeText.h"

#include "kiln/App.h"
#include "kiln/ui/NightMode.h"

#include <kiln/kiln.h>

using namespace kiln;

TEST(NightModeOverridesTheConfigurationWithoutAPlatform) {
    using C = ui::Configuration;
    test::FakeText text;
    App app({ .text = &text });
    ui::Configuration system;
    system.uiMode = C::kUiModeTypeNormal | C::kUiModeNightNo;
    app.SetConfiguration(system);
    CHECK(!app.GetConfiguration().IsNightModeActive() && ui::GetDefaultNightMode() == ui::NightMode::FollowSystem);
    ui::SetDefaultNightMode(ui::NightMode::Yes);
    CHECK(app.GetConfiguration().IsNightModeActive() && ui::GetDefaultNightMode() == ui::NightMode::Yes);
    CHECK((app.GetConfiguration().uiMode & C::kUiModeTypeMask) == C::kUiModeTypeNormal);   // only the night bits
    // A later system change keeps the app's choice; following the system again takes the system's.
    system.uiMode = C::kUiModeTypeNormal | C::kUiModeNightYes;
    app.SetConfiguration(system);
    ui::SetDefaultNightMode(ui::NightMode::No);
    CHECK(!app.GetConfiguration().IsNightModeActive());
    ui::SetDefaultNightMode(ui::NightMode::FollowSystem);
    CHECK(app.GetConfiguration().IsNightModeActive());
    // AutoBattery without battery saver is light; an unknown mode is ignored (AppCompat logs it).
    ui::SetDefaultNightMode(ui::NightMode::AutoBattery);
    CHECK(!app.GetConfiguration().IsNightModeActive());
    ui::SetDefaultNightMode(ui::NightMode(42));
    CHECK(ui::GetDefaultNightMode() == ui::NightMode::AutoBattery);
    ui::SetDefaultNightMode(ui::NightMode::FollowSystem);
    // TwilightManager's fallback hours.
    CHECK(ui::IsNightByHour(5) && !ui::IsNightByHour(6) && !ui::IsNightByHour(21) && ui::IsNightByHour(22) && ui::IsNightByHour(0));
}

TEST(NightModeGoesToThePlatform) {
    std::vector<ui::NightMode> sent;
    ui::SetNightModeHost({ .set = [&](ui::NightMode m) { sent.push_back(m); } });
    test::FakeText text;
    App app({ .text = &text });
    ui::Configuration system;
    system.uiMode = ui::Configuration::kUiModeTypeNormal | ui::Configuration::kUiModeNightNo;
    app.SetConfiguration(system);
    ui::SetDefaultNightMode(ui::NightMode::Yes);
    ui::SetDefaultNightMode(ui::NightMode::Yes);   // unchanged: not sent again
    // The platform applies it (a configuration change follows); kiln does not override.
    CHECK(sent == std::vector<ui::NightMode>{ ui::NightMode::Yes } && !app.GetConfiguration().IsNightModeActive());
    CHECK(kiln_get_default_night_mode() == KILN_NIGHT_MODE_YES);
    kiln_set_default_night_mode(KILN_NIGHT_MODE_FOLLOW_SYSTEM);
    CHECK(sent.size() == 2 && sent.back() == ui::NightMode::FollowSystem);
    ui::SetNightModeHost({});
}
