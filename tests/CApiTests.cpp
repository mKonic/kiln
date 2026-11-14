#include "Check.h"
#include <kiln/Function.h>
#include "FakeText.h"

#include <kiln/kiln_m3.h>
#include <kiln/kilnpp.hpp>

#include "kiln/foundation/Compose.h"
#include "kiln/ui/InteropView.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/ui/Semantics.h"
#include "kiln/ui/Volition.h"

#include <chrono>
#include <string>

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
    extern int capi_volition_dropped;
    kiln_volition_registration* capi_volition_register(void);
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
            kiln::Function<void(const ui::LayoutNode&)> walk = [&](const ui::LayoutNode& l) {
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

TEST(CApiVolitionRegistrationAnswersWhereDestinationsAndGo) {
    namespace v = ui::volition;
    kiln_volition_registration* r = capi_volition_register();
    CHECK(r != nullptr);
    const std::string where = v::Call({}, { "KilnActivity" }, "where", {});
    CHECK(where.find("\"screen\":\"item\",\"page\":4,\"editing\":false,\"stack\":[\"home\",\"item\"]") != std::string::npos);
    CHECK(v::Call({}, {}, "destinations", {}).find("item:<id>  opens an item") != std::string::npos);
    CHECK_EQ(v::Call({}, {}, "go", "item:42"), std::string("ok: opened item 42"));
    CHECK_EQ(v::Call({}, {}, "check", "page>3"), std::string("ok: page is 4"));
    kiln_volition_unregister(r);
    CHECK_EQ(capi_volition_dropped, 1);
    CHECK(v::Call({}, { "KilnActivity" }, "where", {}).find("screen") == std::string::npos);
}

kpp::VolitionRegistration kpp_volition_register();

TEST(KilnppVolitionRegistration) {
    namespace v = ui::volition;
    {
        auto r = kpp_volition_register();
        CHECK(v::Call({}, {}, "where", {}).find("\"screen\":\"kpp\",\"stack\":[\"a\",\"b\"]") != std::string::npos);
        CHECK_EQ(v::Call({}, {}, "go", "detail:7"), std::string("ok: detail 7"));
    }
    CHECK(v::Call({}, {}, "where", {}).find("kpp") == std::string::npos);   // unregistered with the handle
}

TEST(CApiAlignmentLinesPadFromABaselineAndAreTheComponentsOwn) {
    CHECK(reinterpret_cast<const void*>(kiln_first_baseline()) == &ui::FirstBaseline);
    CHECK(reinterpret_cast<const void*>(kiln_m3_minimum_interactive_top_alignment_line()) == &material3::MinimumInteractiveTopAlignmentLine);
    CHECK(reinterpret_cast<const void*>(kiln_m3_minimum_interactive_left_alignment_line()) == &material3::MinimumInteractiveLeftAlignmentLine);
    Harness h;
    h.Run([](void*) {
        kpp::column(kpp::Modifier(), [] {
            kiln_basic_text(KILN_HERE, "Ag", kiln_mod_test_tag(NULL, "plain"), NULL, NULL);
            kiln_basic_text(KILN_HERE, "Ag", kiln_mod_padding_from(kiln_mod_test_tag(NULL, "padded"), kiln_first_baseline(), 30, KILN_DP_UNSPECIFIED),
                            NULL, NULL);
        });
    });
    const ui::LayoutNode* plain = h.Find(h.owner.Root(), "plain");
    const ui::LayoutNode* padded = h.Find(h.owner.Root(), "padded");
    CHECK(plain && padded);
    if (!plain || !padded) return;
    // FakeText puts a line's baseline at 0.8 of its height: 30 above it, the rest below.
    const float lineH = float(plain->Size().height);
    CHECK(std::abs(float(padded->Size().height) - (30 + lineH * 0.2f)) <= 1);
}

TEST(CApiScrollIndicatorStateFollowsAScrollAndFeedsTheScrollbar) {
    static const kiln_scroll_indicator_state* indicator;
    indicator = nullptr;
    Harness h;
    h.Run([](void*) {
        kiln_scroll_state* scroll = kiln_remember_scroll_state(KILN_HERE, 0);
        indicator = kiln_scrollable_state_scroll_indicator(kiln_scroll_state_scrollable(scroll));
        const kiln_modifier* m = kiln_mod_vertical_scroll(NULL, scroll, true);   // the harness root: 200 x 400
        m = kiln_m3_non_interactive_scrollbar(KILN_HERE, m, indicator, KILN_ORIENTATION_VERTICAL, NULL);
        kpp::column(kpp::Modifier(const_cast<kiln_modifier*>(m)), [] { kiln_box(KILN_HERE, kiln_mod_size(NULL, 100, 1000), NULL, KILN_FN(NULL, NULL)); });
    });
    CHECK(indicator != nullptr);
    CHECK(kiln_scroll_indicator_state_viewport_size(indicator) == 400);
    CHECK(kiln_scroll_indicator_state_content_size(indicator) == 1000);
    CHECK(kiln_scroll_indicator_state_scroll_offset(indicator) == 0);
    CHECK(kiln_scroll_indicator_state_content_size(nullptr) == INT32_MAX);
}

TEST(CApiCarouselFlingBehavioursComeFromTheDefaults) {
    static int made;
    made = 0;
    Harness h;
    h.Run([](void*) {
        if (kiln_m3_carousel_defaults_no_snap_fling_behavior(KILN_HERE)) ++made;
        kiln_m3_carousel_state* state = kiln_m3_remember_carousel_state(KILN_HERE, 0, kiln_m3_fn_to_int{ [](void*) -> int32_t { return 5; }, nullptr, nullptr });
        if (kiln_m3_carousel_defaults_single_advance_fling_behavior(KILN_HERE, state)) ++made;
    });
    CHECK(made == 2);
}

TEST(CApiLazyVerticalGridPlacesItemsInColumnsWithFullLineSpans) {
    static kiln_lazy_grid_state* state;
    state = nullptr;
    Harness h;
    h.Run([](void*) {
        state = kiln_remember_lazy_grid_state(KILN_HERE, 0, 0);
        kpp::lazy_vertical_grid(kpp::Modifier(), kpp::grid_cells::fixed(4), [](kpp::LazyGridScope s) {
            s.item(kpp::Key(int64_t(-1)), [](int32_t, int32_t, int32_t maxLineSpan) { return maxLineSpan; },
                   [] { kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_height(NULL, 20), "header"), NULL, KILN_FN(NULL, NULL)); });
            s.items(100, [](int32_t i) {
                kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_height(NULL, 50), ("cell" + std::to_string(i)).c_str()), NULL, KILN_FN(NULL, NULL));
            });
        }, kpp::LazyGridState(state));
    });
    const ui::LayoutNode* header = h.Find(h.owner.Root(), "header");
    const ui::LayoutNode* c0 = h.Find(h.owner.Root(), "cell0");
    const ui::LayoutNode* c4 = h.Find(h.owner.Root(), "cell4");
    CHECK(header && c0 && c4);
    if (!header || !c0 || !c4) return;
    CHECK(header->Size().width == 200);   // the whole line
    CHECK(c0->Size().width == 50);        // a quarter of it
    CHECK(c4->PositionInRoot().y == 70);  // second row of cells, under the 20 tall header
    CHECK(!h.Has("cell60"));              // lazily composed
    CHECK(kiln_lazy_grid_state_can_scroll_forward(state));
    kiln_lazy_grid_state_scroll_to_item(state, 41, 0, kiln_fn{});
    h.Pump();
    CHECK(kiln_lazy_grid_state_first_visible_item_index(state) == 41);
    CHECK(h.Has("cell40"));
}

