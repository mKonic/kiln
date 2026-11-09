#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/Checkbox.h"
#include "kiln/material3/Card.h"
#include "kiln/material3/Chip.h"
#include "kiln/material3/ListItem.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/RadioButton.h"
#include "kiln/material3/Switch.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/IconButton.h"
#include "kiln/material3/FloatingActionButton.h"
#include "kiln/material3/FloatingActionButtonMenu.h"
#include "kiln/material3/ToggleButton.h"
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
#include "kiln/ui/Accessibility.h"

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
    h.Run([&] { elevation = ShadowElevation(ButtonElevation{ 1_dp, 2_dp, 3_dp, 4_dp, 0_dp }, enabled.Get(), source); });
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

TEST(ToggleButtonsResolveSizeColorAndCheckboxSemantics) {
    using Toggle = void (*)(bool, std::function<void(bool)>, const ui::Modifier&, const ToggleButtonOptions&, std::function<void()>);
    for (Toggle toggle : { ToggleButton, ElevatedToggleButton, FilledTonalToggleButton, OutlinedToggleButton }) {
        rt::State<bool> checked{ false };
        Harness h;
        text::TextStyle label;
        h.Run([&] {
            MaterialTheme([&] {
                CHECK(ToggleButtonDefaults::colors().checkedContainerColor == MaterialTheme::colorScheme().primary);
                CHECK(FilledTonalToggleButtonDefaults::colors().checkedContainerColor == MaterialTheme::colorScheme().secondary);
                CHECK(!OutlinedToggleButtonDefaults::border(true, true));
                CHECK(ToggleButtonDefaults::shapesFor(ToggleButtonSize::Medium).checkedShape->Equals(*MaterialTheme::shapes().large));
                Column({}, [&] {
                    ToggleButtonOptions options;
                    options.buttonSize = ToggleButtonSize::Medium;
                    options.icon = [] { Box(SizeMod({}, 24_dp, 24_dp)); };
                    toggle(checked.Get(), [&](bool value) { checked.Set(value); }, {}, options, [&] {
                        label = rt::Composition::Current()->Read(LocalTextStyle());
                        Box(SizeMod({}, 10_dp, 10_dp));
                    });
                });
            });
        });
        CHECK(label == Typography{}.titleMedium);
        CHECK(h.owner.Root().Children()[0]->Children()[0]->Size().height >= 56);
        const auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(1));
        CHECK(tree.children[0].config.role == ui::Role::Checkbox);
        CHECK_EQ(*tree.children[0].config.toggleableState, 0);
        CHECK(tree.children[0].config.onClick());
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(checked.Peek());
        CHECK_EQ(*h.owner.BuildSemanticsTree().children[0].config.toggleableState, 1);
    }
}

TEST(ToggleBorderAnimatesOutAndPreservesGradientBrushes) {
    rt::State<bool> visible{ true };
    Harness h;
    std::optional<BorderStroke> border;
    h.Run([&] {
        MaterialTheme([&] {
            border = material3::internal::AnimateBorderStrokeAsState(visible.Get()
                ? std::optional(BorderStroke(4_dp, gfx::Brush::HorizontalGradient({ Color(0xffff0000), Color(0xff0000ff) })))
                : std::nullopt);
        });
    });
    CHECK(border && border->width == 4_dp && border->brush.kind == gfx::Brush::Kind::Linear);
    visible.Set(false);
    rt::Composition::RecomposeAll();
    rt::Dispatcher::Main().RunFrame(0);
    rt::Composition::RecomposeAll();
    rt::Dispatcher::Main().RunFrame(32'000'000);
    rt::Composition::RecomposeAll();
    CHECK(border && border->width.value > 0 && border->width.value < 4);
    for (std::int64_t ms = 48; ms <= 2000; ms += 16) {
        rt::Dispatcher::Main().RunFrame(ms * 1'000'000);
        rt::Composition::RecomposeAll();
    }
    CHECK(!border);
}

TEST(PrecisionPointerDevicesRequireAPhysicalKeyboardAndNonTabletMouse) {
    using Device = ui::InputDeviceInfo;
    std::vector<bool> listening;
    ui::InputDevices devices;
    devices.SetListenerHost([&](bool enabled) { listening.push_back(enabled); });
    devices.AcquireListener();
    devices.AcquireListener();
    CHECK(listening == std::vector<bool>{ true });
    devices.Update({ 1, false, Device::SourceKeyboard, Device::Alphabetic });
    CHECK(!devices.HasKeyboardAndMouse());
    devices.Update({ 2, true, Device::SourceMouse, 0 });
    CHECK(!devices.HasKeyboardAndMouse());  // virtual mouse
    devices.Update({ 2, false, Device::SourceMouse | Device::SourceStylus, 0 });
    CHECK(!devices.HasKeyboardAndMouse());  // drawing tablet
    devices.Update({ 2, false, Device::SourceMouse, 0 });
    CHECK(devices.HasKeyboardAndMouse());
    devices.Update({ 1, false, Device::SourceKeyboard, 1 });
    CHECK(!devices.HasKeyboardAndMouse());  // non-alphabetic keypad
    devices.Update({ 1, false, Device::SourceKeyboard, Device::Alphabetic });
    CHECK(devices.HasKeyboardAndMouse());
    devices.Remove(2);
    CHECK(!devices.HasKeyboardAndMouse());
    devices.Update({ 3, false, Device::SourceKeyboard | Device::SourceMouse, Device::Alphabetic });
    CHECK(devices.HasKeyboardAndMouse());  // one combined device can supply both
    devices.ReleaseListener();
    CHECK(listening.size() == 1 && devices.HasKeyboardAndMouse());
    devices.ReleaseListener();
    CHECK((listening == std::vector<bool>{ true, false }));
    CHECK(!devices.HasKeyboardAndMouse());
    devices.Update({ 4, false, Device::SourceKeyboard | Device::SourceMouse, Device::Alphabetic });
    CHECK(!devices.HasKeyboardAndMouse());  // callback after disposal
}

TEST(PrecisionPointerSizingRegistersOnlyOptedInThemeHierarchies) {
    const bool oldFlag = ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled;
    ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled = false;
    rt::State<int> version{ 0 };
    rt::State<bool> visible{ true };
    std::vector<bool> listening;
    Harness h;
    h.owner.Devices().SetListenerHost([&](bool enabled) { listening.push_back(enabled); });
    ui::Dp height;
    h.Run([&] {
        version.Get();
        auto& c = *rt::Composition::Current();
        c.Provide(ui::LocalOwner, &h.owner, [&] {
            if (!visible.Get()) return;
            MaterialTheme([&] { MaterialTheme([&] { height = ButtonDefaults::MinHeight(); }); });
            MaterialTheme([] {});  // sibling hierarchy, same platform listener
        });
    });
    CHECK(height == 40_dp && listening.empty());
    ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled = true;
    version.Set(1);
    rt::Composition::RecomposeAll();
    CHECK_EQ(h.owner.Devices().ListenerCount(), 2);
    CHECK(listening == std::vector<bool>{ true });
    using Device = ui::InputDeviceInfo;
    h.owner.Devices().Update({ 1, false, Device::SourceKeyboard, Device::Alphabetic });
    h.owner.Devices().Update({ 2, false, Device::SourceMouse, 0 });
    for (int i = 0; i < 3; ++i) rt::Composition::RecomposeAll();
    CHECK(height == 36_dp);
    h.owner.Devices().Remove(1);
    for (int i = 0; i < 3; ++i) rt::Composition::RecomposeAll();
    CHECK(height == 40_dp);
    visible.Set(false);
    rt::Composition::RecomposeAll();
    CHECK_EQ(h.owner.Devices().ListenerCount(), 0);
    CHECK((listening == std::vector<bool>{ true, false }));
    ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled = oldFlag;
}

TEST(IconButtonDefaultsFollowContentColorAndExpressiveWidths) {
    Harness h;
    h.Run([] {
        MaterialTheme([] {
            const Color content(0xff123456);
            rt::Composition::Current()->Provide(LocalContentColor, content, [content] {
                CHECK(IconButtonDefaults::iconButtonColors().contentColor == content);
                CHECK(IconButtonDefaults::iconButtonColors().disabledContentColor == content.WithAlpha(0.38f));
                CHECK(IconButtonDefaults::outlinedIconButtonBorder(true).brush == gfx::Brush::SolidColor(content));
                CHECK(!IconButtonDefaults::outlinedIconToggleButtonBorder(true, true));
                const auto colors = IconButtonDefaults::filledTonalIconToggleButtonColors();
                const auto& scheme = MaterialTheme::colorScheme();
                CHECK(colors.ContainerColor(true, false) == scheme.secondaryContainer);
                CHECK(colors.ContainerColor(true, true) == scheme.secondary);
                CHECK(colors.ContentColor(false, true) == colors.disabledContentColor);
                CHECK(colors.Copy().checkedContentColor == colors.checkedContentColor);
            });
        });
    });
    CHECK((IconButtonDefaults::extraSmallContainerSize() == ui::DpSize{ 32_dp, 32_dp }));
    CHECK((IconButtonDefaults::smallContainerSize(IconButtonWidthOption::Narrow) == ui::DpSize{ 32_dp, 40_dp }));
    CHECK((IconButtonDefaults::smallContainerSize() == ui::DpSize{ 40_dp, 40_dp }));
    CHECK((IconButtonDefaults::smallContainerSize(IconButtonWidthOption::Wide) == ui::DpSize{ 52_dp, 40_dp }));
    CHECK((IconButtonDefaults::mediumContainerSize() == ui::DpSize{ 56_dp, 56_dp }));
    CHECK((IconButtonDefaults::largeContainerSize() == ui::DpSize{ 96_dp, 96_dp }));
    CHECK((IconButtonDefaults::extraLargeContainerSize() == ui::DpSize{ 136_dp, 136_dp }));
}

