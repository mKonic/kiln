#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/MaterialShapes.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/ripple/Ripple.h"
#include "kiln/material3/ripple/RippleHost.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/TypographyKeyTokens.h"
#include "kiln/ui/LayoutNode.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::material3;
using kiln::ui::Color;

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) {
            composition.SetContent(std::move(content));
            for (int i = 0; i < 3; ++i) {
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 300, 300 });
                rt::Dispatcher::Main().RunPending();
            }
        }
    };
}

TEST(ColorSchemesFollowTheTokensAndTheirArguments) {
    const ColorScheme light = LightColorScheme();
    CHECK(light.primary == Color(0xFF6750A4u));
    CHECK(light.surfaceTint == light.primary);
    CHECK(DarkColorScheme().primary == Color(0xFFD0BCFFu));
    // surfaceTint follows primary unless it is given.
    const ColorScheme custom = LightColorScheme([](ColorScheme& s) { s.primary = Color(0xFF112233u); });
    CHECK(custom.surfaceTint == Color(0xFF112233u));
    const ColorScheme tinted = LightColorScheme([](ColorScheme& s) { s.primary = Color(0xFF112233u); s.surfaceTint = Color(0xFF445566u); });
    CHECK(tinted.surfaceTint == Color(0xFF445566u));
    CHECK(ExpressiveLightColorScheme().onPrimaryContainer == Color(0xFF4F378Bu));   // PaletteTokens.Primary30
    CHECK(ContentColorFor(light, light.primaryContainer) == light.onPrimaryContainer);
    CHECK(ContentColorFor(light, light.surfaceContainerHigh) == light.onSurface);
    CHECK(!ContentColorFor(light, Color(0xFF123456u)).Specified());
    CHECK(FromToken(light, tokens::ColorSchemeKeyTokens::OutlineVariant) == light.outlineVariant);
    CHECK(SurfaceColorAtElevation(light, ui::Dp(0)) == light.surface);
    CHECK(SurfaceColorAtElevation(light, ui::Dp(3)) == Color(0xFFF2E9F8u));   // tint alpha 21/255 over surface
}

TEST(TypographyTakesTheTypeScaleAndAFontFamily) {
    const Typography t;
    CHECK(t.bodyLarge.span.fontSize == text::Sp(16));
    CHECK(t.displayLarge.span.fontSize == text::Sp(57));
    CHECK(&FromToken(t, tokens::TypographyKeyTokens::TitleMediumEmphasized) == &t.titleMediumEmphasized);
    Typography::Overrides o;
    o.titleLarge = text::TextStyle(30);                       // no family: takes the given one
    const Typography serif(text::FontFamily::Serif(), o);
    CHECK(serif.bodySmall.span.fontFamily == text::FontFamily::Serif());
    CHECK(serif.titleLarge.span.fontSize == text::Sp(30) && serif.titleLarge.span.fontFamily == text::FontFamily::Serif());
}

TEST(ShapesResolveTokensAndHalfRoundedForms) {
    const Shapes s;
    CHECK(s == Shapes{});
    CHECK(s.medium->topStart == foundation::CornerSize::Of(ui::Dp(12)));
    const auto top = Top(s.extraLarge);
    CHECK(top->topStart == foundation::CornerSize::Of(ui::Dp(28)) && top->bottomEnd == foundation::CornerSize{});
    const auto end = End(s.large);
    CHECK(end->topStart == foundation::CornerSize{} && end->topEnd == foundation::CornerSize::Of(ui::Dp(16)));
    CHECK(FromToken(s, tokens::ShapeKeyTokens::CornerFull) == foundation::CircleShape());
    CHECK(FromToken(s, tokens::ShapeKeyTokens::CornerNone) == foundation::RectangleShape());
    CHECK(FromToken(s, tokens::ShapeKeyTokens::CornerSmall) == s.small);
}