TEST(CApiTransitionStateAnimatesVisibilityAndIsTheTooltipsOwn) {
    static kiln_transition_state* state;
    static kiln_transition_state* tooltipTransition;
    static bool tooltipVisible;
    state = tooltipTransition = nullptr;
    Harness h;
    h.Run([](void*) {
        state = kiln_remember_transition_state(KILN_HERE, false);
        kiln_animated_visibility_state(KILN_HERE, state, NULL,
                                       KILN_FN([](void*) { kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_size(NULL, 10, 10), "shown"), NULL, KILN_FN(NULL, NULL)); }, NULL));
        kiln_m3_tooltip_state* tip = kiln_m3_remember_tooltip_state(KILN_HERE, true, false, NULL);
        tooltipTransition = kiln_m3_tooltip_state_transition(tip);
        tooltipVisible = kiln_m3_tooltip_state_is_visible(tip);
    });
    CHECK(state && !kiln_transition_state_current(state) && kiln_transition_state_is_idle(state));
    CHECK(!h.Has("shown"));
    kiln_transition_state_set_target(state, true);
    h.Pump();
    CHECK(kiln_transition_state_target(state));
    CHECK(h.Has("shown"));
    CHECK(!kiln_transition_state_is_idle(state));
    h.Frames(60);
    CHECK(kiln_transition_state_current(state) && kiln_transition_state_is_idle(state));
    // A tooltip state's transition is its visibility (TooltipState.transition).
    CHECK(tooltipTransition != nullptr);
    CHECK(kiln_transition_state_target(tooltipTransition) == tooltipVisible);
}