TEST(IconToggleVariantsExposeCheckboxStateAndHandlePresses) {
    using Toggle = void (*)(bool, std::function<void(bool)>, const ui::Modifier&, const IconToggleButtonOptions&, std::function<void()>);
    for (Toggle toggle : { IconToggleButton, FilledIconToggleButton, FilledTonalIconToggleButton, OutlinedIconToggleButton }) {
        rt::State<bool> checked{ false }, enabled{ true };
        auto source = std::make_shared<MutableInteractionSource>();
        Harness h;
        Color content;
        h.Run([&] {
            MaterialTheme([&] {
                Column({}, [&] {
                    IconToggleButtonOptions options;
                    options.enabled = enabled.Get();
                    options.interactionSource = source;
                    toggle(checked.Get(), [&](bool value) { checked.Set(value); }, {}, options, [&] {
                        content = rt::Composition::Current()->Read(LocalContentColor);
                        Box(SizeMod({}, 24_dp, 24_dp));
                    });
                });
            });
        });
        auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(1));
        CHECK(tree.children[0].config.role == ui::Role::Checkbox);
        CHECK_EQ(*tree.children[0].config.toggleableState, 0);
        CHECK_EQ(h.owner.Root().Children()[0]->Children()[0]->Size().width, 48);
        auto mouse = [&](ui::PointerEventType type, bool down, std::int64_t time) {
            ui::RawPointerEvent event;
            event.type = type;
            event.uptimeMillis = time;
            event.buttons = down ? 1 : 0;
            event.pointers.push_back({ 1, { 24, 24 }, down, ui::PointerType::Mouse, {} });
            h.owner.ProcessPointer(event);
            rt::Dispatcher::Main().RunPending();
        };
        mouse(ui::PointerEventType::Press, true, 0);
        CHECK(source->IsPressed());
        mouse(ui::PointerEventType::Release, false, 50);
        CHECK(checked.Peek() && !source->IsPressed());
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        CHECK_EQ(*h.owner.BuildSemanticsTree().children[0].config.toggleableState, 1);
        enabled.Set(false);
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(h.owner.BuildSemanticsTree().children[0].config.disabled);
        const bool inheritsContent = toggle == IconToggleButton || toggle == OutlinedIconToggleButton;
        CHECK(content == (inheritsContent ? Color(0xff000000) : LightColorScheme().onSurface).WithAlpha(0.38f));
        mouse(ui::PointerEventType::Press, true, 100);
        mouse(ui::PointerEventType::Release, false, 150);
        CHECK(checked.Peek());
    }
}

TEST(FloatingActionButtonsResolveSizeStyleAndSemantics) {
    using Fab = void (*)(std::function<void()>, const ui::Modifier&, const FloatingActionButtonOptions&, std::function<void()>);
    const Fab fabs[] = { SmallFloatingActionButton, FloatingActionButton, MediumFloatingActionButton, LargeFloatingActionButton };
    const int sizes[] = { 48, 56, 80, 96 };  // small's 40 dp visual still reserves 48 dp for input
    for (int i = 0; i < 4; ++i) {
        Harness h;
        Color color;
        text::TextStyle style;
        int clicks = 0;
        h.Run([&] {
            MaterialTheme([&] {
                CHECK(FloatingActionButtonDefaults::elevation().TonalElevation() == 6_dp);
                CHECK(FloatingActionButtonDefaults::loweredElevation().hoveredElevation == 3_dp);
                CHECK(FloatingActionButtonDefaults::bottomAppBarFabElevation().TonalElevation() == 0_dp);
                Column({}, [&] {
                    fabs[i]([&] { ++clicks; }, {}, {}, [&] {
                        color = rt::Composition::Current()->Read(LocalContentColor);
                        style = rt::Composition::Current()->Read(LocalTextStyle());
                        Box(SizeMod({}, 24_dp, 24_dp));
                    });
                    fabs[i]([] {}, SizeIn({}, 10_dp, 10_dp, Dp::Unspecified(), Dp::Unspecified()), {}, [] { Box(SizeMod({}, 10_dp, 10_dp)); });
                });
            });
        });
        const auto* fab = h.owner.Root().Children()[0]->Children()[0];
        CHECK_EQ(fab->Size().width, sizes[i]);
        CHECK_EQ(fab->Size().height, sizes[i]);
        CHECK(color == LightColorScheme().onPrimaryContainer);
        CHECK(style == Typography{}.labelLarge);
        const auto* constrained = h.owner.Root().Children()[0]->Children()[1];
        CHECK_EQ(constrained->Size().width, i == 1 ? 48 : sizes[i]);
        CHECK_EQ(constrained->Size().height, i == 1 ? 48 : sizes[i]);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 2 && tree.children[0].config.role == ui::Role::Button);
        CHECK(tree.children[0].config.onClick());
        CHECK_EQ(clicks, 1);
    }
}

TEST(ExtendedFloatingActionButtonsCollapseAndClearOnlyLabelSemantics) {
    using Fab = void (*)(std::function<void()>, std::function<void()>, std::function<void()>, const ui::Modifier&, bool, const FloatingActionButtonOptions&);
    const Fab fabs[] = { ExtendedFloatingActionButton, SmallExtendedFloatingActionButton, MediumExtendedFloatingActionButton, LargeExtendedFloatingActionButton };
    const int sizes[] = { 56, 56, 80, 96 };
    const text::TextStyle styles[] = { Typography{}.labelLarge, Typography{}.titleMedium, Typography{}.titleLarge, Typography{}.headlineSmall };
    for (int i = 0; i < 4; ++i) {
        rt::State<bool> expanded{ true };
        Harness h;
        text::TextStyle style;
        h.Run([&] {
            MaterialTheme([&] {
                Column({}, [&] {
                    fabs[i]([&] {
                        style = rt::Composition::Current()->Read(LocalTextStyle());
                        Box(Semantics(SizeMod({}, 120_dp, 20_dp), [](ui::SemanticsConfiguration& c) { c.contentDescription = { "Label" }; }));
                    }, [] {
                        Box(Semantics(SizeMod({}, 24_dp, 24_dp), [](ui::SemanticsConfiguration& c) { c.contentDescription = { "Create" }; }));
                    }, [] {}, {}, expanded.Get(), {});
                });
            });
        });
        auto fab = [&]() { return h.owner.Root().Children()[0]->Children()[0]; };
        const int fullWidth = fab()->Size().width;
        CHECK(fullWidth > sizes[i]);
        CHECK_EQ(fab()->Size().height, sizes[i]);
        CHECK(style == styles[i]);
        CHECK(h.owner.BuildSemanticsTree().children[0].config.contentDescription == std::vector<std::string>{ "Create" });
        std::int64_t clock = 0;
        auto settle = [&] {
            for (int frame = 0; frame < 130; ++frame) {
                rt::Composition::RecomposeAll();
                rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll();
                h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        expanded.Set(false);
        settle();
        CHECK_EQ(fab()->Size().width, sizes[i]);
        CHECK_EQ(fab()->Size().height, sizes[i]);
        CHECK(h.owner.BuildSemanticsTree().children[0].config.contentDescription == std::vector<std::string>{ "Create" });
        expanded.Set(true);
        settle();
        CHECK_EQ(fab()->Size().width, fullWidth);
    }
}

TEST(TextOnlyExtendedFabsUseTheirOwnMinimumsAndTypography) {
    using Fab = void (*)(std::function<void()>, const ui::Modifier&, const FloatingActionButtonOptions&, std::function<void()>);
    const Fab fabs[] = { ExtendedFloatingActionButton, SmallExtendedFloatingActionButton, MediumExtendedFloatingActionButton, LargeExtendedFloatingActionButton };
    const int widths[] = { 80, 56, 80, 96 }, heights[] = { 56, 56, 80, 96 };
    const text::TextStyle styles[] = { Typography{}.labelLarge, Typography{}.titleMedium, Typography{}.titleLarge, Typography{}.headlineSmall };
    Harness h;
    h.Run([&] {
        MaterialTheme([&] {
            Column({}, [&] {
                for (int i = 0; i < 4; ++i) fabs[i]([] {}, {}, {}, [&, i] {
                    CHECK(rt::Composition::Current()->Read(LocalTextStyle()) == styles[i]);
                    Box(SizeMod({}, 10_dp, 10_dp));
                });
            });
        });
    });
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(h.owner.Root().Children()[0]->Children()[i]->Size().width, widths[i]);
        CHECK_EQ(h.owner.Root().Children()[0]->Children()[i]->Size().height, heights[i]);
    }
}

TEST(FabElevationKeepsEqualTargetAnimationsAndSnapsOnlyChangedTargets) {
    auto source = std::make_shared<MutableInteractionSource>();
    rt::State<int> sourceVersion{ 0 };
    rt::State<FloatingActionButtonElevation> values{ { 1_dp, 6_dp, 6_dp, 8_dp } };
    Harness h;
    ui::Dp elevation;
    h.Run([&] { sourceVersion.Get(); elevation = ShadowElevation(values.Get(), source); });
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
        }
    };
    CHECK(elevation == 1_dp);
    const auto press = source->Start(Interaction::Kind::Press);
    advance(3);
    CHECK(elevation.value > 1 && elevation.value < 6);
    const auto focus = source->Start(Interaction::Kind::Focus);  // equal target; do not cancel the press animation
    advance(25);
    CHECK(elevation == 6_dp);
    source->End(Interaction::Kind::Focus, focus);
    source->End(Interaction::Kind::Press, press);
    advance(25);
    CHECK(elevation == 1_dp);
    const auto hover = source->Start(Interaction::Kind::Hover);
    advance(3);
    CHECK(elevation.value > 1 && elevation.value < 8);
    values.Set({ 2_dp, 6_dp, 6_dp, 8_dp });  // changing default must not interrupt hover
    rt::Composition::RecomposeAll();
    advance(25);
    CHECK(elevation == 8_dp);
    values.Set({ 2_dp, 6_dp, 6_dp, 9_dp });
    rt::Composition::RecomposeAll();
    rt::Composition::RecomposeAll();
    CHECK(elevation == 9_dp);  // current interaction's changed configuration snaps
    source->End(Interaction::Kind::Hover, hover);
    advance(25);
    CHECK(elevation == 2_dp);
    auto oldSource = source;
    source = std::make_shared<MutableInteractionSource>();
    sourceVersion.Set(1);
    rt::Composition::RecomposeAll();
    oldSource->Start(Interaction::Kind::Press);
    advance(25);
    CHECK(elevation == 2_dp);
    source->Start(Interaction::Kind::Hover);
    advance(25);
    CHECK(elevation == 9_dp);
}