TEST(MaterialThemeProvidesItsValuesToTheSubtree) {
    Harness h;
    Color outer, inner, selectionHandle, contentFor;
    text::TextStyle style;
    MotionSchemePtr expressiveMotion, nestedMotion;
    Color expressiveContainer;
    h.Run([&] {
        MaterialTheme([&] {
            outer = MaterialTheme::colorScheme().primary;
            style = rt::Composition::Current()->Read(LocalTextStyle());
            selectionHandle = rt::Composition::Current()->Read(foundation::LocalTextSelectionColors).handleColor;
            MaterialThemeArgs dark;
            dark.colorScheme = DarkColorScheme();
            MaterialTheme(dark, [&] {
                inner = MaterialTheme::colorScheme().primary;
                // Not a role: LocalContentColor.
                rt::Composition::Current()->Provide(LocalContentColor, Color(0xFF00FF00u), [&] { contentFor = ContentColorFor(Color(0xFF010203u)); });
            });
            MaterialExpressiveTheme({}, [&] {
                expressiveMotion = MaterialTheme::motionScheme();
                expressiveContainer = MaterialTheme::colorScheme().onPrimaryContainer;
                // Inside an expressive theme, unset values keep the enclosing ones.
                MaterialExpressiveTheme({}, [&] { nestedMotion = MaterialTheme::motionScheme(); });
            });
        });
    });
    CHECK(outer == Color(0xFF6750A4u));
    CHECK(inner == Color(0xFFD0BCFFu));
    CHECK(style.span.fontSize == text::Sp(16));               // bodyLarge
    CHECK(selectionHandle == outer);
    CHECK(contentFor == Color(0xFF00FF00u));
    CHECK(expressiveMotion == MotionScheme::Expressive());
    CHECK(nestedMotion == MotionScheme::Expressive());
    CHECK(expressiveContainer == Color(0xFF4F378Bu));
}

TEST(MinimumInteractiveComponentSizeCentresSmallContent) {
    Harness h;
    ui::LayoutNode* box = nullptr;
    auto size = std::make_shared<rt::State<float>>(48.0f);
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalMinimumInteractiveComponentSize, ui::Dp(size->Get()), [&] {
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                Box(MinimumInteractiveComponentSize(Modifier{}), Alignment::TopStart(), false, [&] {
                    Box(SizeMod(Modifier{}, 20_dp, 20_dp));
                });
            });
        });
    });
    box = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(box->Size().width, 48);
    CHECK_EQ(box->Size().height, 48);
    CHECK_EQ(int(box->Children()[0]->InnerRectInRoot().left), 14);   // centred: (48 - 20) / 2
    // 0 dp turns enforcement off.
    size->Set(0.0f);
    for (int i = 0; i < 3; ++i) { rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 }); }
    box = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(box->Size().width, 20);
}

TEST(MaterialThemeProvidesTheRippleAsIndication) {
    Harness h;
    IndicationPtr inside, outside, offInside;
    bool offAddsNothing = false, onAddsNode = false;
    h.Run([&] {
        auto& c = *rt::Composition::Current();
        outside = c.Read(LocalIndication);
        MaterialTheme([&] {
            inside = c.Read(LocalIndication);
            onAddsNode = !inside->Create(ui::Modifier{}, std::make_shared<MutableInteractionSource>()).Empty();
            c.Provide(LocalRippleConfiguration, std::optional<RippleConfiguration>{}, [&] {
                offInside = c.Read(LocalIndication);
                offAddsNothing = offInside->Create(ui::Modifier{}, std::make_shared<MutableInteractionSource>()).Empty();
            });
        });
    });
    CHECK(!outside);
    CHECK(inside && inside == Ripple());
    CHECK(Ripple({ .bounded = false }) != Ripple());
    // A null LocalRippleConfiguration keeps the indication but it adds no ripple node.
    CHECK(offInside == inside);
    CHECK(onAddsNode && offAddsNothing);
    CHECK(std::fabs(ripple::RippleEndRadius({ 2, 1 }, true, { 30, 40 }) - 45) < 1e-4f);
    CHECK(std::fabs(ripple::RippleEndRadius({ 2, 1 }, false, { 30, 40 }) - 25) < 1e-4f);
}

