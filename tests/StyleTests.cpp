#include "Check.h"

#include "kiln/foundation/style/StyleProperty.h"
#include "kiln/foundation/style/StyleState.h"

#include <stdexcept>

using namespace kiln;
using namespace kiln::foundation;

TEST(StylePropertiesInterpolateTheirValues) {
    // Properties: identity by address, local or not, a default, and the default interpolation.
    const auto padding = StylePropertyOf<ui::Dp>("padding", [] { return ui::Dp(0); });
    const auto tint = StyleLocalOf<ui::Color>("tint", [] { return ui::Color(0xFF000000u); });
    CHECK(!padding->IsLocal() && tint->IsLocal() && padding->ToString() == "padding property" && tint->ToString() == "tint local");
    CHECK(padding->DefaultValue() == ui::Dp(0));
    CHECK_NEAR(padding->Lerp(ui::Dp(10), ui::Dp(20), 0.25f).value, 12.5f, 1e-4f);
    CHECK_NEAR(padding->Lerp(ui::Dp::Unspecified(), ui::Dp(20), 0.5f).value, 10.0f, 1e-4f);   // unspecified reads as 0
    const auto flag = StylePropertyOf<bool>("flag", [] { return false; });
    CHECK(!flag->Lerp(false, true, 0.4f) && flag->Lerp(false, true, 0.5f));   // switches at 0.5
    const auto custom = StylePropertyOf<float>("x", [] { return 0.0f; }, [](const float& a, const float& b, float t) { return t < 1 ? a : b; });
    CHECK(custom->Lerp(1, 2, 0.9f) == 1);

    // Breadth: None is the zero of the other kind; kinds don't mix.
    CHECK(DefaultStyleInterpolate(Breadth::None(), Breadth(ui::Dp(40)), 0.5f) == Breadth(ui::Dp(20)));
    CHECK(DefaultStyleInterpolate(Breadth(0.5f), Breadth::None(), 0.5f) == Breadth(0.25f));
    CHECK(DefaultStyleInterpolate(Breadth(ui::Dp(10)), Breadth(1.0f), 0.4f) == Breadth(ui::Dp(10)));
    CHECK(DefaultStyleInterpolate(Breadth(ui::Dp(10)), Breadth(1.0f), 0.6f) == Breadth(1.0f));
    // Fill: colours as colours; a colour and a brush through brushes; accessors.
    const Fill red(ui::Color(0xFFFF0000u)), blue(ui::Color(0xFF0000FFu));
    const Fill mid = DefaultStyleInterpolate(red, blue, 0.5f);
    CHECK(mid.kind == Fill::Kind::Color && mid.AsColor() != red.color && mid.AsColor() != blue.color);
    const Fill gradient(gfx::Brush::LinearGradient({ ui::Color(0xFFFF0000u), ui::Color(0xFF00FF00u) }));
    CHECK(DefaultStyleInterpolate(red, gradient, 0.5f).kind == Fill::Kind::Brush);
    CHECK(Fill::None().AsColor() == ui::Color::Unspecified() && !Fill::None().AsBrush() && red.AsBrush() == gfx::Brush::SolidColor(red.color));
    CHECK(Fill(gfx::Brush::SolidColor(ui::Color(0xFF00FF00u))).AsColor() == ui::Color(0xFF00FF00u));
    // Shadows: a simple shadow fades in from None; compound ones switch.
    const gfx::shadow::Shadow s(ui::Dp(8), ui::Color(0xFF000000u));
    const Shadows faded = DefaultStyleInterpolate(Shadows::None(), Shadows(s), 0.5f);
    CHECK(faded.kind == Shadows::Kind::Simple && faded.shadows[0].color != s.color);
    const Shadows two(std::vector{ s, s });
    CHECK(DefaultStyleInterpolate(two, Shadows::None(), 0.4f) == two && DefaultStyleInterpolate(two, Shadows::None(), 0.6f) == Shadows::None());
    // Text units only within one kind.
    CHECK(StyleLerp(text::Sp(10), text::Sp(20), 0.5f) == text::Sp(15) && StyleLerp(text::Sp(10), text::Em(2), 0.6f) == text::Em(2));
}

TEST(StyleStateFollowsKeysAndInteractions) {
    MutableStyleState state;
    CHECK(state.IsEnabled() && !state.IsPressed() && state.TriStateToggle() == ui::ToggleableState::Off);
    state.SetTriStateToggle(ui::ToggleableState::Indeterminate);
    CHECK(!state.IsChecked() && state.Get(StyleStateKeys::Toggle()) == ui::ToggleableState::Indeterminate);
    state.SetChecked(true);
    CHECK(state.IsChecked() && state.Get(StyleStateKeys::Toggle()) == ui::ToggleableState::On);
    state.Set(StyleStateKeys::Enabled(), false);
    CHECK(!state.IsEnabled() && !state.Get(StyleStateKeys::Enabled()));
    // Custom keys: their default until set; removed back to it; predefined keys can't go.
    static const StyleStateKey<int> level(3);
    CHECK_EQ(state.Get(level), 3);
    state.Set(level, 7);
    CHECK_EQ(state.Get(level), 7);
    state.Remove(level);
    CHECK_EQ(state.Get(level), 3);
    bool threw = false;
    try { state.Remove(StyleStateKeys::Pressed()); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    // Reads are observed.
    int notified = 0;
    struct Watch final : rt::Subscriber {
        int* n;
        void OnStateChanged() override { ++*n; }
    } watch;
    watch.n = &notified;
    rt::Observation::Track(watch, [&] { (void)state.IsHovered(); });
    state.SetHovered(true);
    rt::Observation::ApplyChanges();
    CHECK(notified >= 1);

    // An interaction source: overlapping presses keep it pressed until the last ends; other
    // interactions go to the custom keys.
    struct DragKey final : StyleStateKey<bool> {
        DragKey() : StyleStateKey<bool>(false) {}
        void ProcessInteraction(const Interaction& i, MutableStyleState& s) const override {
            if (i.kind == Interaction::Kind::Drag) s.Set(*this, i.phase == Interaction::Phase::Start);
        }
    };
    static const DragKey dragged;
    auto source = std::make_shared<MutableInteractionSource>();
    MutableStyleState follow(source);
    follow.Set<bool>(dragged, false);
    auto stop = follow.ProcessInteractions(source);
    const auto a = source->Start(Interaction::Kind::Press), b = source->Start(Interaction::Kind::Press);
    CHECK(follow.IsPressed());
    source->End(Interaction::Kind::Press, a);
    CHECK(follow.IsPressed());
    source->Cancel(Interaction::Kind::Press, b);
    CHECK(!follow.IsPressed());
    const auto f = source->Start(Interaction::Kind::Focus);
    const auto d = source->Start(Interaction::Kind::Drag);
    CHECK(follow.IsFocused() && follow.Get<bool>(dragged));
    source->End(Interaction::Kind::Drag, d);
    CHECK(!follow.Get<bool>(dragged));
    stop();
    source->End(Interaction::Kind::Focus, f);
    CHECK(follow.IsFocused());   // no longer following
}
