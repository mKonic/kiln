#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

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