TEST(PressRipplesFollowThePlatformTimeline) {
    using ripple::RippleDrawable;
    RippleDrawable d(true);
    d.SetRadius(40);
    d.SetBounds(101, 60);
    ui::DrawScope scope({}, ui::LayoutDirection::Ltr, {});
    d.SetHotspot(10, 20);
    d.SetPressedState(true);
    d.Draw(scope, 1000);
    CHECK(d.Sessions().size() == 1);
    const auto& s = d.Sessions()[0];
    CHECK(s.touchX == 10 && s.touchY == 20);
    CHECK(s.originX == 50 && s.originY == 30);   // Rect.centerX() is an integer
    CHECK(RippleDrawable::Progress(s, 1000) == 0);
    CHECK(std::fabs(RippleDrawable::Progress(s, 1450) - 0.5f) < 1e-5f);
    CHECK(RippleDrawable::NoisePhase(s, 1000) == 1000 && RippleDrawable::NoisePhase(s, 8000) == 1032);
    // A release during the enter waits for it to finish, then runs 375 ms to 1.
    d.SetPressedState(false);
    d.Draw(scope, 1200);
    CHECK(d.Sessions()[0].exitStart == 1450);
    CHECK(std::fabs(RippleDrawable::Progress(d.Sessions()[0], 1637) - (0.5f + 0.5f * 187 / 375)) < 1e-5f);
    d.Draw(scope, 1824);
    CHECK(d.Sessions().size() == 1 && d.Animating());
    d.Draw(scope, 1825);
    CHECK(d.Sessions().empty() && !d.Animating());
    // Pressed and released before a draw: the ripple still enters fully before exiting.
    d.SetPressedState(true);
    d.SetPressedState(false);
    d.Draw(scope, 3000);
    CHECK(d.Sessions().size() == 1 && d.Sessions()[0].exitStart == 3450);
    // A bounds change moves running ripples' centre and radius.
    d.SetRadius(12);
    d.SetBounds(40, 40);
    CHECK(d.Sessions()[0].originX == 20 && d.Sessions()[0].radius == 12 && d.Sessions()[0].width == 40);
}

TEST(RippleHostsArePooledFivePerWindow) {
    struct Key final : ripple::RippleHostKey {
        int resets = 0;
        void OnResetRippleHostView() override { ++resets; }
    };
    ripple::RippleContainer container;
    Key keys[6];
    ripple::RippleHostView* hosts[6];
    for (int i = 0; i < 6; ++i) hosts[i] = &container.GetRippleHostView(keys[i]);
    CHECK(container.HostCount() == 5);
    // The sixth takes the first host round-robin; its node loses it.
    CHECK(hosts[5] == hosts[0] && keys[0].resets == 1);
    CHECK(&container.GetRippleHostView(keys[5]) == hosts[5]);
    // A disposed host is reused before any other.
    container.DisposeRippleIfNeeded(keys[3]);
    Key late;
    CHECK(&container.GetRippleHostView(late) == hosts[3]);
    CHECK(container.HostCount() == 5);
}

TEST(MenuPositionsFollowTheCandidates) {
    using namespace kiln::material3::internal;
    const ui::Density d{ 2, 1 };
    DropdownMenuPositionProvider below({ ui::Dp(0), ui::Dp(0) }, d);
    // Below the anchor, start-aligned, when it fits inside the margins (16 px, 96 px).
    const ui::IntSize window{ 1000, 2000 }, menu{ 300, 400 };
    CHECK((below.CalculatePosition({ 100, 500, 300, 600 }, window, ui::LayoutDirection::Ltr, menu) == ui::IntOffset{ 100, 600 }));
    CHECK(below.GetTransformOrigin().pivotFractionX == 0 + float(200 - 100) / 300 * 0 + 1.0f / 3 && below.GetTransformOrigin().pivotFractionY == 0);
    // No room below: above the anchor; RTL aligns the ends.
    CHECK((below.CalculatePosition({ 600, 1700, 900, 1800 }, window, ui::LayoutDirection::Rtl, menu) == ui::IntOffset{ 600, 1300 }));
    // Wider than the window less the margins: centred.
    CHECK(below.CalculatePosition({ 0, 0, 10, 10 }, window, ui::LayoutDirection::Ltr, { 990, 10 }).x == 5);
    CHECK((CalculateTransformOrigin({ 0, 0, 10, 10 }, { 20, 20, 40, 40 }) == TransformOrigin{ 0, 0 }));
}