TEST(FabVisibilityStartsHiddenInterruptsAndStopsPlacingAtZeroAlpha) {
    rt::State<bool> visible{ false }, attached{ true };
    Harness h;
    int compositions = 0;
    h.Run([&] {
        ++compositions;
        MaterialTheme([&] {
            Column({}, [&] {
                if (attached.Get()) FloatingActionButton([] {}, AnimateFloatingActionButton({}, visible.Get(), Alignment::BottomEnd(), 0.2f,
                                                                                            anim::Tween(160), anim::Tween(160)), {}, [&] {
                    Box(Semantics(SizeMod({}, 56_dp, 56_dp), [](ui::SemanticsConfiguration& c) { c.contentDescription = { "Animated child" }; }));
                });
            });
        });
    });
    auto fab = [&]() { return h.owner.Root().Children()[0]->Children()[0]; };
    CHECK_EQ(fab()->Size().width, 0);
    CHECK(h.owner.BuildSemanticsTree().children.empty());
    CHECK(!fab()->Children()[0]->IsPlaced());
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    visible.Set(true);
    advance(4);
    CHECK_EQ(fab()->Size().width, 56);
    CHECK(fab()->Children()[0]->IsPlaced());
    visible.Set(false);
    advance(4);
    CHECK_EQ(fab()->Size().width, 56);  // stays measured during fade-out
    visible.Set(true);
    advance(4);
    const int afterUpdate = compositions;
    advance(20);
    CHECK_EQ(compositions, afterUpdate);  // animation reads invalidate measure/draw, not composition
    CHECK_EQ(fab()->Size().width, 56);
    h.owner.SetInputMode(ui::InputMode::Keyboard);
    ui::FocusTargetNode* focus = nullptr;
    for (const auto* node : fab()->ModifierNodes())
        if (auto* target = const_cast<ui::ModifierNode*>(node)->AsFocusTarget()) focus = target;
    CHECK(focus && focus->RequestFocus());
    const int semanticsId = fab()->SemanticsId();
    CHECK(h.owner.Accessibility().Nodes().contains(semanticsId));
    visible.Set(false);
    advance(20);
    CHECK_EQ(fab()->Size().width, 0);
    CHECK(h.owner.BuildSemanticsTree().children.empty());
    CHECK(!h.owner.Accessibility().Nodes().contains(semanticsId));
    h.owner.ClearFocus(true);
    CHECK(!h.owner.MoveFocus(ui::FocusDirection::Next));
    CHECK(!fab()->Children()[0]->IsPlaced());
    visible.Set(true);
    advance(4);
    attached.Set(false);  // disposal during animation cancels both jobs
    advance(20);
    CHECK(h.owner.Root().Children()[0]->Children().empty());
}

TEST(ToggleFabsKeepTheirOriginalAllocationWhileContainerAndIconMorph) {
    using namespace ToggleFloatingActionButtonDefaults;
    for (auto direction : { ui::LayoutDirection::Ltr, ui::LayoutDirection::Rtl }) {
        rt::State<bool> checked{ false };
        Harness h;
        h.owner.SetDirection(direction);
        float progress = -1;
        h.Run([&] {
            MaterialTheme([&] {
                CHECK(containerSizeMedium()(0) == 80_dp && containerSizeMedium()(1) == 56_dp);
                CHECK(containerSizeLarge()(0) == 96_dp && containerSizeLarge()(1) == 56_dp);
                CHECK(containerCornerRadius()(0) == 16_dp && containerCornerRadius()(1) == 28_dp);
                CHECK(iconSizeLarge()(0) == 36_dp && iconSizeLarge()(1) == 20_dp);
                CHECK(containerColor()(0) == MaterialTheme::colorScheme().primaryContainer);
                CHECK(iconColor()(1) == MaterialTheme::colorScheme().onPrimary);
                Column({}, [&] {
                    ToggleFloatingActionButtonOptions options;
                    options.containerSize = containerSizeMedium();
                    options.containerCornerRadius = containerCornerRadiusMedium();
                    ToggleFloatingActionButton(checked.Get(), [&](bool value) { checked.Set(value); }, {}, options, [&](const ToggleFloatingActionButtonScope& scope) {
                        progress = scope.checkedProgress;
                        Box(AnimateIcon({}, scope.checkedProgress, {}, iconSizeMedium()));
                    });
                });
            });
        });
        auto outer = [&]() { return h.owner.Root().Children()[0]->Children()[0]; };
        CHECK_EQ(outer()->Size().width, 80);
        CHECK_EQ(outer()->Children()[0]->Size().width, 80);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 1 && tree.children[0].config.toggleableState == 0);
        CHECK(tree.children[0].config.onClick());
        CHECK(checked.Peek());
        for (std::int64_t ms = 0; ms < 2000; ms += 16) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(ms * 1'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
        CHECK(progress == 1);
        CHECK_EQ(outer()->Size().width, 80);
        CHECK_EQ(outer()->Children()[0]->Size().width, 56);
        CHECK_EQ(outer()->Children()[0]->Position().x, direction == ui::LayoutDirection::Ltr ? 24 : 0);
        CHECK_EQ(outer()->Children()[0]->Children()[0]->Size().width, 20);
        CHECK(h.owner.BuildSemanticsTree().children[0].config.toggleableState == 1);
    }
}

TEST(FabMenusComposeItemsOnFirstExpandAndKeepThemAfterCollapse) {
    rt::State<bool> expanded{ false };
    Harness h;
    int itemCompositions = 0;
    h.Run([&] {
        MaterialTheme([&] {
            Column({}, [&] {
                FloatingActionButtonMenu(expanded.Get(), [] { FloatingActionButton([] {}, {}, {}, [] { Box(SizeMod({}, 24_dp, 24_dp)); }); }, {}, AlignmentH::End(), [&](const FloatingActionButtonMenuScope& scope) {
                    ++itemCompositions;
                    for (int i = 0; i < 3; ++i) FloatingActionButtonMenuItem(scope, [] {}, [] { Box(SizeMod({}, 80_dp, 20_dp)); }, [] { Box(SizeMod({}, 24_dp, 24_dp)); });
                });
            });
        });
    });
    CHECK_EQ(itemCompositions, 0);
    auto menu = [&]() { return h.owner.Root().Children()[0]->Children()[0]; };
    CHECK_EQ(menu()->Size().height, 72);  // 56 dp trigger + 16 dp bottom padding
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    expanded.Set(true);
    advance(130);
    CHECK(itemCompositions > 0);
    CHECK_EQ(menu()->Children()[0]->Children().size(), std::size_t(3));
    CHECK_EQ(menu()->Size().height, 256);  // 3*56 + 2*4 + trigger56 + bottom16 + gap8
    auto* items = menu()->Children()[0];
    CHECK_EQ(items->Children()[0]->Position().y, 0);
    CHECK_EQ(items->Children()[1]->Position().y, 60);
    CHECK_EQ(items->Children()[2]->Position().y, 120);
    CHECK_EQ(menu()->Children()[1]->Position().y, 184);
    expanded.Set(false);
    advance(4);
    expanded.Set(true);
    advance(4);
    expanded.Set(false);
    advance(130);
    CHECK_EQ(menu()->Children()[0]->Children().size(), std::size_t(3));
    CHECK_EQ(menu()->Size().height, 72);
    const auto tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children.size() == 1 && tree.children[0].config.role == ui::Role::Button);
    expanded.Set(true);
    advance(130);
    CHECK_EQ(menu()->Size().height, 256);
}

TEST(FabMenusStartExpandedScrollWithinConstraintsAndHandOffKeyboardFocus) {
    const auto originalSpring = anim::Spring(0.8f, 100, 0.01f);
    const auto countSpring = anim::WithSpringVisibilityThreshold(originalSpring, 1);
    const anim::Vec from{}, to{ 4, 0, 0, 0 }, velocity{};
    CHECK(countSpring->ValueAt(32'000'000, from, to, velocity, 1) == originalSpring->ValueAt(32'000'000, from, to, velocity, 1));
    CHECK(countSpring->DurationNanos(from, to, velocity, 1) < originalSpring->DurationNanos(from, to, velocity, 1));
    const auto tween = anim::Tween(200);
    CHECK(anim::WithSpringVisibilityThreshold(tween, 1) == tween);
    Harness h;
    h.owner.SetInputMode(ui::InputMode::Keyboard);
    h.Run([&] {
        MaterialTheme([&] {
            Column({}, [&] {
                FloatingActionButtonMenu(true, [] { FloatingActionButton([] {}, {}, {}, [] { Box(SizeMod({}, 24_dp, 24_dp)); }); },
                                          SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified(), 160_dp), AlignmentH::Start(), [&](const FloatingActionButtonMenuScope& scope) {
                    for (int i = 0; i < 4; ++i) FloatingActionButtonMenuItem(scope, [] {}, [] { Box(SizeMod({}, 80_dp, 20_dp)); }, [] { Box(SizeMod({}, 24_dp, 24_dp)); });
                });
            });
        });
    });
    auto* menu = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(menu->Size().height, 160);
    auto* items = menu->Children()[0];
    CHECK_EQ(items->Children().size(), std::size_t(4));
    ui::SemanticsConfiguration config;
    CHECK(ui::CalculateSemanticsConfiguration(*items, config));
    CHECK(config.isTraversalGroup && config.traversalIndex == -0.9f);
    CHECK(config.verticalScrollAxisRange && config.verticalScrollAxisRange->maxValue() > 0);
    auto* trigger = menu->Children()[1]->Children()[0];
    ui::FocusTargetNode* focus = nullptr;
    for (const auto* node : trigger->ModifierNodes())
        if (auto* target = const_cast<ui::ModifierNode*>(node)->AsFocusTarget()) focus = target;
    CHECK(focus && focus->RequestFocus());
    CHECK(h.owner.DispatchKeyEvent({ .keyCode = ui::keys::Tab, .type = ui::KeyEventType::KeyDown }));
    CHECK(h.owner.ActiveFocus() && h.owner.ActiveFocus()->Owner() == items->Children()[0]);
}

