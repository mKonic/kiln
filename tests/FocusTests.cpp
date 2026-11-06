#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/runtime/Saveable.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

#include <chrono>
#include <thread>

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) {
            composition.SetContent(std::move(content));
            composition.Recompose();
            owner.MeasureAndLayout({ 300, 300 });
        }
        bool Key(int code, std::uint32_t meta = 0) {
            KeyEvent d{ code, KeyEventType::KeyDown, meta };
            KeyEvent u{ code, KeyEventType::KeyUp, meta };
            const bool a = owner.DispatchKeyEvent(d);
            const bool b = owner.DispatchKeyEvent(u);
            composition.Recompose();
            owner.MeasureAndLayout({ 300, 300 });
            return a || b;
        }
    };

    // Three focusable boxes in a row and one below the first: tracks which one is focused.
    void Grid(std::string& focused) {
        auto item = [&](const char* name, Modifier m) {
            Box(Focusable(SizeMod(std::move(m), Dp(50), Dp(50)), Focusability::Always,
                          [&focused, name](FocusState s) { if (IsFocused(s)) focused = name; }));
        };
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] { item("a", {}); item("b", {}); item("c", {}); });
            item("d", {});
        });
    }
}

TEST(TabMovesInPlacementOrderAndWraps) {
    Harness h;
    std::string focused;
    h.Run([&] { Grid(focused); });
    CHECK(h.Key(keys::Tab)); CHECK_EQ(focused, std::string("a"));
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("b"));
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("c"));
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("d"));
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("a"));          // 1D wraps
    h.Key(keys::Tab, kMetaShift); CHECK_EQ(focused, std::string("d"));
}

TEST(ArrowsFollowTheBeam) {
    Harness h;
    std::string focused;
    h.Run([&] { Grid(focused); });
    h.Key(keys::Tab);                         // a
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("b"));
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("c"));
    h.Key(keys::DpadDown);  CHECK_EQ(focused, std::string("d"));   // only candidate below
    h.Key(keys::DpadUp);    CHECK_EQ(focused, std::string("a"));   // in d's beam
    CHECK(!h.Key(keys::DpadLeft));            // nothing to the left: 2D does not wrap
    CHECK_EQ(focused, std::string("a"));
}

TEST(ClickableIsFocusableOnlyWithKeysAndClicksOnEnter) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Clickable(SizeMod(Modifier{}, Dp(40), Dp(40)), [&] { ++clicks; }));
        });
    });
    FocusTargetNode* any = nullptr;
    (void)any;
    h.owner.SetInputMode(InputMode::Touch);
    // In touch mode a SystemDefined target cannot take focus, so Enter does nothing; but the
    // first key switches to keyboard mode, after which Tab focuses it.
    h.Key(keys::Tab);
    CHECK(h.owner.ActiveFocus() != nullptr);
    h.Key(keys::Enter);
    CHECK_EQ(clicks, 1);
}

TEST(ClickableKeyLongPressAndDoubleClick) {
    Harness h;
    int clicks = 0, longs = 0, doubles = 0;
    h.Run([&] {
        Column(Modifier{}, [&] {
            ClickableOptions o;
            o.onLongClick = [&] { ++longs; };
            o.onDoubleClick = [&] { ++doubles; };
            Box(Clickable(SizeMod(Modifier{}, Dp(40), Dp(40)), [&] { ++clicks; }, o));
        });
    });
    h.Key(keys::Tab);
    CHECK(h.owner.ActiveFocus() != nullptr);
    auto wait = [](int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        rt::Dispatcher::Main().RunPending();
    };
    auto send = [&](KeyEventType type) { KeyEvent e{ keys::Enter, type, 0 }; h.owner.DispatchKeyEvent(e); };
    // Held past the long-press timeout: a long click and no click on release.
    send(KeyEventType::KeyDown);
    wait(450);
    send(KeyEventType::KeyUp);
    wait(350);
    CHECK_EQ(longs, 1);
    CHECK_EQ(clicks, 0);
    // Two quick presses: a double click.
    send(KeyEventType::KeyDown); send(KeyEventType::KeyUp);
    wait(60);
    send(KeyEventType::KeyDown); send(KeyEventType::KeyUp);
    CHECK_EQ(doubles, 1);
    // One press: the click comes once the double-tap timeout passes.
    send(KeyEventType::KeyDown); send(KeyEventType::KeyUp);
    CHECK_EQ(clicks, 0);
    wait(60);    // the double-tap minimum time, then the rest of the timeout
    wait(300);
    CHECK_EQ(clicks, 1);
    CHECK_EQ(longs, 1);
}

