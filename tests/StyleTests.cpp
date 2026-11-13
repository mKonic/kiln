#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/style/StyleProperty.h"
#include "kiln/foundation/style/StyleState.h"
#include "kiln/foundation/style/StyleResolver.h"
#include "kiln/foundation/style/StyleModifiers.h"
#include "kiln/foundation/Compose.h"

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

namespace {
    const auto& SizeProp() {
        static const auto p = StylePropertyOf<ui::Dp>("size", [] { return ui::Dp(0); });
        return p;
    }
    const auto& Tint() {
        static const auto p = StyleLocalOf<float>("tint", [] { return 0.0f; });
        return p;
    }
    const auto& Other() {
        static const auto p = StylePropertyOf<float>("other", [] { return -1.0f; });
        return p;
    }
    const NestedStyleKey& Inner() {
        static const NestedStyleKey k("inner");
        return k;
    }
    struct StyleHarness {
        ui::Owner owner;
        rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
        void Pump() {
            rt::Observation::ApplyChanges();
            rt::Dispatcher::Main().RunPending();
            comp.Recompose();
            owner.MeasureAndLayout({ 300, 300 });
        }
    };
    template<typename T> T Read(StyleResolver& r, const StylePropertyRef<T>& p) {
        return r.Resolve([&](const StyleResolverScope& s) { return s.Value(p); });
    }
}

