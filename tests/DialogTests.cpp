#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Dialog.h"
#include "kiln/ui/Back.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/Semantics.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct FakeDialog final : PlatformDialog {
        std::vector<DialogWindowProperties>* updates;
        int* alive;
        explicit FakeDialog(std::vector<DialogWindowProperties>* u, int* a) : updates(u), alive(a) { ++*alive; }
        ~FakeDialog() override { --*alive; }
        void Update(const DialogWindowProperties& p) override { updates->push_back(p); }
    };
}

TEST(DialogOpensAPlatformWindowWithItsOwnOwner) {
    Owner owner;
    owner.SetDensity({ 2, 1 });
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    Owner* content = nullptr;
    kiln::Function<void()> dismiss;
    std::vector<DialogWindowProperties> created, updates;
    int alive = 0;
    DialogHost host;
    host.create = [&](Owner& o, const DialogWindowProperties& p, kiln::Function<void()> onDismiss) {
        content = &o;
        created.push_back(p);
        dismiss = std::move(onDismiss);
        return std::make_unique<FakeDialog>(&updates, &alive);
    };
    owner.SetDialogHost(host);
    owner.SetFlagSecure(true);
    auto shown = std::make_shared<rt::State<bool>>(true);
    int dismissals = 0;
    LayoutDirection seenDirection = LayoutDirection::Ltr;
    Owner* seenOwner = nullptr;
    comp.SetContent([&] {
        rt::Composition::Current()->Provide(LocalOwner, &owner, [&] {
            rt::Composition::Current()->Provide(LocalLayoutDirection, LayoutDirection::Rtl, [&] {
                if (shown->Get())
                    Dialog([&] { ++dismissals; shown->Set(false); }, {}, [&] {
                        seenDirection = rt::Composition::Current()->Read(LocalLayoutDirection);
                        seenOwner = rt::Composition::Current()->Read(LocalOwner);
                        Box(SizeMod(Modifier{}, 100_dp, 50_dp));
                    });
            });
        });
    });
    auto pump = [&] { rt::Composition::RecomposeAll(); rt::Dispatcher::Main().RunPending(); owner.MeasureAndLayout({ 1000, 2000 }); };
    pump();
    CHECK_EQ(alive, 1);
    CHECK(content != nullptr && content != &owner);
    CHECK(created.size() == 1 && created[0].secure);           // Inherit from the secure parent
    CHECK(created[0].direction == LayoutDirection::Rtl);
    CHECK(seenOwner == content);                               // the content's own window owner
    CHECK(seenDirection == LayoutDirection::Rtl);              // the caller's locals
    // The host sizes its window from the content (DialogLayout.internalOnMeasure).
    if (content) {
        CHECK((content->MeasureContent(Constraints{ 0, 800, 0, 1600 }) == IntSize{ 200, 100 }));
        content->SetContentConstraints(Constraints{ 0, 800, 0, 1600 });
        content->MeasureAndLayout({ 400, 300 });
        CHECK((content->ContentBounds() == IntRect{ 100, 100, 300, 200 }));   // centred
        SemanticsConfiguration cfg;
        bool dialog = false;
        for (LayoutNode* n : content->Root().Children()) dialog = dialog || (CalculateSemanticsConfiguration(*n, cfg) && cfg.isDialog);
        CHECK(dialog);
    }
    // Back: the dialog's callback holds it and asks to dismiss; the window goes.
    CHECK(BackDispatcher::Main().Completed());
    pump();
    CHECK_EQ(dismissals, 1);
    CHECK_EQ(alive, 0);
}

TEST(DialogBackRespectsDismissOnBackPress) {
    Owner owner;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    kiln::Function<void()> dismiss;
    int alive = 0;
    std::vector<DialogWindowProperties> updates;
    DialogHost host;
    host.create = [&](Owner&, const DialogWindowProperties&, kiln::Function<void()> d) { dismiss = std::move(d); return std::make_unique<FakeDialog>(&updates, &alive); };
    owner.SetDialogHost(host);
    int dismissals = 0;
    comp.SetContent([&] {
        rt::Composition::Current()->Provide(LocalOwner, &owner, [&] {
            DialogProperties p;
            p.dismissOnBackPress = false;
            Dialog([&] { ++dismissals; }, p, [] {});
        });
    });
    rt::Composition::RecomposeAll();
    rt::Dispatcher::Main().RunPending();
    CHECK(BackDispatcher::Main().Completed());   // held, not passed on
    CHECK_EQ(dismissals, 0);
    if (dismiss) dismiss();                      // the window's own request (an outside press)
    CHECK_EQ(dismissals, 1);
    comp.SetContent([] {});
    rt::Composition::RecomposeAll();
    rt::Dispatcher::Main().RunPending();
    CHECK_EQ(alive, 0);
}

TEST(DialogContentIsDisposedBeforeItsWindowCloses) {
    // The content's cleanup (a magnifier, the keyboard session) still reaches the window's hosts.
    Owner owner;
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<std::string> order;
    struct OrderedDialog final : PlatformDialog {
        std::vector<std::string>* order;
        explicit OrderedDialog(std::vector<std::string>* o) : order(o) {}
        ~OrderedDialog() override { order->push_back("window"); }
        void Update(const DialogWindowProperties&) override {}
    };
    DialogHost host;
    host.create = [&](Owner&, const DialogWindowProperties&, kiln::Function<void()>) { return std::make_unique<OrderedDialog>(&order); };
    owner.SetDialogHost(host);
    auto shown = std::make_shared<rt::State<bool>>(true);
    comp.SetContent([&] {
        rt::Composition::Current()->Provide(LocalOwner, &owner, [&] {
            if (shown->Get())
                Dialog([] {}, {}, [&] {
                    rt::Composition::Current()->DisposableEffect(rt::Keys(0), [&] {
                        return kiln::Function<void()>([&] { order.push_back("content"); });
                    });
                });
        });
    });
    auto pump = [&] { rt::Composition::RecomposeAll(); rt::Dispatcher::Main().RunPending(); owner.MeasureAndLayout({ 1000, 2000 }); };
    pump();
    shown->Set(false);
    pump();
    CHECK((order == std::vector<std::string>{ "content", "window" }));
}