TEST(SelectionControlColorsResolveEveryStateAndKeepUnspecifiedOverrides) {
    Harness h;
    h.Run([] {
        MaterialTheme([] {
            const auto& scheme = MaterialTheme::colorScheme();
            const auto checkbox = CheckboxDefaults::colors();
            CHECK(checkbox.checkedBoxColor == scheme.primary);
            CHECK(checkbox.checkedCheckmarkColor == scheme.onPrimary);
            CHECK(checkbox.uncheckedBoxColor == Color::Transparent());
            CHECK(checkbox.uncheckedBorderColor == scheme.onSurfaceVariant);
            CHECK(checkbox.disabledCheckedBoxColor == scheme.onSurface.WithAlpha(0.38f));
            CHECK(checkbox.disabledUncheckedBorderColor == scheme.onSurface.WithAlpha(0.38f));
            CHECK(checkbox.disabledCheckmarkColor == scheme.surface);
            CHECK(checkbox.Copy({}) == checkbox);
            CheckboxColors overrides;
            overrides.disabledIndeterminateBorderColor = Color(0xff123456);
            const auto copy = checkbox.Copy(overrides);
            CHECK(copy.BorderColor(false, ToggleableState::Indeterminate) == Color(0xff123456));
            CHECK(copy.BorderColor(false, ToggleableState::On) == checkbox.disabledBorderColor);
            CHECK(copy.CheckmarkColor(false, ToggleableState::Off) == checkbox.disabledCheckmarkColor);
            CHECK(copy.BoxColor(false, ToggleableState::Off) == Color::Transparent());
            CHECK(copy.IndicatorColor(ToggleableState::Indeterminate) == scheme.primary);
            CHECK(CheckboxDefaults::colors(Color(0xffabcdef)).checkedBorderColor == Color(0xffabcdef));
            CHECK(CheckboxDefaults::colors(Color(0xffabcdef)).uncheckedBorderColor == checkbox.uncheckedBorderColor);
            const auto radio = RadioButtonDefaults::colors();
            CHECK(radio.selectedColor == scheme.primary && radio.unselectedColor == scheme.onSurfaceVariant);
            CHECK(radio.RadioColor(false, true) == scheme.onSurface.WithAlpha(0.38f));
            CHECK(radio.RadioColor(false, false) == scheme.onSurface.WithAlpha(0.38f));
            CHECK(radio.Copy() == radio);
            CHECK(radio.Copy(Color(0xff123456)).RadioColor(true, true) == Color(0xff123456));
            const auto sw = SwitchDefaults::colors();
            CHECK(sw.checkedThumbColor == scheme.onPrimary && sw.checkedTrackColor == scheme.primary);
            CHECK(sw.uncheckedThumbColor == scheme.outline && sw.uncheckedTrackColor == scheme.surfaceContainerHighest);
            CHECK(sw.checkedBorderColor == Color::Transparent() && sw.uncheckedBorderColor == scheme.outline);
            CHECK(sw.disabledCheckedThumbColor == ui::CompositeOver(scheme.surface.WithAlpha(1), scheme.surface));
            CHECK(sw.disabledCheckedTrackColor == ui::CompositeOver(scheme.onSurface.WithAlpha(0.12f), scheme.surface));
            CHECK(sw.disabledUncheckedThumbColor == ui::CompositeOver(scheme.onSurface.WithAlpha(0.38f), scheme.surface));
            CHECK(sw.Copy({}) == sw);
            SwitchColors switchOverrides;
            switchOverrides.disabledUncheckedIconColor = Color(0xff987654);
            CHECK(sw.Copy(switchOverrides).IconColor(false, false) == Color(0xff987654));
            CHECK(sw.ThumbColor(false, true) == sw.disabledCheckedThumbColor);
            CHECK(sw.TrackColor(false, false) == sw.disabledUncheckedTrackColor);
            CHECK(sw.BorderColor(false, false) == sw.disabledUncheckedBorderColor);
        });
    });
}

TEST(SelectionControlsReserveInputSpaceOnlyWithCallbacksAndKeepDisabledState) {
    for (int kind = 0; kind < 3; ++kind) {
        rt::State<bool> checked{ false };
        Harness h;
        auto control = [&](bool interactive, bool enabled) {
            std::function<void(bool)> change;
            if (interactive) change = [&](bool value) { checked.Set(value); };
            if (kind == 0) Checkbox(checked.Get(), change, {}, { .enabled = enabled });
            else if (kind == 1) {
                std::function<void()> click;
                if (interactive) click = [&] { checked.Set(true); };
                RadioButton(checked.Get(), click, {}, { .enabled = enabled });
            } else Switch(checked.Get(), change, {}, { .enabled = enabled });
        };
        h.Run([&] { MaterialTheme([&] { Column({}, [&] { control(true, true); control(false, true); control(true, false); }); }); });
        const auto& children = h.owner.Root().Children()[0]->Children();
        CHECK_EQ(children[0]->Size().width, kind == 2 ? 52 : 48);
        CHECK_EQ(children[0]->Size().height, 48);
        CHECK_EQ(children[1]->Size().width, kind == 2 ? 52 : 24);
        CHECK_EQ(children[1]->Size().height, kind == 2 ? 32 : 24);
        CHECK_EQ(children[2]->Size().height, 48);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(2));
        CHECK(tree.children[0].config.role == (kind == 0 ? ui::Role::Checkbox : kind == 1 ? ui::Role::RadioButton : ui::Role::Switch));
        CHECK(!tree.children[0].config.disabled && tree.children[1].config.disabled);
        CHECK(tree.children[0].config.onClick());
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(checked.Peek());
        tree = h.owner.BuildSemanticsTree();
        CHECK(kind == 1 ? tree.children[0].config.selected == true : tree.children[0].config.toggleableState == 1);
        checked.Set(false);
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        // Compose retains the semantics action even when disabled; pointer input is gated.
        const auto bounds = children[2]->InnerRectInRoot();
        auto pointer = [&](ui::PointerEventType type, bool down, std::int64_t time) {
            ui::RawPointerEvent event;
            event.type = type;
            event.uptimeMillis = time;
            event.buttons = down ? 1 : 0;
            event.pointers.push_back({ 1, { (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2 }, down, ui::PointerType::Mouse, {} });
            h.owner.ProcessPointer(event);
            rt::Dispatcher::Main().RunPending();
        };
        pointer(ui::PointerEventType::Press, true, 0);
        pointer(ui::PointerEventType::Release, false, 50);
        CHECK(!checked.Peek());
    }
}

TEST(TriStateCheckboxKeepsIndeterminateSemanticsAcrossInterruptedMorphsAndStylingModes) {
    const bool oldFlag = ComposeMaterial3Flags::isCheckboxStylingFixEnabled;
    for (bool fixed : { false, true }) {
        ComposeMaterial3Flags::isCheckboxStylingFixEnabled = fixed;
        rt::State<ToggleableState> state{ ToggleableState::Indeterminate };
        rt::State<bool> enabled{ true };
        Harness h;
        int clicks = 0;
        h.Run([&] {
            MaterialTheme([&] { Column({}, [&] {
                CheckboxOptions options;
                options.enabled = enabled.Get();
                options.checkmarkStroke = gfx::Stroke{ 3, 4, gfx::StrokeCap::Round };
                options.outlineStroke = gfx::Stroke{ 1 };
                TriStateCheckbox(state.Get(), [&] { ++clicks; }, {}, options);
                TriStateCheckbox(state.Get(), {}, {}, options);
            }); });
        });
        auto& children = h.owner.Root().Children()[0]->Children();
        CHECK_EQ(children[0]->Size().width, 48);
        CHECK_EQ(children[1]->Size().width, fixed ? 18 : 24);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 1 && tree.children[0].config.toggleableState == 2);
        CHECK(tree.children[0].config.onClick() && clicks == 1);
        std::int64_t clock = 0;
        auto advance = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                rt::Composition::RecomposeAll();
                rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll();
                h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        state.Set(ToggleableState::On); advance(3);
        state.Set(ToggleableState::Off); advance(3);
        state.Set(ToggleableState::Indeterminate); enabled.Set(false); advance(130);
        tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children[0].config.toggleableState == 2 && tree.children[0].config.disabled);
        enabled.Set(true); advance(2);
        CHECK(!h.owner.BuildSemanticsTree().children[0].config.disabled);
    }
    ComposeMaterial3Flags::isCheckboxStylingFixEnabled = oldFlag;
}