namespace {
    // One pad sample through the adapter and the owner, as App does.
    struct Pad {
        Harness& h;
        IndirectPointerAdapter adapter;
        bool Send(RawIndirectPointerEvent::Action a, float x, float y, std::int64_t t) {
            RawIndirectPointerEvent raw;
            raw.action = a;
            raw.uptimeMillis = t;
            raw.pointers.push_back({ 0, { x, y }, 1, {} });
            IndirectPointerEvent e;
            const bool consumed = adapter.Convert(raw, e) ? h.owner.DispatchIndirectPointerEvent(e)
                                                          : (h.owner.DispatchIndirectPointerCancel(), false);
            rt::Dispatcher::Main().RunPending();
            return consumed;
        }
    };
}

TEST(IndirectPointerClicksTheFocusedClickable) {
    Harness h;
    int a = 0, b = 0;
    FocusRequester ra, rb;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Clickable(FocusRequesterModifier(SizeMod(Modifier{}, Dp(40), Dp(40)), ra), [&] { ++a; }));
            Box(Clickable(FocusRequesterModifier(SizeMod(Modifier{}, Dp(40), Dp(40)), rb), [&] { ++b; }));
        });
    });
    using A = RawIndirectPointerEvent::Action;
    Pad pad{ h };
    // Nothing focused: nobody takes it.
    CHECK(!pad.Send(A::Down, 10, 10, 0));
    pad.Send(A::Up, 10, 10, 20);
    h.owner.SetInputMode(InputMode::Keyboard);
    ra.RequestFocus();
    CHECK(pad.Send(A::Down, 10, 10, 100));
    CHECK(pad.Send(A::Up, 12, 10, 150));
    CHECK_EQ(a, 1);
    // Past the touch slop: no click.
    pad.Send(A::Down, 10, 10, 1000);
    pad.Send(A::Move, 60, 10, 1020);
    pad.Send(A::Up, 60, 10, 1040);
    CHECK_EQ(a, 1);
    // Focus leaves mid-press: the press is cancelled, so its release clicks nothing.
    pad.Send(A::Down, 10, 10, 2000);
    rb.RequestFocus();
    ra.RequestFocus();
    pad.Send(A::Up, 10, 10, 2050);
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 0);
    // The other one, once focused.
    rb.RequestFocus();
    pad.Send(A::Down, 10, 10, 3000);
    pad.Send(A::Up, 10, 10, 3050);
    CHECK_EQ(b, 1);
    // A fling moves focus one step without wrapping.
    CHECK(!h.owner.MoveFocus(FocusDirection::Next, false));
    CHECK(h.owner.MoveFocus(FocusDirection::Previous, false));
}

TEST(IndirectPointerDoubleClick) {
    Harness h;
    int clicks = 0, doubles = 0;
    FocusRequester r;
    h.Run([&] {
        ClickableOptions o;
        o.onDoubleClick = [&] { ++doubles; };
        Box(Clickable(FocusRequesterModifier(SizeMod(Modifier{}, Dp(40), Dp(40)), r), [&] { ++clicks; }, o));
    });
    h.owner.SetInputMode(InputMode::Keyboard);
    r.RequestFocus();
    using A = RawIndirectPointerEvent::Action;
    Pad pad{ h };
    pad.Send(A::Down, 10, 10, 0);
    pad.Send(A::Up, 10, 10, 50);
    pad.Send(A::Down, 10, 10, 120);
    pad.Send(A::Up, 10, 10, 170);
    CHECK_EQ(doubles, 1);
    CHECK_EQ(clicks, 0);
}

