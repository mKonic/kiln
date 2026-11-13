// The Volition endpoint debug builds answer: elements read from semantics, matched by name.
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/Volition.h"

#include <string>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Observation::ApplyChanges();
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 400 });
            }
        }
    };

    // A 100x20 row with the semantics `set` gives it.
    void Row20(kiln::Function<void(SemanticsConfiguration&)> set) {
        Box(Semantics(SizeMod(Modifier{}, Dp(100), Dp(20)), std::move(set), true));
    }

    bool StartsWith(const std::string& s, std::string_view p) { return s.rfind(p, 0) == 0; }
}

TEST(VolitionListsFindsAndActsByName) {
    Harness h;
    int saves = 0, drafts = 0, submits = 0, toggles = 0;
    std::string typed;
    h.Run([&] {
        Column(Modifier{}, Arrangement::Top(), AlignmentH::Start(), [&] {
            Row20([&](SemanticsConfiguration& c) {
                c.role = Role::Button;
                c.text = { "Save" };
                c.testTag = "save";
                c.onClick = [&] { ++saves; return true; };
            });
            Row20([&](SemanticsConfiguration& c) {
                c.text = { "Save draft" };
                c.onClick = [&] { ++drafts; return true; };
            });
            Row20([&](SemanticsConfiguration& c) {
                c.editableText = typed;
                c.focused = true;
                c.setText = [&](const std::string& t) { typed = t; return true; };
                c.onImeAction = [&] { ++submits; return true; };
            });
            Row20([&](SemanticsConfiguration& c) {
                c.role = Role::Switch;
                c.toggleableState = 1;
                c.onClick = [&] { ++toggles; return true; };
            });
            Row20([&](SemanticsConfiguration& c) {
                c.role = Role::Button;
                c.text = { "Delete" };
                c.disabled = true;
                c.onClick = [] { return true; };
            });
        });
    });
    const std::vector<volition::Window> windows{ { "KilnActivity", &h.owner, 0, 100 } };
    const volition::Context context{ "KilnActivity" };
    auto call = [&](std::string_view method, std::string_view arg = {}) { return volition::Call(windows, context, method, arg); };

    const std::string ui = call("ui");
    CHECK(ui.find("button    \"Save\" [save]  @50,110") != std::string::npos);
    CHECK(ui.find("clickable \"Save draft\"  @50,130") != std::string::npos);
    CHECK(ui.find("field     (unlabeled) = \"\"  focused  @50,150") != std::string::npos);
    CHECK(ui.find("switch    (unlabeled)  on  @50,170") != std::string::npos);
    CHECK(ui.find("button    \"Delete\"  disabled  @50,190") != std::string::npos);
    // Reading order: top to bottom.
    CHECK(ui.find("Save") < ui.find("Delete"));

    // An exact label beats one that only contains the query; several partial matches are numbered.
    CHECK(StartsWith(call("find", "save"), "button    \"Save\""));
    const std::string partial = call("find", "sav");
    CHECK(StartsWith(partial, "sav#1  button"));
    CHECK(partial.find("\nsav#2  clickable \"Save draft\"") != std::string::npos);

    CHECK_EQ(call("click", "Save draft"), std::string("ok: clicked clickable \"Save draft\""));
    CHECK_EQ(drafts, 1);
    CHECK_EQ(call("click", "sav#1"), std::string("ok: clicked button \"Save\" [save]"));
    CHECK_EQ(saves, 1);
    CHECK_EQ(call("click", "Delete"), std::string("error: button \"Delete\" is disabled"));
    CHECK_EQ(call("click", "nothing"), std::string("error: nothing on screen is called 'nothing' - try ui"));
    // A kind names the unlabeled ones.
    CHECK_EQ(call("click", "switch"), std::string("ok: clicked switch (unlabeled)"));
    CHECK_EQ(toggles, 1);

    // No name: the focused field.
    CHECK_EQ(call("fill", std::string("\x1f") + "hello"), std::string("ok: filled field (unlabeled)"));
    CHECK_EQ(typed, std::string("hello"));
    CHECK_EQ(call("fill", "field"), std::string("error: fill needs a field and the text for it"));
    CHECK_EQ(call("submit"), std::string("ok: submitted field (unlabeled)"));
    CHECK_EQ(submits, 1);

    CHECK_EQ(call("where"), std::string("{\"activity\":\"KilnActivity\",\"ready\":true,\"focused\":\"field (unlabeled)\"}"));
    CHECK(StartsWith(call("bogus"), "error: no command 'bogus'"));
}

