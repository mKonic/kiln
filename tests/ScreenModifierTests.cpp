#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/ScreenModifiers.h"
#include "kiln/ui/LayoutNode.h"

using namespace kiln;
using namespace kiln::foundation;

TEST(KeepScreenOnAndSensitiveContentCountNodes) {
    ui::Owner owner;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<std::string> log;
    owner.SetScreenHost([&](bool on) { log.push_back(on ? "on" : "off"); }, [&](bool s) { log.push_back(s ? "sensitive" : "auto"); });
    auto a = std::make_shared<rt::State<bool>>(true), b = std::make_shared<rt::State<bool>>(true);
    auto sensitive = std::make_shared<rt::State<bool>>(true);
    comp.SetContent([&] {
        Column(Modifier{}, [&] {
            if (a->Get()) Box(KeepScreenOn(Modifier{}));
            if (b->Get()) Box(KeepScreenOn(SensitiveContent(Modifier{}, sensitive->Get())));
        });
    });
    auto pump = [&] { comp.Recompose(); owner.MeasureAndLayout({ 100, 100 }); };
    pump();
    // Two nodes keep the screen on: the host hears once.
    CHECK((log == std::vector<std::string>{ "on", "sensitive" }));
    a->Set(false);
    pump();
    CHECK_EQ(int(log.size()), 2);
    sensitive->Set(false);   // no longer sensitive: counted down without detaching
    pump();
    CHECK(log.back() == "auto");
    b->Set(false);
    pump();
    CHECK(log.back() == "off");
    CHECK_EQ(owner.KeepScreenOnCount(), 0);
    CHECK_EQ(owner.SensitiveComponentCount(), 0);
}

TEST(GestureExclusionAndKeepClearRectsFollowTheirNodes) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<ui::IntRect> excluded, clear;
    owner.SetViewRectsHost([&](int kind, const std::vector<ui::IntRect>& r) { (kind == 0 ? excluded : clear) = r; });
    auto shown = std::make_shared<rt::State<bool>>(true);
    comp.SetContent([&] {
        Column(Modifier{}, [&] {
            Box(SizeMod(Modifier{}, Dp(100), Dp(40)));
            if (shown->Get()) Box(SystemGestureExclusion(SizeMod(Modifier{}, Dp(30), Dp(20))));
            Box(PreferKeepClear(SizeMod(Modifier{}, Dp(50), Dp(50)), [](ui::Size s) { return Rect{ 0, 0, s.width / 2, s.height }; }));
        });
    });
    auto pump = [&] { comp.Recompose(); owner.MeasureAndLayout({ 200, 200 }); };
    pump();
    CHECK((excluded == std::vector<ui::IntRect>{ { 0, 40, 30, 60 } }));
    CHECK((clear == std::vector<ui::IntRect>{ { 0, 60, 25, 110 } }));
    shown->Set(false);
    pump();
    CHECK(excluded.empty());
    CHECK((clear == std::vector<ui::IntRect>{ { 0, 40, 25, 90 } }));   // moved up
}

TEST(ConfigurationReadersRecomposeAndSecureFlagReachesTheHost) {
    ui::Owner owner;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    auto config = std::make_shared<rt::State<ui::Configuration>>(ui::Configuration{});
    std::vector<bool> dark;
    comp.SetContent([&] {
        rt::Composition::Current()->Provide(ui::LocalConfiguration, config->Get(), [&] { dark.push_back(ui::IsSystemInDarkTheme()); });
    });
    comp.Recompose();
    ui::Configuration night = config->Peek();
    night.uiMode = ui::Configuration::kUiModeTypeNormal | ui::Configuration::kUiModeNightYes;
    config->Set(night);
    comp.Recompose();
    CHECK(!dark.empty() && !dark.front() && dark.back());

    std::vector<bool> host;
    owner.SetFlagSecureHost([&](bool s) { host.push_back(s); });
    owner.SetFlagSecure(true);
    owner.SetFlagSecure(true);
    CHECK((host == std::vector<bool>{ true }));
    CHECK(ui::ShouldApplySecureFlag(ui::SecureFlagPolicy::Inherit, owner.FlagSecure()));
    CHECK(!ui::ShouldApplySecureFlag(ui::SecureFlagPolicy::SecureOff, true));
    CHECK(ui::ShouldApplySecureFlag(ui::SecureFlagPolicy::SecureOn, false));
}