TEST(FocusPropertiesRedirectSkipAndCancel) {
    Harness h;
    std::string focused;
    FocusRequester toD;
    h.Run([&] {
        auto item = [&](const char* name, Modifier m) {
            Box(Focusable(SizeMod(std::move(m), Dp(50), Dp(50)), Focusability::Always,
                          [&focused, name](FocusState s) { if (IsFocused(s)) focused = name; }));
        };
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] {
                item("a", FocusPropertiesMod({}, [&](FocusProperties& p) { p.next = &toD; p.right = FocusProperties::Cancel(); }));
                item("b", FocusPropertiesMod({}, [](FocusProperties& p) { p.canFocus = false; }));
                item("c", {});
            });
            item("d", FocusRequesterModifier({}, toD));
        });
    });
    h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("a"));
    h.Key(keys::DpadRight);
    CHECK_EQ(focused, std::string("a"));     // Cancel
    h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("d"));     // explicit next
    h.Key(keys::Tab, kMetaShift);
    CHECK_EQ(focused, std::string("c"));     // b cannot take focus
}

namespace {
    void Item(std::string& focused, const char* name) {
        Box(Focusable(SizeMod(Modifier{}, Dp(50), Dp(50)), Focusability::Always,
                      [&focused, name](FocusState s) { if (IsFocused(s)) focused = name; }));
    }
}

TEST(FocusRestorerReturnsToTheLastFocusedChild) {
    Harness h;
    std::string focused;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] {
                Item(focused, "a");
                Row(FocusRestorer(Modifier{}), [&] { Item(focused, "b1"); Item(focused, "b2"); Item(focused, "b3"); });
            });
            Item(focused, "d");
        });
    });
    h.Key(keys::Tab); h.Key(keys::Tab); h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("b2"));
    h.Key(keys::DpadDown); CHECK_EQ(focused, std::string("d"));    // leaves the group: b2 saved
    h.Key(keys::DpadUp); CHECK_EQ(focused, std::string("a"));
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("b2"));  // not the nearest, b1
}

TEST(FocusOnEnterCanCancelAndOnExitRedirect) {
    Harness h;
    std::string focused;
    bool blockEntry = true;
    FocusRequester target, other;
    h.Run([&] {
        Row(Modifier{}, [&] {
            Item(focused, "a");
            Row(FocusGroup(FocusPropertiesMod(Modifier{}, [&](ui::FocusProperties& p) {
                    p.onEnter = [&](FocusEnterExitScope& s) { if (blockEntry) s.CancelFocusChange(); };
                    p.onExit = [&](FocusEnterExitScope&) { other.RequestFocus(); };
                })), [&] {
                Box(Focusable(FocusRequesterModifier(SizeMod(Modifier{}, Dp(50), Dp(50)), target), Focusability::Always,
                              [&focused](FocusState s) { if (IsFocused(s)) focused = "t"; }));
                Box(Focusable(FocusRequesterModifier(SizeMod(Modifier{}, Dp(50), Dp(50)), other), Focusability::Always,
                              [&focused](FocusState s) { if (IsFocused(s)) focused = "c"; }));
            });
        });
    });
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("a"));
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("a"));   // entry cancelled
    blockEntry = false;
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("t"));
    h.Key(keys::DpadRight); CHECK_EQ(focused, std::string("c"));
    h.Key(keys::DpadLeft); CHECK_EQ(focused, std::string("t"));    // inside the group: no exit
    h.Key(keys::DpadLeft); CHECK_EQ(focused, std::string("c"));    // leaving: redirected to c
}

TEST(FocusRectMovesTheSearchArea) {
    Harness h;
    std::string focused;
    h.Run([&] {
        auto item = [&](const char* name, Modifier m) {
            Box(Focusable(SizeMod(std::move(m), Dp(50), Dp(50)), Focusability::Always,
                          [&focused, name](FocusState s) { if (IsFocused(s)) focused = name; }));
        };
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] {
                item("a", {});
                item("b", {});
                // c sits at (100, 0) but declares its focus area at (0, 100), below d.
                item("c", FocusPropertiesMod(Modifier{}, [](ui::FocusProperties& p) { p.focusRect = Rect{ -100, 100, -50, 150 }; }));
            });
            item("d", {});
        });
    });
    h.Key(keys::Tab); CHECK_EQ(focused, std::string("a"));
    h.Key(keys::DpadDown); CHECK_EQ(focused, std::string("d"));
    h.Key(keys::DpadDown); CHECK_EQ(focused, std::string("c"));
}