TEST(CApiAndroidViewMakesUpdatesAndReleasesItsView) {
    static int made, updates, released, dropped;
    made = updates = released = dropped = 0;
    static bool show;
    show = true;
    {
        Harness h;
        ui::InteropViewHost host;
        int view = 0;
        host.create = [&](int, const ui::InteropViewFactory& f) { return f(nullptr, &view) == &view; };
        host.with = [&](int, const ui::InteropViewBlock& b) { b(nullptr, &view); };
        host.release = [](int) {};
        host.measure = [](int, const ui::Constraints&) { return ui::IntSize{ 60, 40 }; };
        host.place = [](int, ui::IntRect, bool) {};
        host.touch = [](int, ui::Offset, bool) { return false; };
        h.owner.SetInteropViewHost(std::move(host));
        struct Dropped { ~Dropped() { ++dropped; } };
        h.Run([](void*) {
            if (!show) return;
            auto guard = std::make_shared<Dropped>();
            kpp::android_view([guard](void*, void* context) { ++made; return context; }, kpp::Modifier().size(60, 40),
                              { .update = [](void*, void*) { ++updates; }, .on_release = [](void*, void*) { ++released; } });
        });
        CHECK(made == 1);
        CHECK(updates >= 1);
        CHECK(released == 0);
        show = false;
        h.composition.SetContent([] {});
        h.Pump();
        CHECK(released == 1);
    }
    CHECK(dropped == 1);
}

TEST(CApiTextFieldKeyboardActionGetsTheDefaultToCall) {
    static int handled, defaults;
    handled = defaults = 0;
    Harness h;
    h.Run([](void*) {
        kiln_text_field_state* state = kiln_remember_text_field_state(kpp::Key{}, "a");
        kiln_m3_text_field_options o;
        kiln_m3_text_field_options_init(&o);
        o.on_keyboard_action = kpp::fn<kiln_m3_fn_fn>([](kiln_fn performDefault) {
            ++handled;
            if (performDefault.call) { performDefault.call(performDefault.user); ++defaults; }
        });
        kiln_m3_text_field(kpp::Key{}, state, nullptr, &o);
    });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump();
    CHECK(h.owner.ActiveTextInput() != nullptr);
    if (!h.owner.ActiveTextInput()) return;
    h.owner.ActiveTextInput()->PerformImeAction(ui::ImeAction::Done);
    h.Pump();
    CHECK_EQ(handled, 1);
    CHECK_EQ(defaults, 1);
}
