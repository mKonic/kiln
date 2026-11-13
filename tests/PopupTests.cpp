#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Popup.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/Semantics.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(kiln::Function<void()> content) {
            composition.SetContent(std::move(content));
            Pump();
        }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 300, 300 });
                rt::Dispatcher::Main().RunPending();
            }
        }
        void Tap(float x, float y, std::int64_t t) {
            auto send = [&](PointerEventType type, bool down, std::int64_t at) {
                RawPointerEvent e;
                e.type = type;
                e.uptimeMillis = at;
                e.buttons = down ? 1u : 0u;
                e.pointers.push_back({ 1, { x, y }, down, PointerType::Mouse, {} });
                owner.ProcessPointer(e);
                rt::Dispatcher::Main().RunPending();
            };
            send(PointerEventType::Press, true, t);
            send(PointerEventType::Release, false, t + 10);
            Pump();
        }
        bool Key(int code) {
            const bool a = owner.DispatchKeyEvent(KeyEvent{ code, KeyEventType::KeyDown, 0 });
            const bool b = owner.DispatchKeyEvent(KeyEvent{ code, KeyEventType::KeyUp, 0 });
            Pump();
            return a || b;
        }
    };

    // A 100 x 40 anchor at (50, 60) with a 30 x 20 popup at its bottom end.
    struct Scene {
        rt::State<bool>* shown = nullptr;
        int contentClicks = 0, popupClicks = 0, dismissals = 0;
        PopupProperties properties;
        void Compose() {
            auto& show = RememberState(true);
            shown = &show;
            Box(Clickable(SizeMod(Modifier{}, 300_dp, 300_dp), [this] { ++contentClicks; }), Alignment::TopStart(), false, [this, &show] {
                Box(OffsetMod(SizeMod(Modifier{}, 100_dp, 40_dp), 50_dp, 60_dp), Alignment::TopStart(), false, [this, &show] {
                    if (show.Get())
                        Popup(Alignment::BottomEnd(), {}, [this, &show] { ++dismissals; show.Set(false); }, properties, [this] {
                            Box(Clickable(SizeMod(Modifier{}, 30_dp, 20_dp), [this] { ++popupClicks; }));
                        });
                });
            });
        }
    };
}

TEST(PopupSitsAtItsProviderPositionAndTakesTouchesInside) {
    Harness h;
    Scene s;
    h.Run([&] { s.Compose(); });
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    const auto& b = h.owner.Popups()[0]->bounds;
    CHECK(b.has_value());
    // BottomEnd on BottomEnd: the popup's bottom-right corner on the anchor's (150, 100).
    CHECK((*b == IntRect{ 120, 80, 150, 100 }));
    h.Tap(130, 90, 1000);
    CHECK_EQ(s.popupClicks, 1);
    CHECK_EQ(s.contentClicks, 0);
}

TEST(NonFocusablePopupDismissesOnOutsidePressAndLetsItThrough) {
    Harness h;
    Scene s;
    h.Run([&] { s.Compose(); });
    h.Tap(10, 10, 1000);
    CHECK_EQ(s.dismissals, 1);
    CHECK_EQ(s.contentClicks, 1);   // ACTION_OUTSIDE: the window under it got the press
    CHECK(h.owner.Popups().empty());
}

TEST(FocusablePopupIsTouchModalAndBackDismissesIt) {
    Harness h;
    Scene s;
    s.properties.focusable = true;
    h.Run([&] { s.Compose(); });
    h.Tap(10, 10, 1000);
    CHECK_EQ(s.dismissals, 1);
    CHECK_EQ(s.contentClicks, 0);   // the press stayed with the popup window
    s.shown->Set(true);
    h.Pump();
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    CHECK(h.Key(keys::Back));
    CHECK_EQ(s.dismissals, 2);
    // dismissOnClickOutside off: an outside press neither dismisses nor reaches the content.
    s.properties.dismissOnClickOutside = false;
    s.shown->Set(true);
    h.Pump();
    h.Tap(10, 10, 2000);
    CHECK_EQ(s.dismissals, 2);
    CHECK_EQ(s.contentClicks, 0);
}