TEST(StyleResolverResolvesStatesLocalsAndNestedStyles) {
    StyleHarness h;
    auto parentState = std::make_shared<MutableStyleState>();
    auto childState = std::make_shared<MutableStyleState>();
    std::shared_ptr<StyleResolver> parent, child;
    rt::State<bool> provideOther{ true };
    h.comp.SetContent([&] {
        parent = RememberStyleResolver(CommonStyle([&](CommonStyleScope& s) {
            s.Provide(SizeProp(), ui::Dp(10));
            s.Provide(Tint(), 0.5f);
            if (provideOther.Get()) s.Provide(Other(), 3.0f);
            Pressed(s, [&] { s.Provide(SizeProp(), ui::Dp(20)); });
            s.Provide(SizeProp(), ui::Dp(15));   // the pressed layer still wins over a later base write
            s.ProvideNestedStyle(Inner(), CommonStyle([](CommonStyleScope& n) { n.Provide(SizeProp(), ui::Dp(99)); }));
        }), parentState);
        child = RememberStyleResolver(CommonStyle([](CommonStyleScope& s) { s.ApplyNestedStyle(Inner()); }), childState);
        foundation::Box(StyleResolverMod(Modifier{}, parent), Alignment::TopStart(), false, [&] {
            foundation::Box(StyleResolverMod(Modifier{}, child));
        });
    });
    h.Pump();
    // Base values; a later write in the same layer wins; nested styles stay out of the parent.
    CHECK(Read(*parent, SizeProp()) == ui::Dp(15) && Read(*parent, Other()) == 3.0f);
    // The child: the nested style (not the parent's own properties); the local inherited.
    CHECK(Read(*child, SizeProp()) == ui::Dp(99));
    CHECK(!child->Resolve([](const StyleResolverScope& s) { return s.IsSet(*Other()); }));
    CHECK(Read(*child, Tint()) == 0.5f && child->Resolve([](const StyleResolverScope& s) { return s.IsSet(*Tint()); }));
    // A state change re-resolves: the pressed layer wins.
    parentState->SetPressed(true);
    h.Pump();
    CHECK(Read(*parent, SizeProp()) == ui::Dp(20));
    // A value no longer provided goes back to its default and is no longer set.
    provideOther.Set(false);
    h.Pump();
    CHECK(Read(*parent, Other()) == -1.0f);
    // It stays (at its default) until the next resolution, as in Compose.
    parentState->SetPressed(false);
    h.Pump();
    CHECK(!parent->Resolve([](const StyleResolverScope& s) { return s.IsSet(*Other()); }));
    // Read before binding: an error.
    StyleResolver loose(CommonStyle{});
    bool threw = false;
    try { (void)Read(loose, SizeProp()); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
}

TEST(StyleResolverAnimatesValuesProvidedUnderAnimate) {
    StyleHarness h;
    auto state = std::make_shared<MutableStyleState>();
    std::shared_ptr<StyleResolver> r;
    h.comp.SetContent([&] {
        r = RememberStyleResolver(CommonStyle([](CommonStyleScope& s) {
            s.Animate(anim::Tween(100, 0, anim::Linear()), [&] {
                s.Provide(SizeProp(), ui::Dp(0));
                Selected(s, [&] { s.Provide(SizeProp(), ui::Dp(100)); });
            });
        }), state);
        foundation::Box(StyleResolverMod(Modifier{}, r));
    });
    h.Pump();
    CHECK(Read(*r, SizeProp()) == ui::Dp(0));
    state->SetSelected(true);
    h.Pump();
    auto& d = rt::Dispatcher::Main();
    d.RunFrame(1'000'000'000);
    d.RunFrame(1'050'000'000);
    const float mid = Read(*r, SizeProp()).value;
    CHECK(mid > 30 && mid < 70);
    d.RunFrame(1'200'000'000);
    d.RunPending();
    CHECK(Read(*r, SizeProp()) == ui::Dp(100));
}

TEST(StyleModifiersPlaceSizePadAndDraw) {
    StyleHarness h;
    // placement: right / bottom anchor; fixed width; fractional height of what is left.
    ui::Rect placed, padded, bordered;
    h.comp.SetContent([&] {
        foundation::Box(Modifier{}, Alignment::TopStart(), false, [&] {
            foundation::Box(OnGloballyPositioned(
                Placement(Modifier{}, [](PlacementModifierScope& p) {
                    p.right = ui::Dp(10);
                    p.bottom = ui::Dp(20);
                    p.width = Breadth(ui::Dp(50));
                    p.height = Breadth(0.5f);
                }),
                [&](const LayoutCoordinates& c) { placed = c.BoundsInRoot(); }));
        });
    });
    h.Pump();
    // 300 - 50 - 10 = 240; height: half of the max, which grows by the insets as in Compose
    // (addMaxWithMinimum): (300 + 20) / 2 = 160.
    CHECK_NEAR(placed.left, 240, 0.5f);
    CHECK_NEAR(placed.right - placed.left, 50, 0.5f);
    CHECK_NEAR(placed.bottom - placed.top, 160, 0.5f);

    // Padding and placement from a resolver; the padding follows a state change.
    StyleHarness g;
    auto state = std::make_shared<MutableStyleState>();
    std::shared_ptr<StyleResolver> r;
    g.comp.SetContent([&] {
        r = RememberStyleResolver(CommonStyle([](CommonStyleScope& s) {
            s.Provide(style::ExternalPaddingStart(), ui::Dp(7));
            s.Provide(style::ContentPaddingTop(), ui::Dp(5));
            s.Provide(style::Width(), Breadth(ui::Dp(100)));
            s.Provide(style::Height(), Breadth(ui::Dp(40)));
            s.Provide(style::BorderWidth(), ui::Dp(2.4f));
            Hovered(s, [&] { s.Provide(style::ContentPaddingTop(), ui::Dp(9)); });
        }), state);
        foundation::Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Modifier m = StyleResolverMod(Modifier{}, r);
            m = StyleExternalPadding(std::move(m), r);
            m = StylePlacement(std::move(m), r);
            m = OnGloballyPositioned(std::move(m), [&](const LayoutCoordinates& c) { bordered = c.BoundsInRoot(); });
            m = StyleBorderPadding(std::move(m), r);
            m = StyleContentPadding(std::move(m), r);
            m = OnGloballyPositioned(std::move(m), [&](const LayoutCoordinates& c) { padded = c.BoundsInRoot(); });
            foundation::Box(m);
        });
    });
    g.Pump();
    CHECK_NEAR(bordered.left, 7, 0.5f);
    CHECK_NEAR(bordered.right - bordered.left, 100, 0.5f);
    CHECK_NEAR(bordered.bottom - bordered.top, 40, 0.5f);
    // The border (2.4 dp at density 1, rounded up to 3) then the content padding (5 on top).
    CHECK_NEAR(padded.left, 10, 0.5f);
    CHECK_NEAR(padded.top, 8, 0.5f);
    state->SetHovered(true);
    g.Pump();
    CHECK_NEAR(padded.top, 12, 0.5f);

    // appearance: the shape is the element's semantics shape; the style layer only when needed.
    StyleHarness a;
    int layerBlocks = 0, required = 0;
    rt::State<bool> needLayer{ false };
    a.comp.SetContent([&] {
        Modifier m = Appearance(SizeMod(Modifier{}, ui::Dp(20), ui::Dp(20)), [](AppearanceScope& s) {
            s.shape = CircleShape();
            s.background = Fill(ui::Color(0xFFFF0000u));
            s.borderWidth = ui::Dp(2);
        });
        m = GraphicsLayer(std::move(m), [&] { ++required; return needLayer.Get(); }, [&](GraphicsLayerParams& p) { ++layerBlocks; p.alpha = 0.5f; });
        foundation::Box(m);
    });
    a.Pump();
    a.owner.Draw(ui::DrawTarget{});
    // The node's own semantics carry the shape (extra data, not a node of the tree by itself).
    bool circle = false;
    kiln::Function<void(ui::LayoutNode*)> visit = [&](ui::LayoutNode* n) {
        ui::SemanticsConfiguration c;
        if (ui::CalculateSemanticsConfiguration(*n, c) && c.shape == CircleShape()) circle = true;
        for (ui::LayoutNode* child : n->Children()) visit(child);
    };
    visit(&a.owner.Root());
    CHECK(circle);
    CHECK(required >= 1 && layerBlocks == 0);
    needLayer.Set(true);
    a.Pump();
    a.owner.Draw(ui::DrawTarget{});
    CHECK(layerBlocks >= 1);
}