TEST(SwitchThumbAnimatesSelectionSnapsPressAndHandlesContentRtlAndSourceReplacement) {
    for (auto direction : { ui::LayoutDirection::Ltr, ui::LayoutDirection::Rtl }) {
        rt::State<bool> checked{ false }, content{ false }, enabled{ true }, attached{ true };
        rt::State<int> version{ 0 };
        auto source = std::make_shared<MutableInteractionSource>();
        Harness h;
        h.owner.SetDirection(direction);
        Color iconColor;
        int compositions = 0;
        h.Run([&] {
            ++compositions;
            version.Get();
            MaterialTheme([&] { Column({}, [&] {
                if (!attached.Get()) return;
                SwitchOptions options;
                options.enabled = enabled.Get();
                options.interactionSource = source;
                if (content.Get()) options.thumbContent = [&] {
                    iconColor = rt::Composition::Current()->Read(LocalContentColor);
                    Box(SizeMod({}, 16_dp, 16_dp));
                };
                Switch(checked.Get(), [&](bool value) { checked.Set(value); }, {}, options);
            }); });
        });
        auto thumb = [&] { return h.owner.Root().Children()[0]->Children()[0]->Children()[0]; };
        auto left = [&] {
            return int(thumb()->InnerRectInRoot().left - h.owner.Root().Children()[0]->Children()[0]->InnerRectInRoot().left);
        };
        CHECK_EQ(thumb()->Size().width, 16);
        CHECK_EQ(left(), direction == ui::LayoutDirection::Ltr ? 8 : 28);
        std::int64_t clock = 0;
        auto advance = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                rt::Composition::RecomposeAll();
                rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll();
                h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        checked.Set(true); advance(3);
        CHECK(thumb()->Size().width > 16 && thumb()->Size().width < 24);
        const int before = compositions;
        advance(130);
        CHECK_EQ(compositions, before);  // thumb geometry is observed by layout, not composition
        CHECK_EQ(thumb()->Size().width, 24);
        CHECK_EQ(left(), direction == ui::LayoutDirection::Ltr ? 24 : 4);
        const auto press = source->Start(Interaction::Kind::Press);
        advance(2);
        CHECK_EQ(thumb()->Size().width, 28);
        CHECK_EQ(left(), direction == ui::LayoutDirection::Ltr ? 22 : 2);
        source->Cancel(Interaction::Kind::Press, press); advance(130);
        CHECK_EQ(thumb()->Size().width, 24);
        checked.Set(false); content.Set(true); advance(130);
        CHECK_EQ(thumb()->Size().width, 24);
        CHECK_EQ(left(), direction == ui::LayoutDirection::Ltr ? 4 : 24);
        CHECK(iconColor == LightColorScheme().surfaceContainerHighest);
        enabled.Set(false); advance(2);
        CHECK(iconColor == ui::CompositeOver(LightColorScheme().surfaceContainerHighest.WithAlpha(0.38f), LightColorScheme().surface));
        auto oldSource = source;
        source = std::make_shared<MutableInteractionSource>(); version.Set(1); advance(2);
        oldSource->Start(Interaction::Kind::Press); advance(2);
        CHECK_EQ(thumb()->Size().width, 24);
        source->Start(Interaction::Kind::Press); advance(2);
        CHECK_EQ(thumb()->Size().width, 28);
        attached.Set(false); advance(3);
        source->Start(Interaction::Kind::Press); advance(2);
        CHECK(h.owner.Root().Children()[0]->Children().empty());
    }
}

TEST(SwitchesSharingStateUpdateTogetherWhenThumbContentAlsoReadsIt) {
    rt::State<bool> dark{ false };
    Harness h;
    h.Run([&] {
        MaterialThemeArgs theme;
        theme.colorScheme = dark.Get() ? DarkColorScheme() : LightColorScheme();
        MaterialTheme(theme, [&] {
            auto* on = &RememberState(false);
            Column({}, [on] {
                auto change = [on](bool value) { on->Set(value); };
                Switch(on->Get(), change);
                SwitchOptions options;
                options.thumbContent = [on] { on->Get(); Box(SizeMod({}, 16_dp, 16_dp)); };
                Switch(on->Get(), change, {}, options);
            });
        });
    });
    auto& accessibility = h.owner.Accessibility();
    accessibility.SetEnabled(true);
    accessibility.CheckForSemanticsChanges();
    accessibility.TakeEvents();
    const int firstId = h.owner.Root().Children()[0]->Children()[0]->SemanticsId();
    dark.Set(true);
    for (int i = 0; i < 3; ++i) { rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 }); }
    CHECK_EQ(h.owner.Root().Children()[0]->Children()[0]->SemanticsId(), firstId);
    accessibility.CheckForSemanticsChanges();
    accessibility.TakeEvents();
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(tree.children.size(), std::size_t(2));
    CHECK(tree.children[1].config.onClick());
    for (int i = 0; i < 3; ++i) { rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 }); }
    tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children[0].config.toggleableState == 1);
    CHECK(tree.children[1].config.toggleableState == 1);
    accessibility.CheckForSemanticsChanges();
    int checkedEvents = 0;
    for (const auto& event : accessibility.TakeEvents())
        if (event.contentChangeTypes == ui::a11y::Checked) ++checkedEvents;
    CHECK_EQ(checkedEvents, 2);
}

TEST(CardDefaultsResolveVariantColorsBordersAndCustomContent) {
    Harness h;
    h.Run([] { MaterialTheme([] {
        const auto& scheme = MaterialTheme::colorScheme();
        const auto filled = CardDefaults::cardColors(), elevated = CardDefaults::elevatedCardColors(), outlined = CardDefaults::outlinedCardColors();
        CHECK(filled.containerColor == scheme.surfaceContainerHighest);
        CHECK(filled.contentColor == scheme.onSurface && filled.disabledContentColor == scheme.onSurface.WithAlpha(0.38f));
        CHECK(filled.disabledContainerColor == ui::CompositeOver(scheme.surfaceVariant.WithAlpha(0.38f), scheme.surfaceContainerHighest));
        CHECK(elevated.containerColor == scheme.surfaceContainerLow && elevated.disabledContainerColor == scheme.surface);
        CHECK(outlined.containerColor == scheme.surface && outlined.disabledContainerColor == scheme.surface);
        CHECK(filled.Copy() == filled);
        CHECK(CardDefaults::cardColors(scheme.primary).contentColor == scheme.onPrimary);
        CHECK(CardDefaults::cardColors(scheme.primary).disabledContentColor == scheme.onPrimary.WithAlpha(0.38f));
        const Color custom(0xff123456);
        CHECK(CardDefaults::outlinedCardColors(scheme.primary, custom).contentColor == custom);
        CHECK(CardDefaults::outlinedCardColors(scheme.primary, custom).disabledContentColor == scheme.onPrimary.WithAlpha(0.38f));
        CHECK(CardDefaults::elevatedCardColors(scheme.primary, custom).disabledContentColor == custom.WithAlpha(0.38f));
        CHECK(CardDefaults::outlinedCardBorder().width == 1_dp);
        CHECK(CardDefaults::outlinedCardBorder().brush == gfx::Brush::SolidColor(scheme.outlineVariant));
        CHECK(CardDefaults::outlinedCardBorder(false).brush == gfx::Brush::SolidColor(ui::CompositeOver(scheme.outline.WithAlpha(0.12f), scheme.surfaceContainerLow)));
        CHECK(CardDefaults::cardElevation().defaultElevation == 0_dp && CardDefaults::cardElevation().draggedElevation == 6_dp);
        CHECK(CardDefaults::elevatedCardElevation().defaultElevation == 1_dp && CardDefaults::elevatedCardElevation().draggedElevation == 8_dp);
        CHECK(CardDefaults::outlinedCardElevation().hoveredElevation == 0_dp);
        CHECK(CardDefaults::shape()->Equals(*MaterialTheme::shapes().medium));
    }); });
}

TEST(CardsKeepColumnContentAndOnlyClickableFormsReserveInteractiveSpace) {
    using Plain = void (*)(const ui::Modifier&, const CardOptions&, std::function<void()>);
    using Clickable = void (*)(std::function<void()>, const ui::Modifier&, const CardOptions&, std::function<void()>);
    const Plain plain[] = { Card, ElevatedCard, OutlinedCard };
    const Clickable clickable[] = { Card, ElevatedCard, OutlinedCard };
    for (int variant = 0; variant < 3; ++variant) {
        Harness h;
        Color content[3];
        ui::Dp tonal[3];
        text::TextStyle style;
        int clicks = 0;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            for (int i = 0; i < 3; ++i) {
                CardOptions options;
                options.enabled = i == 1;  // the plain card ignores enabled
                auto body = [&, i] {
                    content[i] = rt::Composition::Current()->Read(LocalContentColor);
                    tonal[i] = rt::Composition::Current()->Read(LocalAbsoluteTonalElevation);
                    style = rt::Composition::Current()->Read(LocalTextStyle());
                    Box(SizeMod({}, 10_dp, 10_dp));
                    Box(SizeMod({}, 20_dp, 10_dp));
                };
                if (i == 0) plain[variant]({}, options, body);
                else clickable[variant]([&] { ++clicks; }, {}, options, body);
            }
        }); }); });
        const auto& cards = h.owner.Root().Children()[0]->Children();
        CHECK_EQ(cards[0]->Size().width, 20);
        CHECK_EQ(cards[0]->Size().height, 20);
        CHECK_EQ(cards[1]->Size().width, 48);
        CHECK_EQ(cards[1]->Size().height, 48);
        CHECK_EQ(cards[2]->Size().height, 48);
        CHECK(content[0] == LightColorScheme().onSurface && content[1] == content[0]);
        CHECK(content[2] == LightColorScheme().onSurface.WithAlpha(0.38f));
        CHECK(tonal[0] == 0_dp && tonal[1] == 0_dp);  // Card sets shadow, not tonal, elevation
        CHECK(style == Typography{}.bodyLarge);
        ui::SemanticsConfiguration config;
        CHECK(ui::CalculateSemanticsConfiguration(*cards[0], config) && !config.onClick && !config.disabled);
        CHECK(ui::CalculateSemanticsConfiguration(*cards[1], config) && config.onClick && !config.disabled);
        CHECK(config.role == ui::Role::None);  // Surface doesn't force a Button role
        CHECK(config.onClick());
        CHECK_EQ(clicks, 1);
        CHECK(ui::CalculateSemanticsConfiguration(*cards[2], config) && config.disabled);
        const auto* column = cards[0]->Children()[0];
        CHECK(column->Children().size() == 2 && column->Children()[1]->Position().y == 10);
    }
}