TEST(FocusablePopupParksTheWindowFocusAndGivesItBack) {
    Harness h;
    std::vector<FocusState> outer;
    FocusRequester outerRequester, innerRequester;
    rt::State<bool>* shown = nullptr;
    std::string innerFocus = "none";
    h.Run([&] {
        auto& show = RememberState(false);
        shown = &show;
        Box(Focusable(OnFocusChanged(FocusRequesterModifier(SizeMod(Modifier{}, 50_dp, 50_dp), outerRequester),
                                     [&](FocusState f) { outer.push_back(f); })),
            Alignment::TopStart(), false, [&] {
            if (show.Get()) {
                PopupProperties p;
                p.focusable = true;
                Popup(Alignment::TopStart(), {}, [&] { show.Set(false); }, p, [&] {
                    Box(Focusable(SizeMod(Modifier{}, 20_dp, 20_dp), Focusability::Always,
                                  [&](FocusState f) { innerFocus = IsFocused(f) ? "focused" : "not"; }));
                });
            }
        });
    });
    CHECK(outerRequester.RequestFocus());
    h.Pump();
    CHECK(!outer.empty() && IsFocused(outer.back()));
    const std::size_t events = outer.size();
    shown->Set(true);
    h.Pump();
    // The content window lost the key focus, not its focused item.
    CHECK_EQ(outer.size(), events);
    CHECK(h.owner.ActiveFocus() == nullptr);
    CHECK(!h.owner.IsContentWindowFocused());
    // Focus search runs inside the popup only.
    CHECK(h.Key(keys::Tab));
    CHECK(innerFocus == "focused");
    // Back dismisses it; the content gets its focused item back without a focus event.
    CHECK(h.Key(keys::Back));
    CHECK(h.owner.Popups().empty());
    CHECK(h.owner.ActiveFocus() != nullptr);
    CHECK_EQ(outer.size(), events);
    CHECK(h.owner.IsContentWindowFocused());
}

TEST(PopupContentSeesTheCallersLocalsAndPopupSemantics) {
    Harness h;
    LayoutDirection seen = LayoutDirection::Ltr;
    bool inPopup = false;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalLayoutDirection, LayoutDirection::Rtl, [&] {
            Box(SizeMod(Modifier{}, 100_dp, 100_dp), Alignment::TopStart(), false, [&] {
                Popup([&] {
                    seen = rt::Composition::Current()->Read(LocalLayoutDirection);
                    inPopup = rt::Composition::Current()->Read(LocalIsInPopupLayout);
                    Box(SizeMod(Modifier{}, 10_dp, 10_dp));
                });
            });
        });
    });
    CHECK(seen == LayoutDirection::Rtl);
    CHECK(inPopup);
    SemanticsConfiguration cfg;
    CHECK(CalculateSemanticsConfiguration(*h.owner.Popups()[0]->node, cfg) && cfg.isPopup);
}

TEST(PopupFollowsItsAnchorAndStaysInTheDisplay) {
    Harness h;
    auto x = std::make_shared<rt::State<int>>(0);
    h.Run([&] {
        Box(OffsetMod(SizeMod(Modifier{}, 40_dp, 40_dp), [x](const Density&) { return IntOffset{ x->Get(), 0 }; }),
            Alignment::TopStart(), false, [&] {
            Popup(Alignment::TopStart(), { 0, 50 }, {}, {}, [] { Box(SizeMod(Modifier{}, 60_dp, 20_dp)); });
        });
    });
    CHECK((*h.owner.Popups()[0]->bounds == IntRect{ 0, 50, 60, 70 }));
    x->Set(100);   // moved in the placement only: the popup follows
    h.Pump();
    CHECK((*h.owner.Popups()[0]->bounds == IntRect{ 100, 50, 160, 70 }));
    x->Set(280);   // would stick out past 300: pushed back inside
    h.Pump();
    CHECK((*h.owner.Popups()[0]->bounds == IntRect{ 240, 50, 300, 70 }));
}