TEST(VolitionAsksTheWindowOnTopFirst) {
    Harness below, above;
    int behind = 0, front = 0;
    below.Run([&] { Row20([&](SemanticsConfiguration& c) { c.role = Role::Button; c.text = { "Cancel" }; c.onClick = [&] { ++behind; return true; }; }); });
    above.Run([&] { Row20([&](SemanticsConfiguration& c) { c.role = Role::Button; c.text = { "Cancel" }; c.onClick = [&] { ++front; return true; }; }); });
    const std::vector<volition::Window> windows{ { "KilnActivity", &below.owner, 0, 0 }, { "dialog", &above.owner, 10, 10 } };
    const std::string ui = volition::Call(windows, {}, "ui", {});
    // A lone box fills the 400x400 root: its middle, moved by the dialog's origin.
    CHECK_EQ(ui, std::string("-- dialog (on top)\nbutton    \"Cancel\"  @210,210\n-- KilnActivity\nbutton    \"Cancel\"  @200,200"));
    CHECK_EQ(volition::Call(windows, {}, "click", "Cancel"), std::string("ok: clicked button \"Cancel\""));
    CHECK_EQ(front, 1);
    CHECK_EQ(behind, 0);
    CHECK_EQ(volition::Call({}, {}, "ui", {}), std::string("error: nothing on screen"));
    CHECK_EQ(volition::Call({}, {}, "where", {}), std::string("{\"activity\":\"none\",\"ready\":false}"));
}

TEST(VolitionChecksConditionsOnWhere) {
    namespace v = ui::volition;
    auto registration = v::Register({ [](v::Object& o) {
        o.push_back({ "page", 4 });
        o.push_back({ "screen", "MangaScreen" });
        o.push_back({ "stack", std::vector<v::Value>{ "Library", "Settings" } });
        o.push_back({ "reader", v::Object{ { "page", 2.5 } } });
        o.push_back({ "nothing", nullptr });
    }, nullptr, nullptr });
    auto check = [](std::string_view c) { return v::Call({}, {}, "check", c); };
    CHECK_EQ(check("page>3"), std::string("ok: page is 4"));
    CHECK_EQ(check("page<=3"), std::string("error: page is 4"));
    CHECK_EQ(check("screen=mangascreen"), std::string("ok: screen is MangaScreen"));
    CHECK_EQ(check("screen~manga"), std::string("ok: screen is MangaScreen"));
    CHECK_EQ(check("stack~settings"), std::string("ok: stack is [\"Library\",\"Settings\"]"));
    CHECK_EQ(check("stack~Sett"), std::string("error: stack is [\"Library\",\"Settings\"]"));   // a list holds whole items
    CHECK_EQ(check("stack.1=Settings"), std::string("ok: stack.1 is Settings"));
    CHECK_EQ(check("reader.page>=2.5"), std::string("ok: reader.page is 2.5"));
    CHECK(check("nothing=1").rfind("error: nothing at 'nothing' - where says {", 0) == 0);
    CHECK_EQ(check("=x"), std::string("error: '=x' is not a condition - try route=home or stack~details"));
    CHECK(v::Call({}, {}, "where", {}).find("\"page\":4,\"screen\":\"MangaScreen\"") != std::string::npos);
    registration.reset();
    CHECK_EQ(v::Call({}, {}, "where", {}), std::string("{\"activity\":\"none\",\"ready\":false}"));
}