TEST(CardElevationFollowsDragAndLastInteractionAndSnapsOnDisable) {
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    rt::State<bool> enabled{ true };
    ui::Dp elevation;
    h.Run([&] { elevation = ShadowElevation(CardElevation{ 1_dp, 3_dp, 3_dp, 5_dp, 8_dp, 0_dp }, enabled.Get(), source); });
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
        }
    };
    CHECK(elevation == 1_dp);
    const auto hover = source->Start(Interaction::Kind::Hover); advance(30);
    CHECK(elevation == 5_dp);
    const auto drag = source->Start(Interaction::Kind::Drag); advance(30);
    CHECK(elevation == 8_dp);
    source->Cancel(Interaction::Kind::Drag, drag); advance(30);
    CHECK(elevation == 5_dp);
    const auto press = source->Start(Interaction::Kind::Press); advance(3);
    const auto focus = source->Start(Interaction::Kind::Focus); advance(30);
    CHECK(elevation == 3_dp);
    enabled.Set(false); advance(1);
    CHECK(elevation == 0_dp);
    enabled.Set(true); advance(30);
    CHECK(elevation == 3_dp);
    source->End(Interaction::Kind::Focus, focus);
    source->End(Interaction::Kind::Press, press);
    source->End(Interaction::Kind::Hover, hover); advance(30);
    CHECK(elevation == 1_dp);
}

TEST(ChipArrangementsPreserveCustomSpacingAndRtlPlacement) {
    auto compact = FilterChipDefaults::horizontalArrangement(true, true);
    CHECK(compact == FilterChipDefaults::horizontalArrangement(true, true));
    CHECK(!(compact == FilterChipDefaults::horizontalArrangement(false, true)));
    CHECK(compact.Spacing() == 4_dp);
    const int sizes[] = { 18, 40, 18 };
    int positions[3]{};
    compact.Arrange({ 1, 1 }, 84, sizes, ui::LayoutDirection::Ltr, positions);
    CHECK(positions[0] == 0 && positions[1] == 22 && positions[2] == 66);
    compact.Arrange({ 1, 1 }, 84, sizes, ui::LayoutDirection::Rtl, positions);
    CHECK(positions[0] == 66 && positions[1] == 22 && positions[2] == 0);
    const int noLeading[] = { 0, 40, 18 };
    FilterChipDefaults::horizontalArrangement(false, true).Arrange({ 1, 1 }, 70, noLeading, ui::LayoutDirection::Ltr, positions);
    CHECK(positions[0] == 0 && positions[1] == 4 && positions[2] == 52);
    compact.Arrange({ 1, 1 }, 0, {}, ui::LayoutDirection::Ltr, {});
    CHECK(InputChipDefaults::contentPadding(true, true, false).start == 4_dp);
    CHECK(InputChipDefaults::contentPadding(false, true, false).start == 8_dp);
    CHECK(InputChipDefaults::contentPadding(false, false, true).end == 8_dp);
}

TEST(ChipDefaultsResolveColorsSelectionAndDisabledOverrides) {
    Harness h;
    h.Run([] { MaterialTheme([] {
        const auto& scheme = MaterialTheme::colorScheme();
        const auto assist = AssistChipDefaults::assistChipColors();
        CHECK(assist.containerColor == Color::Transparent() && assist.labelColor == scheme.onSurface);
        CHECK(assist.leadingIconContentColor == scheme.primary && assist.trailingIconContentColor == scheme.primary);
        CHECK(assist.disabledLabelColor == scheme.onSurface.WithAlpha(0.38f));
        CHECK(assist.Copy({}) == assist);
        CHECK(AssistChipDefaults::elevatedAssistChipColors().containerColor == scheme.surfaceContainerLow);
        CHECK(AssistChipDefaults::elevatedAssistChipColors().disabledContainerColor == scheme.onSurface.WithAlpha(0.12f));
        CHECK(!SuggestionChipDefaults::suggestionChipColors().trailingIconContentColor.Specified());
        CHECK(SuggestionChipDefaults::suggestionChipColors().leadingIconContentColor == scheme.primary);
        const auto filter = FilterChipDefaults::filterChipColors();
        CHECK(filter.ContainerColor(true, true) == scheme.secondaryContainer);
        CHECK(filter.ContainerColor(false, true) == scheme.onSurface.WithAlpha(0.12f));
        CHECK(filter.LabelColor(false, true) == scheme.onSurface.WithAlpha(0.38f));
        CHECK(filter.Copy({}) == filter);
        SelectableChipColors overrides;
        overrides.selectedLeadingIconColor = Color(0xff123456);
        CHECK(filter.Copy(overrides).LeadingIconContentColor(true, true) == Color(0xff123456));
        CHECK(filter.Copy(overrides).LeadingIconContentColor(false, true) == filter.disabledLeadingIconColor);
        CHECK(FilterChipDefaults::tonalFilterChipColors().leadingIconColor == scheme.onSurfaceVariant);
        CHECK(FilterChipDefaults::filterChipBorder(true, true).width == 0_dp);
        CHECK(InputChipDefaults::inputChipBorder(false, false).brush == gfx::Brush::SolidColor(scheme.onSurface.WithAlpha(0.12f)));
        CHECK(AssistChipDefaults::assistChipElevation().draggedElevation == 8_dp);
        CHECK(FilterChipDefaults::elevatedFilterChipElevation().elevation == 1_dp);
        CHECK(InputChipDefaults::shapes() == FilterChipDefaults::shapes());
    }); });
}

TEST(ActionChipsUseIntrinsicWidthTokenTypographyAndButtonSemantics) {
    using Chip = void (*)(std::function<void()>, std::function<void()>, const ui::Modifier&, const ChipOptions&);
    const Chip chips[] = { AssistChip, ElevatedAssistChip, SuggestionChip, ElevatedSuggestionChip };
    for (int variant = 0; variant < 4; ++variant) {
        Harness h;
        Color label, leading, trailing;
        text::TextStyle style;
        int clicks = 0;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            ChipOptions options;
            options.leadingIcon = [&] { leading = rt::Composition::Current()->Read(LocalContentColor); Box(SizeMod({}, 18_dp, 18_dp)); };
            if (variant < 2) options.trailingIcon = [&] { trailing = rt::Composition::Current()->Read(LocalContentColor); Box(SizeMod({}, 18_dp, 18_dp)); };
            chips[variant]([&] { ++clicks; }, [&] {
                label = rt::Composition::Current()->Read(LocalContentColor);
                style = rt::Composition::Current()->Read(LocalTextStyle());
                Box(SizeMod({}, 40_dp, 20_dp));
            }, {}, options);
        }); }); });
        auto* chip = h.owner.Root().Children()[0]->Children()[0];
        CHECK_EQ(chip->Size().width, variant < 2 ? 108 : 90);
        CHECK_EQ(chip->Size().height, 48);
        CHECK(label == (variant < 2 ? LightColorScheme().onSurface : LightColorScheme().onSurfaceVariant));
        CHECK(leading == LightColorScheme().primary);
        if (variant < 2) CHECK(trailing == LightColorScheme().primary);
        CHECK(style == Typography{}.labelLarge);
        const auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 1 && tree.children[0].config.role == ui::Role::Button);
        CHECK(tree.children[0].config.onClick());
        CHECK_EQ(clicks, 1);
    }
}

TEST(SelectableChipsRetainExitIconsAndExposeSelectedCheckboxRoles) {
    using Chip = void (*)(bool, std::function<void()>, std::function<void()>, const ui::Modifier&, const SelectableChipOptions&);
    for (Chip chip : { FilterChip, ElevatedFilterChip, InputChip }) for (bool expressive : { false, true }) {
        rt::State<bool> selected{ false };
        Harness h;
        Color leading;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            SelectableChipOptions options;
            if (expressive) options.shapes = FilterChipDefaults::shapes();
            if (selected.Get()) options.leadingIcon = [&] { leading = rt::Composition::Current()->Read(LocalContentColor); Box(SizeMod({}, 18_dp, 18_dp)); };
            chip(selected.Get(), [&] { selected.Set(!selected.Peek()); }, [] { Box(SizeMod({}, 40_dp, 20_dp)); }, {}, options);
        }); }); });
        auto node = [&] { return h.owner.Root().Children()[0]->Children()[0]; };
        const int collapsed = node()->Size().width;
        CHECK_EQ(collapsed, chip == InputChip ? 64 : 72);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 1 && tree.children[0].config.role == ui::Role::Checkbox && tree.children[0].config.selected == false);
        CHECK(tree.children[0].config.onClick());
        std::int64_t clock = 0;
        auto advance = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                rt::Composition::RecomposeAll();
                rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll();
                h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        advance(130);
        CHECK_EQ(node()->Size().width, (chip == InputChip ? 82 : 86) + (expressive ? 0 : 4));
        CHECK(leading == (chip == InputChip ? LightColorScheme().primary : LightColorScheme().onSecondaryContainer));
        CHECK(h.owner.BuildSemanticsTree().children[0].config.selected == true);
        selected.Set(false); advance(2);
        CHECK(node()->Size().width > collapsed);  // retain the last icon throughout its exit
        selected.Set(true); advance(3);
        selected.Set(false); advance(130);
        CHECK_EQ(node()->Size().width, collapsed);
        CHECK(h.owner.BuildSemanticsTree().children[0].config.selected == false);
    }
}