namespace {
    // A PopupHost standing in for the window manager: the owner's window at `origin` on a
    // 1080 x 2400 display, each popup window recorded where it was placed.
    struct FakePopupWindows {
        struct Shown { IntRect bounds{}; bool placed = false; bool removed = false; PopupWindowProperties properties; Owner* content = nullptr;
                       kiln::Function<void()> dismiss; };
        std::vector<std::shared_ptr<Shown>> shown;
        IntOffset origin{ 200, 500 };
        struct Window final : PlatformPopup {
            std::shared_ptr<Shown> s;
            explicit Window(std::shared_ptr<Shown> s_) : s(std::move(s_)) {}
            ~Window() override { s->removed = true; }
            void Update(const PopupWindowProperties& p) override { s->properties = p; }
            void Place(IntRect r) override { s->bounds = r; s->placed = true; }
        };
        PopupHost Host() {
            PopupHost h;
            h.create = [this](Owner& content, const PopupWindowProperties& p, kiln::Function<void()> dismiss) -> std::unique_ptr<PlatformPopup> {
                auto s = std::make_shared<Shown>();
                s->properties = p;
                s->content = &content;
                s->dismiss = std::move(dismiss);
                shown.push_back(s);
                return std::make_unique<Window>(s);
            };
            h.windowOrigin = [this] { return origin; };
            h.displayBounds = [](bool) { return IntRect{ 0, 0, 1080, 2400 }; };
            return h;
        }
    };
}

TEST(PopupInAWindowThatDoesNotCoverTheDisplayIsAWindowOfItsOwn) {
    Harness h;
    FakePopupWindows wm;
    h.owner.SetPopupHost(wm.Host());
    Scene s;
    // LocalOwner, as the app provides it: the Popup asks its owner for a window.
    h.Run([&] { rt::Composition::Current()->Provide(LocalOwner, &h.owner, [&] { s.Compose(); }); });
    CHECK(h.owner.Popups().empty());   // not in the overlay layer
    CHECK_EQ(int(wm.shown.size()), 1);
    // BottomEnd on BottomEnd, on the screen: the anchor is at (250, 560)-(350, 600).
    CHECK(wm.shown[0]->placed);
    CHECK((wm.shown[0]->bounds == IntRect{ 320, 580, 350, 600 }));
    // A press outside the window asks to dismiss; the popup leaving removes the window.
    wm.shown[0]->dismiss();
    h.Pump();
    CHECK_EQ(s.dismissals, 1);
    CHECK(wm.shown[0]->removed);
}

TEST(PopupWindowReachesPastItsOwnersWindowAndFollowsIt) {
    Harness h;
    FakePopupWindows wm;
    h.owner.SetPopupHost(wm.Host());
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalOwner, &h.owner, [] {
            Box(SizeMod(Modifier{}, 40_dp, 40_dp), Alignment::TopStart(), false, [] {
                Popup(Alignment::TopStart(), { 0, 50 }, {}, {}, [] { Box(SizeMod(Modifier{}, 60_dp, 400_dp)); });
            });
        });
    });
    // 400 px tall below a 300 px window: the window's own bounds do not cut it.
    CHECK((wm.shown.at(0)->bounds == IntRect{ 200, 550, 260, 950 }));
    // The owner's window moved on the screen (a dialog re-centred): the popup goes with it.
    wm.origin = { 100, 400 };
    h.owner.OnWindowMovedOnScreen();
    CHECK((wm.shown[0]->bounds == IntRect{ 100, 450, 160, 850 }));
    // Kept on the display: near its bottom the popup is pushed up to fit.
    wm.origin = { 100, 2200 };
    h.owner.OnWindowMovedOnScreen();
    CHECK((wm.shown[0]->bounds == IntRect{ 100, 2000, 160, 2400 }));
}