TEST(MaterialShapesAreNormalized) {
    for (auto* shape : { &MaterialShapes::Circle, &MaterialShapes::Heart, &MaterialShapes::Puffy, &MaterialShapes::SoftBoom, &MaterialShapes::PixelTriangle,
                         &MaterialShapes::Arch, &MaterialShapes::Oval, &MaterialShapes::Cookie12Sided }) {
        const auto b = (*shape)().CalculateBounds();
        CHECK(b[0] > -1e-3f && b[1] > -1e-3f && b[2] < 1 + 1e-3f && b[3] < 1 + 1e-3f);
        CHECK(std::max(b[2] - b[0], b[3] - b[1]) > 0.999f);   // fills the unit square on one axis
    }
}

TEST(IconsAndTextTakeTheContentColour) {
    Harness h;
    ui::LayoutNode* icon = nullptr;
    h.Run([&] {
        MaterialTheme([&] {
            auto& c = *rt::Composition::Current();
            c.Provide(LocalContentColor, Color(0xff112233), [&] {
                Column(ui::Modifier{}, Arrangement::Top(), AlignmentH::Start(), [] {
                    Icon(material3::internal::Icons::Filled::Check(), std::string("Done"));
                    Text("Hi");
                });
            });
        });
    });
    // The vector's 24 dp intrinsic size (density 1).
    std::function<void(ui::LayoutNode*)> find = [&](ui::LayoutNode* n) {
        if (n->Size().width == 24 && n->Size().height == 24) icon = n;
        for (ui::LayoutNode* c : n->Children()) find(c);
    };
    find(&h.owner.Root());
    CHECK(icon != nullptr);
    CHECK(material3::internal::Icons::AutoMirrored::Filled::KeyboardArrowLeft()->autoMirror);
    CHECK(material3::internal::Icons::Filled::Check()->root->children.size() == 1);
}

TEST(ButtonDefaultsFollowTheThemeAndExpressiveSizeThresholds) {
    Harness h;
    h.Run([&] {
        MaterialTheme([&] {
            const auto colors = ButtonDefaults::buttonColors();
            const auto& scheme = MaterialTheme::colorScheme();
            CHECK(colors.ContainerColor(true) == scheme.primary);
            CHECK(colors.ContentColor(true) == scheme.onPrimary);
            CHECK(colors.ContainerColor(false) == scheme.onSurface.WithAlpha(0.1f));
            const auto copy = colors.Copy(Color::Unspecified(), Color(0xff123456));
            CHECK(copy.containerColor == colors.containerColor && copy.contentColor == Color(0xff123456));
            CHECK(ButtonDefaults::outlinedButtonColors().containerColor == Color::Transparent());
            CHECK(ButtonDefaults::elevatedButtonElevation().defaultElevation == 1_dp);
            CHECK(ButtonDefaults::elevatedButtonElevation().hoveredElevation == 3_dp);
            CHECK(ButtonDefaults::iconSizeFor(39_dp) == ButtonDefaults::ExtraSmallIconSize);
            CHECK(ButtonDefaults::iconSizeFor(40_dp) == ButtonDefaults::SmallIconSize);
            CHECK(ButtonDefaults::iconSizeFor(56_dp) == ButtonDefaults::MediumIconSize);
            CHECK(ButtonDefaults::textStyleFor(56_dp) == MaterialTheme::typography().titleMedium);
            CHECK(ButtonDefaults::shapesFor(34_dp).pressedShape->Equals(*ButtonDefaults::extraSmallPressedShape()));
            CHECK(ButtonDefaults::shapesFor(35_dp).pressedShape->Equals(*ButtonDefaults::pressedShape()));
        });
    });
}