TEST(ExpressiveChipShapesMorphOnPressAndKeepValueEqualShapeConfigurations) {
    Harness h;
    rt::State<bool> selected{ false };
    auto source = std::make_shared<MutableInteractionSource>();
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        SelectableChipOptions options;
        options.interactionSource = source;
        options.shapes = ChipShapes{ RoundedCorner(12_dp), RoundedCornerPercent(50), RoundedCorner(4_dp) };
        FilterChip(selected.Get(), [] {}, [] { Box(SizeMod({}, 40_dp, 20_dp)); }, {}, options);
    }); }); });
    auto containsCorner = [&] {
        auto* chip = h.owner.Root().Children()[0]->Children()[0];
        for (const auto* modifier : chip->ModifierNodes()) if (modifier->ClipsChildren()) {
            const auto bounds = chip->RectInRoot(std::nullopt, modifier);
            return const_cast<ui::ModifierNode*>(modifier)->AsDraw()->LayerClipContains({ bounds.Width(), bounds.Height() }, { 3, 3 }, ui::LayoutDirection::Ltr, { 1, 1 });
        }
        return false;
    };
    CHECK(!containsCorner());
    std::int64_t clock = 0;
    auto advance = [&] {
        for (int i = 0; i < 130; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    const auto press = source->Start(Interaction::Kind::Press); advance();
    CHECK(containsCorner());
    source->End(Interaction::Kind::Press, press);
    selected.Set(true); advance();
    CHECK(!containsCorner());
    CHECK(h.owner.BuildSemanticsTree().children[0].config.selected == true);
}

TEST(InputChipAvatarTakesPrecedenceAndDisabledOpacityWithoutForcedSizing) {
    Harness h;
    int avatars = 0, icons = 0;
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        SelectableChipOptions options;
        options.enabled = false;
        options.leadingIcon = [&] { ++icons; Box(SizeMod({}, 18_dp, 18_dp)); };
        options.avatar = [&] { ++avatars; Box(SizeMod({}, 30_dp, 30_dp)); };
        InputChip(true, [] {}, [] { Box(SizeMod({}, 40_dp, 20_dp)); }, {}, options);
    }); }); });
    auto* chip = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(chip->Size().width, 94);  // 30 avatar + 40 label + 16 gaps + 8 padding
    CHECK(avatars > 0 && icons == 0);
    bool faded = false;
    std::function<void(ui::LayoutNode*)> visit = [&](ui::LayoutNode* node) {
        for (const auto* modifier : node->ModifierNodes())
            if (auto* draw = const_cast<ui::ModifierNode*>(modifier)->AsDraw(); draw && draw->LayerAlpha() == 0.38f) faded = true;
        for (auto* child : node->Children()) visit(child);
    };
    visit(chip);
    CHECK(faded);
    CHECK(h.owner.BuildSemanticsTree().children[0].config.disabled);
}

TEST(ChipElevationTracksLastInteractionsAcrossInterruptionAndDisabledSnap) {
    Harness h;
    rt::State<bool> enabled{ true };
    auto source = std::make_shared<MutableInteractionSource>();
    ui::Dp elevation;
    h.Run([&] { elevation = ShadowElevation(ChipElevation{ 1_dp, 3_dp, 3_dp, 5_dp, 8_dp, 0_dp }, enabled.Get(), source); });
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
        }
    };
    const auto hover = source->Start(Interaction::Kind::Hover); advance(30);
    CHECK(elevation == 5_dp);
    const auto press = source->Start(Interaction::Kind::Press); advance(3);
    const auto focus = source->Start(Interaction::Kind::Focus); advance(30);
    CHECK(elevation == 3_dp);
    const auto drag = source->Start(Interaction::Kind::Drag); advance(30);
    CHECK(elevation == 8_dp);
    enabled.Set(false); advance(1);
    CHECK(elevation == 0_dp);
    enabled.Set(true); advance(30);
    CHECK(elevation == 8_dp);
    source->End(Interaction::Kind::Drag, drag);
    source->End(Interaction::Kind::Focus, focus);
    source->End(Interaction::Kind::Press, press);
    source->End(Interaction::Kind::Hover, hover); advance(30);
    CHECK(elevation == 1_dp);
}

TEST(ListItemDefaultsResolveAllStatesAndSegmentOnlyOuterCorners) {
    Harness h;
    h.Run([] { MaterialTheme([] {
        const auto scheme = LightColorScheme();
        const auto colors = ListItemDefaults::colors();
        CHECK(colors.ContainerColor(true, false, false) == scheme.surface);
        CHECK(colors.ContentColor(true, false, false) == scheme.onSurface);
        CHECK(colors.ContainerColor(true, true, false) == scheme.secondaryContainer);
        CHECK(colors.ContentColor(true, true, false) == scheme.onSecondaryContainer);
        CHECK(colors.ContainerColor(true, true, true) == scheme.tertiaryContainer);
        CHECK(colors.LeadingContentColor(true, true, true) == scheme.onTertiaryContainer);
        CHECK(colors.ContainerColor(false, true, true) == scheme.surface);
        for (const auto color : { colors.ContentColor(false, true, true), colors.LeadingContentColor(false, true, true), colors.TrailingContentColor(false, true, true), colors.OverlineContentColor(false, true, true), colors.SupportingContentColor(false, true, true) })
            CHECK(color == scheme.onSurface.WithAlpha(0.38f));
        CHECK(colors.Copy({}) == colors);
        CHECK(ListItemDefaults::colors({ .selectedContentColor = Color(0xFF123456) }).ContentColor(true, true, false) == Color(0xFF123456));
        CHECK(ListItemDefaults::segmentedColors().containerColor == scheme.surface);
        CHECK(ListItemDefaults::elevation() == (ListItemElevation{ 0_dp, 8_dp }));
        const auto shapes = ListItemDefaults::shapes();
        CHECK(shapes == ListItemDefaults::shapes());
        CHECK(shapes.ShapeForInteraction(true, true, true, true, true) == shapes.pressedShape);
        CHECK(shapes.ShapeForInteraction(true, false, true, true, true) == shapes.draggedShape);
        CHECK(shapes.ShapeForInteraction(true, false, true, true, false) == shapes.selectedShape);
        CHECK(shapes.ShapeForInteraction(false, false, true, true, false) == shapes.focusedShape);
        CHECK(shapes.ShapeForInteraction(false, false, false, true, false) == shapes.hoveredShape);
        for (int i = 0; i < 3; ++i) {
            const auto segmented = ListItemDefaults::segmentedShapes(i, 3);
            const auto base = std::dynamic_pointer_cast<const CornerBasedShape>(segmented.shape);
            CHECK(base && base->topStart == CornerSize::Of(i == 0 ? 16_dp : 4_dp));
            CHECK(base && base->bottomEnd == CornerSize::Of(i == 2 ? 16_dp : 4_dp));
            CHECK(segmented.pressedShape == shapes.pressedShape);
        }
        const auto single = std::dynamic_pointer_cast<const CornerBasedShape>(ListItemDefaults::segmentedShapes(0, 1).shape);
        CHECK(single && single->topEnd == CornerSize::Of(16_dp) && single->bottomStart == CornerSize::Of(16_dp));
        CHECK(ListItemDefaults::segmentedShapes(0, 3, shapes.Copy(RectangleShape())).shape == RectangleShape());
        const auto align = ListItemDefaults::verticalAlignment();
        CHECK_EQ(align(20, 59), 20);
        CHECK_EQ(align(20, 60), 0);
    }); });
}

namespace {
    ui::LayoutNode& ListTestLeaf(int width, int height, bool multiline = false) {
        return Layout({}, std::make_shared<ui::LambdaMeasurePolicy>([=](ui::MeasureScope& s, std::span<ui::Measurable* const>, const ui::Constraints& c) {
            return s.Layout(c.ConstrainWidth(width), c.ConstrainHeight(height), {},
                            { { &ui::FirstBaseline, 10 }, { &ui::LastBaseline, multiline ? 30 : 10 } });
        }));
    }
}

TEST(ListItemsMatchClassicAndModernSlotOrderSizingAndRtl) {
    for (bool legacy : { false, true }) for (bool rtl : { false, true }) for (int type = 1; type <= 4; ++type) {
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        ui::LayoutNode *headline = nullptr, *leading = nullptr, *trailing = nullptr, *overline = nullptr, *supporting = nullptr;
        const bool threeLine = type >= 3;
        h.Run([&] { MaterialTheme([&] {
            rt::Composition::Current()->Provide(ui::LocalLayoutDirection, rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                Column({}, [&] {
                    auto head = [&] { headline = &ListTestLeaf(40, 20); };
                    auto lead = [&] { leading = &ListTestLeaf(24, 24); };
                    auto trail = [&] { trailing = &ListTestLeaf(16, 16); };
                    std::function<void()> over, support;
                    if (type == 3) over = [&] { overline = &ListTestLeaf(30, 10); };
                    if (type > 1) support = [&] { supporting = &ListTestLeaf(60, type == 4 ? 40 : 20, type == 4); };
                    if (legacy) ListItem(head, {}, { .overlineContent = over, .supportingContent = support, .leadingContent = lead, .trailingContent = trail });
                    else ListItem({}, { .leadingContent = lead, .trailingContent = trail, .overlineContent = over, .supportingContent = support }, head);
                });
            });
        }); });
        auto* list = h.owner.Root().Children()[0]->Children()[0];
        CHECK_EQ(list->Size().width, 300);
        CHECK_EQ(list->Size().height, type == 1 ? 56 : type == 2 ? 72 : 88);
        const int gap = legacy ? 16 : 12;
        CHECK_EQ(int(leading->InnerRectInRoot().left), rtl ? 260 : 16);
        CHECK_EQ(int(trailing->InnerRectInRoot().left), rtl ? 16 : 268);
        CHECK_EQ(int(headline->InnerRectInRoot().left), rtl ? 300 - 16 - 24 - gap - 40 : 16 + 24 + gap);
        const int top = legacy ? (threeLine ? 12 : (list->Size().height - (type == 1 ? 20 : 40)) / 2) : (threeLine ? 10 : (list->Size().height - (type == 1 ? 20 : 40)) / 2);
        CHECK_EQ(int(headline->InnerRectInRoot().top), top + (overline ? 10 : 0));
        if (supporting) CHECK_EQ(int(supporting->InnerRectInRoot().top), top + (overline ? 10 : 0) + 20);
        if (overline) CHECK_EQ(int(overline->InnerRectInRoot().top), top);
        CHECK_EQ(int(leading->InnerRectInRoot().top), threeLine ? (legacy ? 12 : 10) : (list->Size().height - 24) / 2);
        CHECK_EQ(list->AsMeasurable().MinIntrinsicWidth(300), 32 + 24 + 16 + gap * 2 + (supporting ? 60 : 40));
        CHECK_EQ(list->AsMeasurable().MaxIntrinsicHeight(300), type == 1 ? 56 : type == 2 ? 72 : 88);
    }
}

