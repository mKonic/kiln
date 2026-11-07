#include "Check.h"

#include "kiln/foundation/Layouts.h"
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
