#include "Check.h"

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/draganddrop/DragAndDrop.h"
#include "kiln/ui/LayoutNode.h"

#include <chrono>
#include <thread>

using namespace kiln;
using namespace kiln::foundation;

namespace {
    struct LoggingTarget final : ui::DragAndDropTarget {
        std::vector<std::string>* log;
        std::string name;
        bool OnDrop(const DragAndDropEvent& e) override {
            log->push_back(name + " drop " + (e.clip && !e.clip->items.empty() && e.clip->items[0].text ? *e.clip->items[0].text : ""));
            return true;
        }
        void OnStarted(const DragAndDropEvent&) override { log->push_back(name + " started"); }
        void OnEntered(const DragAndDropEvent&) override { log->push_back(name + " entered"); }
        void OnExited(const DragAndDropEvent&) override { log->push_back(name + " exited"); }
        void OnEnded(const DragAndDropEvent&) override { log->push_back(name + " ended"); }
    };
}

TEST(DragAndDropSourceStartsAndTargetsReceive) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.Config().longPressTimeoutMillis = 40;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<std::string> log;
    int started = 0, shadowW = 0;
    ui::DragAndDropHost host;
    host.startDrag = [&](const ui::DragAndDropTransferData& d, int w, int, std::vector<std::uint8_t>) {
        ++started;
        shadowW = w;
        return !d.clip.items.empty();
    };
    owner.DragAndDrop().SetHost(host);
    auto outer = std::make_shared<LoggingTarget>();
    outer->log = &log;
    outer->name = "outer";
    auto inner = std::make_shared<LoggingTarget>();
    inner->log = &log;
    inner->name = "inner";
    comp.SetContent([&] {
        Column(Modifier{}, [&] {
            Box(DragAndDropSource(SizeMod(Modifier{}, Dp(50), Dp(30)), [](Offset) {
                ui::DragAndDropTransferData d;
                d.clip.items.push_back({ std::string("hello"), std::nullopt, std::nullopt });
                return std::optional(d);
            }));
            Box(foundation::DragAndDropTarget(SizeMod(Modifier{}, Dp(100), Dp(100)), [](const DragAndDropEvent&) { return true; }, outer),
                Alignment::TopStart(), false, [&] {
                    Box(foundation::DragAndDropTarget(SizeMod(Modifier{}, Dp(40), Dp(40)), [](const DragAndDropEvent& e) {
                        return std::find(e.mimeTypes.begin(), e.mimeTypes.end(), "text/plain") != e.mimeTypes.end();
                    }, inner));
                });
        });
    });
    auto pump = [&] { rt::Dispatcher::Main().RunPending(); comp.Recompose(); owner.MeasureAndLayout({ 300, 300 }); };
    pump();
    // A long press on the source starts the drag with a shadow its size.
    std::int64_t t = 1000;
    auto send = [&](ui::PointerEventType type, bool down) {
        ui::RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t += 16;
        e.pointers.push_back({ 1, { 10, 10 }, down, ui::PointerType::Touch, {} });
        owner.ProcessPointer(e);
        pump();
    };
    send(ui::PointerEventType::Press, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    pump();
    CHECK_EQ(started, 1);
    CHECK_EQ(shadowW, 50);
    send(ui::PointerEventType::Release, false);
    // The session: both targets take text; moving onto the inner one exits the outer one.
    DragAndDropEvent e;
    e.mimeTypes = { "text/plain" };
    e.action = DragAndDropEvent::Action::Started;
    CHECK(owner.DragAndDrop().OnDrag(e));
    e.action = DragAndDropEvent::Action::Entered;
    owner.DragAndDrop().OnDrag(e);
    e.action = DragAndDropEvent::Action::Location;
    e.positionInRoot = { 80, 100 };   // in the outer target only (it starts at y 30)
    owner.DragAndDrop().OnDrag(e);
    e.positionInRoot = { 20, 50 };    // in the inner one too
    owner.DragAndDrop().OnDrag(e);
    e.action = DragAndDropEvent::Action::Drop;
    e.clip = ui::ClipEntry{ { { std::string("hi"), std::nullopt, std::nullopt } } };
    CHECK(owner.DragAndDrop().OnDrag(e));
    e.action = DragAndDropEvent::Action::Ended;
    owner.DragAndDrop().OnDrag(e);
    const std::vector<std::string> expected{ "outer started", "inner started", "outer entered", "inner entered", "outer exited",
                                             "inner drop hi", "outer ended", "inner ended" };
    CHECK(log == expected);
    if (log != expected) for (const auto& l : log) std::printf("  %s\n", l.c_str());
}

#include "FakeText.h"
#include "kiln/foundation/text/BasicTextField.h"

TEST(TextDroppedOnAFieldGoesWhereTheDragPointed) {
    test::FakeText backend;
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.SetText(&backend);
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    TextFieldState state("hello world");
    auto interaction = std::make_shared<MutableInteractionSource>();
    BasicTextFieldOptions o;
    o.textStyle = text::TextStyle(text::Sp(20));
    o.interactionSource = interaction;
    comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, o); }); });
    auto pump = [&] { for (int i = 0; i < 3; ++i) { rt::Dispatcher::Main().RunPending(); comp.Recompose(); owner.MeasureAndLayout({ 300, 300 }); } };
    pump();
    DragAndDropEvent e;
    e.mimeTypes = { "text/plain" };
    e.action = DragAndDropEvent::Action::Started;
    CHECK(owner.DragAndDrop().OnDrag(e));
    e.action = DragAndDropEvent::Action::Entered;
    owner.DragAndDrop().OnDrag(e);
    e.action = DragAndDropEvent::Action::Location;
    e.positionInRoot = { 52, 10 };   // between "hello" and " world" (10 px per character)
    owner.DragAndDrop().OnDrag(e);
    pump();
    CHECK(interaction->IsDragAndDropHovered());
    CHECK(state.Selection() == TextRange(5));
    e.action = DragAndDropEvent::Action::Drop;
    e.clip = ui::ClipEntry{ { { std::string(","), std::nullopt, std::nullopt } } };
    CHECK(owner.DragAndDrop().OnDrag(e));
    e.action = DragAndDropEvent::Action::Ended;
    owner.DragAndDrop().OnDrag(e);
    pump();
    CHECK(state.Peek().text == u"hello, world");
    CHECK(!interaction->IsDragAndDropHovered());
    // An image-only drag is not for it.
    DragAndDropEvent image;
    image.mimeTypes = { "image/png" };
    CHECK(!owner.DragAndDrop().OnDrag(image));
    image.action = DragAndDropEvent::Action::Ended;
    owner.DragAndDrop().OnDrag(image);
}