TEST(ListItemsPreserveEmptyAndMultipleNodeSlotBehavior) {
    for (bool legacy : { false, true }) {
        Harness h;
        ui::LayoutNode *first = nullptr, *second = nullptr;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            auto headline = [&] { first = &ListTestLeaf(40, 20); second = &ListTestLeaf(60, 40); };
            if (legacy) ListItem(headline, {}, { .overlineContent = [] {}, .supportingContent = [] {} });
            else ListItem({}, { .overlineContent = [] {}, .supportingContent = [] {} }, headline);
        }); }); });
        auto* list = h.owner.Root().Children()[0]->Children()[0];
        CHECK_EQ(list->Size().height, legacy ? 56 : 88); // modern decorators emit a Box even when empty
        CHECK(first->IsPlaced());
        CHECK(second->IsPlaced() == !legacy); // legacy firstOrNull, modern Box overlaps content
        CHECK_EQ(list->AsMeasurable().MaxIntrinsicWidth(300), legacy ? 72 : 92);
    }
}

TEST(ModernListItemsHonorExplicitMinimumHeightAndHeightCompatibilityFlag) {
    for (bool fix : { false, true }) for (bool explicitMinimum : { false, true }) {
        const bool old = ComposeMaterial3Flags::isExpressiveListItemHeightBasedOnTextLinesFixEnabled;
        ComposeMaterial3Flags::isExpressiveListItemHeightBasedOnTextLinesFixEnabled = fix;
        Harness h;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            ListItem(explicitMinimum ? SizeIn({}, Dp::Unspecified(), 30_dp, Dp::Unspecified(), Dp::Unspecified()) : Modifier{},
                     { .overlineContent = [] { ListTestLeaf(20, 5); }, .supportingContent = [] { ListTestLeaf(20, 5); } }, [] { ListTestLeaf(40, 5); });
        }); }); });
        CHECK_EQ(h.owner.Root().Children()[0]->Children()[0]->Size().height, explicitMinimum ? 35 : fix ? 88 : 56);
        ComposeMaterial3Flags::isExpressiveListItemHeightBasedOnTextLinesFixEnabled = old;
    }
}

TEST(ModernListItemsExposeSelectionLongClickAndDisabledSemanticsWithoutButtonRole) {
    for (int variant = 0; variant < 4; ++variant) for (bool segmented : { false, true }) {
        Harness h;
        rt::State<bool> enabled{ true }, selected{ false };
        int clicks = 0, longs = 0;
        ui::Dp childMinimum;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            ListItemOptions o;
            o.enabled = enabled.Get();
            o.trailingContent = [&] { childMinimum = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize); };
            o.onLongClick = [&] { ++longs; };
            o.onLongClickLabel = "Select item";
            auto content = [] { Box(SizeMod({}, 40_dp, 20_dp)); };
            auto click = [&] { ++clicks; selected.Set(!selected.Peek()); };
            std::function<void(bool)> change = [&](bool value) { ++clicks; selected.Set(value); };
            const auto shapes = ListItemDefaults::segmentedShapes(0, 1);
            if (segmented) {
                if (variant == 0) SegmentedListItem(shapes, {}, o, content);
                if (variant == 1) SegmentedListItem(click, shapes, {}, o, content);
                if (variant == 2) SegmentedListItem(selected.Get(), std::function<void()>(click), shapes, {}, o, content);
                if (variant == 3) SegmentedListItem(selected.Get(), change, shapes, {}, o, content);
            } else {
                if (variant == 0) ListItem({}, o, content);
                if (variant == 1) ListItem(click, {}, o, content);
                if (variant == 2) ListItem(selected.Get(), std::function<void()>(click), {}, o, content);
                if (variant == 3) ListItem(selected.Get(), change, {}, o, content);
            }
        }); }); });
        auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(1));
        const auto& config = tree.children[0].config;
        CHECK(childMinimum == 20_dp); // 48 - 16 external padding - 12 internal spacing
        CHECK(!config.disabled);
        CHECK(config.role == (variant == 2 ? ui::Role::RadioButton : variant == 3 ? ui::Role::Checkbox : ui::Role::None));
        CHECK(bool(config.onClick) == (variant != 0));
        if (variant != 0) {
            CHECK(config.onLongClickLabel == "Select item");
            CHECK(config.onLongClick());
            CHECK_EQ(longs, 1);
            CHECK(config.onClick());
            CHECK_EQ(clicks, 1);
        }
        rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        tree = h.owner.BuildSemanticsTree();
        if (variant == 2) CHECK(tree.children[0].config.selected == true);
        if (variant == 3) CHECK(tree.children[0].config.toggleableState == 1);
        enabled.Set(false); rt::Composition::RecomposeAll();
        h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(h.owner.BuildSemanticsTree().children[0].config.disabled);
    }
}

TEST(ModernListColorsAndElevationAnimateAcrossInterruptedSelectionDragAndDisable) {
    Harness h;
    rt::State<bool> selected{ false }, enabled{ true };
    auto source = std::make_shared<MutableInteractionSource>();
    Color content, leading, trailing, overline, supporting;
    text::TextStyle headStyle, leadStyle, trailStyle, overStyle, supportStyle;
    int clicks = 0;
    auto observe = [](Color& color, text::TextStyle& style) {
        color = rt::Composition::Current()->Read(LocalContentColor);
        style = rt::Composition::Current()->Read(LocalTextStyle());
        ListTestLeaf(20, 10);
    };
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        ListItemOptions o;
        o.enabled = enabled.Get();
        o.interactionSource = source;
        o.leadingContent = [&] { observe(leading, leadStyle); };
        o.trailingContent = [&] { observe(trailing, trailStyle); };
        o.overlineContent = [&] { observe(overline, overStyle); };
        o.supportingContent = [&] { observe(supporting, supportStyle); };
        ListItem(selected.Get(), std::function<void()>([&] { ++clicks; }), {}, o, [&] { observe(content, headStyle); });
    }); }); });
    const auto scheme = LightColorScheme();
    CHECK(content == scheme.onSurface && leading == scheme.onSurfaceVariant);
    CHECK(headStyle == Typography{}.bodyLarge && leadStyle == Typography{}.titleMedium);
    CHECK(trailStyle == Typography{}.labelSmall && overStyle == Typography{}.labelSmall && supportStyle == Typography{}.bodyMedium);
    auto* list = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(list->ZIndex(), 0.0f);
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    selected.Set(true); advance(2);
    CHECK(content != scheme.onSecondaryContainer); // selection does not snap
    const auto drag = source->Start(Interaction::Kind::Drag); advance(130);
    CHECK(content == scheme.onTertiaryContainer && leading == content && trailing == content && overline == content && supporting == content);
    CHECK_EQ(list->ZIndex(), 1.0f);
    enabled.Set(false); advance(2);
    CHECK(content != scheme.onSurface.WithAlpha(0.38f)); // disabling animates too
    advance(130);
    CHECK(content == scheme.onSurface.WithAlpha(0.38f) && leading == content && trailing == content && overline == content && supporting == content);
    CHECK_EQ(list->ZIndex(), 1.0f); // disabling doesn't remove the dragged elevation
    const auto bounds = list->InnerRectInRoot();
    auto mouse = [&](ui::PointerEventType type, bool down, std::int64_t time) {
        ui::RawPointerEvent event;
        event.type = type; event.uptimeMillis = time; event.buttons = down ? 1 : 0;
        event.pointers.push_back({ 1, { (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2 }, down, ui::PointerType::Mouse, {} });
        h.owner.ProcessPointer(event);
        rt::Dispatcher::Main().RunPending();
    };
    mouse(ui::PointerEventType::Press, true, 0);
    mouse(ui::PointerEventType::Release, false, 50);
    CHECK_EQ(clicks, 0);
    source->End(Interaction::Kind::Drag, drag); advance(130);
    CHECK_EQ(list->ZIndex(), 0.0f);
    enabled.Set(true); advance(130);
    CHECK(content == scheme.onSecondaryContainer);
}

TEST(ModernListShapesKeepValueEqualConfigurationsWhileMorphing) {
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        ListItemOptions o;
        o.interactionSource = source;
        o.shapes = ListItemShapes{ RoundedCorner(16_dp), RoundedCorner(14_dp), RoundedCorner(4_dp), RoundedCorner(12_dp), RoundedCorner(10_dp), RoundedCorner(8_dp) };
        ListItem([] {}, {}, o, [] { ListTestLeaf(40, 20); });
    }); }); });
    auto contains = [&] {
        auto* list = h.owner.Root().Children()[0]->Children()[0];
        for (const auto* modifier : list->ModifierNodes()) if (modifier->ClipsChildren()) {
            const auto bounds = list->RectInRoot(std::nullopt, modifier);
            return const_cast<ui::ModifierNode*>(modifier)->AsDraw()->LayerClipContains({ bounds.Width(), bounds.Height() }, { 3, 3 }, ui::LayoutDirection::Ltr, { 1, 1 });
        }
        return false;
    };
    CHECK(!contains());
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll();
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    const auto press = source->Start(Interaction::Kind::Press); advance(130);
    CHECK(contains());
    source->End(Interaction::Kind::Press, press); advance(2);
    source->Start(Interaction::Kind::Press); advance(130);
    CHECK(contains());
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
