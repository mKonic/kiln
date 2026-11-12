#include "Check.h"
#include "FakeText.h"

#include <kiln/kiln_m3.h>

#include "kiln/foundation/Compose.h"
#include "kiln/ui/Semantics.h"

#include <chrono>

using namespace kiln;

extern "C" {
    extern int capi_live_callbacks, capi_effect_starts, capi_effect_disposals, capi_task_frames, capi_items_composed;
    extern kiln_state_i64* capi_count;
    void capi_counter(void* user);
    void capi_list(void* user);
    int capi_handles_ok(void);
    void capi_annotated(void* user);
    extern kiln_color capi_m3_primary, capi_m3_expected;
    void capi_m3(void* user);
    extern int capi_snack_result;
    extern kiln_color capi_provided_color;
    void capi_m3_snackbar(void* user);
    void capi_m3_show_snackbar(void);
    void capi_m3_dismiss_snackbar(void);
    void capi_m3_app_bar(void* user);
    float capi_bar_height_offset(void);
}

extern int kpp_effect_runs, kpp_disposals;
extern std::string kpp_field_text;
void kpp_content();

namespace {
    struct WithFakeText { test::FakeText text; };
    struct Harness : WithFakeText {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNanos = 0;
        Harness() { owner.SetDensity({ 1, 1 }); owner.SetText(&text); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        // The C content as the app's content (what kiln_app_set_content does with it).
        void Run(void (*content)(void*)) {
            composition.SetContent([content] { content(nullptr); });
            Pump();
        }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 200, 400 });
            }
        }
        void Frames(int n) {
            for (int i = 0; i < n; ++i) {
                auto& d = rt::Dispatcher::Main();
                d.AdvanceTimeBy(std::chrono::milliseconds(16));
                frameNanos += 16'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                Pump();
            }
        }
        // Tags are found on the layout nodes (a tag alone makes no accessibility node).
        const ui::LayoutNode* Find(const ui::LayoutNode& n, const std::string& tag, ui::SemanticsConfiguration* out = nullptr) {
            if (!n.IsPlaced() && n.Parent()) return nullptr;
            ui::SemanticsConfiguration c;
            if (ui::CalculateSemanticsConfiguration(const_cast<ui::LayoutNode&>(n), c) && c.testTag == tag) {
                if (out) *out = c;
                return &n;
            }
            for (const auto* child : n.Children())
                if (const auto* r = Find(*child, tag, out)) return r;
            return nullptr;
        }
        bool Has(const std::string& tag) { return Find(owner.Root(), tag) != nullptr; }
        std::string Text(const std::string& tag) {
            ui::SemanticsConfiguration c;
            return Find(owner.Root(), tag, &c) && !c.text.empty() ? c.text.front() : std::string();
        }
        // The text under a tagged node (a button's label).
        std::string SubtreeText(const std::string& tag) {
            const ui::LayoutNode* n = Find(owner.Root(), tag);
            std::string out;
            std::function<void(const ui::LayoutNode&)> walk = [&](const ui::LayoutNode& l) {
                ui::SemanticsConfiguration c;
                if (ui::CalculateSemanticsConfiguration(const_cast<ui::LayoutNode&>(l), c))
                    for (const auto& t : c.text) out += t;
                for (const auto* child : l.Children()) walk(*child);
            };
            if (n) walk(*n);
            return out;
        }
        std::optional<int> Toggle(const std::string& tag) {
            ui::SemanticsConfiguration c;
            return Find(owner.Root(), tag, &c) ? c.toggleableState : std::nullopt;
        }
        void Drag(float x, float y0, float y1, int steps) {
            std::int64_t t = 1000;
            auto send = [&](ui::PointerEventType type, float y, bool down) {
                ui::RawPointerEvent e;
                e.type = type;
                e.uptimeMillis = t;
                e.pointers.push_back({ 1, { x, y }, down, ui::PointerType::Touch, {} });
                owner.ProcessPointer(e);
                Pump();
            };
            send(ui::PointerEventType::Press, y0, true);
            for (int i = 1; i <= steps; ++i) { t += 16; send(ui::PointerEventType::Move, y0 + (y1 - y0) * float(i) / float(steps), true); }
            t += 16;
            send(ui::PointerEventType::Release, y1, false);
        }
        bool Click(const std::string& tag) {
            ui::SemanticsConfiguration c;
            return Find(owner.Root(), tag, &c) && c.onClick && c.onClick();
        }
    };
}