TEST(RotaryAndPreImeKeysFollowTheFocusedPath) {
    Harness h;
    std::vector<std::string> log;
    bool innerConsumes = false;
    h.Run([&] {
        Box(OnRotaryScrollEvent(Modifier{}, [&](const RotaryScrollEvent&) { log.push_back("outer"); return false; },
                                [&](const RotaryScrollEvent&) { log.push_back("pre-outer"); return false; }),
            Alignment::TopStart(), false, [&] {
            Box(Focusable(OnInterceptKeyBeforeSoftKeyboard(
                              OnRotaryScrollEvent(SizeMod(Modifier{}, Dp(50), Dp(50)),
                                                  [&](const RotaryScrollEvent& e) { log.push_back("inner " + std::to_string(int(e.verticalScrollPixels))); return innerConsumes; },
                                                  [&](const RotaryScrollEvent&) { log.push_back("pre-inner"); return false; }),
                              [&](const KeyEvent& k) { log.push_back("ime " + std::to_string(k.keyCode)); return true; })));
        });
    });
    // Nothing focused: no one sees it (the host's view gets it instead).
    CHECK(!h.owner.DispatchRotaryEvent({ 64, 0, 0, 0 }));
    CHECK(log.empty());
    h.Key(keys::Tab);
    CHECK(!h.owner.DispatchRotaryEvent({ 64, 0, 0, 0 }));
    CHECK(log == std::vector<std::string>({ "pre-outer", "pre-inner", "inner 64", "outer" }));
    log.clear();
    innerConsumes = true;
    CHECK(h.owner.DispatchRotaryEvent({ 32, 0, 0, 0 }));
    CHECK(log == std::vector<std::string>({ "pre-outer", "pre-inner", "inner 32" }));
    log.clear();
    CHECK(h.owner.DispatchInterceptedSoftKeyboardEvent(KeyEvent{ keys::Back, KeyEventType::KeyDown }));
    CHECK(log == std::vector<std::string>({ "ime " + std::to_string(keys::Back) }));
}

TEST(KeyFocusMovesPlayNavigationSounds) {
    Harness h;
    std::string focused;
    std::vector<int> sounds;
    h.owner.SetPlaySoundEffect([&](int c) { sounds.push_back(c); });
    h.Run([&] { Grid(focused); });
    h.Key(keys::Tab);                                   // next: navigation down
    h.Key(keys::DpadRight);
    KeyEvent repeat{ keys::DpadLeft, KeyEventType::KeyDown };
    repeat.repeatCount = 1;
    h.owner.DispatchKeyEvent(repeat);                   // held: the repeating variant
    h.owner.DispatchKeyEvent(KeyEvent{ keys::DpadLeft, KeyEventType::KeyUp });
    h.Key(keys::DpadLeft);                              // nothing to the left of a: silent
    CHECK(sounds == std::vector<int>({ 4, 3, 5 }));
}

TEST(InteractionBarrierBlocksFocusUnderIt) {
    Harness h;
    FocusRequester under, beside, inside;
    rt::State<bool> covered{ false };
    std::string focused;
    auto item = [&](const char* name, FocusRequester& r, Modifier m) {
        Box(Focusable(FocusRequesterModifier(SizeMod(std::move(m), Dp(50), Dp(50)), r), Focusability::Always,
                      [&focused, name](FocusState s) { if (IsFocused(s)) focused = name; }));
    };
    h.Run([&] {
        Row(Modifier{}, [&] {
            // A barrier drawn above `under` once `covered` is set.
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                item("under", under, {});
                if (covered.Get()) Box(InteractionBarrier(SizeMod(Modifier{}, Dp(100), Dp(100))));
            });
            item("beside", beside, {});
            // A barrier around its own content does not block it.
            Box(InteractionBarrier(Modifier{}), Alignment::TopStart(), false, [&] { item("inside", inside, {}); });
        });
    });
    CHECK(under.RequestFocus());
    CHECK_EQ(focused, std::string("under"));
    // The barrier arrives on top: the focused item loses focus and cannot get it back.
    covered.Set(true);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(h.owner.ActiveFocus() == nullptr);
    CHECK(!under.RequestFocus());
    CHECK(inside.RequestFocus());
    CHECK_EQ(focused, std::string("inside"));
    // Tab order skips the covered item.
    h.owner.ClearFocus(true);
    h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("beside"));
}