TEST(ButtonsProvideLabelStyleAndKeepDisabledSemantics) {
    rt::State<bool> enabled{ true };
    Harness h;
    Color contentColor;
    text::TextStyle style;
    int clicks = 0;
    h.Run([&] {
        MaterialTheme([&] {
            Column({}, [&] {
                ButtonOptions options;
                options.enabled = enabled.Get();
                Button([&] { ++clicks; }, {}, options, [&] {
                    auto& c = *rt::Composition::Current();
                    contentColor = c.Read(LocalContentColor);
                    style = c.Read(LocalTextStyle());
                    Box(SizeMod({}, 10_dp, 10_dp));
                });
            });
        });
    });
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(tree.children.size(), std::size_t(1));
    CHECK(tree.children[0].config.role == ui::Role::Button);
    CHECK(!tree.children[0].config.disabled);
    CHECK(tree.children[0].config.onClick && tree.children[0].config.onClick());
    CHECK_EQ(clicks, 1);
    CHECK(contentColor == LightColorScheme().onPrimary);
    CHECK(style.span.fontSize == Typography{}.labelLarge.span.fontSize);
    CHECK_EQ(h.owner.Root().Children()[0]->Children()[0]->Size().width, 58);
    CHECK_EQ(h.owner.Root().Children()[0]->Children()[0]->Size().height, 48);
    enabled.Set(false);
    for (int i = 0; i < 3; ++i) { rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 }); }
    tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children[0].config.disabled);
    CHECK(contentColor == LightColorScheme().onSurfaceVariant.WithAlpha(0.38f));
}

TEST(ButtonElevationFollowsTheLastInteractionAndSnapsWhenDisabled) {
    rt::State<bool> enabled{ true };
    auto source = std::make_shared<MutableInteractionSource>();
    Harness h;
    ui::Dp elevation;
    h.Run([&] { elevation = ShadowElevation({ 1_dp, 2_dp, 3_dp, 4_dp, 0_dp }, enabled.Get(), source); });
    std::int64_t clock = 0;
    auto settle = [&] {
        for (int i = 0; i < 26; ++i) {
            clock += 16'000'000;
            rt::Dispatcher::Main().RunFrame(clock);
            rt::Composition::RecomposeAll();
        }
    };
    CHECK(elevation == 1_dp);
    const auto hover = source->Start(Interaction::Kind::Hover);
    settle();
    CHECK(elevation == 4_dp);
    const auto press = source->Start(Interaction::Kind::Press);
    settle();
    CHECK(elevation == 2_dp);
    source->Cancel(Interaction::Kind::Press, press);
    settle();
    CHECK(elevation == 4_dp);
    enabled.Set(false);
    rt::Composition::RecomposeAll();
    rt::Composition::RecomposeAll();
    CHECK(elevation == 0_dp);
    source->End(Interaction::Kind::Hover, hover);
}

TEST(BadgeDoesNotResizeItsAnchorAndUsesContentMinimumSize) {
    Harness h;
    h.Run([&] {
        MaterialTheme([&] {
            Column({}, [] {
                BadgedBox([] { Badge(); }, {}, [] { Box(SizeMod({}, 24_dp, 24_dp)); });
                Badge({}, std::nullopt, std::nullopt, [] { Box(SizeMod({}, 20_dp, 10_dp)); });
            });
        });
    });
    const auto& children = h.owner.Root().Children()[0]->Children();
    CHECK_EQ(children.size(), std::size_t(2));
    CHECK_EQ(children[0]->Size().width, 24);
    CHECK_EQ(children[0]->Size().height, 24);
    const auto& badge = *children[0]->Children()[1];
    CHECK_EQ(badge.Size().width, 6);
    CHECK_EQ(badge.Size().height, 6);
    CHECK_EQ(int(badge.InnerRectInRoot().left), 18);
    CHECK_EQ(int(badge.InnerRectInRoot().top), 0);
    CHECK_EQ(children[1]->Size().width, 28);  // 20 + 4 dp padding on each side
    CHECK_EQ(children[1]->Size().height, 16);
}