TEST(CApiContentRecomposesOnStateAndDropsCallbacks) {
    capi_live_callbacks = capi_effect_starts = capi_effect_disposals = capi_task_frames = 0;
    {
        Harness h;
        h.Run(capi_counter);
        CHECK_EQ(h.Text("label"), std::string("Count 0"));
        CHECK(!h.Has("extra"));
        CHECK(h.Click("button"));
        h.Pump();
        CHECK_EQ(h.Text("label"), std::string("Count 1"));
        // The conditional group entered: its effect started once, its box is there.
        CHECK(h.Has("extra"));
        CHECK_EQ(capi_effect_starts, 1);
        CHECK(h.Click("button"));
        h.Pump();
        CHECK_EQ(h.Text("label"), std::string("Count 2"));
        CHECK(!h.Has("extra"));
        CHECK_EQ(capi_effect_disposals, 1);
        // Setting the state from outside content recomposes the same way.
        kiln_state_i64_set(capi_count, 41);
        h.Pump();
        CHECK_EQ(h.Text("label"), std::string("Count 41"));
        // The launched task ran three frames and ended.
        h.Frames(6);
        CHECK_EQ(capi_task_frames, 3);
    }
    // Every callback handed to kiln was dropped once the content left.
    CHECK_EQ(capi_live_callbacks, 0);
}

TEST(CApiLazyColumnComposesOnlyVisibleItems) {
    capi_items_composed = 0;
    Harness h;
    h.Run(capi_list);
    CHECK(h.Has("item 0"));
    CHECK(h.Has("item 19"));
    CHECK(!h.Has("item 999"));
    CHECK(capi_items_composed < 40);
}

TEST(CApiHandlesAndOptions) {
    CHECK(capi_handles_ok());
}

TEST(CApiMaterial3ThemeButtonAndCheckbox) {
    Harness h;
    h.Run(capi_m3);
    // The theme took the dark scheme given to it.
    CHECK(capi_m3_expected != KILN_COLOR_UNSPECIFIED);
    CHECK_EQ(capi_m3_primary, capi_m3_expected);
    CHECK_EQ(h.SubtreeText("button"), std::string("Clicked 0"));
    CHECK(h.Click("button"));
    h.Pump();
    CHECK_EQ(h.SubtreeText("button"), std::string("Clicked 1"));
    CHECK(h.Toggle("checkbox") == std::optional<int>(0));
    CHECK(h.Click("checkbox"));
    h.Pump();
    CHECK(h.Toggle("checkbox") == std::optional<int>(1));
}

TEST(CApiMaterial3SnackbarResultAndProvidedLocal) {
    Harness h;
    h.Run(capi_m3_snackbar);
    CHECK_EQ(capi_provided_color, KILN_ARGB(0xFF123456));
    capi_m3_show_snackbar();
    h.Frames(30);
    CHECK_EQ(h.SubtreeText("host"), std::string("SavedUndo"));
    CHECK_EQ(capi_snack_result, -1);
    capi_m3_dismiss_snackbar();
    h.Frames(30);
    CHECK_EQ(capi_snack_result, int(KILN_M3_SNACKBAR_RESULT_ACTION_PERFORMED));
}

TEST(KilnppLambdasStateEffectsAndMaterial3) {
    kpp_effect_runs = kpp_disposals = 0;
    Harness h;
    h.composition.SetContent([] { kpp_content(); });
    h.Pump();
    CHECK_EQ(h.SubtreeText("kpp-button"), std::string("Taps 0"));
    CHECK(h.Has("row 0") && h.Has("row 4") && !h.Has("row 49"));
    CHECK_EQ(kpp_field_text, std::string("hello"));
    CHECK(h.Click("kpp-button"));
    h.Pump();
    CHECK_EQ(h.SubtreeText("kpp-button"), std::string("Taps 1"));
    CHECK_EQ(kpp_effect_runs, 1);
    CHECK(h.Click("kpp-button"));
    h.Pump();
    CHECK_EQ(kpp_disposals, 1);
}

TEST(CApiMaterial3TopAppBarFollowsTheListThroughNestedScroll) {
    Harness h;
    h.Run(capi_m3_app_bar);
    CHECK_EQ(capi_bar_height_offset(), 0.0f);
    // Drag the list up: the enterAlways bar moves up with it.
    h.Drag(100, 300, 200, 10);
    CHECK(capi_bar_height_offset() < -10.0f);
}

TEST(CApiAnnotatedTextFromABuilder) {
    Harness h;
    h.Run(capi_annotated);
    CHECK_EQ(h.Text("rich"), std::string("Read the terms."));
}