TEST(FocusSearchFromAHostRect) {
    Harness h;
    std::string focused;
    h.Run([&] { Grid(focused); });
    std::string found;
    auto name = [&](FocusTargetNode& n) { found = h.owner.FocusRectOf(n).left < 25 ? "left" : "other"; return true; };
    // Nothing focused: a search Down from a rect above the grid's left column finds "a".
    const Rect above{ 0, -40, 50, -10 };
    auto r = h.owner.FocusSearch(FocusDirection::Down, &above, name);
    CHECK(r.has_value() && *r);
    CHECK_EQ(found, std::string("left"));
    CHECK(focused.empty());   // a search alone moves nothing
    // Request through the search, then reset: focus starts over from the first item.
    h.owner.SetInputMode(InputMode::Keyboard);
    h.owner.FocusSearch(FocusDirection::Down, &above, [&](FocusTargetNode& n) { return n.RequestFocus(FocusDirection::Down); });
    CHECK_EQ(focused, std::string("a"));
    h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("b"));
    CHECK(h.owner.ResetFocus(FocusDirection::Next));
    CHECK_EQ(focused, std::string("a"));
    CHECK(h.owner.HasFocusableContent());
    const auto rect = h.owner.FocusedRect();
    CHECK(rect.has_value() && rect->left == 0 && rect->top == 0);
}

TEST(FocusChangesSettleAfterComposition) {
    Harness h;
    rt::State<bool> canFocus{ true }, present{ true };
    FocusRequester r;
    std::vector<std::string> outer;
    int itemInactive = 0;
    h.Run([&] {
        Column(OnFocusChanged(Modifier{}, [&](FocusState s) {
                   outer.push_back(s == FocusState::Inactive ? "inactive" : s == FocusState::Active ? "active" : "other");
               }), [&] {
            if (present.Get()) {
                const bool can = canFocus.Get();
                Box(Focusable(FocusRequesterModifier(
                                  FocusPropertiesMod(OnFocusChanged(SizeMod(Modifier{}, Dp(40), Dp(40)),
                                                                    [&](FocusState s) { if (s == FocusState::Inactive) ++itemInactive; }),
                                                     [can](ui::FocusProperties& p) { p.canFocus = can; }),
                                  r)));
            }
        });
    });
    // A new focus-event node hears the current state once.
    CHECK_EQ(outer.size(), std::size_t(1));
    CHECK_EQ(outer.back(), std::string("inactive"));
    CHECK_EQ(itemInactive, 1);
    CHECK(r.RequestFocus());
    CHECK_EQ(outer.back(), std::string("active"));   // the state of the target below it
    // The focused item stops being focusable: focus goes once the change is applied.
    canFocus.Set(false);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(h.owner.ActiveFocus() == nullptr);
    CHECK_EQ(outer.back(), std::string("inactive"));
    // The focused item goes away: its ancestors hear it.
    canFocus.Set(true);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(r.RequestFocus());
    CHECK_EQ(outer.back(), std::string("active"));   // the state of the target below it
    present.Set(false);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(h.owner.ActiveFocus() == nullptr);
    CHECK_EQ(outer.back(), std::string("inactive"));
}

TEST(FocusRestorerSurvivesRecreation) {
    // One "process": focus b2 inside the restorer, leave the group, save.
    // One call site for both runs, as in an app (the provider's position is part of the keys).
    auto content = [](rt::SaveableStateRegistry& registry, std::string& focused) {
        rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, &registry, [&] {
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] {
                Item(focused, "a");
                Row(FocusRestorer(Modifier{}), [&] { Item(focused, "b1"); Item(focused, "b2"); Item(focused, "b3"); });
            });
            Item(focused, "d");
        });
        });
    };
    std::vector<std::uint8_t> saved;
    {
        Harness h;
        rt::SaveableStateRegistry registry;
        std::string focused;
        h.Run([&] { content(registry, focused); });
        h.Key(keys::Tab); h.Key(keys::Tab); h.Key(keys::Tab);
        CHECK_EQ(focused, std::string("b2"));
        h.Key(keys::DpadDown);
        CHECK_EQ(focused, std::string("d"));
        saved = rt::SaveableStateRegistry::Serialize(registry.PerformSave());
    }
    // The next one restores: entering the group goes back to b2.
    Harness h;
    rt::SaveableStateRegistry registry(rt::SaveableStateRegistry::Deserialize(saved));
    std::string focused;
    h.Run([&] { content(registry, focused); });
    h.Key(keys::Tab);
    CHECK_EQ(focused, std::string("a"));
    h.Key(keys::DpadRight);
    CHECK_EQ(focused, std::string("b2"));
}
