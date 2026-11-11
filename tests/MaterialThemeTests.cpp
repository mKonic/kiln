#include "Check.h"

#include <map>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Subcompose.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/Checkbox.h"
#include "kiln/material3/Card.h"
#include "kiln/material3/Chip.h"
#include "kiln/material3/ListItem.h"
#include "kiln/material3/ProgressIndicator.h"
#include "kiln/material3/LoadingIndicator.h"
#include "kiln/material3/Slider.h"
#include "kiln/material3/SegmentedButton.h"
#include "kiln/material3/SplitButton.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/NavigationBar.h"
#include "kiln/material3/NavigationRail.h"
#include "kiln/material3/ShortNavigationBar.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/material3/AppBar.h"
#include "kiln/material3/FloatingToolbar.h"
#include "kiln/material3/Scrollbar.h"
#include "kiln/material3/Snackbar.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/Divider.h"
#include "kiln/material3/Tab.h"
#include "kiln/material3/TabRow.h"
#include "kiln/material3/WideNavigationRail.h"
#include "kiln/material3/Menu.h"
#include "kiln/material3/ButtonGroup.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/foundation/Focus.h"
#include "kiln/material3/internal/ProgressIndicator.h"
#include "kiln/material3/WavyProgressIndicator.h"
#include "kiln/material3/internal/WavyProgress.h"
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
#include "kiln/material3/tokens/AppBarSmallTokens.h"
#include "kiln/material3/tokens/TypographyKeyTokens.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Accessibility.h"
#include "FakeText.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::material3;
using kiln::ui::Color;

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        ui::IntSize window{ 300, 300 };   // pixels
        // Compose's tests check to 0.5 dp: density 2 resolves that; `window` is in pixels.
        // Owners of open dialog windows (see ModalHarness), laid out full-window with the main one.
        std::vector<ui::Owner*> dialogs;
        explicit Harness(float density = 1, ui::IntSize size = { 300, 300 }) : window(size) { owner.SetDensity({ density, 1 }); }
        void Layout() {
            owner.MeasureAndLayout(window);
            for (ui::Owner* d : dialogs) {
                d->SetContentConstraints(ui::Constraints{ 0, window.width, 0, window.height });
                d->MeasureAndLayout(window);
            }
        }
        // mainClock.advanceTimeBy with autoAdvance off: moves the dispatcher's time and the frame
        // clock forward in 16 ms frames, running timers, frames, recomposition and layout.
        std::int64_t frameNanos = 0;
        void AdvanceTimeBy(std::int64_t ms) {
            auto& d = rt::Dispatcher::Main();
            std::int64_t done = 0;
            do {
                const std::int64_t step = std::min<std::int64_t>(16, ms - done);
                done += step;
                d.AdvanceTimeBy(std::chrono::milliseconds(step));
                frameNanos += step * 1'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                rt::Composition::RecomposeAll();
                Layout();
                d.RunPending();
            } while (done < ms);
        }
        void Run(std::function<void()> content) {
            // LocalDensity, as App provides it (vector painters size themselves with it).
            composition.SetContent([this, content = std::move(content)] {
                rt::Composition::Current()->Provide(ui::LocalDensity, owner.GetDensity(), content);
            });
            for (int i = 0; i < 3; ++i) {
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout(window);
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

// Ported from Compose's MenuPositionTest (a 500 x 1000 window, a 50 px anchor at 100, 100 and a
// 100 x 200 menu). Its two checks that build AnchorAlignmentOffsetPosition and
// WindowAlignmentMarginPosition directly are left out: kiln keeps only the named positions.
namespace {
    using kiln::material3::internal::DropdownMenuPositionProvider;
    using AnchorPos = kiln::material3::internal::MenuAnchorPosition;
    namespace MenuPos = kiln::material3::internal::MenuPosition;
    constexpr ui::IntSize MenuWindow{ 500, 1000 }, MenuSize{ 100, 200 };
    constexpr ui::IntRect MenuAnchorBounds{ 100, 100, 150, 150 };
    ui::IntRect At(int x, int y, int w, int h) { return { x, y, x + w, y + h }; }
    std::unique_ptr<DropdownMenuPositionProvider> Provider(int offsetX, int offsetY, const AnchorPos& anchor, std::optional<int> horizontalMargin = {},
                                          std::optional<int> verticalMargin = {}, std::function<void(ui::IntRect, ui::IntRect)> callback = {}) {
        auto p = std::make_unique<DropdownMenuPositionProvider>(ui::DpOffset{ ui::Dp(float(offsetX)), ui::Dp(float(offsetY)) }, ui::Density{ 1, 1 },
                                                                anchor, std::move(callback));
        if (horizontalMargin) p->horizontalMargin = *horizontalMargin;
        if (verticalMargin) p->verticalMargin = *verticalMargin;
        return p;
    }
}

TEST(MenuHorizontalPositionsAlignToTheAnchorInBothDirections) {
    const auto x = [](const MenuPos::Horizontal& f, ui::LayoutDirection dir) { return f(MenuAnchorBounds, MenuWindow, MenuSize.width, dir); };
    const auto ltr = ui::LayoutDirection::Ltr, rtl = ui::LayoutDirection::Rtl;
    CHECK_EQ(x(MenuPos::startToAnchorStart, ltr), 100);
    CHECK_EQ(x(MenuPos::endToAnchorEnd, ltr), 150 - 100);
    CHECK_EQ(x(MenuPos::startToAnchorEnd, ltr), 150);
    CHECK_EQ(x(MenuPos::endToAnchorStart, ltr), 100 - 100);
    CHECK_EQ(x(MenuPos::startToAnchorStart, rtl), 150 - 100);
    CHECK_EQ(x(MenuPos::endToAnchorEnd, rtl), 100);
    CHECK_EQ(x(MenuPos::startToAnchorEnd, rtl), 100 - 100);
    CHECK_EQ(x(MenuPos::endToAnchorStart, rtl), 150);
    // Window alignment is absolute.
    for (auto dir : { ltr, rtl }) {
        CHECK_EQ(x(MenuPos::leftToWindowLeft, dir), 0);
        CHECK_EQ(x(MenuPos::rightToWindowRight, dir), 500 - 100);
    }
    // The content offset moves toward the end.
    CHECK_EQ(Provider(10, 0, AnchorPos::Below())->CalculatePosition(MenuAnchorBounds, MenuWindow, ltr, MenuSize).x, 100 + 10);
    CHECK_EQ(Provider(10, 0, AnchorPos::Below())->CalculatePosition(MenuAnchorBounds, MenuWindow, rtl, MenuSize).x, 150 - 100 - 10);
}

TEST(MenuVerticalPositionsAlignToTheAnchorOrTheWindow) {
    const auto y = [](const MenuPos::Vertical& f) { return f(MenuAnchorBounds, MenuWindow, MenuSize.height); };
    CHECK_EQ(y(MenuPos::topToAnchorBottom), 150);
    CHECK_EQ(y(MenuPos::bottomToAnchorTop), 100 - 200);
    CHECK_EQ(y(MenuPos::topToAnchorTop), 100);
    CHECK_EQ(y(MenuPos::bottomToAnchorBottom), 150 - 200);
    CHECK_EQ(y(MenuPos::centerToAnchorTop), 100 - 100);
    CHECK_EQ(y(MenuPos::topToWindowTop), 0);
    CHECK_EQ(y(MenuPos::bottomToWindowBottom), 1000 - 200);
    CHECK_EQ(Provider(0, 10, AnchorPos::Below())->CalculatePosition(MenuAnchorBounds, MenuWindow, ui::LayoutDirection::Ltr, MenuSize).y, 150 + 10);
}

TEST(MenuKeepsItsMarginsAndCentresWhenTheyDoNotFit) {
    const auto ltr = ui::LayoutDirection::Ltr;
    // Horizontally: an anchor off the left edge puts a Start menu at the margin, unless two
    // margins and the menu exceed the window.
    CHECK_EQ(Provider(0, 0, AnchorPos::Start(), 150)->CalculatePosition(At(-100, 0, 50, 50), MenuWindow, ltr, MenuSize).x, 150);
    CHECK_EQ(Provider(0, 0, AnchorPos::Start(), 220)->CalculatePosition(At(-100, 0, 50, 50), MenuWindow, ltr, MenuSize).x, (500 - 100) / 2);
    // Vertically the same, for an anchor above the window.
    CHECK_EQ(Provider(0, 0, AnchorPos::Below(), {}, 150)->CalculatePosition(At(0, -200, 50, 50), MenuWindow, ltr, MenuSize).y, 150);
    CHECK_EQ(Provider(0, 0, AnchorPos::Below(), {}, 450)->CalculatePosition(At(0, -200, 50, 50), MenuWindow, ltr, MenuSize).y, (1000 - 200) / 2);
}

TEST(MenuPositionProviderPlacesBelowAboveAndWithinTheWindow) {
    using internal::MenuVerticalMargin;
    using internal::MenuHorizontalMargin;
    const auto ltr = ui::LayoutDirection::Ltr, rtl = ui::LayoutDirection::Rtl;
    // Below the anchor's bottom start, offset.
    auto below = Provider(20, 40, AnchorPos::Below(), 0);
    CHECK((below->CalculatePosition(At(100, 200, 10, 20), MenuWindow, ltr, { 50, 80 }) == ui::IntOffset{ 100 + 20, 200 + 20 + 40 }));
    CHECK((below->CalculatePosition(At(100, 200, 10, 20), MenuWindow, rtl, { 50, 80 }) == ui::IntOffset{ 100 + 10 - 50 - 20, 200 + 20 + 40 }));
    // Near the bottom end: above the anchor, end-aligned.
    auto up = Provider(20, -40, AnchorPos::Below(), 0);
    CHECK((up->CalculatePosition(At(450, 950, 10, 20), MenuWindow, ltr, { 150, 80 }) == ui::IntOffset{ 450 + 10 - 150 + 20, 950 - 80 - 40 }));
    CHECK((up->CalculatePosition(At(50, 950, 10, 20), MenuWindow, rtl, { 150, 80 }) == ui::IntOffset{ 50 - 20, 950 - 80 - 40 }));
    // Too tall for either side: at the top margin.
    const int vMargin = int(MenuVerticalMargin.value), hMargin = int(MenuHorizontalMargin.value);
    CHECK_EQ(Provider(0, 0, AnchorPos::Below())->CalculatePosition(At(0, 0, 50, 20), MenuWindow, ltr, { 150, 500 }).y, vMargin);
    // A partly visible anchor: inside both margins.
    CHECK((Provider(0, 0, AnchorPos::Below())->CalculatePosition(At(-25, -10, 50, 20), MenuWindow, ltr, { 150, 500 }) == ui::IntOffset{ hMargin, vMargin }));
    CHECK((Provider(0, 0, AnchorPos::Below())->CalculatePosition(At(525, -10, 50, 20), MenuWindow, rtl, { 150, 500 }) ==
           ui::IntOffset{ 500 - hMargin - 150, vMargin }));
    // Custom candidates.
    AnchorPos custom;
    custom.xCandidates = [](const internal::MenuPositionScope& s) { return std::vector<int>{ s.anchorBounds.right }; };
    custom.yCandidates = [](const internal::MenuPositionScope& s) { return std::vector<int>{ s.anchorBounds.bottom }; };
    CHECK((Provider(0, 0, custom)->CalculatePosition(At(250, 500, 10, 20), MenuWindow, ltr, { 50, 25 }) == ui::IntOffset{ 260, 520 }));
    // The callback gets the anchor and the menu's bounds.
    ui::IntRect parent{}, menu{};
    Provider(20, 40, AnchorPos::Below(), {}, {}, [&](ui::IntRect p, ui::IntRect m) { parent = p; menu = m; })
        ->CalculatePosition(At(100, 200, 10, 20), MenuWindow, ltr, { 50, 80 });
    CHECK((parent == At(100, 200, 10, 20)));
    CHECK((menu == At(120, 260, 50, 80)));
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

TEST(ProgressIndicatorsClampValuesKeepVisualSizeAndExpandLinearSemantics) {
    for (float value : { -1.0f, 0.5f, 2.0f, NAN, INFINITY }) {
        Harness h;
        auto progress = [&] { return value; };
        int stopDraws = 0;
        ui::Size stopScope;
        h.Run([&] { MaterialTheme([&] { Column(Padding({}, 20_dp), Arrangement::SpacedBy(20_dp), AlignmentH::Start(), [&] {
            LinearProgressIndicator(progress, {}, { .drawStopIndicator = std::function<void(ui::DrawScope&)>([&](ui::DrawScope& s) { ++stopDraws; stopScope = s.GetSize(); }) });
            CircularProgressIndicator(progress);
            LinearProgressIndicator();
            CircularProgressIndicator();
        }); }); });
        const auto& nodes = h.owner.Root().Children()[0]->Children();
        CHECK_EQ(nodes.size(), std::size_t(4));
        CHECK((nodes[0]->Size() == ui::IntSize{ 240, 4 }));
        CHECK((nodes[1]->Size() == ui::IntSize{ 40, 40 }));
        const auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(4));
        const float coerced = std::isnan(value) ? 0 : std::clamp(value, 0.0f, 1.0f);
        CHECK(tree.children[0].config.progressBarRangeInfo->current == coerced);
        CHECK(tree.children[1].config.progressBarRangeInfo->current == coerced);
        CHECK(tree.children[2].config.progressBarRangeInfo->indeterminate);
        CHECK(tree.children[3].config.progressBarRangeInfo->indeterminate);
        CHECK_EQ(int(tree.children[0].boundsInRoot.Height()), 24);
        CHECK_EQ(int(tree.children[0].boundsInRoot.top), 10);
        h.owner.Draw({});
        CHECK_EQ(stopDraws, 1);
        CHECK((stopScope == ui::Size{ 240, 4 }));
    }
}

TEST(ProgressIndicatorDefaultsAndIndeterminateMotionFollowSourceTimings) {
    Harness h;
    h.Run([] { MaterialTheme([] {
        const auto scheme = LightColorScheme();
        CHECK(ProgressIndicatorDefaults::linearColor() == scheme.primary);
        CHECK(ProgressIndicatorDefaults::circularColor() == scheme.primary);
        CHECK(ProgressIndicatorDefaults::linearTrackColor() == scheme.secondaryContainer);
        CHECK(ProgressIndicatorDefaults::circularDeterminateTrackColor() == scheme.secondaryContainer);
        CHECK(ProgressIndicatorDefaults::circularIndeterminateTrackColor() == Color::Transparent());
    }); });
    auto linear = [](int ms) { return material3::internal::LinearProgressMotionAt(std::int64_t(ms) * 1'000'000); };
    const auto zero = linear(0), early = linear(250), middle = linear(1000), late = linear(1500);
    CHECK(zero.firstHead == 0 && zero.firstTail == 0 && zero.secondHead == 0 && zero.secondTail == 0);
    CHECK(early.firstHead > 0 && early.firstTail == 0 && early.secondHead == 0 && early.secondTail == 0);
    CHECK(middle.firstHead == 1 && middle.firstTail > 0 && middle.firstTail < 1 && middle.secondHead > 0 && middle.secondTail > 0);
    CHECK(late.firstHead == 1 && late.firstTail == 1 && late.secondHead == 1 && late.secondTail < 1);
    CHECK(linear(1751).firstHead < 0.01f && linear(1751).firstTail == 0);
    const auto start = material3::internal::CircularProgressMotionAt(0);
    const auto half = material3::internal::CircularProgressMotionAt(3'000'000'000);
    CHECK(start.globalRotation == 0 && start.additionalRotation == 0 && start.progress == 0.1f);
    CHECK(half.globalRotation == 540 && half.additionalRotation == 180 && half.progress == 0.87f);
    const auto spring = ProgressIndicatorDefaults::progressAnimationSpec();
    CHECK(spring->DurationNanos({}, { 1, 0, 0, 0 }, {}, 1) > anim::Spring(1, 50)->DurationNanos({}, { 1, 0, 0, 0 }, {}, 1));
}

TEST(LoadingIndicatorsUseSquareActiveContentAndProgressSemanticsForBothContainers) {
    for (bool contained : { false, true }) for (bool indeterminate : { false, true }) {
        Harness h;
        rt::State<float> progress{ 0.5f };
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            if (contained) {
                if (indeterminate) ContainedLoadingIndicator(SizeMod({}, 80_dp, 48_dp));
                else ContainedLoadingIndicator([&] { return progress.Get(); }, SizeMod({}, 80_dp, 48_dp));
            } else {
                if (indeterminate) LoadingIndicator(SizeMod({}, 80_dp, 48_dp));
                else LoadingIndicator([&] { return progress.Get(); }, SizeMod({}, 80_dp, 48_dp));
            }
        }); }); });
        auto* indicator = h.owner.Root().Children()[0]->Children()[0];
        CHECK((indicator->Size() == ui::IntSize{ 80, 48 }));
        CHECK((indicator->Children()[0]->Size() == ui::IntSize{ 48, 48 }));
        CHECK_EQ(int(indicator->Children()[0]->InnerRectInRoot().left), 16);
        auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(1));
        CHECK(tree.children[0].config.progressBarRangeInfo->indeterminate == indeterminate);
        if (!indeterminate) CHECK(tree.children[0].config.progressBarRangeInfo->current == 0.5f);
        for (int frame = 0; frame < 100; ++frame) {
            rt::Dispatcher::Main().RunFrame(std::int64_t(frame) * 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
            h.owner.Draw({});
        }
        if (!indeterminate) {
            progress.Set(1); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 }); h.owner.Draw({});
            CHECK(h.owner.BuildSemanticsTree().children[0].config.progressBarRangeInfo->current == 1);
        }
    }
    CHECK_EQ(LoadingIndicatorDefaults::indeterminateIndicatorPolygons().size(), std::size_t(7));
    CHECK_EQ(LoadingIndicatorDefaults::determinateIndicatorPolygons().size(), std::size_t(2));
    bool rejected = false;
    try {
        Harness h;
        h.Run([] { MaterialTheme([] { LoadingIndicator({}, { .polygons = std::vector<gfx::shapes::RoundedPolygon>{} }); }); });
    } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

TEST(WavyProgressGeometryCachesCurvesCapsGapsAndMotion) {
    const gfx::Stroke stroke{ 4, 4, gfx::StrokeCap::Round };
    material3::internal::LinearWavyDrawingCache linear;
    const float half[] = { 0, 0.5f };
    linear.Update({ 240, 10 }, 40, half, 0, 0, 4, stroke, stroke);
    CHECK_EQ(linear.capWidth, 2.0f);
    CHECK_EQ(linear.progress.size(), std::size_t(1));
    CHECK_EQ(linear.progress[0].Bounds().left, 2.0f);
    CHECK_EQ(linear.progress[0].Bounds().right, 120.0f);
    CHECK_EQ(linear.track.Points()[1].x, 128.0f); // the later move-only contour has no stroke
    CHECK_EQ(linear.track.Bounds().right, 238.0f);
    linear.Update({ 240, 10 }, 40, half, 1, 0, 4, stroke, stroke);
    const auto hash = linear.progress[0].Hash();
    CHECK(std::find(linear.progress[0].Verbs().begin(), linear.progress[0].Verbs().end(), gfx::Path::Verb::Quad) != linear.progress[0].Verbs().end());
    linear.Update({ 240, 10 }, 40, half, 1, 0.25f, 4, stroke, stroke);
    CHECK(linear.progress[0].Hash() != hash);
    const float indeterminate[] = { 0.5f, 0.9f, 0.1f, 0.3f };
    linear.Update({ 240, 10 }, 20, indeterminate, 1, 0.25f, 4, stroke, stroke);
    CHECK_EQ(linear.progress.size(), std::size_t(2));
    CHECK(!linear.progress[0].IsEmpty() && !linear.progress[1].IsEmpty() && !linear.track.IsEmpty());
    linear.Update({ 240, 10 }, 0, half, 0, 0, 4, stroke, stroke); // flat paths need no wavelength
    bool rejected = false;
    try { linear.Update({ 240, 10 }, 0, half, 1, 0, 4, stroke, stroke); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);

    material3::internal::CircularWavyDrawingCache circular;
    CHECK_EQ(circular.VertexCount({ 48, 48 }, 15, 4), 9);
    CHECK_EQ(circular.VertexCount({ 48, 48 }, 15, 8), 8);
    CHECK_EQ(circular.VertexCount({ 48, 48 }, 100, 4), 5);
    circular.Update({ 48, 48 }, 15, 0.4f, 1, 0, 4, stroke, stroke, true);
    const auto progress = circular.progress.Hash(), track = circular.track.Hash();
    CHECK(!circular.progress.IsEmpty() && !circular.track.IsEmpty());
    circular.Update({ 48, 48 }, 15, 0.4f, 1, 0.25f, 4, stroke, stroke, true);
    CHECK(circular.progress.Hash() != progress && circular.track.Hash() == track);
    circular.Update({ 48, 48 }, 15, 0.4f, 0.5f, 0.25f, 4, stroke, stroke, true);
    CHECK(!circular.progress.IsEmpty());
    circular.Update({ 48, 48 }, 15, 1, 0, 0, 4, stroke, stroke, false);
    CHECK(circular.track.IsEmpty() && !circular.progress.IsEmpty());
    circular.Update({ 48, 48 }, 15, 0, 0, 0, 4, stroke, stroke, false);
    CHECK(circular.progress.IsEmpty() && !circular.track.IsEmpty());
}

TEST(WavyProgressHandoffsExitLinearSegmentsAndRotateCircularProgressToTheStart) {
    for (float start : { 0.0f, 300.0f, 800.0f, 1600.0f }) {
        material3::internal::WavyProgressState linear;
        linear.StartIndeterminate();
        linear.Step(start, NAN);
        linear.Step(0, 0.6f);
        CHECK(linear.indeterminate);
        CHECK(linear.drawSecondLine == (start > 650));
        for (int i = 0; i < 100 && linear.indeterminate; ++i) linear.Step(16, 0.6f);
        CHECK(!linear.indeterminate && linear.handoff && linear.EndProgress(0.6f) == 0);
        linear.Step(250, 0.6f);
        CHECK(std::abs(linear.EndProgress(0.6f) - 0.3f) < 0.0001f);
        linear.Step(250, 0.6f);
        CHECK(!linear.handoff && linear.EndProgress(0.6f) == 0.6f);
        linear.Step(16, NAN);
        CHECK(linear.indeterminate && linear.handoffFraction == 1);
    }
    material3::internal::WavyProgressState circular;
    circular.circular = true;
    circular.StartIndeterminate(); circular.Step(1200, NAN);
    const float rotation = circular.Rotation(), sweep = circular.EndProgress(NAN);
    circular.Step(0, 0.6f);
    CHECK(circular.handoff && !circular.indeterminate);
    CHECK(circular.handoffStartSweep == sweep);
    CHECK(circular.handoffStartRotation == std::fmod(rotation, 360.0f));
    circular.Step(150, 0.6f);
    CHECK(circular.handoffFraction > 0 && circular.handoffFraction < 1);
    circular.Step(600, 0.6f);
    CHECK(!circular.handoff && circular.Rotation() == 0 && circular.EndProgress(0.6f) == 0.6f);
}

TEST(WavyProgressComponentsSwitchBetweenUnknownAndKnownWithoutChangingSemanticsIdentity) {
    Harness h;
    rt::State<float> progress{ NAN };
    h.Run([&] { MaterialTheme([&] { Column(Padding({}, 20_dp), Arrangement::SpacedBy(20_dp), AlignmentH::Start(), [&] {
        LinearWavyProgressIndicator([&] { return progress.Get(); });
        CircularWavyProgressIndicator([&] { return progress.Get(); });
    }); }); });
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(tree.children.size(), std::size_t(2));
    const auto id = tree.children[0].id;
    CHECK(tree.children[0].config.progressBarRangeInfo->indeterminate && tree.children[1].config.progressBarRangeInfo->indeterminate);
    CHECK_EQ(int(tree.children[0].boundsInRoot.Height()), 30);
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
            h.owner.Draw({});
        }
    };
    advance(60);
    progress.Set(0.6f); advance(140);
    tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children[0].id == id && tree.children[0].config.progressBarRangeInfo->current == 0.6f);
    CHECK(!tree.children[1].config.progressBarRangeInfo->indeterminate && tree.children[1].config.progressBarRangeInfo->current == 0.6f);
    progress.Set(INFINITY); advance(40);
    CHECK(h.owner.BuildSemanticsTree().children[0].config.progressBarRangeInfo->indeterminate);
    anim::SetDurationScale(0); advance(2);
    progress.Set(1); advance(2);
    CHECK(h.owner.BuildSemanticsTree().children[0].config.progressBarRangeInfo->current == 1);
    anim::SetDurationScale(1); advance(140);
    CHECK(WavyProgressIndicatorDefaults::indicatorAmplitude(0.1f) == 0 && WavyProgressIndicatorDefaults::indicatorAmplitude(0.5f) == 1 && WavyProgressIndicatorDefaults::indicatorAmplitude(0.95f) == 0);
}

TEST(SliderStatesSnapClampPreserveRawDragOffsetsAndKeepRangesOrdered) {
    SliderState state(0.25f, 1);
    CHECK_EQ(state.Value(), 0.25f); // the constructor, unlike subsequent writes, does not snap
    state.SetValue(0.25f); CHECK_EQ(state.Value(), 0.0f); // first tick wins ties
    state.SetValue(2); CHECK_EQ(state.Value(), 1.0f);
    state.shouldAutoSnap = false;
    state.SetValue(-2); CHECK_EQ(state.Value(), -2.0f); CHECK_EQ(state.CoercedValueAsFraction(), 0.0f);
    state.SetValue(0.3f); CHECK_EQ(state.Value(), 0.3f);
    state.UpdateDimensions(104, 44, 4, 44);
    state.OnPress({ 52, 22 }); state.DispatchRawDelta(0);
    CHECK_EQ(state.Value(), 0.5f); // gestures always snap, even while automatic setter snapping is off
    state.DispatchRawDelta(200); CHECK_EQ(state.Value(), 1.0f);
    state.DispatchRawDelta(-100); CHECK_EQ(state.Value(), 1.0f); // retain excess drag outside the track
    state.DispatchRawDelta(-100); CHECK_EQ(state.Value(), 0.5f);
    state.Configure(Orientation::Vertical, false, true);
    state.UpdateDimensions(44, 104, 44, 4);
    state.OnPress({ 22, 27 }); state.DispatchRawDelta(0);
    CHECK_EQ(state.Value(), 0.5f);
    RangeSliderState range(0.8f, 0.2f, 3);
    CHECK_EQ(range.StartValue(), 0.2f); CHECK_EQ(range.EndValue(), 0.8f);
    range.SetStartValue(0.3f); range.SetEndValue(0.7f);
    CHECK_EQ(range.StartValue(), 0.25f); CHECK_EQ(range.EndValue(), 0.75f);
    CHECK_EQ(range.StartSteps(), 2); CHECK_EQ(range.EndSteps(), 2);
    range.UpdateDimensions(104, { 4, 44 }, { 4, 44 });
    range.OnDrag(true, 1000); CHECK_EQ(range.StartValue(), range.EndValue());
    range.OnDrag(false, -1000); CHECK_EQ(range.StartValue(), range.EndValue());
    bool rejected = false;
    try { SliderState invalid(0, -1); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { RangeSliderState invalid(0, 1, 0, { 1, 0 }); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

TEST(SliderDefaultsResolveColorsAndKeepThumbAllocationWhilePressed) {
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    h.Run([&] { MaterialTheme([&] {
        const auto scheme = LightColorScheme();
        const auto colors = SliderDefaults::colors();
        CHECK(colors.ThumbColor(true) == scheme.primary);
        CHECK(colors.TrackColor(true, true) == scheme.primary && colors.TrackColor(true, false) == scheme.secondaryContainer);
        CHECK(colors.TickColor(true, true) == scheme.secondaryContainer && colors.TickColor(true, false) == scheme.primary);
        CHECK(colors.ThumbColor(false) == ui::CompositeOver(scheme.onSurface.WithAlpha(0.38f), scheme.surface));
        CHECK(colors.TrackColor(false, true) == scheme.onSurface.WithAlpha(0.38f));
        CHECK(colors.TrackColor(false, false) == scheme.onSurface.WithAlpha(0.12f));
        CHECK(colors.Copy({}) == colors);
        CHECK(SliderDefaults::colors({ .inactiveTickColor = Color(0xff123456) }).TickColor(true, false) == Color(0xff123456));
        Column({}, [&] { SliderDefaults::Thumb(source); });
    }); });
    auto thumb = [&] { return h.owner.Root().Children()[0]->Children()[0]; };
    CHECK((thumb()->Size() == ui::IntSize{ 4, 44 }));
    CHECK_EQ(thumb()->Children()[0]->Size().width, 4);
    const auto press = source->Start(Interaction::Kind::Press);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK((thumb()->Size() == ui::IntSize{ 4, 44 }));
    CHECK_EQ(thumb()->Children()[0]->Size().width, 2);
    source->End(Interaction::Kind::Press, press);
}

TEST(SlidersPlaceSteppedThumbsMatchRtlAndDrawOnlyTicksOutsideTheGap) {
    for (bool rtl : { false, true }) {
        SliderState state(0.25f, 3);
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        std::vector<float> ticks;
        std::vector<ui::Offset> stops;
        h.Run([&] { MaterialTheme([&] {
            rt::Composition::Current()->Provide(ui::LocalLayoutDirection, rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                Column({}, [&] {
                    SliderOptions o;
                    o.track = [&](SliderState& s) {
                        SliderTrackOptions track;
                        track.drawTick = [&](ui::DrawScope&, ui::Offset position, Color) { ticks.push_back(position.x); };
                        track.drawStopIndicator = std::function<void(ui::DrawScope&, ui::Offset)>([&](ui::DrawScope&, ui::Offset p) { stops.push_back(p); });
                        SliderDefaults::Track(s, {}, track);
                    };
                    Slider(state, [&](float v) { state.SetValue(v); }, Width({}, 240_dp), o);
                });
            });
        }); });
        auto* slider = h.owner.Root().Children()[0]->Children()[0];
        CHECK((slider->Size() == ui::IntSize{ 240, 48 }));
        CHECK_EQ(state.ThumbWidth(), 4); CHECK_EQ(state.ThumbHeight(), 44);
        const auto thumb = slider->Children()[0]->InnerRectInRoot();
        CHECK_EQ(int(thumb.left - slider->InnerRectInRoot().left), rtl ? 173 : 63);
        CHECK_EQ(int(thumb.top), 2);
        h.owner.Draw({});
        CHECK_EQ(stops.size(), std::size_t(1));
        CHECK_EQ(ticks.size(), std::size_t(3));
        CHECK_EQ(stops[0].x, rtl ? 8.0f : 228.0f);
        const auto tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children.size() == 1 && tree.children[0].config.progressBarRangeInfo->steps == 3);
        CHECK_EQ(tree.children[0].config.progressBarRangeInfo->current, 0.25f);
    }
}

TEST(SliderSemanticsUsesSourceTieBreakingAndKeyboardHonorsOrientation) {
    for (bool vertical : { false, true }) for (bool reverse : { false, true }) {
        SliderState state(0, 1);
        Harness h;
        ui::FocusRequester focus;
        int finished = 0;
        h.owner.SetInputMode(ui::InputMode::Keyboard);
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            auto m = FocusRequesterModifier({}, focus);
            SliderOptions o; o.onValueChangeFinished = [&] { ++finished; };
            if (vertical) VerticalSlider(state, [&](float v) { state.SetValue(v); }, SizeMod(m, 48_dp, 240_dp), o, !reverse);
            else {
                rt::Composition::Current()->Provide(ui::LocalLayoutDirection, reverse ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                    Slider(state, [&](float v) { state.SetValue(v); }, Width(m, 240_dp), o);
                });
            }
        }); }); });
        const auto config = h.owner.BuildSemanticsTree().children[0].config;
        CHECK(config.setProgress(0.25f)); CHECK_EQ(state.Value(), 0.5f); CHECK_EQ(finished, 1);
        CHECK(!config.setProgress(0.5f)); CHECK_EQ(finished, 1);
        CHECK(focus.RequestFocus());
        const int key = vertical ? ui::keys::DpadDown : ui::keys::DpadRight;
        CHECK(h.owner.DispatchKeyEvent({ key, ui::KeyEventType::KeyDown }));
        CHECK_EQ(state.Value(), reverse ? 0.0f : 1.0f);
        CHECK(h.owner.DispatchKeyEvent({ key, ui::KeyEventType::KeyUp })); CHECK_EQ(finished, 2);
        CHECK(!h.owner.DispatchKeyEvent({ vertical ? ui::keys::DpadRight : ui::keys::DpadDown, ui::KeyEventType::KeyDown }));
    }
}

TEST(SliderTapCommitsOnReleaseAndDragReportsItsCompletion) {
    SliderState state(0.25f);
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    int finished = 0;
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        Slider(state, [&](float v) { state.SetValue(v); }, Width({}, 240_dp), { .onValueChangeFinished = [&] { ++finished; }, .interactionSource = source });
    }); }); });
    auto touch = [&](ui::PointerEventType type, float x, bool down, std::int64_t time) {
        ui::RawPointerEvent event;
        event.type = type; event.uptimeMillis = time;
        event.pointers.push_back({ 1, { x, 24 }, down, ui::PointerType::Touch, {} });
        h.owner.ProcessPointer(event); rt::Dispatcher::Main().RunPending();
        rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    };
    touch(ui::PointerEventType::Press, 180, true, 0);
    CHECK_EQ(state.Value(), 0.25f); CHECK(source->IsPressed());
    touch(ui::PointerEventType::Release, 180, false, 50);
    CHECK(std::abs(state.Value() - 178.0f / 236) < 0.0001f);
    CHECK_EQ(finished, 1); CHECK(!source->IsPressed());
    touch(ui::PointerEventType::Press, 180, true, 1000);
    touch(ui::PointerEventType::Move, 220, true, 1100);
    CHECK(state.IsDragging() && source->IsDragged());
    touch(ui::PointerEventType::Release, 220, false, 1200);
    CHECK_EQ(finished, 2); CHECK(!state.IsDragging() && !source->IsDragged() && !source->IsPressed());
}

TEST(RangeSliderThumbsHaveSeparateSemanticsAndCannotCrossFromActionsOrKeys) {
    RangeSliderState state(0.25f, 0.75f, 3);
    Harness h;
    h.owner.SetInputMode(ui::InputMode::Keyboard);
    int finished = 0;
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        RangeSlider(state, [&](SliderRange v) { state.SetStartValue(v.start); state.SetEndValue(v.endInclusive); }, Width({}, 240_dp), { .onValueChangeFinished = [&] { ++finished; } });
    }); }); });
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(tree.children.size(), std::size_t(2));
    CHECK(!tree.children[0].config.contentDescription.empty() && tree.children[0].config.contentDescription != tree.children[1].config.contentDescription);
    CHECK_EQ(tree.children[0].config.progressBarRangeInfo->end, 0.75f);
    CHECK_EQ(tree.children[1].config.progressBarRangeInfo->start, 0.25f);
    CHECK(tree.children[0].config.setProgress(1));
    CHECK_EQ(state.StartValue(), 0.75f); CHECK_EQ(state.EndValue(), 0.75f); CHECK_EQ(finished, 1);
    CHECK(tree.children[1].config.setProgress(1));
    CHECK_EQ(state.EndValue(), 1.0f);
    CHECK(h.owner.MoveFocus(ui::FocusDirection::Next));
    CHECK(h.owner.DispatchKeyEvent({ ui::keys::MoveEnd, ui::KeyEventType::KeyDown }));
    CHECK_EQ(state.StartValue(), state.EndValue());
}

TEST(RangeSliderOverlappingThumbsChooseDragDirectionAndCancelOnRemoval) {
    for (bool rtl : { false, true }) {
        RangeSliderState state(0.5f, 0.5f);
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        rt::State<bool> attached{ true };
        auto start = std::make_shared<MutableInteractionSource>(), end = std::make_shared<MutableInteractionSource>();
        int finished = 0;
        h.Run([&] { MaterialTheme([&] {
            rt::Composition::Current()->Provide(ui::LocalLayoutDirection, rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                Column({}, [&] {
                    if (attached.Get()) RangeSlider(state, [&](SliderRange v) { state.SetStartValue(v.start); state.SetEndValue(v.endInclusive); }, Width({}, 240_dp),
                        { .onValueChangeFinished = [&] { ++finished; }, .startThumbInteractionSource = start, .endThumbInteractionSource = end });
                });
            });
        }); });
        const float left = h.owner.Root().Children()[0]->Children()[0]->InnerRectInRoot().left;
        auto touch = [&](ui::PointerEventType type, float x, bool down, std::int64_t time) {
            ui::RawPointerEvent event; event.type = type; event.uptimeMillis = time;
            event.pointers.push_back({ 1, { left + x, 24 }, down, ui::PointerType::Touch, {} });
            h.owner.ProcessPointer(event); rt::Dispatcher::Main().RunPending();
            rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        };
        touch(ui::PointerEventType::Press, 120, true, 0);
        touch(ui::PointerEventType::Move, 80, true, 100);
        CHECK(state.IsDragging());
        CHECK(rtl ? state.StartValue() == 0.5f && state.EndValue() > 0.5f : state.StartValue() < 0.5f && state.EndValue() == 0.5f);
        CHECK(!start->IsPressed() && !end->IsPressed());
        CHECK(rtl ? end->IsDragged() : start->IsDragged());
        attached.Set(false); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(!state.IsDragging() && !start->IsDragged() && !end->IsDragged()); CHECK_EQ(finished, 1);
    }
}

TEST(RemovingASliderDuringADragClearsInteractionsAndDraggingState) {
    SliderState state(0.5f);
    Harness h;
    rt::State<bool> attached{ true };
    auto source = std::make_shared<MutableInteractionSource>();
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        if (attached.Get()) Slider(state, [&](float v) { state.SetValue(v); }, Width({}, 240_dp), { .interactionSource = source });
    }); }); });
    auto touch = [&](ui::PointerEventType type, float x, std::int64_t time) {
        ui::RawPointerEvent event; event.type = type; event.uptimeMillis = time;
        event.pointers.push_back({ 1, { x, 24 }, true, ui::PointerType::Touch, {} });
        h.owner.ProcessPointer(event); rt::Dispatcher::Main().RunPending();
        rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    };
    touch(ui::PointerEventType::Press, 120, 0);
    touch(ui::PointerEventType::Move, 180, 100);
    CHECK(state.IsDragging() && source->IsDragged());
    attached.Set(false); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(!state.IsDragging() && !source->IsDragged() && !source->IsPressed());
}

TEST(RememberedSliderStatesIgnoreNewInitialValuesAndRestoreThroughTheRegistry) {
    rt::SaveableStateRegistry::Values saved;
    rt::SaveableStateRegistry* registry = nullptr;
    SliderState* single = nullptr;
    RangeSliderState* range = nullptr;
    float initial = 0.25f;
    rt::State<int> version{ 0 };
    auto content = [&] {
        version.Get();
        rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, registry, [&] {
            single = &RememberSliderState(initial, 3);
            range = &RememberRangeSliderState(initial, 0.75f, 3);
        });
    };
    for (int pass = 0; pass < 2; ++pass) {
        rt::SaveableStateRegistry current(saved);
        registry = &current;
        Harness h;
        h.Run(content);
        CHECK(single && range);
        CHECK_EQ(single->Value(), pass == 0 ? 0.25f : 1.0f);
        CHECK_EQ(range->StartValue(), pass == 0 ? 0.25f : 0.5f);
        CHECK_EQ(range->EndValue(), pass == 0 ? 0.75f : 1.0f);
        CHECK_EQ(single->Steps(), 3); CHECK_EQ(range->Steps(), 3);
        if (pass == 0) {
            single->SetValue(1); range->SetStartValue(0.5f); range->SetEndValue(1);
            initial = 0; version.Set(1); rt::Composition::RecomposeAll();
            CHECK_EQ(single->Value(), 1.0f); CHECK_EQ(range->StartValue(), 0.5f);
            saved = rt::SaveableStateRegistry::Deserialize(rt::SaveableStateRegistry::Serialize(current.PerformSave()));
            CHECK_EQ(saved.size(), std::size_t(2));
        }
    }
}

TEST(DisabledSlidersKeepProgressSemanticsButRejectPointerKeyboardAndAccessibilityActions) {
    SliderState state(-0.25f, 0, { -1, 1 });
    Harness h;
    rt::State<bool> enabled{ true };
    ui::FocusRequester focus;
    auto source = std::make_shared<MutableInteractionSource>();
    h.owner.SetInputMode(ui::InputMode::Keyboard);
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        Slider(state, [&](float v) { state.SetValue(v); }, FocusRequesterModifier(Width({}, 240_dp), focus), { .enabled = enabled.Get(), .interactionSource = source });
    }); }); });
    CHECK(focus.RequestFocus());
    auto& accessibility = h.owner.Accessibility();
    accessibility.SetEnabled(true); accessibility.CheckForSemanticsChanges();
    auto tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children[0].config.stateDescription == "-0.25");
    const int id = tree.children[0].id;
    CHECK(accessibility.CreateNodeInfo(id)->className == "android.widget.SeekBar");
    enabled.Set(false); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children[0].config.disabled);
    CHECK_EQ(tree.children[0].config.progressBarRangeInfo->current, -0.25f);
    CHECK(!source->IsFocused());
    CHECK(!h.owner.DispatchKeyEvent({ ui::keys::DpadRight, ui::KeyEventType::KeyDown }));
    CHECK(!accessibility.PerformAction(id, ui::a11y::SetProgress, { .progress = 1.0f }));
    ui::RawPointerEvent event;
    event.type = ui::PointerEventType::Press; event.pointers.push_back({ 1, { 180, 24 }, true, ui::PointerType::Touch, {} });
    h.owner.ProcessPointer(event);
    event.type = ui::PointerEventType::Release; event.pointers[0].down = false;
    h.owner.ProcessPointer(event);
    CHECK_EQ(state.Value(), -0.25f);
    CHECK(!source->IsPressed() && !source->IsDragged());
}

TEST(SegmentedButtonDefaultsResolveEveryColorStateAndOnlyRoundTheOuterCorners) {
    Harness h;
    h.Run([&] { MaterialTheme([&] {
        const auto scheme = LightColorScheme();
        const auto colors = SegmentedButtonDefaults::colors();
        CHECK(colors.ContainerColor(true, true) == scheme.secondaryContainer);
        CHECK(colors.ContainerColor(false, true) == scheme.secondaryContainer);
        CHECK(colors.ContainerColor(true, false) == Color::Transparent());
        CHECK(colors.ContainerColor(false, false) == Color::Transparent());
        CHECK(colors.ContentColor(true, true) == scheme.onSecondaryContainer);
        CHECK(colors.ContentColor(true, false) == scheme.onSurface);
        CHECK(colors.ContentColor(false, true) == scheme.onSurface.WithAlpha(0.38f));
        CHECK(colors.ContentColor(false, false) == scheme.onSurface.WithAlpha(0.38f));
        CHECK(colors.BorderColor(true, true) == scheme.outline);
        CHECK(colors.BorderColor(true, false) == scheme.outline);
        CHECK(colors.BorderColor(false, true) == scheme.outline.WithAlpha(0.12f));
        CHECK(colors.BorderColor(false, false) == scheme.outline.WithAlpha(0.12f));
        const auto custom = SegmentedButtonDefaults::colors({ .activeBorderColor = Color(0xFF123456u), .disabledInactiveContentColor = Color(0xFF654321u) });
        CHECK(custom.activeBorderColor == Color(0xFF123456u));
        CHECK(custom.disabledInactiveContentColor == Color(0xFF654321u));
        CHECK(custom.activeContainerColor == colors.activeContainerColor);
        CHECK(custom.Copy({}) == custom);
        CHECK(SegmentedButtonDefaults::borderStroke(scheme.outline).width == 1_dp);
        const auto base = SegmentedButtonDefaults::baseShape();
        CHECK(SegmentedButtonDefaults::itemShape(0, 1)->Equals(*base));
        for (bool rtl : { false, true }) {
            const auto first = SegmentedButtonDefaults::itemShape(0, 3)->CreateOutline({ 100, 40 }, rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, { 1, 1 });
            const auto* corners = std::get_if<gfx::RoundRect>(&first.value);
            CHECK(corners != nullptr);
            if (corners) {
                CHECK_EQ(corners->topLeft.x, rtl ? 0.0f : 20.0f);
                CHECK_EQ(corners->topRight.x, rtl ? 20.0f : 0.0f);
            }
        }
        CHECK(SegmentedButtonDefaults::itemShape(1, 3)->Equals(*RectangleShape()));
        const auto cut = std::dynamic_pointer_cast<const CornerBasedShape>(CutCorner(6_dp));
        const auto last = std::dynamic_pointer_cast<const CornerBasedShape>(SegmentedButtonDefaults::itemShape(2, 3, cut));
        CHECK(last && last->topStart == CornerSize{} && last->topEnd == CornerSize::Of(6_dp));
    }); });
}

TEST(SegmentedButtonRowsKeepEqualWeightsOnePixelOverlapAndSourceSlotPlacement) {
    for (bool rtl : { false, true }) for (bool customPadding : { false, true }) {
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        ui::LayoutNode *label = nullptr, *icon = nullptr;
        Color content;
        text::TextStyle style;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            MultiChoiceSegmentedButtonRow({}, [&] {
                SegmentedButtonOptions o;
                if (customPadding) o.contentPadding = PaddingValues{ 24_dp, 24_dp, 24_dp, 24_dp };
                o.icon = std::function<void()>([&] { icon = &ListTestLeaf(18, 18); ListTestLeaf(10, 10); });
                SegmentedButton(false, [](bool) {}, SegmentedButtonDefaults::itemShape(0, 2), [&] {
                    content = rt::Composition::Current()->Read(LocalContentColor);
                    style = rt::Composition::Current()->Read(LocalTextStyle());
                    label = &ListTestLeaf(60, 20); ListTestLeaf(30, 10);
                }, {}, o);
                SegmentedButton(false, [](bool) {}, SegmentedButtonDefaults::itemShape(1, 2), [] { ListTestLeaf(30, 20); });
            });
        }); }); });
        auto* row = h.owner.Root().Children()[0]->Children()[0];
        const int expectedWidth = customPadding ? 267 : 219;
        CHECK_EQ(row->Size().width, expectedWidth);
        CHECK_EQ(row->Size().height, customPadding ? 68 : 48);
        const auto& buttons = row->Children();
        CHECK_EQ(buttons.size(), std::size_t(2));
        CHECK_EQ(buttons[0]->Size().width, buttons[1]->Size().width);
        CHECK_EQ(std::abs(int(buttons[1]->InnerRectInRoot().left - buttons[0]->InnerRectInRoot().left)), buttons[0]->Size().width - 1);
        CHECK_EQ(int(label->InnerRectInRoot().left - icon->InnerRectInRoot().left), 26);
        CHECK_EQ(label->Size().height, 20);
        CHECK(content == LightColorScheme().onSurface && style == Typography{}.labelLarge);
    }
}

TEST(SegmentedButtonsExposeSingleAndMultiChoiceStateAndRejectDisabledActions) {
    for (bool single : { false, true }) {
        rt::State<bool> selected{ false }, enabled{ true };
        Harness h;
        ui::FocusRequester focus;
        auto source = std::make_shared<MutableInteractionSource>();
        h.owner.SetInputMode(ui::InputMode::Keyboard);
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            auto content = [&] {
                SegmentedButtonOptions o; o.enabled = enabled.Get(); o.interactionSource = source;
                const auto m = FocusRequesterModifier({}, focus);
                if (single) SegmentedButton(selected.Get(), [&] { selected.Set(true); }, SegmentedButtonDefaults::itemShape(0, 1), [] { Text("Day"); }, m, o);
                else SegmentedButton(selected.Get(), [&](bool v) { selected.Set(v); }, SegmentedButtonDefaults::itemShape(0, 1), [] { Text("Day"); }, m, o);
            };
            if (single) SingleChoiceSegmentedButtonRow({}, content); else MultiChoiceSegmentedButtonRow({}, content);
        }); }); });
        auto tree = h.owner.BuildSemanticsTree();
        if (single) CHECK(tree.children[0].config.selectableGroup);
        auto button = [&] {
            auto root = h.owner.BuildSemanticsTree();
            return single ? root.children[0].children[0] : root.children[0];
        };
        CHECK(button().config.role == (single ? ui::Role::RadioButton : ui::Role::None));
        CHECK(single ? button().config.selected == false : button().config.toggleableState == 0);
        CHECK(button().config.onClick());
        rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(single ? button().config.selected == true : button().config.toggleableState == 1);
        CHECK(focus.RequestFocus());
        CHECK(h.owner.DispatchKeyEvent({ ui::keys::Space, ui::KeyEventType::KeyDown }));
        CHECK(h.owner.DispatchKeyEvent({ ui::keys::Space, ui::KeyEventType::KeyUp }));
        rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        CHECK_EQ(selected.Peek(), single);
        enabled.Set(false); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        CHECK(button().config.disabled && !source->IsFocused());
        auto& a11y = h.owner.Accessibility(); a11y.SetEnabled(true); a11y.CheckForSemanticsChanges();
        CHECK(!a11y.PerformAction(button().id, ui::a11y::Click));
        CHECK(!h.owner.DispatchKeyEvent({ ui::keys::Space, ui::KeyEventType::KeyDown }));
    }
}

TEST(SegmentedButtonLabelMotionKeepsAllocationAndInteractionsRaiseTheBorder) {
    rt::State<bool> active{ false };
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    ui::LayoutNode* label = nullptr;
    h.Run([&] { MaterialTheme([&] { Column({}, [&] {
        MultiChoiceSegmentedButtonRow({}, [&] {
            SegmentedButtonOptions o; o.interactionSource = source;
            o.icon = std::function<void()>([&] { if (active.Get()) ListTestLeaf(18, 18); });
            SegmentedButton(active.Get(), [&](bool v) { active.Set(v); }, SegmentedButtonDefaults::itemShape(0, 1), [&] { label = &ListTestLeaf(40, 20); }, {}, o);
        });
    }); }); });
    auto* row = h.owner.Root().Children()[0]->Children()[0];
    auto* button = row->Children()[0];
    const auto allocation = row->Size();
    const float collapsedX = label->InnerRectInRoot().left;
    CHECK_EQ(button->ZIndex(), 0.0f);
    const auto hover = source->Start(Interaction::Kind::Hover);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK_EQ(button->ZIndex(), 0.0f);
    const auto focus = source->Start(Interaction::Kind::Focus), press = source->Start(Interaction::Kind::Press);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK_EQ(button->ZIndex(), 2.0f);
    active.Set(true);
    std::int64_t clock = 0;
    auto advance = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            rt::Composition::RecomposeAll(); rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
            rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
        }
    };
    advance(3);
    CHECK(label->InnerRectInRoot().left > collapsedX && label->InnerRectInRoot().left < collapsedX + 13);
    advance(130);
    CHECK_EQ(label->InnerRectInRoot().left, collapsedX + 13);
    CHECK(row->Size() == allocation);
    CHECK_EQ(button->ZIndex(), 7.0f);
    source->Cancel(Interaction::Kind::Press, press); source->End(Interaction::Kind::Focus, focus); source->End(Interaction::Kind::Hover, hover);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK_EQ(button->ZIndex(), 5.0f);
    active.Set(false); advance(3); active.Set(true); advance(3); active.Set(false); advance(130);
    CHECK_EQ(label->InnerRectInRoot().left, collapsedX);
    CHECK(row->Size() == allocation && button->ZIndex() == 0.0f);
}

TEST(SegmentedButtonCustomSlotsKeepWideIconsEmptyLabelsAndMinimumSizes) {
    for (bool rtl : { false, true }) for (int kind = 0; kind < 3; ++kind) {
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        ui::LayoutNode *icon = nullptr, *label = nullptr;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            MultiChoiceSegmentedButtonRow({}, [&] {
                SegmentedButtonOptions o;
                o.icon = std::function<void()>([&] { if (kind == 0) icon = &ListTestLeaf(30, 18); });
                SegmentedButton(false, [](bool) {}, SegmentedButtonDefaults::itemShape(0, 1), [&] {
                    if (kind != 2) label = &ListTestLeaf(40, 20);
                }, {}, o);
            });
        }); }); });
        auto* row = h.owner.Root().Children()[0]->Children()[0];
        CHECK_EQ(row->Size().width, kind == 0 ? 102 : kind == 1 ? 90 : 58);
        CHECK_EQ(row->Size().height, 48);
        if (kind == 0) CHECK_EQ(label->InnerRectInRoot().left - icon->InnerRectInRoot().left, 26.0f);
        if (kind == 1) CHECK_EQ(label->InnerRectInRoot().left - row->InnerRectInRoot().left, 25.0f);
    }
}

TEST(SegmentedButtonIconsCrossfadeOrEnterWithoutAnExitAndCancelOnDisposal) {
    for (bool crossfade : { false, true }) {
        rt::State<bool> active{ false }, attached{ true };
        Harness h;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            if (!attached.Get()) return;
            MultiChoiceSegmentedButtonRow({}, [&] {
                SegmentedButtonOptions o;
                o.icon = std::function<void()>([&] {
                    // The toggleable merges its descendants' semantics, so the icons are told apart by size.
                    const auto named = [](const char* name) { Box(SizeMod({}, 18_dp, name[0] == 'a' ? 18_dp : 17_dp)); };
                    std::function<void()> inactive;
                    if (crossfade) inactive = [=] { named("inactive icon"); };
                    SegmentedButtonDefaults::Icon(active.Get(), [=] { named("active icon"); }, inactive);
                });
                SegmentedButton(active.Get(), [&](bool value) { active.Set(value); }, SegmentedButtonDefaults::itemShape(0, 1), [] { ListTestLeaf(40, 20); }, {}, o);
            });
        }); }); });
        std::int64_t clock = 0;
        auto advance = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                rt::Composition::RecomposeAll(); rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        auto count = [&](const char* name) {
            int result = 0;
            // Inside AnimatedVisibility the active icon is stretched to the label's 20px height.
            const bool activeIcon = name[0] == 'a';
            std::function<void(ui::LayoutNode&)> visit = [&](ui::LayoutNode& n) {
                if (n.Children().empty() && n.Size().width == 18 && (n.Size().height == 17) != activeIcon) ++result;
                for (auto* child : n.Children()) visit(*child);
            };
            visit(h.owner.Root()); return result;
        };
        CHECK_EQ(count("active icon"), 0);
        CHECK_EQ(count("inactive icon"), crossfade ? 1 : 0);
        const auto size = h.owner.Root().Children()[0]->Children()[0]->Size();
        active.Set(true); advance(3);
        CHECK_EQ(count("active icon"), 1);
        if (crossfade) CHECK_EQ(count("inactive icon"), 1);
        advance(130);
        CHECK_EQ(count("inactive icon"), 0);
        CHECK(h.owner.Root().Children()[0]->Children()[0]->Size() == size);
        active.Set(false); advance(3);
        CHECK_EQ(count("active icon"), crossfade ? 1 : 0);
        active.Set(true); advance(3); attached.Set(false); advance(130);
        CHECK(h.owner.Root().Children()[0]->Children().empty());
    }
}

TEST(SegmentedButtonTouchSelectionAndSourceReplacementCleanUpInteractions) {
    rt::State<int> selected{ 0 }, version{ 0 };
    rt::State<bool> attached{ true };
    auto source = std::make_shared<MutableInteractionSource>();
    Harness h;
    h.Run([&] { version.Get(); MaterialTheme([&] { Column({}, [&] {
        if (!attached.Get()) return;
        SingleChoiceSegmentedButtonRow({}, [&] {
            for (int i = 0; i < 2; ++i) SegmentedButton(selected.Get() == i, [&, i] { selected.Set(i); },
                SegmentedButtonDefaults::itemShape(i, 2), [] { ListTestLeaf(40, 20); }, {}, { .interactionSource = i == 1 ? source : nullptr });
        });
    }); }); });
    auto button = [&] { return h.owner.Root().Children()[0]->Children()[0]->Children()[1]; };
    const auto bounds = button()->InnerRectInRoot();
    auto pointer = [&](ui::PointerEventType type, bool down, std::int64_t time) {
        ui::RawPointerEvent event; event.type = type; event.uptimeMillis = time; event.buttons = down ? 1 : 0;
        event.pointers.push_back({ 1, { (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2 }, down, ui::PointerType::Mouse, {} });
        h.owner.ProcessPointer(event); rt::Dispatcher::Main().RunPending();
        rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    };
    pointer(ui::PointerEventType::Press, true, 0);
    CHECK(source->IsPressed() && button()->ZIndex() == 1);
    pointer(ui::PointerEventType::Release, false, 50);
    CHECK(!source->IsPressed() && selected.Peek() == 1 && button()->ZIndex() == 5);
    auto old = source;
    source = std::make_shared<MutableInteractionSource>(); version.Set(1);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    old->Start(Interaction::Kind::Press);
    rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK_EQ(button()->ZIndex(), 5.0f);
    pointer(ui::PointerEventType::Press, true, 100);
    CHECK(source->IsPressed());
    attached.Set(false); rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(!source->IsPressed() && h.owner.Root().Children()[0]->Children().empty());
}

TEST(SplitButtonDefaultsPickShapesPaddingAndIconSizesByHeight) {
    using namespace SplitButtonDefaults;
    Harness h;
    h.Run([] { MaterialTheme([] {
    const float heights[] = { 32, 40, 56, 96, 136 };
    const float inner[] = { 4, 4, 4, 8, 12 }, pressed[] = { 8, 12, 12, 20, 20 };
    const float lead[][2] = { { 12, 10 }, { 16, 12 }, { 24, 24 }, { 48, 48 }, { 64, 64 } }, trail[] = { 13, 13, 15, 29, 43 };
    const float icon[] = { 22, 22, 26, 38, 50 };
    for (int i = 0; i < 5; ++i) {
        const Dp h{ heights[i] };
        const auto leading = leadingButtonShapesFor(h), trailing = trailingButtonShapesFor(h);
        const auto l = std::dynamic_pointer_cast<const CornerBasedShape>(leading.shape);
        const auto lp = std::dynamic_pointer_cast<const CornerBasedShape>(leading.pressedShape);
        const auto t = std::dynamic_pointer_cast<const CornerBasedShape>(trailing.shape);
        const auto tp = std::dynamic_pointer_cast<const CornerBasedShape>(trailing.pressedShape);
        CHECK(l && lp && t && tp && !leading.checkedShape && trailing.checkedShape == CircleShape());
        CHECK(l->topStart == OuterCornerSize() && l->bottomStart == OuterCornerSize() && l->topEnd == CornerSize::Of(Dp(inner[i])) && l->bottomEnd == l->topEnd);
        CHECK(lp->topEnd == CornerSize::Of(Dp(pressed[i])) && lp->topStart == OuterCornerSize());
        CHECK(t->topEnd == OuterCornerSize() && t->bottomEnd == OuterCornerSize() && t->topStart == CornerSize::Of(Dp(inner[i])) && t->bottomStart == t->topStart);
        CHECK(tp->bottomStart == CornerSize::Of(Dp(pressed[i])));
        // The same instances each time, so a morph in progress is not restarted by recomposition.
        CHECK(leadingButtonShapesFor(h).shape == leading.shape && trailingButtonShapesFor(h).pressedShape == trailing.pressedShape);
        CHECK(leadingButtonContentPaddingFor(h) == (PaddingValues{ Dp(lead[i][0]), 0_dp, Dp(lead[i][1]), 0_dp }));
        CHECK(trailingButtonContentPaddingFor(h) == (PaddingValues{ Dp(trail[i]), 0_dp, Dp(trail[i]), 0_dp }));
        CHECK_EQ(trailingButtonIconSizeFor(h).value, icon[i]);
        CHECK_EQ(leadingButtonIconSizeFor(h).value, ButtonDefaults::iconSizeFor(h).value);
    }
    // Shapes change at the midpoints between heights, padding and icons at the heights themselves.
    CHECK(leadingButtonShapesFor(36_dp).shape == leadingButtonShapesFor(32_dp).shape);
    CHECK(leadingButtonShapesFor(37_dp).shape == leadingButtonShapesFor(40_dp).shape);
    CHECK(leadingButtonContentPaddingFor(39_dp) == ExtraSmallLeadingButtonContentPadding);
    CHECK(trailingButtonContentPaddingFor(200_dp) == ExtraLargeTrailingButtonContentPadding);
    CHECK_EQ(Spacing.value, 2.0f);
    }); });
}

TEST(SplitButtonLayoutGivesTheTrailingButtonItsWidthAndBothTheSameHeight) {
    for (bool rtl : { false, true }) for (int kind = 0; kind < 3; ++kind) {
        Harness h;
        h.owner.SetDirection(rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr);
        ui::LayoutNode *label = nullptr, *icon = nullptr;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            // kind 0: natural size; 1: too narrow for both; 2: a tall leading button.
            SplitButtonLayout([&] { SplitButtonDefaults::LeadingButton([] {}, {}, {}, [&] { label = &ListTestLeaf(kind == 1 ? 200 : 40, kind == 2 ? 60 : 20); }); },
                              [&] { SplitButtonDefaults::TrailingButton(false, [](bool) {}, {}, {}, [&] { icon = &ListTestLeaf(22, 22); }); },
                              kind == 1 ? Width({}, 100_dp) : Modifier{});
        }); }); });
        auto* layout = h.owner.Root().Children()[0]->Children()[0];
        auto* leading = layout->Children()[0];
        auto* trailing = layout->Children()[1];
        const int height = kind == 2 ? 60 : 40;
        CHECK_EQ(trailing->Size().width, 48);
        CHECK_EQ(leading->Size().width, kind == 1 ? 50 : 68);
        CHECK(leading->Size().height == height && trailing->Size().height == height);
        // The whole control, not each button, gets the 48dp touch target.
        CHECK_EQ(layout->Size().height, std::max(48, height));
        CHECK_EQ(layout->Size().width, kind == 1 ? 100 : 118);
        const auto l = leading->InnerRectInRoot(), t = trailing->InnerRectInRoot();
        if (rtl) CHECK_EQ(l.left - t.right, 2.0f); else CHECK_EQ(t.left - l.right, 2.0f);
        CHECK_EQ(l.top, t.top);
        // The label sits after the 16dp leading space; the icon is optically centred towards
        // the inner corner (0.11 * (4 - 20) rounds to 2px).
        const auto text = label->InnerRectInRoot(), glyph = icon->InnerRectInRoot();
        if (kind != 1) CHECK_EQ(rtl ? l.right - text.right : text.left - l.left, 16.0f);
        // (The full outer corner is capped at half the height: 20px, or 30px when tall.)
        CHECK_EQ(rtl ? t.right - glyph.right : glyph.left - t.left, kind == 2 ? 10.0f : 11.0f);
    }
}

TEST(SplitButtonsClickToggleAndRecentreACheckedTrailingButton) {
    using Maker = void (*)(bool, std::function<void(bool)>, const ui::Modifier&, const SplitButtonOptions&, std::function<void()>);
    const Maker makers[] = { static_cast<Maker>(SplitButtonDefaults::TrailingButton), SplitButtonDefaults::TonalTrailingButton,
                             SplitButtonDefaults::OutlinedTrailingButton, SplitButtonDefaults::ElevatedTrailingButton };
    for (int variant = 0; variant < 4; ++variant) {
        rt::State<bool> checked{ false }, enabled{ true };
        int clicks = 0;
        ui::LayoutNode* icon = nullptr;
        Harness h;
        h.Run([&] { MaterialTheme([&] { Column({}, [&] {
            const SplitButtonOptions o{ .enabled = enabled.Get() };
            const auto click = [&] { ++clicks; };
            const auto label = [] { ListTestLeaf(40, 20); };
            SplitButtonLayout([&, o, click, label] {
                if (variant == 0) SplitButtonDefaults::LeadingButton(click, {}, o, label);
                else if (variant == 1) SplitButtonDefaults::TonalLeadingButton(click, {}, o, label);
                else if (variant == 2) SplitButtonDefaults::OutlinedLeadingButton(click, {}, o, label);
                else SplitButtonDefaults::ElevatedLeadingButton(click, {}, o, label);
            }, [&, o] { makers[variant](checked.Get(), [&](bool value) { checked.Set(value); }, {}, o, [&] { icon = &ListTestLeaf(22, 22); }); });
        }); }); });
        std::int64_t clock = 0;
        auto advance = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                rt::Composition::RecomposeAll(); rt::Dispatcher::Main().RunFrame(clock += 16'000'000);
                rt::Composition::RecomposeAll(); h.owner.MeasureAndLayout({ 300, 300 });
            }
        };
        auto tree = h.owner.BuildSemanticsTree();
        CHECK_EQ(tree.children.size(), std::size_t(2));
        CHECK(tree.children[0].config.role == ui::Role::Button && !tree.children[0].config.toggleableState);
        CHECK(tree.children[1].config.role == ui::Role::Button && *tree.children[1].config.toggleableState == 0);
        const float restX = icon->InnerRectInRoot().left;
        const auto size = h.owner.Root().Children()[0]->Children()[0]->Size();
        CHECK(tree.children[0].config.onClick() && clicks == 1);
        CHECK(tree.children[1].config.onClick());
        advance(60);
        CHECK(checked.Peek());
        CHECK_EQ(*h.owner.BuildSemanticsTree().children[1].config.toggleableState, 1);
        // A circle has equal corners, so the icon returns to the geometric centre.
        CHECK_EQ(icon->InnerRectInRoot().left, restX + 2);
        CHECK(h.owner.Root().Children()[0]->Children()[0]->Size() == size);
        enabled.Set(false); advance(3);
        tree = h.owner.BuildSemanticsTree();
        CHECK(tree.children[0].config.disabled && tree.children[1].config.disabled);
    }
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

namespace {
    ui::IntOffset Place(const std::shared_ptr<const ui::PopupPositionProvider>& p, ui::IntRect anchor, ui::LayoutDirection dir = ui::LayoutDirection::Ltr) {
        return p->CalculatePosition(anchor, { 300, 300 }, dir, { 60, 20 });
    }
}

TEST(TooltipPositionProvidersPreferTheirSideAndFlipOrClampAtTheWindowEdges) {
    Harness h;
    std::map<int, std::shared_ptr<const ui::PopupPositionProvider>> p;
    std::shared_ptr<const ui::PopupPositionProvider> rich;
    h.owner.MeasureAndLayout({ 300, 300 });
    h.Run([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
            for (int i = 1; i <= 6; ++i) p[i] = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition(i), ui::Dp(4));
            rich = TooltipDefaults::rememberRichTooltipPositionProvider();
        }); });
    });
    using P = TooltipAnchorPosition;
    const ui::IntRect mid{ 100, 100, 140, 140 };
    // Above: centred, 4 px over the anchor; below it when it would leave the top.
    CHECK(Place(p[int(P::Above)], mid) == ui::IntOffset(90, 76));
    CHECK(Place(p[int(P::Above)], { 100, 0, 140, 40 }) == ui::IntOffset(90, 44));
    CHECK(Place(p[int(P::Above)], { 0, 100, 20, 140 }) == ui::IntOffset(0, 76));   // clamped to the left edge
    CHECK(Place(p[int(P::Below)], mid) == ui::IntOffset(90, 144));
    CHECK(Place(p[int(P::Below)], { 100, 270, 140, 290 }) == ui::IntOffset(90, 246));
    // Left / right: centred vertically, flipped when there is no room.
    CHECK(Place(p[int(P::Left)], mid) == ui::IntOffset(36, 110));
    CHECK(Place(p[int(P::Left)], { 10, 100, 50, 140 }) == ui::IntOffset(54, 110));
    CHECK(Place(p[int(P::Right)], mid) == ui::IntOffset(144, 110));
    CHECK(Place(p[int(P::Right)], { 250, 100, 290, 140 }) == ui::IntOffset(186, 110));
    // Start / end follow the layout direction.
    CHECK(Place(p[int(P::Start)], mid) == ui::IntOffset(36, 110));
    CHECK(Place(p[int(P::Start)], mid, ui::LayoutDirection::Rtl) == ui::IntOffset(144, 110));
    CHECK(Place(p[int(P::End)], mid, ui::LayoutDirection::Rtl) == ui::IntOffset(36, 110));
    // The deprecated rich provider starts at the anchor and ends at it near the right edge.
    CHECK(Place(rich, mid) == ui::IntOffset(100, 76));
    CHECK(Place(rich, { 260, 100, 290, 140 }) == ui::IntOffset(230, 76));
    // The caret sits mid-tooltip unless the tooltip is pushed against an edge.
    CHECK_EQ(internal::CaretX(60, 300, { 100, 100, 140, 140 }), 30.0f);
    CHECK_EQ(internal::CaretX(60, 300, { 0, 100, 20, 140 }), 10.0f);
    CHECK_EQ(internal::CaretX(60, 300, { 280, 100, 300, 140 }), 50.0f);
    CHECK_EQ(internal::CaretX(300, 300, { 100, 100, 140, 140 }), 120.0f);
}

TEST(TooltipStatesShareAMutexAndEndTheirShowOnDismissCancelOrTimeout) {
    auto& d = rt::Dispatcher::Main();
    auto mutex = std::make_shared<MutatorMutex>();
    TooltipState a = MakeTooltipState(false, true, mutex), b = MakeTooltipState(false, true, mutex);
    rt::Job showA = rt::Launch(d, a.Show());
    CHECK(a.IsVisible());
    CHECK(!showA.Done());
    // Another show on the mutex hides this one and ends its show.
    rt::Job showB = rt::Launch(d, b.Show());
    CHECK(b.IsVisible());
    CHECK(!a.IsVisible());
    d.RunPending();
    CHECK(showA.Done());
    // Dismissing a persistent tooltip ends its show.
    b.Dismiss();
    CHECK(!b.IsVisible());
    d.RunPending();
    CHECK(showB.Done());
    // A lower priority does not take over; the long-press show is not hidden when it ends.
    rt::Job held = rt::Launch(d, a.Show(MutatePriority::PreventUserInput));
    rt::Job refused = rt::Launch(d, b.Show());
    CHECK(refused.Done());
    CHECK(!b.IsVisible());
    a.OnDispose();
    d.RunPending();
    CHECK(held.Done());
    CHECK(a.IsVisible());
    a.Dismiss();
    CHECK(!a.IsVisible());
    // Cancelling the caller runs the show's cleanup and frees the mutex.
    rt::Job cancelled = rt::Launch(d, a.Show());
    cancelled.Cancel();
    CHECK(!a.IsVisible());
    rt::Job after = rt::Launch(d, b.Show());
    CHECK(b.IsVisible());
    b.Dismiss();
    d.RunPending();
    // A non-persistent tooltip hides after TooltipDuration; hover (UserInput) keeps it up.
    TooltipState brief = MakeTooltipState(false, false, mutex);
    rt::Job timed = rt::Launch(d, brief.Show());
    CHECK(brief.IsVisible());
    d.AdvanceTimeBy(std::chrono::milliseconds(BasicTooltipDefaults::TooltipDuration));
    d.RunPending();
    d.RunPending();
    CHECK(!brief.IsVisible());
    CHECK(timed.Done());
    // The basic state has no transition: visibility flips at once and ends with the show.
    TooltipState basic = internal::MakeBasicTooltipState(false, true, mutex);
    rt::Job basicShow = rt::Launch(d, basic.Show(MutatePriority::PreventUserInput));
    CHECK(basic.IsVisible());
    basic.OnDispose();
    CHECK(!basic.IsVisible());
    d.RunPending();
    CHECK(basicShow.Done());
}

namespace {
    void Touch(Harness& h, std::int64_t& t, ui::PointerEventType kind, float x, float y, bool down) {
        ui::RawPointerEvent e;
        e.type = kind;
        e.uptimeMillis = t += 16;
        e.pointers.push_back({ 1, { x, y }, down, ui::PointerType::Touch, {} });
        h.owner.ProcessPointer(e);
        for (int i = 0; i < 3; ++i) {
            rt::Dispatcher::Main().RunPending();
            rt::Composition::RecomposeAll();
            h.Layout();
        }
    }
}

TEST(TooltipBoxShowsOnLongPressWithoutClickingTheAnchorAndDismissesOutside) {
    Harness h;
    h.owner.Config().longPressTimeoutMillis = 40;
    std::int64_t t = 1000;
    h.owner.MeasureAndLayout({ 300, 300 });
    int clicks = 0;
    std::optional<TooltipState> state;
    std::shared_ptr<const ui::PopupPositionProvider> provider;
    h.Run([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
            provider = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Below);
            state = RememberTooltipState(false, true);
            Box({}, Alignment::TopStart(), false, [&] { TooltipBox(
                provider, [](const TooltipScope& scope) { PlainTooltip(scope, {}, {}, [] { Text("Tip"); }); }, *state, {}, {},
                [&] { Box(Clickable(SizeMod(Modifier{}, ui::Dp(40), ui::Dp(40)), [&] { ++clicks; }), Alignment::TopStart(), false, {}); }); });
        }); });
    });
    CHECK(h.owner.Popups().empty());
    // The anchor offers the long press to accessibility services.
    auto tree = h.owner.BuildSemanticsTree();
    CHECK(tree.children.size() == 1 && tree.children[0].config.onLongClick && tree.children[0].config.onLongClickLabel);
    // A short tap clicks the anchor.
    Touch(h, t, ui::PointerEventType::Press, 20, 20, true);
    Touch(h, t, ui::PointerEventType::Release, 20, 20, false);
    CHECK_EQ(clicks, 1);
    CHECK(!state->IsVisible());
    // A long press shows the tooltip below the anchor, and lifting does not click.
    Touch(h, t, ui::PointerEventType::Press, 20, 20, true);
    rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(80));
    t += 80;
    Touch(h, t, ui::PointerEventType::Move, 20, 20, true);
    CHECK(state->IsVisible());
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    Touch(h, t, ui::PointerEventType::Release, 20, 20, false);
    CHECK_EQ(clicks, 1);
    CHECK(state->IsVisible());
    if (h.owner.Popups().size() == 1 && h.owner.Popups()[0]->bounds) CHECK_EQ(h.owner.Popups()[0]->bounds->top, 44);
    // A press outside dismisses it.
    Touch(h, t, ui::PointerEventType::Press, 250, 250, true);
    Touch(h, t, ui::PointerEventType::Release, 250, 250, false);
    CHECK(!state->Transition().TargetState());
    // The accessibility long click shows it again.
    tree = h.owner.BuildSemanticsTree();
    if (!tree.children.empty() && tree.children[0].config.onLongClick) CHECK(tree.children[0].config.onLongClick());
    Touch(h, t, ui::PointerEventType::Move, 0, 0, false);
    CHECK(state->Transition().TargetState());
    state->Dismiss();
}

TEST(DropdownMenuOpensBelowItsAnchorClicksItemsAndDismissesOutside) {
    Harness h;
    std::int64_t t = 1000;
    h.owner.MeasureAndLayout({ 300, 300 });
    auto expanded = std::make_shared<rt::State<bool>>(false);
    int clicked = -1, dismissed = 0;
    h.Run([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
            Box({}, Alignment::TopStart(), false, [&] {
                Box(Padding(Modifier{}, PaddingValues{ ui::Dp(0), ui::Dp(60), ui::Dp(0), ui::Dp(0) }), Alignment::TopStart(), false, [&] {
                    Box(SizeMod(Modifier{}, ui::Dp(40), ui::Dp(40)), Alignment::TopStart(), false, [&] {
                        DropdownMenu(expanded->Get(), [&] { ++dismissed; expanded->Set(false); }, {}, {}, [&] {
                            for (int i = 0; i < 2; ++i) DropdownMenuItem([] { Text("Item"); }, [&clicked, i] { clicked = i; }, {}, {});
                        });
                    });
                });
            });
        }); });
    });
    CHECK(h.owner.Popups().empty());
    expanded->Set(true);
    Touch(h, t, ui::PointerEventType::Move, 0, 0, false);
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    // Below the anchor at its start: 112 dp wide (the item minimum), 8 + 2 x 48 + 8 tall.
    if (h.owner.Popups().size() == 1 && h.owner.Popups()[0]->bounds) {
        const auto& b = *h.owner.Popups()[0]->bounds;
        CHECK((b == ui::IntRect{ 0, 100, 112, 212 }));
    }
    Touch(h, t, ui::PointerEventType::Press, 25, 120, true);
    Touch(h, t, ui::PointerEventType::Release, 25, 120, false);
    CHECK_EQ(clicked, 0);
    // A press outside asks to dismiss.
    Touch(h, t, ui::PointerEventType::Press, 250, 280, true);
    Touch(h, t, ui::PointerEventType::Release, 250, 280, false);
    CHECK_EQ(dismissed, 1);
    CHECK(!expanded->Peek());
}

TEST(MenuDefaultsShapeColourAndArrangeExpressiveItems) {
    Harness h;
    auto checked = std::make_shared<rt::State<bool>>(false);
    auto selected = std::make_shared<rt::State<bool>>(false);
    bool sameShapes = false, distinctShapes = false, groupsCached = false;
    MenuItemColors copied{}, base{};
    std::vector<int> ltr(3), rtl(3);
    h.Run([&] {
        MaterialTheme({}, [&] {
            sameShapes = MenuDefaults::itemShape(0, 3) == MenuDefaults::itemShape(0, 3) && MenuDefaults::itemShape(0, 1) == MenuDefaults::itemShapes();
            distinctShapes = MenuDefaults::itemShape(0, 3).shape != MenuDefaults::itemShape(2, 3).shape &&
                             MenuDefaults::itemShape(1, 3).shape == MenuDefaults::middleItemShape();
            groupsCached = MenuDefaults::groupShape(2, 3) == MenuDefaults::groupShape(2, 3) &&
                           MenuDefaults::groupShape(0, 1).shape == MenuDefaults::standaloneGroupShape();
            base = MenuDefaults::itemColors();
            copied = base.Copy(Color(0xFFFF0000u));
            const auto a = MenuDefaults::DropdownMenuItemHorizontalArrangement();
            const int sizes[] = { 20, 50, 20 };
            a.Arrange(ui::Density{ 1, 1 }, 200, sizes, ui::LayoutDirection::Ltr, ltr);
            a.Arrange(ui::Density{ 1, 1 }, 200, sizes, ui::LayoutDirection::Rtl, rtl);
            Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
                CheckableDropdownMenuItem(checked->Get(), [checked](bool v) { checked->Set(v); }, [] { Text("Bold"); }, MenuDefaults::itemShape(0, 2), {}, {});
                SelectableDropdownMenuItem(selected->Get(), [selected] { selected->Set(true); }, [] { Text("Left"); }, MenuDefaults::itemShape(1, 2), {}, {});
            });
        });
    });
    CHECK(sameShapes);
    CHECK(distinctShapes);
    CHECK(groupsCached);
    CHECK(copied.textColor == Color(0xFFFF0000u));
    CHECK(copied.leadingIconColor == base.leadingIconColor && copied.containerColor == Color::Transparent());
    // Leading icon at the start, the text 8 dp after it, trailing content at the end; mirrored in RTL.
    CHECK((ltr == std::vector{ 0, 28, 180 }));
    CHECK((rtl == std::vector{ 180, 122, 0 }));
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(tree.children.size(), std::size_t(2));
    if (tree.children.size() == 2) {
        CHECK(tree.children[0].config.role == ui::Role::Checkbox);
        CHECK(tree.children[1].config.role == ui::Role::RadioButton);
        CHECK(tree.children[0].config.onClick && tree.children[0].config.onClick());
        CHECK(tree.children[1].config.onClick && tree.children[1].config.onClick());
    }
    CHECK(checked->Peek());
    CHECK(selected->Peek());
    // Items fill the width; the clickable surface keeps the minimum touch height.
    const auto& column = *h.owner.Root().Children()[0];
    if (!column.Children().empty()) CHECK_EQ(column.Children()[0]->Size().height, 48);   // the 44 dp row inside the 48 dp touch target
}

namespace {
    void Frames(Harness& h, std::int64_t& nanos, int count) {
        for (int i = 0; i < count; ++i) {
            nanos += 16'000'000;
            rt::Dispatcher::Main().RunFrame(nanos);
            rt::Dispatcher::Main().RunPending();
            rt::Composition::RecomposeAll();
            h.Layout();
        }
    }
    std::vector<int> ChildXs(const ui::LayoutNode& n) {
        std::vector<int> xs;
        for (auto* c : n.Children()) xs.push_back(int(c->PositionInRoot().x - n.PositionInRoot().x));
        return xs;
    }
}

TEST(ButtonGroupMovesWhatDoesNotFitIntoItsOverflowMenu) {
    Harness h;
    std::int64_t t = 1000;
    h.owner.MeasureAndLayout({ 300, 300 });
    auto width = std::make_shared<rt::State<int>>(300);
    ButtonGroupMenuState* menu = nullptr;
    int thirdClicks = 0;
    h.Run([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
            Box({}, Alignment::TopStart(), false, [&] {
                ButtonGroup([&](ButtonGroupMenuState& m) { menu = &m; Box(SizeMod(Modifier{}, ui::Dp(30), ui::Dp(40))); },
                            Width(Modifier{}, ui::Dp(float(width->Get()))), [&](ButtonGroupScope& scope) {
                    for (int i = 0; i < 2; ++i)
                        scope.CustomItem([] { Box(SizeMod(Modifier{}, ui::Dp(60), ui::Dp(40))); }, [](ButtonGroupMenuState&) {});
                    scope.ClickableItem([&] { ++thirdClicks; }, "Third");
                });
            });
        }); });
    });
    // Everything fits: 12 dp apart, no overflow indicator placed.
    const ui::LayoutNode* group = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(group->Children().size(), std::size_t(4));
    auto xs = ChildXs(*group);
    CHECK(xs.size() == 4 && xs[0] == 0 && xs[1] == 72 && xs[2] == 144);
    CHECK(group->Children()[3]->IsPlaced() == false);
    // 150 wide: the indicator takes 30, one 60 item fits, the indicator follows it.
    width->Set(150);
    Touch(h, t, ui::PointerEventType::Move, 299, 299, false);
    group = h.owner.Root().Children()[0]->Children()[0];
    CHECK(group->Children()[3]->IsPlaced());
    xs = ChildXs(*group);
    CHECK(xs.size() == 4 && xs[0] == 0 && xs[3] == 72);
    CHECK(!group->Children()[1]->IsPlaced() && !group->Children()[2]->IsPlaced());
    // The menu holds the rest; a clickable item clicks and closes it.
    CHECK(menu != nullptr);
    if (!menu) return;
    menu->Show();
    Touch(h, t, ui::PointerEventType::Move, 299, 299, false);
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    if (h.owner.Popups().size() == 1 && h.owner.Popups()[0]->bounds) {
        const auto b = *h.owner.Popups()[0]->bounds;
        // Two items in the menu: the second custom one (no menu content) and "Third".
        Touch(h, t, ui::PointerEventType::Press, float(b.left + 10), float(b.bottom - 20), true);
        Touch(h, t, ui::PointerEventType::Release, float(b.left + 10), float(b.bottom - 20), false);
    }
    CHECK_EQ(thirdClicks, 1);
    CHECK(!menu->IsShowing());
}

// Ported from Compose's ButtonGroupTest, with its numbers: density 2 resolves its 0.5 dp
// tolerance, and the window is a 412 x 800 dp phone.
namespace {
    const ui::SemanticsNode* FindSemantics(const ui::SemanticsNode& n, const std::string& label) {
        const auto has = [&](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), label) != v.end(); };
        if (has(n.config.text) || has(n.config.contentDescription) || (n.config.testTag && *n.config.testTag == label)) return &n;
        for (const auto& c : n.children)
            if (const auto* r = FindSemantics(c, label)) return r;
        return nullptr;
    }
    // onNodeWithText / onNodeWithTag(label).getUnclippedBoundsInRoot(), in dp; nullopt when the
    // node is not shown.
    // A tag alone does not make an accessibility node: tags are found on the layout nodes (the
    // unmerged tree), placed ones only.
    const ui::LayoutNode* FindTagged(const ui::LayoutNode& n, const std::string& tag) {
        if (!n.IsPlaced() && n.Parent()) return nullptr;
        ui::SemanticsConfiguration c;
        if (ui::CalculateSemanticsConfiguration(const_cast<ui::LayoutNode&>(n), c) && c.testTag == tag) return &n;
        for (const auto* child : n.Children())
            if (const auto* r = FindTagged(*child, tag)) return r;
        return nullptr;
    }
    // A tagged node's bounds where the tag sits in its modifiers: what wraps it (a layout
    // modifier's offset, for instance) moves them.
    ui::Rect TagRectInRoot(const ui::LayoutNode& n, const std::string& tag) {
        for (const auto* m : n.ModifierNodes()) {
            auto* sem = const_cast<ui::ModifierNode*>(m)->AsSemantics();
            if (!sem) continue;
            ui::SemanticsConfiguration c;
            sem->ApplySemantics(c);
            if (c.testTag == tag) return n.RectInRoot(std::nullopt, m);
        }
        return n.BoundsInRoot();
    }
    // The content and every popup (Root() is the content root; popups are its siblings).
    const ui::LayoutNode* FindTagged(Harness& h, const std::string& tag) {
        if (const auto* r = FindTagged(h.owner.Root(), tag)) return r;
        for (const ui::PopupLayer* p : h.owner.Popups())
            if (p->node)
                if (const auto* r = FindTagged(*p->node, tag)) return r;
        return nullptr;
    }
    std::optional<ui::Rect> BoundsDp(Harness& h, const std::string& label) {
        const ui::SemanticsNode tree = h.owner.BuildSemanticsTree();
        const ui::SemanticsNode* n = FindSemantics(tree, label);
        std::optional<ui::Rect> b;
        if (n) b = n->boundsInRoot;
        else if (const auto* tagged = FindTagged(h, label)) {
            // Clipped by the ancestors that clip, as semantics bounds are (isDisplayed).
            ui::Rect r = TagRectInRoot(*tagged, label);
            for (const ui::LayoutNode* a = tagged->Parent(); a; a = a->Parent()) {
                bool clips = false;
                for (const ui::ModifierNode* m : a->ModifierNodes()) clips |= m->ClipsChildren();
                if (!clips) continue;
                const ui::Rect c = a->BoundsInRoot();
                r = ui::Rect{ std::max(r.left, c.left), std::max(r.top, c.top), std::min(r.right, c.right), std::min(r.bottom, c.bottom) };
                if (r.right < r.left) r.right = r.left;
                if (r.bottom < r.top) r.bottom = r.top;
            }
            b = r;
        }
        // Then the dialog windows (full-window, at the origin).
        for (ui::Owner* dialog : h.dialogs) {
            if (b) break;
            const ui::SemanticsNode dt = dialog->BuildSemanticsTree();
            if (const auto* dn = FindSemantics(dt, label)) b = dn->boundsInRoot;
        }
        if (!b) return std::nullopt;
        const float d = h.owner.GetDensity().density;
        return ui::Rect{ b->left / d, b->top / d, b->right / d, b->bottom / d };
    }
    // isDisplayed: placed, and at least partly inside the window.
    bool Displayed(Harness& h, const std::string& label) {
        const auto b = BoundsDp(h, label);
        const float d = h.owner.GetDensity().density;
        return b && b->Width() > 0 && b->Height() > 0 && b->right > 0 && b->left < h.window.width / d && b->bottom > 0 && b->top < h.window.height / d;
    }
    ui::Modifier Tag(ui::Modifier m, std::string tag) {
        return Semantics(std::move(m), [tag = std::move(tag)](ui::SemanticsConfiguration& c) { c.testTag = tag; });
    }
    void Press(Harness& h, std::int64_t& t, const std::string& label, bool release) {
        const auto b = BoundsDp(h, label);
        CHECK(b.has_value());
        if (!b) return;
        const float d = h.owner.GetDensity().density;
        const float x = (b->left + b->right) / 2 * d, y = (b->top + b->bottom) / 2 * d;
        Touch(h, t, ui::PointerEventType::Press, x, y, true);
        if (release) Touch(h, t, ui::PointerEventType::Release, x, y, false);
    }
    // Text measures (monospace, half the font size per character): constructed before the
    // owner and destroyed after it.
    struct WithFakeText { test::FakeText text; };
    struct PhoneHarness : WithFakeText, Harness {
        explicit PhoneHarness(ui::IntSize size = { 824, 1600 }) : Harness(2, size) { owner.SetText(&text); }
    };

    // Four 75 dp buttons that widen while pressed; the widths 200 ms after pressing one.
    std::vector<float> PressedWidths(float expandedRatio, int pressed) {
        PhoneHarness h;
        std::int64_t t = 1000, nanos = 0;
        std::vector<std::shared_ptr<MutableInteractionSource>> sources;
        for (int i = 0; i < 4; ++i) sources.push_back(std::make_shared<MutableInteractionSource>());
        const std::vector<std::string> labels{ "A", "B", "C", "D" };
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            ButtonGroupOptions o;
            o.expandedRatio = expandedRatio;
            ButtonGroup({}, {}, [&](ButtonGroupScope& scope) {
                for (int i = 0; i < 4; ++i) {
                    auto source = sources[i];
                    const std::string label = labels[i];
                    scope.CustomItem([&scope, source, label] {
                        ButtonOptions b;
                        b.interactionSource = source;
                        Button([] {}, Tag(scope.AnimateWidth(Width({}, ui::Dp(75)), source), label), b, [label] { Text(label); });
                    }, [](ButtonGroupMenuState&) {});
                }
            }, o);
        }); }); });
        Press(h, t, labels[pressed], false);
        Frames(h, nanos, 1 + 13);   // a frame, then 200 ms
        std::vector<float> widths;
        for (const auto& l : labels) widths.push_back(BoundsDp(h, l) ? BoundsDp(h, l)->Width() : -1);
        return widths;
    }

    // Items labelled 0..count-1 (or "Item i"), with an IconButton tagged overflowIndicator.
    void NumberedGroup(int count, const ui::Modifier& modifier, std::string prefix = "") {
        ButtonGroup([](ButtonGroupMenuState&) { IconButton([] {}, Tag({}, "overflowIndicator"), {}, [] {}); }, modifier,
                    [=](ButtonGroupScope& scope) {
            for (int i = 0; i < count; ++i) scope.ClickableItem([] {}, prefix + std::to_string(i));
        });
    }
}

TEST(ButtonGroupSpacesItems12DpApartWithNothingAroundThem) {   // default_positioning
    PhoneHarness h;
    h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
        Box(Tag({}, "wrapper"), Alignment::TopStart(), false, [&] {
            ButtonGroup({}, {}, [](ButtonGroupScope& scope) {
                for (const char* l : { "A", "B", "C", "D" }) scope.ClickableItem([] {}, l);
            });
        });
    }); }); });
    const auto w = BoundsDp(h, "wrapper"), a = BoundsDp(h, "A"), b = BoundsDp(h, "B"), c = BoundsDp(h, "C"), d = BoundsDp(h, "D");
    CHECK(w && a && b && c && d);
    if (!(w && a && b && c && d)) return;
    CHECK_NEAR(a->left - w->left, 0, 0.5);
    CHECK_NEAR(b->left - a->right, 12, 0.5);
    CHECK_NEAR(c->left - b->right, 12, 0.5);
    CHECK_NEAR(d->left - c->right, 12, 0.5);
    CHECK_NEAR(w->right - d->right, 0, 0.5);
}

TEST(ButtonGroupPressedItemTakesItsRatioFromItsNeighbours) {   // default_/customAnimateFraction_*Pressed_buttonSizing
    for (const float ratio : { 0.15f, 0.3f }) {
        for (int pressed = 0; pressed < 4; ++pressed) {
            const auto widths = PressedWidths(ratio, pressed);
            CHECK_EQ(widths.size(), std::size_t(4));
            if (widths.size() != 4) continue;
            // An end item takes it all from its one neighbour; a middle one half from each.
            const bool edge = pressed == 0 || pressed == 3;
            for (int i = 0; i < 4; ++i) {
                float expected = 75;
                if (i == pressed) expected = 75 + 75 * ratio;
                else if (std::abs(i - pressed) == 1) expected = 75 - 75 * (edge ? ratio : ratio / 2);
                CHECK_NEAR(widths[i], expected, 0.5);
            }
        }
    }
}

TEST(ButtonGroupOverflowIndicatorShowsOnlyWhenItemsDoNotFit) {   // overflowIndicator_*
    {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] { NumberedGroup(100, {}); }); }); });
        CHECK(Displayed(h, "overflowIndicator"));
    }
    {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] { NumberedGroup(4, {}); }); }); });
        CHECK(!Displayed(h, "overflowIndicator"));
        CHECK(Displayed(h, "3"));
    }
    {   // fixed constraints (minWidth == maxWidth) from a 150 dp parent and fillMaxWidth
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            Box(Width({}, ui::Dp(150)), Alignment::TopStart(), false, [&] { NumberedGroup(10, FillMax({}, Axis::Horizontal), "Item "); });
        }); }); });
        CHECK(Displayed(h, "overflowIndicator"));
    }
}

TEST(ButtonGroupOverflowMenuShowsTheItemsThatDoNotFit) {   // overflowMenu_tooManyItems_exists
    PhoneHarness h;
    std::int64_t t = 1000;
    h.owner.MeasureAndLayout(h.window);
    h.Run([&] { rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
        Box({}, Alignment::TopStart(), false, [&] {
            ButtonGroup([](ButtonGroupMenuState& menu) {
                IconButton([&menu] { menu.IsShowing() ? menu.Dismiss() : menu.Show(); }, Tag({}, "overflowIndicator"), {}, [] {});
            }, {}, [](ButtonGroupScope& scope) {
                for (int i = 0; i < 10; ++i) {
                    const std::string label = std::to_string(i);
                    scope.CustomItem([label] { Button([] {}, {}, {}, [label] { Text(label); }); }, [label](ButtonGroupMenuState&) {
                        DropdownMenuItem([label] { Text(label); }, [] {}, Tag({}, label + " MenuItem"), {});
                    });
                }
            });
        });
    }); }); });
    CHECK(!Displayed(h, "9 MenuItem"));
    Press(h, t, "overflowIndicator", true);
    CHECK(Displayed(h, "9 MenuItem"));
}

TEST(ButtonGroupScrollsWhenGivenHorizontalScroll) {   // horizontalScroll_handlingScroll_buttonDisplay
    PhoneHarness h;
    std::int64_t t = 1000;
    h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
        Box(Tag({}, "wrapper"), Alignment::TopStart(), false, [&] { NumberedGroup(10, HorizontalScroll({}, RememberScrollState())); });
    }); }); });
    CHECK(!Displayed(h, "9"));
    const auto w = BoundsDp(h, "wrapper");
    CHECK(w.has_value());
    if (!w) return;
    // swipe(center, center - 1500 px) over 100 ms
    const float x = (w->left + w->right), y = (w->top + w->bottom);   // the centre, in pixels at density 2
    Touch(h, t, ui::PointerEventType::Press, x, y, true);
    for (int i = 1; i <= 6; ++i) Touch(h, t, ui::PointerEventType::Move, x - 1500.f * i / 6, y, true);
    Touch(h, t, ui::PointerEventType::Release, x - 1500, y, false);
    std::int64_t nanos = 0;
    Frames(h, nanos, 120);   // let any fling settle
    CHECK(Displayed(h, "9"));
}

TEST(ButtonGroupHorizontalArrangementPlacesTheItems) {   // horizontalArrangement_*_buttonGroupPositioning
    const std::pair<Arrangement, float> cases[] = { { Arrangement::Start(), 1.f / 3 }, { Arrangement::Center(), 1.f / 2 }, { Arrangement::End(), 2.f / 3 } };
    for (const auto& [arrangement, at] : cases) {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            ButtonGroupOptions o;
            o.horizontalArrangement = arrangement;
            ButtonGroup({}, FillMax({}, Axis::Horizontal), [](ButtonGroupScope& scope) {
                for (int i = 0; i < 3; ++i) scope.ClickableItem([] {}, std::to_string(i));
            }, o);
        }); }); });
        const auto first = BoundsDp(h, "0"), last = BoundsDp(h, "2");
        CHECK(first && last);
        if (first && last) CHECK(first->left <= 412 * at && 412 * at <= last->right);
    }
}

TEST(ButtonGroupAlignsItemsVerticallyAndItemsCanOverrideIt) {   // buttonGroup_verticalAlignment_*, individualAlignment_*
    const auto run = [](ui::AlignmentV group, std::optional<ui::AlignmentV> own, ui::Dp shortHeight, ui::Dp tallHeight) {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            ButtonGroupOptions o;
            o.verticalAlignment = group;
            ButtonGroup({}, {}, [&](ButtonGroupScope& scope) {
                scope.CustomItem([&scope, own, shortHeight] {
                    const ui::Modifier m = Height({}, shortHeight);
                    Button([] {}, Tag(own ? scope.Align(m, *own) : m, "short"), {}, [] { Text("S"); });
                }, [](ButtonGroupMenuState&) {});
                scope.CustomItem([tallHeight] { Button([] {}, Tag(Height({}, tallHeight), "tall"), {}, [] { Text("T"); }); }, [](ButtonGroupMenuState&) {});
            }, o);
        }); }); });
        return std::pair{ BoundsDp(h, "short").value_or(ui::Rect{}), BoundsDp(h, "tall").value_or(ui::Rect{}) };
    };
    auto [s, t] = run(ui::AlignmentV::Top(), std::nullopt, ui::Dp(48), ui::Dp(64));
    CHECK_NEAR(s.top, t.top, 0.5);
    std::tie(s, t) = run(ui::AlignmentV::CenterVertically(), std::nullopt, ui::Dp(48), ui::Dp(64));
    CHECK_NEAR((s.top + s.bottom) / 2, (t.top + t.bottom) / 2, 0.5);
    std::tie(s, t) = run(ui::AlignmentV::Bottom(), std::nullopt, ui::Dp(48), ui::Dp(64));
    CHECK_NEAR(s.bottom, t.bottom, 0.5);
    // Per item: the group is as tall as the 100 dp one.
    std::tie(s, t) = run(ui::AlignmentV::Top(), ui::AlignmentV::Top(), ui::Dp(50), ui::Dp(100));
    CHECK_NEAR(s.top, 0, 0.5);
    CHECK_NEAR(t.top, 0, 0.5);
    std::tie(s, t) = run(ui::AlignmentV::Top(), ui::AlignmentV::CenterVertically(), ui::Dp(50), ui::Dp(100));
    CHECK_NEAR(s.top, 25, 0.5);
    std::tie(s, t) = run(ui::AlignmentV::Top(), ui::AlignmentV::Bottom(), ui::Dp(50), ui::Dp(100));
    CHECK_NEAR(s.top, 50, 0.5);
    std::tie(s, t) = run(ui::AlignmentV::Bottom(), ui::AlignmentV::Top(), ui::Dp(50), ui::Dp(100));
    CHECK_NEAR(s.top, 0, 0.5);
    CHECK_NEAR(t.top, 0, 0.5);
}

TEST(ButtonGroupNeighboursGiveWayNoFurtherThanTheirCompressionLimit) {   // widthAnimation_*PaddingLimit, asymmetricPadding_*
    // Two weighted buttons in 100 dp, 10 dp limits: B gives min(ratio x 50, 10) dp.
    const auto pair = [](std::optional<float> ratio) {
        PhoneHarness h;
        std::int64_t t = 1000, nanos = 0;
        auto a = std::make_shared<MutableInteractionSource>(), b = std::make_shared<MutableInteractionSource>();
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] { Box(Width({}, ui::Dp(100)), Alignment::TopStart(), false, [&] {
            ButtonGroupOptions o;
            if (ratio) o.expandedRatio = *ratio;
            o.horizontalArrangement = Arrangement::SpacedBy(ui::Dp(0));
            ButtonGroup({}, {}, [&](ButtonGroupScope& scope) {
                for (auto& [source, label] : { std::pair{ a, std::string("A") }, std::pair{ b, std::string("B") } }) {
                    scope.CustomItem([&scope, source, label] {
                        ButtonOptions opts;
                        opts.interactionSource = source;
                        opts.contentPadding = PaddingValues{ ui::Dp(10), ui::Dp(0), ui::Dp(10), ui::Dp(0) };
                        Button([] {}, Tag(scope.AnimateWidth(scope.Weight({}, 1), source, ui::Dp(10)), label), opts, [label] { Text(label); });
                    }, [](ButtonGroupMenuState&) {});
                }
            }, o);
        }); }); }); });
        Press(h, t, "A", false);
        Frames(h, nanos, 1 + 13);
        return BoundsDp(h, "B").value_or(ui::Rect{}).Width();
    };
    CHECK_NEAR(pair(1.f), 40, 0.5);
    CHECK_NEAR(pair(std::nullopt), 42.5, 0.5);
    // Pressing the middle of three 100 dp buttons (ratio 0.5: 25 dp from each side), whose
    // outer neighbours stop at 10 dp.
    for (const bool rtl : { false, true }) {
        PhoneHarness h;
        std::int64_t t = 1000, nanos = 0;
        std::vector<std::shared_ptr<MutableInteractionSource>> sources;
        for (int i = 0; i < 3; ++i) sources.push_back(std::make_shared<MutableInteractionSource>());
        h.Run([&] { rt::Composition::Current()->Provide(ui::LocalLayoutDirection, rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
            MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
                ButtonGroupOptions o;
                o.expandedRatio = 0.5f;
                o.horizontalArrangement = Arrangement::SpacedBy(ui::Dp(0));
                ButtonGroup({}, {}, [&](ButtonGroupScope& scope) {
                    for (int i = 0; i < 3; ++i) {
                        auto source = sources[i];
                        const std::string label = std::string(1, char('A' + i));
                        const std::optional<ui::Dp> limit = i == 1 ? std::nullopt : std::optional(ui::Dp(10));
                        scope.CustomItem([&scope, source, label, limit] {
                            ButtonOptions opts;
                            opts.interactionSource = source;
                            if (limit) opts.contentPadding = PaddingValues{ ui::Dp(40), ui::Dp(0), ui::Dp(10), ui::Dp(0) };
                            Button([] {}, Tag(scope.AnimateWidth(Width({}, ui::Dp(100)), source, limit), label), opts, [label] { Text(label); });
                        }, [](ButtonGroupMenuState&) {});
                    }
                }, o);
            }); });
        }); });
        Press(h, t, "B", false);
        Frames(h, nanos, 1 + 13);
        CHECK_NEAR(BoundsDp(h, "A").value_or(ui::Rect{}).Width(), 90, 1);
        CHECK_NEAR(BoundsDp(h, "C").value_or(ui::Rect{}).Width(), 90, 1);
        CHECK_NEAR(BoundsDp(h, "B").value_or(ui::Rect{}).Width(), 120, 1);
    }
}

// Ported from Compose's SplitButtonTest.
TEST(SplitButtonVariantsShowTheirContentAndExposeButtonSemantics) {   // *SplitButton_contentDisplay, *Semantics
    using Leading = void (*)(SplitButtonDefaults::Content, const ui::Modifier&, const SplitButtonOptions&, SplitButtonDefaults::Content);
    using Trailing = void (*)(bool, std::function<void(bool)>, const ui::Modifier&, const SplitButtonOptions&, SplitButtonDefaults::Content);
    const std::pair<Leading, Trailing> variants[] = {
        { SplitButtonDefaults::LeadingButton, static_cast<Trailing>(SplitButtonDefaults::TrailingButton) },
        { SplitButtonDefaults::TonalLeadingButton, SplitButtonDefaults::TonalTrailingButton },
        { SplitButtonDefaults::ElevatedLeadingButton, SplitButtonDefaults::ElevatedTrailingButton },
        { SplitButtonDefaults::OutlinedLeadingButton, SplitButtonDefaults::OutlinedTrailingButton },
    };
    for (const bool enabled : { true, false }) {
        for (const auto& [leading, trailing] : variants) {
            PhoneHarness h;
            h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
                SplitButtonOptions o;
                o.enabled = enabled;
                SplitButtonLayout([&] {
                    leading([] {}, Tag({}, "leadingButton"), o, [] {
                        Icon(material3::internal::Icons::Filled::Check(), std::string("Leading Icon"));
                        Spacer(SizeMod({}, ButtonDefaults::IconSpacing, ButtonDefaults::IconSpacing));
                        Text("My Button");
                    });
                }, [&] {
                    trailing(false, [](bool) {}, Tag(SizeMod({}, ui::Dp(34), ui::Dp(34)), "trailingButton"), o, [] {
                        Icon(material3::internal::Icons::Filled::Check(), std::string("Trailing Icon"));
                    });
                });
            }); }); });
            CHECK(Displayed(h, "My Button"));
            const ui::SemanticsNode tree = h.owner.BuildSemanticsTree();
            const auto* lead = FindSemantics(tree, "leadingButton");
            const auto* trail = FindSemantics(tree, "trailingButton");
            CHECK(lead && trail);
            if (!lead || !trail) continue;
            CHECK(lead->config.role == ui::Role::Button && trail->config.role == ui::Role::Button);
            CHECK(lead->config.onClick != nullptr && trail->config.onClick != nullptr);
            CHECK_EQ(lead->config.disabled, !enabled);
            CHECK_EQ(trail->config.disabled, !enabled);
            // The icons merge into their buttons; each still lays out where it is shown.
            CHECK(Displayed(h, "Leading Icon") || FindTagged(h, "Leading Icon") != nullptr || (lead->config.contentDescription.size() == 1));
            CHECK((trail->config.contentDescription == std::vector<std::string>{ "Trailing Icon" }));
        }
    }
}

TEST(SplitButtonTrailingButtonTogglesWhenClicked) {   // filledSplitButton_trailingButtonChecked
    PhoneHarness h;
    std::int64_t t = 1000;
    h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
        auto& checked = RememberState(false);
        SplitButtonLayout([] { SplitButtonDefaults::LeadingButton([] {}, {}, {}, [] { Text("My Button"); }); }, [&checked] {
            SplitButtonDefaults::TrailingButton(checked.Get(), [&checked](bool) { checked.Set(!checked.Peek()); }, Tag({}, "trailingButton"), {},
                                                [] { Icon(material3::internal::Icons::Filled::Check(), std::string("Trailing Icon")); });
        });
    }); }); });
    const auto state = [&] {
        const ui::SemanticsNode tree = h.owner.BuildSemanticsTree();
        const auto* n = FindSemantics(tree, "trailingButton");
        return n && n->config.toggleableState && n->config.role == ui::Role::Button && !n->config.disabled ? int(*n->config.toggleableState) : -1;
    };
    CHECK_EQ(state(), 0);
    Press(h, t, "trailingButton", true);
    CHECK_EQ(state(), 1);
}

TEST(SplitButtonSizesPadTheirLeadingContentLikeTheTokens) {   // filledSplitButton_*_positioning
    using namespace SplitButtonDefaults;
    struct Case { Dp height; float start, between, end; };
    const Case cases[] = { { ExtraSmallContainerHeight, 12, 4, 10 }, { SmallContainerHeight, 16, 8, 12 }, { MediumContainerHeight, 24, 8, 24 },
                           { LargeContainerHeight, 48, 12, 48 }, { ExtraLargeContainerHeight, 64, 16, 64 } };
    for (const Case& c : cases) {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            SplitButtonLayout([&] {
                SplitButtonOptions o;
                o.shapes = leadingButtonShapesFor(c.height);
                o.contentPadding = leadingButtonContentPaddingFor(c.height);
                LeadingButton([] {}, Tag(SizeIn({}, Dp::Unspecified(), c.height, Dp::Unspecified(), Dp::Unspecified()), "leadingButton"), o, [&] {
                    Icon(material3::internal::Icons::Filled::Check(), std::string("Leading Icon"), Tag({}, "leadingIcon"));
                    Spacer(SizeMod({}, ButtonDefaults::iconSpacingFor(c.height), ButtonDefaults::iconSpacingFor(c.height)));
                    Text("My Button", Tag({}, "text"));
                });
            }, [&] {
                SplitButtonOptions o;
                o.shapes = trailingButtonShapesFor(c.height);
                o.contentPadding = trailingButtonContentPaddingFor(c.height);
                TrailingButton(false, [](bool) {}, SizeIn({}, Dp::Unspecified(), c.height, Dp::Unspecified(), Dp::Unspecified()), o,
                               [] { Icon(material3::internal::Icons::Filled::Check(), std::string("Trailing Icon")); });
            });
        }); }); });
        const auto button = BoundsDp(h, "leadingButton"), icon = BoundsDp(h, "leadingIcon"), text = BoundsDp(h, "text");
        CHECK(button && icon && text);
        if (!(button && icon && text)) continue;
        CHECK_NEAR(icon->left - button->left, c.start, 0.5);
        CHECK_NEAR(text->left - icon->right, c.between, 0.5);
        CHECK_NEAR(button->right - text->right, c.end, 0.5);
    }
}

TEST(SplitButtonHeightsMatchAndStandaloneButtonsDoNotStretch) {   // *_doesNotStretchHeight, splitButton_*Height*
    const auto height = [](Harness& h, const char* tag) { return BoundsDp(h, tag).value_or(ui::Rect{}).Height(); };
    {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] { Box(Height({}, ui::Dp(200)), Alignment::TopStart(), false, [&] {
            Column({}, Arrangement::Top(), AlignmentH::Start(), [] {
                SplitButtonDefaults::LeadingButton([] {}, Tag({}, "leadingButton"), {}, [] { Text("Label"); });
                SplitButtonDefaults::TrailingButton([] {}, Tag({}, "trailingButton"), {}, [] { Icon(material3::internal::Icons::Filled::Check(), std::nullopt); });
            });
        }); }); }); });
        CHECK_NEAR(height(h, "leadingButton"), 40, 0.5);
        CHECK_NEAR(height(h, "trailingButton"), 40, 0.5);
    }
    // A forced layout height, or either button 100 dp tall: both buttons take it.
    for (int which = 0; which < 3; ++which) {
        PhoneHarness h;
        h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
            const ui::Modifier tall = Height({}, ui::Dp(100));
            SplitButtonLayout([&] { SplitButtonDefaults::LeadingButton([] {}, Tag(which == 1 ? tall : Modifier{}, "leadingButton"), {}, [] { Text("Label"); }); },
                              [&] { SplitButtonDefaults::TrailingButton([] {}, Tag(which == 2 ? tall : Modifier{}, "trailingButton"), {},
                                                                        [] { Icon(material3::internal::Icons::Filled::Check(), std::nullopt); }); },
                              which == 0 ? tall : Modifier{});
        }); }); });
        CHECK_NEAR(height(h, "leadingButton"), 100, 0.5);
        CHECK_NEAR(height(h, "trailingButton"), 100, 0.5);
    }
}

TEST(SplitButtonFillMaxWidthLeadingButtonInAHorizontalScrollDoesNotCrash) {   // splitButton_leadingFillMaxWidth_horizontalScroll_noCrash
    PhoneHarness h;
    h.Run([&] { MaterialTheme({}, [&] {
        Row(HorizontalScroll({}, RememberScrollState()), Arrangement::Start(), AlignmentV::Top(), [] {
            SplitButtonLayout([] { SplitButtonDefaults::LeadingButton([] {}, FillMax({}, Axis::Horizontal), {}, [] { Text("Label"); }); },
                              [] { SplitButtonDefaults::TrailingButton(false, [](bool) {}, {}, {}, [] { Icon(material3::internal::Icons::Filled::Check(), std::string("Trailing Icon")); }); });
        });
    }); });
    CHECK(Displayed(h, "Label"));
}

// Ported from Compose's MenuTest.
namespace {
    // A 20 px anchor whose menu holds `content`, shown while `expanded`.
    void MenuAnchor(PhoneHarness& h, rt::State<bool>& expanded, std::function<void()> content, DropdownMenuOptions options = {}) {
        h.Run([&h, &expanded, content = std::move(content), options] {
            rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
                Box({}, Alignment::TopStart(), false, [&] {
                    Box(SizeMod({}, ui::Dp(10), ui::Dp(10)), Alignment::TopStart(), false, [&] {
                        DropdownMenu(expanded.Get(), [] {}, {}, options, content);
                    });
                });
            }); });
        });
    }
}

TEST(DropdownMenuShowsAndHidesWithExpanded) {   // menu_canBeTriggered
    PhoneHarness h;
    h.owner.MeasureAndLayout(h.window);
    rt::State<bool> expanded{ false };
    std::int64_t nanos = 0;
    MenuAnchor(h, expanded, [] { DropdownMenuItem([] { Text("Option 1"); }, [] {}, Tag({}, "MenuContent"), {}); });
    CHECK(FindTagged(h, "MenuContent") == nullptr);
    for (int round = 0; round < 2; ++round) {
        expanded.Set(true);
        Frames(h, nanos, 2 + 19);   // two frames, then 300 ms
        CHECK(FindTagged(h, "MenuContent") != nullptr);
        if (round == 1) break;
        expanded.Set(false);
        Frames(h, nanos, 2 + 19 + 1);
        CHECK(FindTagged(h, "MenuContent") == nullptr);
    }
}

TEST(DropdownMenuIsAsWideAsItsWidestItemAndPadsVertically) {   // menu_hasExpectedSize
    PhoneHarness h;
    h.owner.MeasureAndLayout(h.window);
    rt::State<bool> expanded{ true };
    std::int64_t nanos = 0;
    // 70 px and 130 px squares at density 2.
    MenuAnchor(h, expanded, [] {
        Box(Tag(SizeMod({}, ui::Dp(35), ui::Dp(35)), "MenuContent1"));
        Box(Tag(SizeMod({}, ui::Dp(65), ui::Dp(65)), "MenuContent2"));
    });
    Frames(h, nanos, 20);
    CHECK(FindTagged(h, "MenuContent1") && FindTagged(h, "MenuContent2"));
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    if (h.owner.Popups().size() != 1 || !h.owner.Popups()[0]->bounds) return;
    const auto b = *h.owner.Popups()[0]->bounds;
    CHECK_EQ(b.right - b.left, 130);
    CHECK_EQ(b.bottom - b.top, 2 * 16 + 200);   // DropdownMenuVerticalPadding (8 dp) above and below
}

TEST(DropdownMenuScrollsItsContent) {   // menu_scrolledContent
    PhoneHarness h;
    h.owner.MeasureAndLayout(h.window);
    rt::State<bool> expanded{ true };
    std::int64_t nanos = 0;
    ScrollState* scroll = nullptr;
    h.Run([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { MaterialTheme({}, [&] {
            Box({}, Alignment::TopStart(), false, [&] {
                Box(SizeMod({}, ui::Dp(10), ui::Dp(10)), Alignment::TopStart(), false, [&] {
                    scroll = &RememberScrollState();
                    DropdownMenuOptions o;
                    o.scrollState = scroll;
                    DropdownMenu(true, [] {}, {}, o, [] {
                        for (int i = 1; i <= 100; ++i) Box(Tag(SizeMod({}, ui::Dp(35), ui::Dp(35)), "MenuContent " + std::to_string(i)));
                    });
                    rt::Composition::Current()->LaunchedEffect(rt::Keys(0), [&]() -> rt::Task<> { scroll->ScrollTo(scroll->MaxValue()); co_return; });
                });
            });
        }); });
    });
    Frames(h, nanos, 20);
    CHECK(scroll && scroll->MaxValue() > 0 && scroll->Value() == scroll->MaxValue());
    CHECK(!Displayed(h, "MenuContent 1"));
    CHECK(Displayed(h, "MenuContent 100"));
}

TEST(DropdownMenuItemClicksAndPlacesItsTrailingIconAtTheEnd) {   // dropdownMenuItem_onClick, _noLeadingIcon_trailingIconPosition
    PhoneHarness h;
    std::int64_t t = 1000;
    bool clicked = false;
    float selectablePadding = 0;
    h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
        selectablePadding = MenuDefaults::DropdownMenuSelectableItemContentPadding().start.value;
        Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
            DropdownMenuItem([] { Box(SizeMod({}, ui::Dp(40), ui::Dp(40))); }, [&] { clicked = true; }, Tag({}, "ClickItem"), {});
            DropdownMenuItemOptions o;
            o.trailingIcon = [] { Box(Tag(SizeMod({}, ui::Dp(20), ui::Dp(20)), "TrailingIcon")); };
            DropdownMenuItem([] { Box(Tag(SizeMod({}, ui::Dp(40), ui::Dp(40)), "Text")); }, [] {}, Tag({}, "MenuItem"), o);
        });
    }); }); });
    Press(h, t, "ClickItem", true);
    CHECK(clicked);
    const auto item = BoundsDp(h, "MenuItem"), text = BoundsDp(h, "Text"), icon = BoundsDp(h, "TrailingIcon");
    CHECK(item && text && icon);
    if (!(item && text && icon)) return;
    CHECK(text->left < icon->left);
    // Compose: right edge minus the selectable items' start padding and 4 dp, within 2.
    CHECK_NEAR(icon->right, item->right - selectablePadding - 4, 2);
}

TEST(ExpressiveDropdownMenuItemKeepsItsTrailingContentWithLongText) {   // expressiveDropdownMenuItem_longText_doesNotClipTrailingContent
    PhoneHarness h;
    h.Run([&] { MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, [&] {
        Box(Width({}, ui::Dp(200)), Alignment::TopStart(), false, [&] {
            MenuItemOptions o;
            o.leadingIcon = [] { Box(Tag(SizeMod({}, ui::Dp(24), ui::Dp(24)), "LeadingIcon")); };
            o.trailingContent = [] { Box(Tag(SizeMod({}, ui::Dp(24), ui::Dp(24)), "TrailingContent")); };
            DropdownMenuItem([] {}, [] { Box(Tag(SizeMod({}, ui::Dp(400), ui::Dp(20)), "Text")); }, RectangleShape(), Tag({}, "MenuItem"), o);
        });
    }); }); });
    const auto item = BoundsDp(h, "MenuItem"), text = BoundsDp(h, "Text"), trailing = BoundsDp(h, "TrailingContent");
    CHECK(item && text && trailing);
    if (!(item && text && trailing)) return;
    CHECK_NEAR(trailing->Width(), 24, 0.5);
    CHECK(trailing->right <= item->right + 0.01f);
    CHECK(text->right <= trailing->left + 0.01f);
}

TEST(ButtonGroupWidensAPressedItemIntoItsNeighboursAndSharesWeight) {
    Harness h;
    std::int64_t nanos = 0;
    std::vector<std::shared_ptr<MutableInteractionSource>> sources;
    for (int i = 0; i < 3; ++i) sources.push_back(std::make_shared<MutableInteractionSource>());
    h.Run([&] {
        MaterialTheme({}, [&] {
            Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
                ButtonGroup({}, {}, [&](ButtonGroupScope& scope) {
                    for (int i = 0; i < 3; ++i) {
                        auto source = sources[i];
                        scope.CustomItem([&scope, source] { Box(scope.AnimateWidth(SizeMod(Modifier{}, ui::Dp(60), ui::Dp(40)), source)); },
                                         [](ButtonGroupMenuState&) {});
                    }
                });
                // Weighted items share what the unweighted one leaves (300 - 60 - 12 - 12).
                ButtonGroup({}, FillMax(Modifier{}, Axis::Horizontal), [&](ButtonGroupScope& scope) {
                    scope.CustomItem([] { Box(SizeMod(Modifier{}, ui::Dp(60), ui::Dp(40))); }, [](ButtonGroupMenuState&) {});
                    scope.CustomItem([&scope] { Box(scope.Weight(Modifier{}, 1)); }, [](ButtonGroupMenuState&) {});
                    scope.CustomItem([&scope] { Box(scope.Weight(Modifier{}, 2)); }, [](ButtonGroupMenuState&) {});
                });
            });
        });
    });
    const auto* column = h.owner.Root().Children()[0];
    const auto* pressedGroup = column->Children()[0];
    const auto* weighted = column->Children()[1];
    CHECK_EQ(weighted->Children()[1]->Size().width, 72);
    CHECK_EQ(weighted->Children()[2]->Size().width, 144);
    // Pressing the middle one: it grows by 0.15 x 60 / 2 (rounded) from each side.
    const auto id = sources[1]->Start(Interaction::Kind::Press);
    Frames(h, nanos, 60);
    CHECK_EQ(pressedGroup->Children()[0]->Size().width, 55);
    CHECK_EQ(pressedGroup->Children()[1]->Size().width, 70);
    CHECK_EQ(pressedGroup->Children()[2]->Size().width, 55);
    auto xs = ChildXs(*pressedGroup);
    CHECK(xs.size() >= 3 && xs[1] == 67 && xs[2] == 149);
    // Released: back to 60 each.
    sources[1]->End(Interaction::Kind::Press, id);
    Frames(h, nanos, 90);
    CHECK_EQ(pressedGroup->Children()[1]->Size().width, 60);
    CHECK_EQ(pressedGroup->Children()[0]->Size().width, 60);
}

// Ported from Compose's TooltipTest: show, advance the clock (mainClock.advanceTimeBy) and
// measure. Its trackpad cases send mouse events, as Compose's do; kiln has no trackpad type.
namespace {
    constexpr std::int64_t TooltipFadeInDuration = 300, TooltipFadeOutDuration = 300;
    // rule.setMaterialContent: the theme and the owner popups attach to. The Box stands for
    // the test activity's wrap-content ComposeView: content is not stretched to the window.
    void Themed(Harness& h, std::function<void()> content) {
        h.Run([&h, content = std::move(content)] {
            rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] {
                MaterialTheme({}, [&] { Box({}, Alignment::TopStart(), false, content); });
            });
        });
    }
    // getUnclippedBoundsInRoot inside a popup: kiln's popup bounds are in the window, Compose's
    // in the popup, so they are taken from the container's corner.
    std::optional<ui::Rect> InPopup(Harness& h, const std::string& tag) {
        const auto b = BoundsDp(h, tag), c = BoundsDp(h, "Container");
        if (!b || !c) return std::nullopt;
        return ui::Rect{ b->left - c->left, b->top - c->top, b->right - c->left, b->bottom - c->top };
    }
    std::shared_ptr<const ui::PopupPositionProvider> Above() {
        return TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above);
    }
    void Anchor(ui::Modifier m = {}) { Box(SizeMod(std::move(m), 24_dp, 24_dp), Alignment::TopStart(), false, {}); }
    void PlainTooltipTest(const TooltipState& state, ui::Modifier modifier = {}, std::function<void()> content = [] {}) {
        TooltipBox(Above(), [modifier, content](const TooltipScope& scope) { PlainTooltip(scope, Tag(modifier, "Container"), {}, content); },
                   state, {}, {}, [] { Anchor(); });
    }
    void RichTooltipTest(const TooltipState& state, ui::Modifier modifier = {}, std::function<void()> text = [] {},
                         std::function<void()> title = {}, std::function<void()> action = {}) {
        TooltipBox(Above(), [=](const TooltipScope& scope) {
            RichTooltipOptions o; o.title = title; o.action = action;
            RichTooltip(scope, Tag(modifier, "Container"), o, text);
        }, state, {}, {}, [] { Anchor(); });
    }
    // scope.launch { state.show() }
    rt::Job ShowTooltip(Harness& h, const TooltipState& state) {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.Show());
        h.AdvanceTimeBy(0);
        return job;
    }
    bool Near(float a, float b) { return std::abs(a - b) <= 0.5f; }
}

TEST(TooltipsWithoutContentKeepTheirMinimumSize) {
    for (bool rich : { false, true }) {
        PhoneHarness h;
        std::optional<TooltipState> state;
        Themed(h, [&] {
            state = RememberTooltipState(false, rich);
            if (rich) RichTooltipTest(*state); else PlainTooltipTest(*state);
        });
        rt::Job show = ShowTooltip(h, *state);
        h.AdvanceTimeBy(TooltipFadeInDuration);
        const auto b = BoundsDp(h, "Container");
        CHECK(b && Near(b->Height(), 24) && Near(b->Width(), 40));
        state->Dismiss();
    }
}

TEST(TooltipsTakeACustomSize) {
    for (bool rich : { false, true }) {
        PhoneHarness h;
        std::optional<TooltipState> state;
        Themed(h, [&] {
            state = RememberTooltipState(false, rich);
            const auto m = SizeMod({}, 100_dp, 100_dp);
            if (rich) RichTooltipTest(*state, m); else PlainTooltipTest(*state, m);
        });
        rt::Job show = ShowTooltip(h, *state);
        h.AdvanceTimeBy(TooltipFadeInDuration);
        const auto b = BoundsDp(h, "Container");
        CHECK(b && Near(b->Height(), 100) && Near(b->Width(), 100));
        state->Dismiss();
    }
}

TEST(PlainTooltipPadsItsContent) {
    PhoneHarness h;
    std::optional<TooltipState> state;
    Themed(h, [&] {
        state = RememberTooltipState();
        PlainTooltipTest(*state, {}, [] { Text("Test", Tag({}, "TextTag")); });
    });
    rt::Job show = ShowTooltip(h, *state);
    h.AdvanceTimeBy(TooltipFadeInDuration);
    const auto b = InPopup(h, "TextTag");
    CHECK(b && Near(b->left, 8) && Near(b->top, 4));
}

TEST(RichTooltipPlacesTitleTextAndActionByTheirBaselines) {
    PhoneHarness h;
    std::optional<TooltipState> state;
    Themed(h, [&] {
        state = RememberTooltipState(false, true);
        RichTooltipTest(*state, {}, [] { Text("Text", Tag({}, "TextTag")); }, [] { Text("Subhead", Tag({}, "SubheadTag")); },
                        [] { Text("Action", Tag({}, "ActionTag")); });
    });
    rt::Job show = ShowTooltip(h, *state);
    h.AdvanceTimeBy(TooltipFadeInDuration);
    // getFirstBaselinePosition: FakeText puts a line's baseline at 0.8 of its height.
    const auto baseline = [&](const char* tag) {
        const auto* n = FindTagged(h, tag);
        CHECK(n != nullptr);
        if (!n) return 0.0f;
        const float d = h.owner.GetDensity().density;
        return std::round(n->Size().height * 0.8f) / d;
    };
    const auto subhead = InPopup(h, "SubheadTag"), text = InPopup(h, "TextTag"), action = InPopup(h, "ActionTag");
    CHECK(subhead && text && action);
    if (!subhead || !text || !action) return;
    CHECK(Near(subhead->left, 16));
    CHECK(Near(subhead->top, 28 - baseline("SubheadTag")));
    CHECK(Near(text->left, 16));
    CHECK(Near(text->top, subhead->bottom + 24 - baseline("TextTag")));
    CHECK(Near(action->left, 16));
    CHECK(Near(action->top, text->bottom + 16));
}

TEST(TooltipsHideAfterTheirDurationUnlessPersistent) {
    // Plain, and rich without an action: shown, then gone after TooltipDuration.
    for (bool rich : { false, true }) {
        PhoneHarness h;
        std::optional<TooltipState> state;
        Themed(h, [&] {
            state = RememberTooltipState(false, false);
            if (rich) RichTooltipTest(*state, {}, [] { Text("Text"); }, [] { Text("Subhead"); });
            else PlainTooltipTest(*state, {}, [] { Text("Test"); });
        });
        CHECK(!state->IsVisible());
        rt::Job show = ShowTooltip(h, *state);
        h.AdvanceTimeBy(TooltipFadeInDuration);
        CHECK(state->IsVisible());
        h.AdvanceTimeBy(BasicTooltipDefaults::TooltipDuration);
        CHECK(!state->IsVisible());
    }
    // A persistent rich tooltip stays until its action dismisses it.
    PhoneHarness h;
    std::int64_t t = 1000;
    std::optional<TooltipState> state;
    Themed(h, [&] {
        state = RememberTooltipState(false, true);
        RichTooltipTest(*state, {}, [] { Text("Text"); }, [] { Text("Subhead"); },
                        [&] { TextButton([&] { state->Dismiss(); }, Tag({}, "ActionTag"), {}, [] { Text("Action"); }); });
    });
    rt::Job show = ShowTooltip(h, *state);
    h.AdvanceTimeBy(TooltipFadeInDuration);
    CHECK(state->IsVisible());
    h.AdvanceTimeBy(BasicTooltipDefaults::TooltipDuration);
    CHECK(state->IsVisible());
    Press(h, t, "ActionTag", true);
    h.AdvanceTimeBy(TooltipFadeOutDuration);
    CHECK(!state->IsVisible());
}

TEST(PlainTooltipLongPressShowsItUntilTheFingerLifts) {
    // A long click shows it, and it is gone once the timeout runs out.
    {
        PhoneHarness h;
        std::int64_t t = 1000;
        std::optional<TooltipState> state;
        Themed(h, [&] { state = RememberTooltipState(); Box(Tag({}, "tooltip"), Alignment::TopStart(), false, [&] {
            PlainTooltipTest(*state, {}, [] { Text("Test"); }); }); });
        bool changedToVisible = false;
        Press(h, t, "tooltip", false);
        for (int i = 0; i < 10; ++i) { h.AdvanceTimeBy(50); changedToVisible |= state->IsVisible(); }
        Press(h, t, "tooltip", true);
        CHECK(changedToVisible);
        h.AdvanceTimeBy(TooltipFadeInDuration + TooltipFadeOutDuration + 500 + BasicTooltipDefaults::TooltipDuration);
        CHECK(!state->IsVisible());
    }
    // Held for 10 s, it stays the whole time.
    PhoneHarness h;
    std::int64_t t = 1000;
    std::optional<TooltipState> state;
    Themed(h, [&] { state = RememberTooltipState(); Box(Tag({}, "tooltip"), Alignment::TopStart(), false, [&] {
        PlainTooltipTest(*state, {}, [] { Text("Test"); }); }); });
    std::int64_t hiddenAt = -1, elapsed = 0;
    Press(h, t, "tooltip", false);
    for (; elapsed < 10'000; elapsed += 100) {
        h.AdvanceTimeBy(100);
        if (elapsed > 500 && !state->IsVisible() && hiddenAt < 0) hiddenAt = elapsed;
    }
    CHECK(state->IsVisible());
    CHECK_EQ(hiddenAt, -1);
    const auto b = BoundsDp(h, "tooltip");
    if (b) Touch(h, t += 10'000, ui::PointerEventType::Release, (b->left + b->right), (b->top + b->bottom), false);
    h.AdvanceTimeBy(TooltipFadeOutDuration);
    CHECK(!state->IsVisible());
}

TEST(TooltipInitiallyVisibleShows) {
    PhoneHarness h;
    std::optional<TooltipState> state;
    Themed(h, [&] {
        state = RememberTooltipState(true);
        Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
            Box(Focusable(SizeMod({}, 30_dp, 30_dp)), Alignment::TopStart(), false, {});
            TooltipBox(Above(), [](const TooltipScope& s) { PlainTooltip(s, {}, {}, [] {}); }, *state, {}, {},
                       [] { Box(Focusable(SizeMod({}, 30_dp, 30_dp)), Alignment::TopStart(), false, {}); });
        });
    });
    h.AdvanceTimeBy(0);
    CHECK(state->IsVisible());
}

TEST(TooltipKeyboardFocusShowsItAndLosingFocusOrEscapeHidesIt) {
    struct Setup { PhoneHarness h; std::optional<TooltipState> state; ui::FocusRequester anchor, outside; };
    const auto setup = [](Setup& s, bool persistent, bool withTooltip) {
        Themed(s.h, [&s, persistent, withTooltip] {
            s.state = RememberTooltipState(false, persistent);
            Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
                TooltipBox(Above(), [withTooltip](const TooltipScope& sc) { if (withTooltip) PlainTooltip(sc, {}, {}, [] {}); }, *s.state, {}, {},
                           [&] { Box(Focusable(FocusRequesterModifier(Tag(SizeMod({}, 30_dp, 30_dp), "Anchor"), s.anchor)), Alignment::TopStart(), false, {}); });
                Box(Focusable(FocusRequesterModifier(SizeMod({}, 30_dp, 30_dp), s.outside)), Alignment::TopStart(), false, {});
            });
        });
    };
    // Focus shows it; focus moving away dismisses it.
    {
        Setup s; setup(s, false, true);
        CHECK(!s.state->IsVisible());
        s.anchor.RequestFocus();
        s.h.AdvanceTimeBy(0);
        CHECK(s.state->IsVisible());
        s.outside.RequestFocus();
        s.h.AdvanceTimeBy(0);
        CHECK(!s.state->IsVisible());
    }
    // Escape dismisses it.
    {
        Setup s; setup(s, false, true);
        s.anchor.RequestFocus();
        s.h.AdvanceTimeBy(0);
        CHECK(s.state->IsVisible());
        s.h.owner.DispatchKeyEvent(ui::KeyEvent{ ui::keys::Escape, ui::KeyEventType::KeyDown, 0 });
        s.h.owner.DispatchKeyEvent(ui::KeyEvent{ ui::keys::Escape, ui::KeyEventType::KeyUp, 0 });
        s.h.AdvanceTimeBy(0);
        CHECK(!s.state->IsVisible());
    }
    // A persistent one: focus shows it, losing focus hides it, and it still shows from code.
    Setup s; setup(s, true, false);
    s.anchor.RequestFocus();
    s.h.AdvanceTimeBy(0);
    CHECK(s.state->IsVisible());
    s.outside.RequestFocus();
    s.h.AdvanceTimeBy(0);
    CHECK(!s.state->IsVisible());
    rt::Job show = ShowTooltip(s.h, *s.state);
    CHECK(s.state->IsVisible());
}

TEST(TooltipFlipsBelowItsAnchorAtTheTopOfTheWindow) {
    PhoneHarness h;
    std::shared_ptr<const ui::PopupPositionProvider> provider;
    Themed(h, [&] { provider = Above(); });
    const ui::IntRect anchor{ 0, 0, 10, 20 };
    const ui::IntSize popup{ 500, 500 };
    const auto at = provider->CalculatePosition(anchor, { 500, 1000 }, ui::LayoutDirection::Ltr, popup);
    CHECK(at.y > anchor.bottom);
}

TEST(TooltipScopeReportsItsAnchorBounds) {
    PhoneHarness h;
    std::optional<ui::Rect> anchorBounds;
    Themed(h, [&] {
        TooltipBox(Above(), [&](const TooltipScope& scope) {
            // Asked while the tooltip is laid out, as Compose's layout modifier does.
            PlainTooltip(scope, OnSizeChanged({}, [&scope, &anchorBounds](IntSize) {
                if (auto coordinates = scope.ObtainAnchorBounds()) anchorBounds = coordinates->BoundsInRoot();   // the window's root
            }), {}, [] {});
        }, RememberTooltipState(true, true), {}, {}, [] { Anchor(Tag({}, "Anchor")); });
    });
    h.AdvanceTimeBy(0);
    const auto expected = BoundsDp(h, "Anchor");
    CHECK(anchorBounds && expected);
    if (anchorBounds && expected) {
        const float d = h.owner.GetDensity().density;
        CHECK_EQ(anchorBounds->left, std::round(expected->left * d));
        CHECK_EQ(anchorBounds->top, std::round(expected->top * d));
        CHECK_EQ(anchorBounds->right, std::round(expected->right * d));
        CHECK_EQ(anchorBounds->bottom, std::round(expected->bottom * d));
    }
}

TEST(TooltipsShareTheGlobalMutexUnlessGivenTheirOwn) {
    for (bool own : { false, true }) {
        PhoneHarness h;
        std::optional<TooltipState> top, bottom;
        Themed(h, [&] {
            // Remembered: kiln recomposes this lambda where Compose would skip it, and a new mutex
            // makes a new state.
            auto& c = *rt::Composition::Current();
            const auto mutex = [&c] { return c.Remember<std::shared_ptr<MutatorMutex>>([] { return std::make_shared<MutatorMutex>(); }); };
            top = own ? RememberTooltipState(false, true, mutex()) : RememberTooltipState(false, true);
            bottom = own ? RememberTooltipState(false, true, mutex()) : RememberTooltipState(false, true);
            for (const auto* state : { &*top, &*bottom })
                TooltipBox(Above(), [](const TooltipScope& s) {
                    RichTooltipOptions o;
                    o.title = [] { Text("Subhead"); };
                    o.action = [] { TextButton([] {}, {}, {}, [] { Text("Action"); }); };
                    RichTooltip(s, {}, o, [] { Text("Text"); });
                }, *state, {}, {}, [] {});
        });
        rt::Job a = ShowTooltip(h, *top), b = ShowTooltip(h, *bottom);
        CHECK_EQ(top->IsVisible(), own);
        CHECK(bottom->IsVisible());
        top->Dismiss(); bottom->Dismiss();
    }
}

TEST(TooltipMouseHoverShowsItUntilTheMouseLeavesUnlessPersistent) {
    for (bool persistent : { false, true }) {
        PhoneHarness h;
        std::optional<TooltipState> state;
        Themed(h, [&] {
            state = RememberTooltipState(false, persistent);
            TooltipBox(Above(), [](const TooltipScope&) {}, *state, {}, {}, [] { Anchor(Tag({}, "Anchor")); });
        });
        CHECK(!state->IsVisible());
        const auto mouse = [&](ui::PointerEventType type, float x, float y) {
            ui::RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = 1000;
            e.pointers.push_back({ 7, { x, y }, false, ui::PointerType::Mouse, {} });
            h.owner.ProcessPointer(e);
            h.AdvanceTimeBy(0);
        };
        mouse(ui::PointerEventType::Enter, 24, 24);   // the anchor's centre, in pixels
        h.AdvanceTimeBy(TooltipFadeInDuration);
        CHECK(state->IsVisible());
        // Still up past the timeout while hovered.
        h.AdvanceTimeBy(BasicTooltipDefaults::TooltipDuration);
        CHECK(state->IsVisible());
        mouse(ui::PointerEventType::Move, 200, 200);
        h.AdvanceTimeBy(TooltipFadeOutDuration);
        CHECK_EQ(state->IsVisible(), persistent);
        state->Dismiss();
    }
}

TEST(TooltipAnchorKeepsItsClickAndOffersALongClick) {
    PhoneHarness h;
    Themed(h, [&] {
        TooltipBox(Above(), [](const TooltipScope& s) { PlainTooltip(s, Tag({}, "Container"), {}, [] { Text("Tooltip"); }); },
                   RememberTooltipState(), {}, {}, [] { IconButton([] {}, Tag({}, "Anchor"), {}, [] { Anchor(); }); });
    });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* n = FindSemantics(tree, "Anchor");
    CHECK(n && n->config.onClick && n->config.onLongClick);
}

TEST(FullWidthTooltipCaretFollowsTheAnchor) {
    CHECK_EQ(internal::CaretX(1080, 1080, { 135, 0, 185, 0 }), 160.0f);
    CHECK_EQ(internal::CaretX(1080, 1080, { 515.5f, 0, 565.5f, 0 }), 540.5f);
    CHECK_EQ(internal::CaretX(1080, 1080, { 920.88f, 0, 970.88f, 0 }), 945.88f);
}

// Ported from Compose's ScaffoldTest (bounds in dp, within its 0.5 dp rounding error). Left out:
// the shadow pixel check, the lookahead regression (kiln has no lookahead) and the
// no-recomposition case as written: kiln's padding is a value, so the body recomposes when the
// padding changes, and only then (checked below with a FAB that changes size instead).
namespace {
    constexpr float FabSpacingDp = 16;
    // Box(Modifier.requiredSize(w, h)) { Scaffold(...) } at the window's corner.
    void InBox(PhoneHarness& h, float w, float hgt, std::function<void()> content, bool rtl = false) {
        Themed(h, [=] {
            auto inner = [=] { Box(RequiredSize({}, ui::Dp(w), ui::Dp(hgt)), Alignment::TopStart(), false, content); };
            if (rtl) rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Rtl, inner);
            else inner();
        });
    }
    ScaffoldOptions Insets(float left, float top, float right, float bottom) {
        ScaffoldOptions o;
        o.contentWindowInsets = WindowInsets::Fixed(ui::Dp(left), ui::Dp(top), ui::Dp(right), ui::Dp(bottom));
        return o;
    }
    void Fab() { FloatingActionButton([] {}, Tag({}, "fab"), {}, [] { Text("Fab"); }); }
}

TEST(ScaffoldContentFillsItAndStacks) {
    PhoneHarness h;
    InBox(h, 100, 100, [] {
        Scaffold(Tag({}, "scaffold"), {}, [](const PaddingValues&) { Text("One", Tag({}, "one")); Text("Two", Tag({}, "two")); });
    });
    const auto scaffold = BoundsDp(h, "scaffold"), one = BoundsDp(h, "one"), two = BoundsDp(h, "two");
    CHECK(scaffold && Near(scaffold->Width(), 100) && Near(scaffold->Height(), 100));
    CHECK(one && two && one->left == two->left && one->top == two->top);
}

TEST(ScaffoldBarsOverlapTheContentAndSetItsPadding) {
    PhoneHarness h;
    PaddingValues inner;
    InBox(h, 200, 300, [&] {
        ScaffoldOptions o;
        o.topBar = [] { Box(Tag(Height(FillMax({}, Axis::Horizontal), 50_dp), "top"), Alignment::TopStart(), false, {}); };
        o.bottomBar = [] { Box(Tag(Height(FillMax({}, Axis::Horizontal), 100_dp), "bottom"), Alignment::TopStart(), false, {}); };
        Scaffold(Tag({}, "scaffold"), o, [&](const PaddingValues& p) {
            inner = p;
            Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {});
        });
    });
    const auto scaffold = BoundsDp(h, "scaffold"), top = BoundsDp(h, "top"), bottom = BoundsDp(h, "bottom"), content = BoundsDp(h, "content");
    CHECK(scaffold && top && bottom && content);
    if (!scaffold || !top || !bottom || !content) return;
    // The content takes the whole scaffold, under both bars.
    CHECK(top->top == content->top);
    CHECK(bottom->bottom == content->bottom);
    CHECK(Near(content->Width(), scaffold->Width()) && Near(content->Height(), scaffold->Height()));
    CHECK(Near(inner.top.value, top->Height()));
    CHECK(Near(inner.bottom.value, bottom->Height()));
}

TEST(ScaffoldPaddingTakesTheInsetsWhereThereIsNoBar) {
    const auto padding = [](ScaffoldOptions o, bool consumeAbove = false) {
        PhoneHarness h;
        PaddingValues inner;
        InBox(h, 10, 40, [&] {
            const auto scaffold = [&] { Scaffold({}, o, [&](const PaddingValues& p) { inner = p; Box(RequiredSize({}, 10_dp, 10_dp), Alignment::TopStart(), false, {}); }); };
            if (consumeAbove) Box(WindowInsetsPadding({}, WindowInsets::Fixed(0_dp, 10_dp, 0_dp, 10_dp)), Alignment::TopStart(), false, scaffold);
            else scaffold();
        });
        h.AdvanceTimeBy(0);
        return std::pair{ inner.top.value, inner.bottom.value };
    };
    // A top bar replaces the top inset; the bottom keeps its inset.
    auto withTop = Insets(0, 5, 0, 3);
    withTop.topBar = [] { Box(RequiredSize({}, 10_dp, 10_dp), Alignment::TopStart(), false, {}); };
    auto [t1, b1] = padding(withTop);
    CHECK(Near(t1, 10) && Near(b1, 3));
    // Without bars: the insets.
    auto [t2, b2] = padding(Insets(0, 15, 0, 10));
    CHECK(Near(t2, 15) && Near(b2, 10));
    // Insets a parent consumed are left out.
    auto [t3, b3] = padding(Insets(0, 15, 0, 15), true);
    CHECK(Near(t3, 5) && Near(b3, 5));
    // A collapsed top bar still replaces the inset.
    auto collapsed = Insets(0, 5, 0, 3);
    collapsed.topBar = [] { Box(Height(FillMax({}, Axis::Horizontal), 0_dp), Alignment::TopStart(), false, {}); };
    auto [t4, b4] = padding(collapsed);
    CHECK(Near(t4, 0) && Near(b4, 3));
    // A bottom bar replaces the bottom inset.
    auto withBottom = Insets(0, 5, 0, 3);
    withBottom.bottomBar = [] { Box(RequiredSize({}, 10_dp, 10_dp), Alignment::TopStart(), false, {}); };
    auto [t5, b5] = padding(withBottom);
    CHECK(Near(t5, 5) && Near(b5, 10));
}

TEST(ScaffoldSnackbarAndFabKeepClearOfTheInsets) {
    // Bottom insets: the snackbar host and the FAB end 3 dp above the bottom.
    {
        PhoneHarness h;
        auto o = Insets(0, 5, 0, 3);
        o.snackbarHost = [] { Box(Tag(SizeMod({}, 8_dp, 6_dp), "snackbar"), Alignment::TopStart(), false, {}); };
        InBox(h, 10, 40, [&] { Scaffold({}, o, [](const PaddingValues&) {}); });
        const auto b = BoundsDp(h, "snackbar");
        CHECK(b && std::abs(40 - b->bottom - 3) < 1);
    }
    {
        // (Compose checks this in a 10 x 20 dp box the FAB cannot fit, one-sidedly; here it fits.)
        PhoneHarness h;
        auto o = Insets(0, 5, 0, 3);
        o.floatingActionButton = [] { Fab(); };
        InBox(h, 100, 200, [&] { Scaffold({}, o, [](const PaddingValues&) {}); });
        const auto b = BoundsDp(h, "fab");
        CHECK(b && Near(200 - b->bottom, 3 + FabSpacingDp));
    }
    // Horizontal insets shift the centred snackbar by half their difference.
    {
        PhoneHarness h;
        auto o = Insets(10, 0, 15, 0);
        o.snackbarHost = [] { Box(Tag(SizeMod({}, 40_dp, 20_dp), "snackbar"), Alignment::TopStart(), false, {}); };
        InBox(h, 100, 100, [&] { Scaffold({}, o, [](const PaddingValues&) {}); });
        const auto b = BoundsDp(h, "snackbar");
        CHECK(b && Near(50 - (b->left + b->right) / 2, (15 - 10) / 2.0f));
    }
    // The FAB keeps the inset on its side: End, Start in RTL (landscape), Center.
    const auto fabAt = [](FabPosition position, float left, float right, float w, bool rtl) {
        PhoneHarness h;
        auto o = Insets(left, 0, right, 0);
        o.floatingActionButtonPosition = position;
        o.floatingActionButton = [] { Fab(); };
        InBox(h, w, 100, [&] { Scaffold({}, o, [](const PaddingValues&) { Box(RequiredSize({}, 20_dp, 20_dp), Alignment::TopStart(), false, {}); }); }, rtl);
        return BoundsDp(h, "fab").value_or(ui::Rect{});
    };
    const auto end = fabAt(FabPosition::End, 10, 15, 100, false);
    CHECK(Near(100 - (end.right + FabSpacingDp), 15));
    const auto startRtl = fabAt(FabPosition::Start, 15, 10, 160, true);
    CHECK(Near(160 - (startRtl.right + FabSpacingDp), 10));
    const auto center = fabAt(FabPosition::Center, 15, 5, 100, false);
    CHECK(Near(50 - (center.left + center.right) / 2, (5 - 15) / 2.0f));
}

TEST(ScaffoldPlacesTheFabAtItsPosition) {
    for (auto position : { FabPosition::Start, FabPosition::Center, FabPosition::End }) {
        PhoneHarness h;
        ScaffoldOptions o;
        o.contentWindowInsets = WindowInsets{};
        o.floatingActionButtonPosition = position;
        o.floatingActionButton = [] { Fab(); };
        InBox(h, 200, 200, [&] {
            Scaffold({}, o, [](const PaddingValues& p) { Box(Padding(RequiredSize({}, 10_dp, 10_dp), p), Alignment::TopStart(), false, {}); });
        });
        const auto b = BoundsDp(h, "fab");
        CHECK(b.has_value());
        if (!b) continue;
        const float expectedX = position == FabPosition::Start ? FabSpacingDp : position == FabPosition::Center ? (200 - b->Width()) / 2 : 200 - b->Width() - FabSpacingDp;
        CHECK(std::abs(b->left - expectedX) <= 0.5f);
        CHECK(std::abs(b->top - (200 - b->Height() - FabSpacingDp)) <= 0.5f);
    }
}

TEST(ScaffoldBodyRecomposesOnlyWhenItsPaddingChanges) {
    PhoneHarness h;
    rt::State<bool> big{ false };
    int compositions = 0;
    InBox(h, 200, 300, [&] {
        ScaffoldOptions o;
        o.topBar = [] { Box(Height(FillMax({}, Axis::Horizontal), 50_dp), Alignment::TopStart(), false, {}); };
        o.floatingActionButton = [&] { Box(Tag(SizeMod({}, big.Get() ? 80_dp : 40_dp, 40_dp), "fab"), Alignment::TopStart(), false, {}); };
        Scaffold({}, o, [&](const PaddingValues& p) {
            ++compositions;
            Box(Padding(FillMax({}, Axis::Both), p), Alignment::TopStart(), false, {});
        });
    });
    h.AdvanceTimeBy(0);
    const int settled = compositions;
    CHECK(settled >= 1);
    // The FAB grows: the scaffold measures again, the padding is the same, the body stays.
    big.Set(true);
    h.AdvanceTimeBy(16);
    const auto fab = BoundsDp(h, "fab");
    CHECK(fab && Near(fab->Width(), 80));
    CHECK_EQ(compositions, settled);
}

// Ported from Compose's NavigationBarTest (412 dp wide, density 2). Its colour checks read
// LocalContentColor inside the icon and label, as Compose's do.
namespace {
    const ui::SemanticsNode* ParentOf(const ui::SemanticsNode& n, const ui::SemanticsNode* child) {
        for (const auto& c : n.children) {
            if (&c == child) return &n;
            if (const auto* r = ParentOf(c, child)) return r;
        }
        return nullptr;
    }
    void NavIcon(ui::Modifier m = {}, std::optional<std::string> description = std::nullopt) {
        Icon(internal::Icons::Filled::Edit(), description, std::move(m));
    }
    NavigationBarItemOptions Labelled(std::string text, bool alwaysShowLabel = true, ui::Modifier labelModifier = {}) {
        NavigationBarItemOptions o;
        o.label = [text, labelModifier] { Text(text, labelModifier); };
        o.alwaysShowLabel = alwaysShowLabel;
        return o;
    }
}

TEST(NavigationBarItemsAreSelectableTabsInASelectableGroup) {
    for (bool enabled : { true, false }) {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationBar({}, {}, [&] {
                auto o = Labelled("ItemText");
                o.enabled = enabled;
                NavigationBarItem(true, [] {}, [] { NavIcon(); }, Tag({}, "item"), o);
            });
        });
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* item = FindSemantics(tree, "item");
        CHECK(item != nullptr);
        if (!item) continue;
        CHECK(item->config.role == ui::Role::Tab);
        CHECK(item->config.selected == std::optional<bool>(true));
        CHECK_EQ(item->config.disabled, !enabled);
        CHECK(bool(item->config.onClick));
        if (enabled) {
            const auto* parent = ParentOf(tree, item);
            CHECK(parent && parent->config.selectableGroup);
        }
    }
}

TEST(NavigationBarItemDropsTheIconDescriptionWhileItsLabelShows) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationBar({}, {}, [&] {
            NavigationBarItem(true, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item1"), Labelled("Favorite", false));
            NavigationBarItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item2"), Labelled("Favorite", false));
            NavigationBarItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item3"), {});
        });
    });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto *a = FindSemantics(tree, "item1"), *b = FindSemantics(tree, "item2"), *c = FindSemantics(tree, "item3");
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    CHECK(a->config.contentDescription.empty());
    CHECK(b->config.contentDescription == std::vector<std::string>{ "Favorite" });
    CHECK(c->config.contentDescription == std::vector<std::string>{ "Favorite" });
}

TEST(NavigationBarFillsTheWidthAndPadsByItsInsets) {
    {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationBarOptions o;
            o.windowInsets = WindowInsets{};
            NavigationBar(Tag({}, "bar"), o, [&] {
                int i = 0;
                for (const char* item : { "Songs", "Artists", "Playlists" })
                    NavigationBarItem(i++ == 0, [] {}, [] { NavIcon(); }, {}, Labelled(item));
            });
        });
        const auto b = BoundsDp(h, "bar");
        CHECK(b && Near(b->Width(), 412) && Near(b->Height(), 80));
    }
    PhoneHarness h;
    Themed(h, [&] {
        NavigationBarOptions o;
        o.windowInsets = WindowInsets::Fixed(17_dp, 17_dp, 17_dp, 17_dp);
        NavigationBar({}, o, [] { Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {}); });
    });
    const auto b = BoundsDp(h, "content");
    CHECK(b && Near(b->left, 17) && Near(b->top, 17));
}

TEST(NavigationBarItemsShareTheWidth) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationBar({}, {}, [&] {
            for (int i = 0; i < 4; ++i)
                NavigationBarItem(i == 0, [] {}, [] { NavIcon(); }, Tag({}, "item" + std::to_string(i)), Labelled("Item " + std::to_string(i)));
        });
    });
    const float expectedWidth = (412 - 8 * 3) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        const auto b = BoundsDp(h, "item" + std::to_string(i));
        CHECK(b.has_value());
        if (!b) continue;
        CHECK(std::abs(b->Width() - expectedWidth) <= 0.5f);
        CHECK(std::abs(b->Height() - 80) <= 0.5f);
        CHECK(std::abs(b->left - (expectedWidth + 8) * float(i)) <= 0.5f);
    }
}

TEST(NavigationBarItemColoursFollowSelection) {
    PhoneHarness h;
    std::vector<Color> seen;
    const auto record = [&] { seen.push_back(rt::Composition::Current()->Read(LocalContentColor)); };
    Themed(h, [&] {
        const auto custom = NavigationBarItemDefaults::colors(Color(0xFFFF0000u), Color::Unspecified(), Color::Unspecified(), Color::Unspecified(),
                                                              Color(0xFF00FF00u));
        NavigationBar({}, {}, [&] {
            NavigationBarItemOptions o;
            o.label = record;
            NavigationBarItem(true, [] {}, record, {}, o);
            NavigationBarItem(false, [] {}, record, {}, o);
            o.colors = custom;
            NavigationBarItem(true, [] {}, record, {}, o);
            NavigationBarItem(false, [] {}, record, {}, o);
        });
    });
    const auto& s = LightColorScheme();
    CHECK(seen.size() >= 8);
    if (seen.size() < 8) return;
    // icon, label for each item, in order.
    CHECK(seen[0] == s.onSecondaryContainer && seen[1] == s.secondary);
    CHECK(seen[2] == s.onSurfaceVariant && seen[3] == s.onSurfaceVariant);
    CHECK(seen[4] == Color(0xFFFF0000u) && seen[5] == s.secondary);
    CHECK(seen[6] == s.onSurfaceVariant && seen[7] == Color(0xFF00FF00u));
}

TEST(NavigationBarItemPlacesItsIconAndLabel) {
    // Selected with a label: the icon 16 dp from the top, centred; the label 4 + 4 dp below it.
    const auto place = [](bool selected, bool alwaysShow, bool label, std::optional<float> height) {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationBarOptions bar;
            bar.windowInsets = WindowInsets{};
            NavigationBar(height ? Height({}, ui::Dp(*height)) : ui::Modifier{}, bar, [&] {
                NavigationBarItemOptions o;
                if (label) o = Labelled("ItemText", alwaysShow, Tag({}, "label"));
                NavigationBarItem(selected, [] {}, [] { NavIcon(Tag({}, "icon")); }, Tag({}, "item"), o);
            });
        });
        h.AdvanceTimeBy(1000);
        return std::tuple{ BoundsDp(h, "item"), BoundsDp(h, "icon"), BoundsDp(h, "label"), Displayed(h, "label") };
    };
    {
        auto [item, icon, label, shown] = place(true, true, true, std::nullopt);
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->left, item->left + (item->Width() - icon->Width()) / 2));
            CHECK(Near(icon->top, item->top + 16));
            CHECK(Near(label->top, icon->bottom + 4 + 4));
        }
    }
    {
        // Unselected, label only when selected: no label, the icon centred.
        auto [item, icon, label, shown] = place(false, false, true, std::nullopt);
        CHECK(!shown);
        CHECK(item && icon && Near(icon->top, item->top + (item->Height() - icon->Height()) / 2));
    }
    {
        // No label: the icon centred.
        auto [item, icon, label, shown] = place(false, true, false, std::nullopt);
        CHECK(item && icon && Near(icon->top, item->top + (item->Height() - icon->Height()) / 2));
        CHECK(item && icon && Near(icon->left, item->left + (item->Width() - icon->Width()) / 2));
    }
    {
        // A 64 dp bar takes the difference out of the padding above and below.
        auto [item, icon, label, shown] = place(true, true, true, 64.0f);
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->top, item->top + 16 - (80 - 64) / 2.0f));
            CHECK(Near(label->top, icon->bottom + 4 + 4));
        }
    }
}

TEST(NavigationBarGrowsForLongLabels) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationBar(Tag({}, "bar"), {}, [&] {
            for (int i = 0; i < 4; ++i) NavigationBarItem(i == 0, [] {}, [] { NavIcon(); }, {}, Labelled("Long\nLabel\nMultiple\nLines"));
        });
    });
    const auto b = BoundsDp(h, "bar");
    CHECK(b && b->Height() > 80);
}

TEST(NavigationBarSelectsAClickedItemButNotADisabledOne) {
    PhoneHarness h;
    std::int64_t t = 1000;
    rt::State<int> selected{ 0 };
    int disabledClicks = 0;
    Themed(h, [&] {
        NavigationBar({}, {}, [&] {
            int i = 0;
            for (const char* item : { "Songs", "Artists", "Playlists" }) {
                const int index = i++;
                NavigationBarItem(selected.Get() == index, [&, index] { selected.Set(index); }, [] { NavIcon(); }, Tag({}, item), Labelled(item));
            }
            auto off = Labelled("Off");
            off.enabled = false;
            NavigationBarItem(true, [&] { ++disabledClicks; }, [] { NavIcon(); }, Tag({}, "off"), off);
        });
    });
    const auto isSelected = [&](const char* tag) {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* n = FindSemantics(tree, tag);
        return n && n->config.selected == std::optional<bool>(true);
    };
    CHECK(isSelected("Songs") && !isSelected("Artists") && !isSelected("Playlists"));
    Press(h, t, "Playlists", true);
    h.AdvanceTimeBy(16);
    CHECK(!isSelected("Songs") && !isSelected("Artists") && isSelected("Playlists"));
    Press(h, t, "off", true);
    CHECK_EQ(disabledClicks, 0);
}

TEST(NavigationBarItemKeepsItsMinimumIntrinsicWidth) {
    PhoneHarness h;
    Themed(h, [&] {
        Row(Tag(Width({}, IntrinsicSize::Min), "row"), Arrangement::Start(), AlignmentV::Top(), [&] {
            NavigationBarItem(true, [] {}, [] { NavIcon(SizeMod({}, 24_dp, 24_dp)); }, {}, {});
        });
    });
    const auto b = BoundsDp(h, "row");
    CHECK(b && Near(b->Width(), 24 + 44 * 2));
}

TEST(ScaffoldOverlayFabSitsAboveTheBottomBar) {
    // Slots stack by their content's zIndex (the FAB's 4 over the bottom bar's 3), not by the
    // order they were subcomposed in.
    PhoneHarness h;
    std::int64_t t = 1000;
    int fabClicks = 0, barClicks = 0;
    InBox(h, 200, 300, [&] {
        ScaffoldOptions o;
        o.floatingActionButtonPosition = FabPosition::EndOverlay;
        o.contentWindowInsets = WindowInsets{};
        o.bottomBar = [&] { Box(Clickable(Height(FillMax({}, Axis::Horizontal), 80_dp), [&] { ++barClicks; }), Alignment::TopStart(), false, {}); };
        o.floatingActionButton = [&] { FloatingActionButton([&] { ++fabClicks; }, Tag({}, "fab"), {}, [] { Text("Fab"); }); };
        Scaffold({}, o, [](const PaddingValues&) {});
    });
    const auto fab = BoundsDp(h, "fab");
    CHECK(fab && fab->bottom > 300 - 80);   // overlapping the bar
    Press(h, t, "fab", true);
    CHECK_EQ(fabClicks, 1);
    CHECK_EQ(barClicks, 0);
}

// Ported from Compose's NavigationRailTest.
namespace {
    NavigationRailItemOptions RailLabelled(std::string text, bool alwaysShowLabel = true, ui::Modifier labelModifier = {}) {
        NavigationRailItemOptions o;
        o.label = [text, labelModifier] { Text(text, labelModifier); };
        o.alwaysShowLabel = alwaysShowLabel;
        return o;
    }
    NavigationRailOptions NoInsets() {
        NavigationRailOptions o;
        o.windowInsets = WindowInsets{};
        return o;
    }
}

TEST(NavigationRailItemsAreSelectableTabsInATraversalGroup) {
    for (bool enabled : { true, false }) {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationRail({}, {}, [&] {
                auto o = RailLabelled("ItemText");
                o.enabled = enabled;
                NavigationRailItem(true, [] {}, [] { NavIcon(); }, Tag({}, "item"), o);
            });
        });
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* item = FindSemantics(tree, "item");
        CHECK(item != nullptr);
        if (!item) continue;
        CHECK(item->config.role == ui::Role::Tab);
        CHECK(item->config.selected == std::optional<bool>(true));
        CHECK_EQ(item->config.disabled, !enabled);
        CHECK(bool(item->config.onClick));
        if (enabled) {
            const auto* parent = ParentOf(tree, item);
            CHECK(parent && parent->config.selectableGroup && parent->config.isTraversalGroup);
        }
    }
}

TEST(NavigationRailItemDropsTheIconDescriptionWhileItsLabelShows) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationRail({}, {}, [&] {
            NavigationRailItem(true, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item1"), RailLabelled("Favorite", false));
            NavigationRailItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item2"), RailLabelled("Favorite", false));
            NavigationRailItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, Tag({}, "item3"), {});
        });
    });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto *a = FindSemantics(tree, "item1"), *b = FindSemantics(tree, "item2"), *c = FindSemantics(tree, "item3");
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    CHECK(a->config.contentDescription.empty());
    CHECK(b->config.contentDescription == std::vector<std::string>{ "Favorite" });
    CHECK(c->config.contentDescription == std::vector<std::string>{ "Favorite" });
}

TEST(NavigationRailIsItsItemsWideAndPadsByItsInsets) {
    {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationRail(Tag({}, "rail"), {}, [&] {
                int i = 0;
                for (const char* item : { "Home", "Search", "Settings" })
                    NavigationRailItem(i++ == 0, [] {}, [] { NavIcon(); }, {}, RailLabelled(item));
            });
        });
        const auto b = BoundsDp(h, "rail");
        CHECK(b && Near(b->Width(), 80));
    }
    PhoneHarness h;
    Themed(h, [&] {
        NavigationRailOptions o;
        o.windowInsets = WindowInsets::Fixed(13_dp, 13_dp, 13_dp, 13_dp);
        NavigationRail({}, o, [] { Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {}); });
    });
    const auto b = BoundsDp(h, "content");
    CHECK(b && Near(b->top, 13 + 4) && Near(b->left, 13));
}

TEST(NavigationRailItemsStackWithPadding) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationRail({}, NoInsets(), [&] {
            for (int i = 0; i < 4; ++i)
                NavigationRailItem(i == 0, [] {}, [] { NavIcon(); }, Tag({}, "item" + std::to_string(i)), RailLabelled("Item " + std::to_string(i)));
        });
    });
    for (int i = 0; i < 4; ++i) {
        const auto b = BoundsDp(h, "item" + std::to_string(i));
        CHECK(b.has_value());
        if (!b) continue;
        CHECK(Near(b->Width(), 80) && Near(b->Height(), 56));
        // The items above and the padding above each of them (the first included).
        CHECK(Near(b->top, 56.0f * float(i) + 4.0f * float(i + 1)));
    }
}

TEST(NavigationRailItemGrowsForALongLabel) {
    PhoneHarness h;
    Themed(h, [&] {
        NavigationRail({}, {}, [&] { NavigationRailItem(true, [] {}, [] { NavIcon(); }, Tag({}, "item"), RailLabelled("Long\nLabel\nMultiple\nLines")); });
    });
    const auto b = BoundsDp(h, "item");
    CHECK(b && b->Height() > 56);
}

TEST(NavigationRailItemPlacesItsIconAndLabel) {
    const auto place = [](bool selected, bool alwaysShow, bool label) {
        PhoneHarness h;
        Themed(h, [&] {
            NavigationRail({}, NoInsets(), [&] {
                NavigationRailItemOptions o;
                if (label) o = RailLabelled("ItemText", alwaysShow, Tag({}, "label"));
                NavigationRailItem(selected, [] {}, [] { NavIcon(Tag({}, "icon")); }, Tag({}, "item"), o);
            });
        });
        h.AdvanceTimeBy(1000);
        return std::tuple{ BoundsDp(h, "item"), BoundsDp(h, "icon"), BoundsDp(h, "label"), Displayed(h, "label") };
    };
    {
        // Selected with a label: the icon 4 dp below the item's top, the label 4 dp above its bottom.
        auto [item, icon, label, shown] = place(true, true, true);
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(label->bottom, item->bottom - 4));
            CHECK(Near(icon->left, (item->Width() - icon->Width()) / 2));
            CHECK(Near(icon->top, item->top + 4));
        }
    }
    {
        // Unselected, label only when selected: no label, the icon centred.
        auto [item, icon, label, shown] = place(false, false, true);
        CHECK(!shown);
        CHECK(item && icon && Near(icon->left, (item->Width() - icon->Width()) / 2));
        CHECK(item && icon && Near(icon->top, 4 + (item->Height() - icon->Height()) / 2));
    }
    {
        // No label: the icon centred.
        auto [item, icon, label, shown] = place(false, true, false);
        CHECK(item && icon && Near(icon->left, (item->Width() - icon->Width()) / 2));
        CHECK(item && icon && Near(icon->top, 4 + (item->Height() - icon->Height()) / 2));
    }
}

TEST(NavigationRailSelectsAClickedItemButNotADisabledOne) {
    PhoneHarness h;
    std::int64_t t = 1000;
    rt::State<int> selected{ 0 };
    int disabledClicks = 0;
    Themed(h, [&] {
        NavigationRail({}, {}, [&] {
            int i = 0;
            for (const char* item : { "Home", "Search", "Settings" }) {
                const int index = i++;
                NavigationRailItem(selected.Get() == index, [&, index] { selected.Set(index); }, [] { NavIcon(); }, Tag({}, item), RailLabelled(item));
            }
            auto off = RailLabelled("Off");
            off.enabled = false;
            NavigationRailItem(true, [&] { ++disabledClicks; }, [] { NavIcon(); }, Tag({}, "off"), off);
        });
    });
    const auto isSelected = [&](const char* tag) {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* n = FindSemantics(tree, tag);
        return n && n->config.selected == std::optional<bool>(true);
    };
    CHECK(isSelected("Home") && !isSelected("Search") && !isSelected("Settings"));
    Press(h, t, "Settings", true);
    h.AdvanceTimeBy(16);
    CHECK(!isSelected("Home") && !isSelected("Search") && isSelected("Settings"));
    Press(h, t, "off", true);
    CHECK_EQ(disabledClicks, 0);
}

TEST(NavigationItemColoursFadeTheGivenUnselectedColoursWhenDisabled) {
    PhoneHarness h;
    const Color icon(0xFF00FF00u), text(0xFF0000FFu);
    std::optional<NavigationRailItemColors> rail;
    std::optional<NavigationBarItemColors> bar;
    Themed(h, [&] {
        rail = NavigationRailItemDefaults::colors(Color::Unspecified(), Color::Unspecified(), Color::Unspecified(), icon, text);
        bar = NavigationBarItemDefaults::colors(Color::Unspecified(), Color::Unspecified(), Color::Unspecified(), icon, text);
    });
    CHECK(rail && bar);
    if (!rail || !bar) return;
    const auto& s = LightColorScheme();
    CHECK(rail->selectedIconColor == s.onSecondaryContainer && rail->unselectedIconColor == icon);
    CHECK(rail->IconColor(true, false) == icon.WithAlpha(DisabledAlpha) && rail->TextColor(false, false) == text.WithAlpha(DisabledAlpha));
    CHECK(bar->IconColor(true, false) == icon.WithAlpha(DisabledAlpha) && bar->TextColor(false, false) == text.WithAlpha(DisabledAlpha));
}

// Ported from Compose's ShortNavigationBarTest. The centred arrangements use a 1000 dp window, as
// Compose's simulated 1000 dp screen.
namespace {
    void ShortItem(int index, NavigationItemIconPosition position = NavigationItemIconPosition::Top, std::string label = "") {
        ShortNavigationBarItemOptions o;
        o.iconPosition = position;
        const std::string text = label.empty() ? "Item " + std::to_string(index) : label;
        ShortNavigationBarItem(index == 0, [] {}, [] { NavIcon(); }, [text] { Text(text); }, Tag({}, "item" + std::to_string(index)), o);
    }
}

TEST(ShortNavigationBarIsASelectableGroupOfTabs) {
    for (bool enabled : { true, false }) {
        PhoneHarness h;
        Themed(h, [&] {
            ShortNavigationBar({}, {}, [&] {
                ShortNavigationBarItemOptions o;
                o.enabled = enabled;
                ShortNavigationBarItem(true, [] {}, [] { NavIcon(); }, [] { Text("ItemText"); }, Tag({}, "item"), o);
            });
        });
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* item = FindSemantics(tree, "item");
        CHECK(item != nullptr);
        if (!item) continue;
        CHECK(item->config.role == ui::Role::Tab);
        CHECK(item->config.selected == std::optional<bool>(true));
        CHECK_EQ(item->config.disabled, !enabled);
        CHECK(bool(item->config.onClick));
        if (enabled) {
            const auto* parent = ParentOf(tree, item);
            CHECK(parent && parent->config.selectableGroup);
        }
    }
}

TEST(ShortNavigationBarFillsTheWidthAndPadsByItsInsets) {
    {
        PhoneHarness h;
        Themed(h, [&] {
            ShortNavigationBarOptions o;
            o.windowInsets = WindowInsets{};
            ShortNavigationBar(Tag({}, "bar"), o, [] { for (int i = 0; i < 3; ++i) ShortItem(i); });
        });
        const auto b = BoundsDp(h, "bar");
        CHECK(b && Near(b->Width(), 412) && Near(b->Height(), 64));
    }
    PhoneHarness h;
    Themed(h, [&] {
        ShortNavigationBarOptions o;
        o.windowInsets = WindowInsets::Fixed(17_dp, 17_dp, 17_dp, 17_dp);
        ShortNavigationBar({}, o, [] { Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {}); });
    });
    const auto b = BoundsDp(h, "content");
    CHECK(b && Near(b->left, 17) && Near(b->top, 17));
}

TEST(ShortNavigationBarEqualWeightItemsShareTheWidth) {
    PhoneHarness h;
    Themed(h, [&] {
        ShortNavigationBarOptions o;
        o.windowInsets = WindowInsets{};
        ShortNavigationBar({}, o, [] { for (int i = 0; i < 4; ++i) ShortItem(i); });
    });
    for (int i = 0; i < 4; ++i) {
        const auto b = BoundsDp(h, "item" + std::to_string(i));
        CHECK(b.has_value());
        if (!b) continue;
        CHECK(std::abs(b->Width() - 103) <= 0.5f && std::abs(b->Height() - 64) <= 0.5f);
        CHECK(std::abs(b->left - 103.0f * float(i)) <= 0.5f);
    }
}

TEST(ShortNavigationBarCentresItsItemsByCount) {
    // 3 items take 60% of the width, 4 70%, 5 80%, 6 90%, centred.
    for (int count = 3; count <= 6; ++count) {
        PhoneHarness h({ 2000, 400 });
        Themed(h, [&] {
            ShortNavigationBarOptions o;
            o.windowInsets = WindowInsets{};
            o.arrangement = ShortNavigationBarArrangement::Centered;
            ShortNavigationBar(Tag({}, "bar"), o, [&] { for (int i = 0; i < count; ++i) ShortItem(i, NavigationItemIconPosition::Start); });
        });
        const auto bar = BoundsDp(h, "bar");
        CHECK(bar.has_value());
        if (!bar) continue;
        const float space = float(10 * (count + 3)) / 100, padding = (1 - space) / 2 * 1000;
        const float expectedWidth = std::floor(2000 * space / float(count)) / 2;   // px, truncated
        for (int i = 0; i < count; ++i) {
            const auto b = BoundsDp(h, "item" + std::to_string(i));
            CHECK(b.has_value());
            if (!b) continue;
            CHECK(std::abs(b->Width() - expectedWidth) <= 0.5f);
            CHECK(std::abs(b->left - bar->left - (expectedWidth * float(i) + padding)) <= 0.5f);
        }
    }
}

TEST(ShortNavigationBarGrowsForLongLabels) {
    for (auto arrangement : { ShortNavigationBarArrangement::EqualWeight, ShortNavigationBarArrangement::Centered }) {
        PhoneHarness h;
        Themed(h, [&] {
            ShortNavigationBarOptions o;
            o.arrangement = arrangement;
            const auto position = arrangement == ShortNavigationBarArrangement::Centered ? NavigationItemIconPosition::Start : NavigationItemIconPosition::Top;
            ShortNavigationBar(Tag({}, "bar"), o, [&] { for (int i = 0; i < 4; ++i) ShortItem(i, position, "Long\nLabel\nMultiple\nLines"); });
        });
        const auto b = BoundsDp(h, "bar");
        CHECK(b && b->Height() > 64);
    }
}

TEST(ShortNavigationBarItemColours) {
    PhoneHarness h;
    std::vector<Color> seen;
    const auto record = [&] { seen.push_back(rt::Composition::Current()->Read(LocalContentColor)); };
    const Color red(0xFFFF0000u), blue(0xFF0000FFu), green(0xFF00FF00u), white(0xFFFFFFFFu), gray(0xFF888888u), black(0xFF000000u);
    Themed(h, [&] {
        const auto custom = ShortNavigationBarItemDefaults::colors().Copy(red, blue, blue, Color::Unspecified(), green, white, gray, black);
        const auto partial = ShortNavigationBarItemDefaults::colors(red, Color::Unspecified(), Color::Unspecified(), Color::Unspecified(),
                                                                    Color::Unspecified(), green);
        ShortNavigationBar({}, {}, [&] {
            ShortNavigationBarItemOptions o;
            o.colors = custom;
            ShortNavigationBarItem(true, [] {}, record, record, {}, o);
            ShortNavigationBarItem(false, [] {}, record, record, {}, o);
            o.enabled = false;
            ShortNavigationBarItem(false, [] {}, record, record, {}, o);
            o.enabled = true;
            o.colors = partial;
            ShortNavigationBarItem(true, [] {}, record, record, {}, o);
            ShortNavigationBarItem(false, [] {}, record, record, {}, o);
        });
    });
    const auto& s = LightColorScheme();
    CHECK(seen.size() >= 10);
    if (seen.size() < 10) return;
    CHECK(seen[0] == red && seen[1] == blue);
    CHECK(seen[2] == green && seen[3] == white);
    CHECK(seen[4] == gray && seen[5] == black);
    CHECK(seen[6] == red && seen[7] == s.secondary);
    CHECK(seen[8] == s.onSurfaceVariant && seen[9] == green);
}

TEST(ShortNavigationBarItemDisabledTakesNoClicks) {
    PhoneHarness h;
    std::int64_t t = 1000;
    int clicks = 0;
    Themed(h, [&] {
        ShortNavigationBarItemOptions o;
        o.enabled = false;
        ShortNavigationBarItem(true, [&] { ++clicks; }, [] { NavIcon(); }, [] { Text("ItemText"); }, Tag({}, "item"), o);
    });
    Press(h, t, "item", true);
    CHECK_EQ(clicks, 0);
}

TEST(ShortNavigationBarItemKeepsTheIconDescriptionWithoutALabel) {
    PhoneHarness h;
    Themed(h, [&] { ShortNavigationBarItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, {}, Tag({}, "item"), {}); });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* n = FindSemantics(tree, "item");
    CHECK(n && n->config.contentDescription == std::vector<std::string>{ "Favorite" });
}

TEST(ShortNavigationBarItemPlacesItsIconAndLabel) {
    {
        // Icon on top: the icon 4 + 6 dp below the item's top, the label 4 + 4 dp below the icon.
        PhoneHarness h;
        Themed(h, [&] {
            ShortNavigationBarItem(true, [] {}, [] { NavIcon(Tag({}, "icon")); }, [] { Text("ItemText", Tag({}, "label")); }, Tag({}, "item"), {});
        });
        const auto item = BoundsDp(h, "item"), icon = BoundsDp(h, "icon"), label = BoundsDp(h, "label");
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(item->Width() >= 48 && item->Height() >= 48);
            CHECK(Near(icon->left, (item->Width() - icon->Width()) / 2));
            CHECK(Near(icon->top, item->top + 4 + 6));
            CHECK(Near(label->top, icon->bottom + 4 + 4));
        }
    }
    {
        // Icon at the start: 16 dp in, the label 4 dp after it, both centred vertically.
        PhoneHarness h;
        Themed(h, [&] {
            ShortNavigationBarItemOptions o;
            o.iconPosition = NavigationItemIconPosition::Start;
            ShortNavigationBarItem(true, [] {}, [] { NavIcon(Tag({}, "icon")); }, [] { Text("ItemText", Tag({}, "label")); }, Tag({}, "item"), o);
        });
        const auto item = BoundsDp(h, "item"), icon = BoundsDp(h, "icon"), label = BoundsDp(h, "label");
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(std::abs(item->Width() - (16 + icon->Width() + 4 + label->Width() + 16)) <= 1);
            CHECK(Near(item->Height(), 48));
            CHECK(Near(icon->left, item->left + 16));
            CHECK(Near(icon->top, (item->Height() - icon->Height()) / 2));
            CHECK(Near(label->left, item->left + 16 + icon->Width() + 4));
            CHECK(Near(label->top, (item->Height() - label->Height()) / 2));
        }
    }
    {
        // No label: the icon centred.
        PhoneHarness h;
        Themed(h, [&] { ShortNavigationBarItem(false, [] {}, [] { NavIcon(Tag({}, "icon")); }, {}, Tag({}, "item"), {}); });
        const auto item = BoundsDp(h, "item"), icon = BoundsDp(h, "icon");
        CHECK(item && icon && Near(icon->left, (item->Width() - icon->Width()) / 2) && Near(icon->top, (item->Height() - icon->Height()) / 2));
    }
}

// Ported from Compose's WideNavigationRailTest (the window is 412 x 800 dp).
namespace {
    void WideItem(int index, bool expanded, int selected = 0, std::function<void()> onClick = [] {}) {
        WideNavigationRailItem(selected == index, std::move(onClick), [] { NavIcon(); }, [index] { Text("Item " + std::to_string(index)); }, expanded,
                               Tag({}, "item" + std::to_string(index)), {});
    }
    WideNavigationRailOptions WideRail(WideNavigationRailValue value = WideNavigationRailValue::Collapsed) {
        WideNavigationRailOptions o;
        o.state = RememberWideNavigationRailState(value);
        return o;
    }
}

TEST(WideNavigationRailIsASelectableTraversalGroup) {
    PhoneHarness h;
    Themed(h, [&] { WideNavigationRail({}, {}, [] { WideItem(0, false); }); });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* item = FindSemantics(tree, "item0");
    CHECK(item != nullptr);
    if (!item) return;
    const auto* parent = ParentOf(tree, item);
    CHECK(parent && parent->config.selectableGroup && parent->config.isTraversalGroup);
}

TEST(WideNavigationRailSizes) {
    {
        PhoneHarness h;
        Themed(h, [&] { WideNavigationRail(Tag({}, "rail"), WideRail(), [] { for (int i = 0; i < 3; ++i) WideItem(i, false); }); });
        const auto b = BoundsDp(h, "rail");
        CHECK(b && Near(b->Width(), 96) && Near(b->Height(), 800));
    }
    {
        PhoneHarness h;
        Themed(h, [&] {
            WideNavigationRail(Tag({}, "rail"), WideRail(WideNavigationRailValue::Expanded), [] { for (int i = 0; i < 3; ++i) WideItem(i, true); });
        });
        const auto b = BoundsDp(h, "rail");
        CHECK(b && Near(b->Width(), 220) && Near(b->Height(), 800));
    }
    PhoneHarness h;
    Themed(h, [&] {
        auto o = WideRail(WideNavigationRailValue::Expanded);
        o.header = [] { Spacer(Width({}, 400_dp)); };
        WideNavigationRail(Tag({}, "rail"), o, [] { for (int i = 0; i < 3; ++i) WideItem(i, true); });
    });
    const auto b = BoundsDp(h, "rail");
    CHECK(b && Near(b->Width(), 360));
}

TEST(WideNavigationRailTogglesBetweenCollapsedAndExpanded) {
    for (auto initial : { WideNavigationRailValue::Collapsed, WideNavigationRailValue::Expanded }) {
        PhoneHarness h;
        std::int64_t t = 1000;
        std::vector<rt::Job> jobs;
        Themed(h, [&] {
            auto o = WideRail(initial);
            const auto state = o.state;
            o.header = [&, state] { Button([&, state] { jobs.push_back(rt::Launch(rt::Dispatcher::Main(), state->Toggle())); }, Tag({}, "header"), {}, [] {}); };
            WideNavigationRail(Tag({}, "rail"), o, [] {});
        });
        const bool expanded = initial == WideNavigationRailValue::Expanded;
        auto b = BoundsDp(h, "rail");
        CHECK(b && Near(b->Width(), expanded ? 220 : 96));
        Press(h, t, "header", true);
        h.AdvanceTimeBy(1000);
        b = BoundsDp(h, "rail");
        CHECK(b && Near(b->Width(), expanded ? 96 : 220));
    }
}

TEST(WideNavigationRailPadsByItsInsetsAndContentPadding) {
    {
        PhoneHarness h;
        Themed(h, [&] {
            WideNavigationRailOptions o;
            o.windowInsets = WindowInsets::Fixed(13_dp, 13_dp, 13_dp, 13_dp);
            WideNavigationRail({}, o, [] { Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {}); });
        });
        const auto b = BoundsDp(h, "content");
        CHECK(b && Near(b->top, 13 + 44) && Near(b->left, 13));
    }
    PhoneHarness h;
    Themed(h, [&] {
        WideNavigationRailOptions o;
        o.windowInsets = WindowInsets{};
        o.contentPadding = { 10_dp, 10_dp, 10_dp, 10_dp };
        WideNavigationRail({}, o, [] { Box(Tag(FillMax({}, Axis::Both), "content"), Alignment::TopStart(), false, {}); });
    });
    const auto b = BoundsDp(h, "content");
    CHECK(b && Near(b->top, 10) && Near(b->left, 10));
}

TEST(WideNavigationRailItemColours) {
    PhoneHarness h;
    std::vector<Color> seen;
    const auto record = [&] { seen.push_back(rt::Composition::Current()->Read(LocalContentColor)); };
    const Color red(0xFFFF0000u), blue(0xFF0000FFu), green(0xFF00FF00u), white(0xFFFFFFFFu), gray(0xFF888888u), black(0xFF000000u);
    Themed(h, [&] {
        const auto custom = WideNavigationRailItemDefaults::colors().Copy(red, blue, blue, Color::Unspecified(), green, white, gray, black);
        const auto partial = WideNavigationRailItemDefaults::colors(red, Color::Unspecified(), Color::Unspecified(), Color::Unspecified(),
                                                                    Color::Unspecified(), green);
        WideNavigationRail({}, {}, [&] {
            WideNavigationRailItemOptions o;
            o.colors = custom;
            WideNavigationRailItem(true, [] {}, record, record, false, {}, o);
            WideNavigationRailItem(false, [] {}, record, record, false, {}, o);
            o.enabled = false;
            WideNavigationRailItem(false, [] {}, record, record, false, {}, o);
            o.enabled = true;
            o.colors = partial;
            WideNavigationRailItem(true, [] {}, record, record, false, {}, o);
            WideNavigationRailItem(false, [] {}, record, record, false, {}, o);
        });
    });
    const auto& s = LightColorScheme();
    CHECK(seen.size() >= 10);
    if (seen.size() < 10) return;
    CHECK(seen[0] == red && seen[1] == blue);
    CHECK(seen[2] == green && seen[3] == white);
    CHECK(seen[4] == gray && seen[5] == black);
    CHECK(seen[6] == red && seen[7] == s.secondary);
    CHECK(seen[8] == s.onSurfaceVariant && seen[9] == green);
}

TEST(WideNavigationRailSelectsAClickedItemButNotADisabledOne) {
    PhoneHarness h;
    std::int64_t t = 1000;
    rt::State<int> selected{ 0 };
    int disabledClicks = 0;
    Themed(h, [&] {
        WideNavigationRail({}, {}, [&] {
            for (int i = 0; i < 3; ++i) WideItem(i, false, selected.Get(), [&, i] { selected.Set(i); });
            WideNavigationRailItemOptions off;
            off.enabled = false;
            WideNavigationRailItem(true, [&] { ++disabledClicks; }, [] { NavIcon(); }, [] { Text("Off"); }, false, Tag({}, "off"), off);
        });
    });
    const auto isSelected = [&](const char* tag) {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* n = FindSemantics(tree, tag);
        return n && n->config.selected == std::optional<bool>(true);
    };
    const auto off = [&] {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* n = FindSemantics(tree, "off");
        return n && n->config.role == ui::Role::Tab && n->config.disabled && bool(n->config.onClick);
    };
    CHECK(off());
    CHECK(isSelected("item0") && !isSelected("item1") && !isSelected("item2"));
    Press(h, t, "item2", true);
    h.AdvanceTimeBy(16);
    CHECK(!isSelected("item0") && !isSelected("item1") && isSelected("item2"));
    Press(h, t, "off", true);
    CHECK_EQ(disabledClicks, 0);
}

TEST(WideNavigationRailItemGrowsForLongLabels) {
    for (auto position : { NavigationItemIconPosition::Top, NavigationItemIconPosition::Start }) {
        PhoneHarness h;
        Themed(h, [&] {
            WideNavigationRailItemOptions o;
            o.iconPosition = position;
            WideNavigationRailItem(true, [] {}, [] { NavIcon(); }, [] { Text("Long\nLabel\nMultiple\nLines"); }, false, Tag({}, "item"), o);
        });
        const auto b = BoundsDp(h, "item");
        CHECK(b && b->Height() > (position == NavigationItemIconPosition::Top ? 64 : 48));
    }
}

TEST(WideNavigationRailItemPlacesItsIconAndLabel) {
    const auto place = [](bool expanded, bool label, bool custom = false,
                          PaddingValues collapsed = WideNavigationRailItemDefaults::IndicatorCollapsedPadding,
                          PaddingValues expandedPadding = WideNavigationRailItemDefaults::IndicatorExpandedPadding) {
        PhoneHarness h;
        Themed(h, [&] {
            WideNavigationRailItemOptions o;
            if (custom) o.indicatorPadding = WideNavigationRailItemDefaults::indicatorPadding(expanded, collapsed, expandedPadding);
            WideNavigationRailItem(true, [] {}, [] { NavIcon(Tag({}, "icon")); },
                                   label ? std::function<void()>([] { Text("Label", Tag({}, "label")); }) : std::function<void()>{}, expanded,
                                   Tag({}, "item"), o);
        });
        return std::tuple{ BoundsDp(h, "item"), BoundsDp(h, "icon"), BoundsDp(h, "label") };
    };
    {
        // No label: a 16 dp indicator padding around the icon, and 20 dp either side.
        auto [item, icon, label] = place(false, false);
        CHECK(item && icon);
        if (item && icon) {
            CHECK(Near(item->Width(), 16 + icon->Width() + 16 + 40) && Near(item->Height(), 16 + icon->Height() + 16));
            CHECK(Near(icon->left, (item->Width() - icon->Width()) / 2) && Near(icon->top, (item->Height() - icon->Height()) / 2));
        }
    }
    {
        // Icon on top: 20 + 16 dp in from either side, 4 dp below the top, the label 4 + 4 dp below.
        auto [item, icon, label] = place(false, true);
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->left - item->left, 36) && Near(item->right - icon->right, 36));
            CHECK(Near(icon->top - item->top, 4) && Near(label->top - icon->bottom, 8));
        }
    }
    {
        // Icon at the start: 20 + 16 dp in, 16 dp from the top and bottom, the label 8 dp after it
        // and 16 dp from the end.
        auto [item, icon, label] = place(true, true);
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->left - item->left, 36) && Near(icon->top - item->top, 16) && Near(item->bottom - icon->bottom, 16));
            CHECK(Near(item->right - label->right, 16) && Near(label->left - icon->right, 8));
        }
    }
    {
        // Custom padding, icon on top.
        auto [item, icon, label] = place(false, true, true, PaddingValues{ 5_dp, 3_dp, 5_dp, 3_dp });
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->left - item->left, 25) && Near(item->right - icon->right, 25));
            CHECK(Near(icon->top - item->top, 3) && Near(label->top - icon->bottom, 4 + 3));
        }
    }
    {
        // Custom padding, icon at the start.
        auto [item, icon, label] = place(true, true, true, WideNavigationRailItemDefaults::IndicatorCollapsedPadding,
                                         PaddingValues{ 3_dp, 13_dp, 3_dp, 13_dp });
        CHECK(item && icon && label);
        if (item && icon && label) {
            CHECK(Near(icon->left - item->left, 23) && Near(icon->top - item->top, 13));
            CHECK(Near(item->right - label->right, 3) && Near(item->bottom - icon->bottom, 13));
        }
    }
}

TEST(WideNavigationRailItemKeepsTheIconDescriptionWithoutALabel) {
    PhoneHarness h;
    Themed(h, [&] { WideNavigationRailItem(false, [] {}, [] { NavIcon({}, "Favorite"); }, {}, false, Tag({}, "item"), {}); });
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* n = FindSemantics(tree, "item");
    CHECK(n && n->config.contentDescription == std::vector<std::string>{ "Favorite" });
}

TEST(WideNavigationRailPlacesItsHeaderAtTheTop) {
    for (auto arrangement : { Arrangement::Top(), Arrangement::Center(), Arrangement::Bottom() }) {
        PhoneHarness h;
        Themed(h, [&] {
            WideNavigationRailOptions o;
            o.arrangement = arrangement;
            o.windowInsets = WindowInsets{};
            o.header = [] { Box(Tag(SizeMod({}, 10_dp, 10_dp), "header"), Alignment::TopStart(), false, {}); };
            WideNavigationRail(Tag({}, "rail"), o, [] {
                WideNavigationRailItem(true, [] {}, [] { NavIcon(); }, [] { Text("ItemText"); }, false, Tag({}, "item"), {});
            });
        });
        const auto rail = BoundsDp(h, "rail"), header = BoundsDp(h, "header"), item = BoundsDp(h, "item");
        CHECK(rail && header && item);
        if (!rail || !header || !item) continue;
        CHECK(Near(header->top, 44) && Near(header->left, 0));
        if (arrangement == Arrangement::Top()) CHECK(Near(item->top, 44 + header->Height() + 40));
        else if (arrangement == Arrangement::Center()) CHECK(Near(item->top, (rail->Height() - item->Height()) / 2));
        else CHECK(Near(item->top, rail->Height() - item->Height()));
    }
}

// Ported from Compose's ModalWideNavigationRailTest. The expanded rail opens a dialog window: the
// harness records its owner and lays it out over the whole window.
namespace {
    struct ModalHarness : PhoneHarness {
        struct Window final : ui::PlatformDialog {
            ModalHarness* h;
            ui::Owner* o;
            Window(ModalHarness* h, ui::Owner* o) : h(h), o(o) { h->dialogs.push_back(o); }
            ~Window() override { std::erase(h->dialogs, o); }
            void Update(const ui::DialogWindowProperties&) override {}
        };
        ui::DialogHost host;
        ModalHarness() {
            host.create = [this](ui::Owner& o, const ui::DialogWindowProperties&, std::function<void()>) { return std::make_unique<Window>(this, &o); };
            owner.SetDialogHost(host);
        }
        ~ModalHarness() {
            composition.SetContent([] {});
            composition.Recompose();
        }
    };
    void ModalRail(ModalHarness& h, std::shared_ptr<WideNavigationRailState>& state, WideNavigationRailValue initial, bool hide,
                   std::function<void()> header = {}) {
        Themed(h, [&, initial, hide, header] {
            state = RememberWideNavigationRailState(initial);
            ModalWideNavigationRailOptions o;
            o.state = state;
            o.hideOnCollapse = hide;
            o.header = header;
            const bool expanded = state->TargetValue() == WideNavigationRailValue::Expanded;
            ModalWideNavigationRail(Tag({}, "rail"), o, [expanded, hide] {
                WideNavigationRailItem(true, [] {}, [] { NavIcon(); }, [] { Text("ItemText"); }, hide || expanded, Tag({}, "item"), {});
            });
        });
        h.AdvanceTimeBy(1000);
    }
    // performSemanticsAction(OnClick) on the node described as `description`, in any window.
    bool ClickDescribed(Harness& h, const std::string& description) {
        std::vector<ui::Owner*> owners{ &h.owner };
        owners.insert(owners.end(), h.dialogs.begin(), h.dialogs.end());
        for (ui::Owner* o : owners) {
            const ui::SemanticsNode tree = o->BuildSemanticsTree();
            std::function<const ui::SemanticsNode*(const ui::SemanticsNode&)> find = [&](const ui::SemanticsNode& n) -> const ui::SemanticsNode* {
                if (n.config.contentDescription == std::vector<std::string>{ description }) return &n;
                for (const auto& c : n.children) if (const auto* r = find(c)) return r;
                return nullptr;
            };
            if (const auto* n = find(tree)) {
                if (!n->config.onClick) return false;
                n->config.onClick();
                return true;
            }
        }
        return false;
    }
    const ui::SemanticsNode* FindIn(const ui::SemanticsNode& n, const std::function<bool(const ui::SemanticsNode&)>& pred) {
        if (pred(n)) return &n;
        for (const auto& c : n.children) if (const auto* r = FindIn(c, pred)) return r;
        return nullptr;
    }
}

TEST(ModalWideNavigationRailItemsAreASelectableGroup) {
    ModalHarness h;
    std::shared_ptr<WideNavigationRailState> state;
    ModalRail(h, state, WideNavigationRailValue::Collapsed, false);
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* item = FindSemantics(tree, "item");
    CHECK(item != nullptr);
    if (!item) return;
    const auto* parent = ParentOf(tree, item);
    CHECK(parent && parent->config.selectableGroup);
}

TEST(ModalWideNavigationRailExpandsIntoAWindowAndCollapses) {
    ModalHarness h;
    std::int64_t t = 1000;
    std::shared_ptr<WideNavigationRailState> state;
    std::vector<rt::Job> jobs;
    ModalRail(h, state, WideNavigationRailValue::Collapsed, false,
              [&] { Button([&] { jobs.push_back(rt::Launch(rt::Dispatcher::Main(), state->Toggle())); }, Tag({}, "header"), {}, [] {}); });
    CHECK(h.dialogs.empty());
    auto rail = BoundsDp(h, "rail");
    CHECK(rail && Near(rail->Width(), 96));
    Press(h, t, "header", true);
    h.AdvanceTimeBy(1000);
    CHECK(state->TargetValue() == WideNavigationRailValue::Expanded);
    CHECK_EQ(h.dialogs.size(), std::size_t(1));
    // The expanded rail (the item's parent, in the window) is the expanded minimum wide.
    if (!h.dialogs.empty()) {
        const auto tree = h.dialogs[0]->BuildSemanticsTree();
        const auto* item = FindSemantics(tree, "item");
        const auto* parent = item ? ParentOf(tree, item) : nullptr;
        CHECK(parent && Near(parent->boundsInRoot.Width() / 2, 220));
    }
    // The collapsed rail behind keeps its width.
    rail = BoundsDp(h, "rail");
    CHECK(rail && Near(rail->Width(), 96));
    jobs.push_back(rt::Launch(rt::Dispatcher::Main(), state->Toggle()));
    h.AdvanceTimeBy(1000);
    CHECK(state->TargetValue() == WideNavigationRailValue::Collapsed);
    CHECK(h.dialogs.empty());
    rail = BoundsDp(h, "rail");
    CHECK(rail && Near(rail->Width(), 96));
}

TEST(ModalWideNavigationRailScrimCollapsesIt) {
    for (bool hide : { false, true }) {
        ModalHarness h;
        std::shared_ptr<WideNavigationRailState> state;
        ModalRail(h, state, WideNavigationRailValue::Expanded, hide);
        CHECK(state->TargetValue() == WideNavigationRailValue::Expanded);
        CHECK(ClickDescribed(h, "Close rail"));
        h.AdvanceTimeBy(1000);
        CHECK(state->TargetValue() == WideNavigationRailValue::Collapsed);
        CHECK(h.dialogs.empty());
        if (hide) CHECK(!BoundsDp(h, "item"));
        else {
            const auto rail = BoundsDp(h, "rail");
            CHECK(rail && Near(rail->Width(), 96));
        }
    }
}

TEST(ModalWideNavigationRailHasAPaneTitle) {
    ModalHarness h;
    std::shared_ptr<WideNavigationRailState> state;
    ModalRail(h, state, WideNavigationRailValue::Expanded, false);
    CHECK_EQ(h.dialogs.size(), std::size_t(1));
    if (h.dialogs.empty()) return;
    const auto tree = h.dialogs[0]->BuildSemanticsTree();
    const auto* pane = FindIn(tree, [](const ui::SemanticsNode& n) { return n.config.paneTitle == std::optional<std::string>("Navigation rail"); });
    CHECK(pane != nullptr);
    CHECK(pane && FindIn(*pane, [](const ui::SemanticsNode& n) { return n.config.testTag == "item"; }));
}

TEST(ModalWideNavigationRailHiddenOnCollapseSlidesInAndOut) {
    {
        ModalHarness h;
        std::shared_ptr<WideNavigationRailState> state;
        std::vector<rt::Job> jobs;
        ModalRail(h, state, WideNavigationRailValue::Collapsed, true);
        CHECK(!BoundsDp(h, "rail"));
        jobs.push_back(rt::Launch(rt::Dispatcher::Main(), state->Expand()));
        h.AdvanceTimeBy(1000);
        CHECK(state->TargetValue() == WideNavigationRailValue::Expanded);
        const auto rail = BoundsDp(h, "rail");
        CHECK(rail && Near(rail->left, 0) && rail->Width() > 0);
    }
    ModalHarness h;
    std::shared_ptr<WideNavigationRailState> state;
    std::vector<rt::Job> jobs;
    ModalRail(h, state, WideNavigationRailValue::Expanded, true);
    CHECK(BoundsDp(h, "rail").has_value());
    jobs.push_back(rt::Launch(rt::Dispatcher::Main(), state->Collapse()));
    h.AdvanceTimeBy(1000);
    CHECK(state->TargetValue() == WideNavigationRailValue::Collapsed);
    CHECK(!BoundsDp(h, "rail"));
}

TEST(ModalWideNavigationRailHiddenOnCollapseSwipesAway) {
    ModalHarness h;
    std::int64_t t = 1000;
    std::shared_ptr<WideNavigationRailState> state;
    ModalRail(h, state, WideNavigationRailValue::Expanded, true);
    const auto rail = BoundsDp(h, "rail");
    CHECK(rail.has_value() && !h.dialogs.empty());
    if (!rail || h.dialogs.empty()) return;
    // swipeLeft: across the rail, right to left, in the window.
    ui::Owner& window = *h.dialogs[0];
    const float y = (rail->top + rail->bottom) / 2 * 2;
    const float from = (rail->right - 1) * 2, to = rail->left * 2;
    const auto touch = [&](ui::PointerEventType type, float x, bool down) {
        ui::RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t += 10;
        e.pointers.push_back({ 1, { x, y }, down, ui::PointerType::Touch, {} });
        window.ProcessPointer(e);
        rt::Dispatcher::Main().RunPending();
        rt::Composition::RecomposeAll();
        h.Layout();
    };
    touch(ui::PointerEventType::Press, from, true);
    for (int i = 1; i <= 10; ++i) touch(ui::PointerEventType::Move, from + (to - from) * float(i) / 10, true);
    touch(ui::PointerEventType::Release, to, false);
    h.AdvanceTimeBy(1000);
    CHECK(state->TargetValue() == WideNavigationRailValue::Collapsed);
    CHECK(!BoundsDp(h, "rail"));
}

// Ported from Compose's TabTest (412 dp wide, density 2).
namespace {
    std::vector<const ui::SemanticsNode*> Selectables(const ui::SemanticsNode& tree) {
        std::vector<const ui::SemanticsNode*> out;
        std::function<void(const ui::SemanticsNode&)> walk = [&](const ui::SemanticsNode& n) {
            if (n.config.selected) out.push_back(&n);
            for (const auto& c : n.children) walk(c);
        };
        walk(tree);
        return out;
    }
    // onAllNodes(isSelectable()): every placed tab, scrolled out of view or not.
    std::vector<bool> SelectedStates(Harness& h) {
        const auto tree = h.owner.BuildSemanticsTree(false);
        std::vector<bool> out;
        for (const auto* n : Selectables(tree)) out.push_back(*n->config.selected);
        return out;
    }
    void PressSelectable(Harness& h, std::int64_t& t, std::size_t index) {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto nodes = Selectables(tree);
        CHECK(index < nodes.size());
        if (index >= nodes.size()) return;
        const auto b = nodes[index]->boundsInRoot;
        Touch(h, t, ui::PointerEventType::Press, (b.left + b.right) / 2, (b.top + b.bottom) / 2, true);
        Touch(h, t, ui::PointerEventType::Release, (b.left + b.right) / 2, (b.top + b.bottom) / 2, false);
        h.AdvanceTimeBy(1000);
    }
    // An indicator 1 dp tall, the selected tab's width, tagged "indicator".
    std::function<void(TabIndicatorScope&)> RedIndicator(const rt::State<int>& state) {
        return [&state](TabIndicatorScope& s) {
            Box(Tag(Background(Height(FillMax(s.TabIndicatorOffset({}, state.Get()), Axis::Horizontal), 1_dp), Color(0xFFFF0000u)), "indicator"),
                Alignment::TopStart(), false, {});
        };
    }
    void TextTabs(rt::State<int>& state, std::vector<std::string> titles, bool scrollable, bool leadingIcon = false) {
        const auto tabs = [&state, titles, leadingIcon] {
            for (std::size_t i = 0; i < titles.size(); ++i) {
                const int index = int(i);
                const std::string title = titles[i];
                if (leadingIcon)
                    LeadingIconTab(state.Get() == index, [&state, index] { state.Set(index); }, [title] { Text(title); }, [] { NavIcon(); }, {});
                else
                    Tab(state.Get() == index, [&state, index] { state.Set(index); }, {}, [title] { Text(title); }, {});
            }
        };
        if (scrollable) SecondaryScrollableTabRow(state.Get(), {}, {}, tabs);
        else PrimaryTabRow(state.Get(), {}, {}, tabs);
    }
}

TEST(TabsAreSelectableTabsInASelectableGroup) {
    for (bool leading : { false, true }) {
        for (bool enabled : { true, false }) {
            PhoneHarness h;
            Themed(h, [&] {
                TabOptions o;
                o.enabled = enabled;
                const auto tab = [&] {
                    if (leading) LeadingIconTab(true, [] {}, [] { Text("Text"); }, [] { NavIcon(); }, Tag({}, "tab"), o);
                    else Tab(true, [] {}, Tag({}, "tab"), [] { Text("Text"); }, {}, o);
                };
                if (enabled) SecondaryTabRow(0, {}, {}, tab);
                else Box({}, Alignment::TopStart(), false, tab);
            });
            const auto tree = h.owner.BuildSemanticsTree();
            const auto* tab = FindSemantics(tree, "tab");
            CHECK(tab != nullptr);
            if (!tab) continue;
            CHECK(tab->config.role == ui::Role::Tab);
            CHECK(tab->config.selected == std::optional<bool>(true));
            CHECK_EQ(tab->config.disabled, !enabled);
            CHECK(bool(tab->config.onClick));
            if (enabled) {
                const auto* parent = ParentOf(tree, tab);
                CHECK(parent && parent->config.selectableGroup);
            }
        }
    }
}

TEST(TabHeights) {
    const auto height = [](std::function<void()> tab) {
        PhoneHarness h;
        Themed(h, [&] { Box(Tag({}, "box"), Alignment::TopStart(), false, tab); });
        const auto b = BoundsDp(h, "box");
        return b ? b->Height() : -1.0f;
    };
    CHECK(Near(height([] { Tab(true, [] {}, {}, [] { Text("Text"); }, {}); }), 48));
    CHECK(Near(height([] { Tab(true, [] {}, {}, nullptr, [] { NavIcon(); }); }), 48));
    CHECK(Near(height([] { Tab(true, [] {}, {}, [] { Text("Text and Icon"); }, [] { NavIcon(); }); }), 72));
    CHECK(Near(height([] { LeadingIconTab(true, [] {}, [] { Text("Text"); }, [] { NavIcon(); }, {}); }), 48));
}

TEST(TabRowIndicatorFollowsTheSelectedTab) {
    for (bool rtl : { false, true }) {
        for (bool scrollable : { false, true }) {
            PhoneHarness h;
            std::int64_t t = 1000;
            rt::State<int> state{ 0 };
            Themed(h, [&] {
                const auto row = [&] {
                    Box(Tag({}, "tabRow"), Alignment::TopStart(), false, [&] {
                        const auto tabs = [&] {
                            for (int i = 0; i < 2; ++i)
                                Tab(state.Get() == i, [&state, i] { state.Set(i); }, {}, [i] { Text("TAB " + std::to_string(i + 1)); }, {});
                        };
                        if (scrollable) {
                            ScrollableTabRowOptions o;
                            o.indicator = RedIndicator(state);
                            SecondaryScrollableTabRow(state.Get(), {}, o, tabs);
                        } else {
                            TabRowOptions o;
                            o.indicator = RedIndicator(state);
                            SecondaryTabRow(state.Get(), {}, o, tabs);
                        }
                    });
                };
                if (rtl) rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Rtl, row);
                else row();
            });
            const auto expectAt = [&](float left) {
                const auto row = BoundsDp(h, "tabRow");
                const auto ind = BoundsDp(h, "indicator");
                CHECK(row && ind);
                if (!row || !ind) return;
                CHECK(std::abs(ind->left - left) <= 1);
                CHECK(Near(ind->top, row->Height() - 1));
            };
            const float width = BoundsDp(h, "tabRow") ? BoundsDp(h, "tabRow")->Width() : 0;
            // Fixed: halves of the row; scrollable: 52 dp in, then 90 dp tabs.
            if (!scrollable) expectAt(rtl ? width / 2 : 0);
            else expectAt(rtl ? width - 52 - 90 : 52);
            PressSelectable(h, t, 1);
            if (!scrollable) expectAt(rtl ? 0 : width / 2);
            else expectAt(rtl ? width - 52 - 90 - 90 : 52 + 90);
        }
    }
}

TEST(TabRowIndicatorKeepsItsHeight) {
    PhoneHarness h;
    rt::State<int> state{ 1 };
    Themed(h, [&] {
        Box(FillMax({}, Axis::Both), Alignment::TopStart(), true, [&] {
            TabRowOptions o;
            o.indicator = RedIndicator(state);
            PrimaryTabRow(1, {}, o, [] { for (int i = 0; i < 2; ++i) Tab(i == 1, [] {}, {}, [i] { Text("TAB " + std::to_string(i + 1)); }, {}); });
        });
    });
    const auto b = BoundsDp(h, "indicator");
    CHECK(b && Near(b->Height(), 1));
}

TEST(TabRowDividerSitsAtTheBottom) {
    for (bool scrollable : { false, true }) {
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "tabRow"), Alignment::TopStart(), false, [&] {
                const auto tabs = [] { for (int i = 0; i < 2; ++i) Tab(i == 0, [] {}, Height({}, 100_dp), [i] { Text("TAB " + std::to_string(i + 1)); }, {}); };
                const auto divider = [] { HorizontalDivider(Tag({}, "divider")); };
                if (scrollable) {
                    ScrollableTabRowOptions o;
                    o.divider = divider;
                    SecondaryScrollableTabRow(0, Height({}, 100_dp), o, tabs);
                } else {
                    TabRowOptions o;
                    o.divider = divider;
                    SecondaryTabRow(0, Height({}, 100_dp), o, tabs);
                }
            });
        });
        const auto row = BoundsDp(h, "tabRow"), divider = BoundsDp(h, "divider");
        CHECK(row && divider);
        if (row && divider) CHECK(Near(divider->left, 0) && Near(divider->top, row->Height() - 1) && Near(divider->Height(), 1));
    }
}

TEST(TabPlacesItsTextAndIcon) {
    {
        // One line, and two: centred vertically.
        for (const char* title : { "TAB", "Two line \n text" }) {
            PhoneHarness h;
            Themed(h, [&] { SecondaryTabRow(0, Tag({}, "tabRow"), {}, [&] { Tab(true, [] {}, {}, [&] { Text(title, Tag({}, "text")); }, {}); }); });
            const auto row = BoundsDp(h, "tabRow"), text = BoundsDp(h, "text");
            CHECK(row && text && Near(text->top, (row->Height() - text->Height()) / 2));
        }
    }
    {
        // With an icon: the baseline 14 dp above the 3 dp indicator.
        PhoneHarness h;
        Themed(h, [&] { SecondaryTabRow(0, Tag({}, "tabRow"), {}, [] { Tab(true, [] {}, {}, [] { Text("TAB", Tag({}, "text")); }, [] { NavIcon(); }); }); });
        const auto row = BoundsDp(h, "tabRow"), text = BoundsDp(h, "text");
        CHECK(row && text);
        if (row && text) CHECK(Near(text->top + 0.8f * text->Height(), row->Height() - 14 - 3));
    }
    {
        // A leading icon: 8 dp before the text, the pair centred.
        PhoneHarness h;
        Themed(h, [&] {
            SecondaryTabRow(0, Tag({}, "tabRow"), {}, [] { LeadingIconTab(true, [] {}, [] { Text("TAB", Tag({}, "text")); }, [] { NavIcon(Tag({}, "icon")); }, {}); });
        });
        const auto row = BoundsDp(h, "tabRow"), text = BoundsDp(h, "text"), icon = BoundsDp(h, "icon");
        CHECK(row && text && icon);
        if (row && text && icon) {
            CHECK(Near(text->left, icon->right + 8));
            CHECK(Near(icon->left, (row->Width() - icon->Width() - text->Width() - 8) / 2));
        }
    }
}

TEST(TabRowsSelectANewTab) {
    for (int kind = 0; kind < 3; ++kind) {
        PhoneHarness h;
        std::int64_t t = 1000;
        rt::State<int> state{ 0 };
        const bool scrollable = kind == 2;
        const int count = scrollable ? 10 : 3;
        std::vector<std::string> titles;
        for (int i = 0; i < count; ++i) titles.push_back(i == 2 && !scrollable ? "Tab 3 with lots of text" : "Tab " + std::to_string(i + 1));
        Themed(h, [&] { TextTabs(state, titles, scrollable, kind == 1); });
        auto states = SelectedStates(h);
        CHECK_EQ(states.size(), std::size_t(count));
        std::vector<bool> expected(std::size_t(count), false);
        expected[0] = true;
        CHECK(states == expected);
        const std::size_t pick = scrollable ? 1 : 2;
        PressSelectable(h, t, pick);
        expected[0] = false;
        expected[pick] = true;
        CHECK(SelectedStates(h) == expected);
    }
}

TEST(ScrollableTabRowScrollsToAnOffScreenSelectedTab) {
    PhoneHarness h;
    rt::State<int> state{ 9 };
    std::vector<std::string> titles;
    for (int i = 0; i < 10; ++i) titles.push_back("Tab " + std::to_string(i + 1));
    Themed(h, [&] {
        SecondaryScrollableTabRow(state.Get(), {}, {}, [&] {
            for (int i = 0; i < 10; ++i) Tab(state.Get() == i, [&state, i] { state.Set(i); }, Tag({}, "tab" + std::to_string(i)), [&titles, i] { Text(titles[std::size_t(i)]); }, {});
        });
    });
    h.AdvanceTimeBy(1000);
    CHECK((SelectedStates(h).size() == 10 && SelectedStates(h)[9]));
    CHECK(Displayed(h, "tab9"));
}

TEST(TabRowIndicatorAnimatesItsWidth) {
    PhoneHarness h;
    std::int64_t t = 1000;
    rt::State<int> state{ 0 };
    Themed(h, [&] {
        ScrollableTabRowOptions o;
        o.indicator = [&state](TabIndicatorScope& s) { TabRowDefaults::SecondaryIndicator(Tag(s.TabIndicatorOffset({}, state.Get(), false), "indicator")); };
        SecondaryScrollableTabRow(state.Get(), {}, o, [&] {
            const char* titles[] = { "TAB 1", "TAB 2", "TAB 3 WITH LOTS OF TEXT" };
            for (int i = 0; i < 3; ++i) Tab(state.Get() == i, [&state, i] { state.Set(i); }, {}, [&titles, i] { Text(titles[i]); }, {});
        });
    });
    const auto initial = BoundsDp(h, "indicator");
    CHECK(initial.has_value());
    // Select the third, wider tab and look 50 ms in.
    const auto tree = h.owner.BuildSemanticsTree();
    const auto nodes = Selectables(tree);
    CHECK_EQ(nodes.size(), std::size_t(3));
    if (nodes.size() < 3 || !initial) return;
    const auto b = nodes[2]->boundsInRoot;
    Touch(h, t, ui::PointerEventType::Press, (b.left + b.right) / 2, (b.top + b.bottom) / 2, true);
    Touch(h, t, ui::PointerEventType::Release, (b.left + b.right) / 2, (b.top + b.bottom) / 2, false);
    h.AdvanceTimeBy(50);
    const auto mid = BoundsDp(h, "indicator");
    h.AdvanceTimeBy(1000);
    const auto final = BoundsDp(h, "indicator");
    CHECK(mid && final);
    if (mid && final) CHECK(initial->Width() < mid->Width() && mid->Width() < final->Width());
}

TEST(DisabledTabsTakeNoClicks) {
    for (bool leading : { false, true }) {
        PhoneHarness h;
        std::int64_t t = 1000;
        int clicks = 0;
        Themed(h, [&] {
            TabOptions o;
            o.enabled = false;
            if (leading) LeadingIconTab(true, [&] { ++clicks; }, [] { Text("Text"); }, [] { NavIcon(); }, Tag({}, "tab"), o);
            else Tab(true, [&] { ++clicks; }, Tag({}, "tab"), [] { Text("Text"); }, {}, o);
        });
        Press(h, t, "tab", true);
        CHECK_EQ(clicks, 0);
    }
}

TEST(TabRowsRespectTheirLayoutHeight) {
    for (bool scrollable : { false, true }) {
        PhoneHarness h;
        rt::State<float> height{ 0 };
        rt::State<int> state{ 0 };
        Themed(h, [&] {
            Column(Tag(SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified(), ui::Dp(height.Get())), "Tabs"), [&] {
                TextTabs(state, { "Tab 1", "Tab 2", "Tab 3" }, scrollable);
            });
        });
        auto b = BoundsDp(h, "Tabs");
        CHECK(b && Near(b->Height(), 0));
        height.Set(40);
        h.AdvanceTimeBy(16);
        b = BoundsDp(h, "Tabs");
        CHECK(b && Near(b->Height(), 40));
    }
}

TEST(TabRowWithoutTabsHasNoHeight) {
    PhoneHarness h;
    Themed(h, [&] { SecondaryTabRow(0, Tag({}, "tabRow"), {}, [] {}); });
    const auto b = BoundsDp(h, "tabRow");
    CHECK(b && Near(b->Height(), 0));
}

TEST(TabGrowsWithTheFontScale) {
    // A 10 sp label with an icon outgrows the 72 dp tab at a large font scale. (Compose's test
    // uses 10; past the last table a scale's linear converter is cached among the tables, so a
    // scale this large would bend every later lookup between 2 and 10, as it does in Compose.)
    PhoneHarness h;
    h.owner.SetDensity({ 2, 3 });
    Themed(h, [&] {
        Box(Tag({}, "box"), Alignment::TopStart(), false, [] {
            Tab(true, [] {}, {}, [] { Text("Text", {}, { .fontSize = text::Sp(10) }); }, [] { NavIcon(); });
        });
    });
    const auto b = BoundsDp(h, "box");
    CHECK(b && b->Height() >= 90);
}

// Ported from Compose's AppBarTest (412 x 800 dp, density 2). Colour checks read the content
// colours the slots get; the container colour, a drawn colour, is checked through the state
// that picks it (kiln's tests take no screenshots).
namespace {
    void FakeIcon(ui::Modifier m) {
        IconButton([] {}, Semantics(std::move(m), [](ui::SemanticsConfiguration&) {}, true), {}, [] { NavIcon(); });
    }
    // The bar with the usual tags: navigation icon, title, actions.
    TopAppBarOptions TaggedBar(bool navigation = true, bool actions = true) {
        TopAppBarOptions o;
        if (navigation) o.navigationIcon = [] { FakeIcon(Tag({}, "navigationIcon")); };
        if (actions) o.actions = [] { FakeIcon(Tag({}, "actions")); };
        return o;
    }
    std::function<void()> TitleText(std::string text = "Title") { return [text] { Text(text, Tag({}, "title")); }; }
    // assertSmallDefaultPositioning / assertSmallPositioningWithoutNavigation.
    void CheckSmallPositioning(Harness& h, bool navigation, bool centered) {
        const auto bar = BoundsDp(h, "topAppBar"), title = BoundsDp(h, "title"), actions = BoundsDp(h, "actions");
        CHECK(bar && title && actions);
        if (!bar || !title || !actions) return;
        const auto subtitle = BoundsDp(h, "subtitle");
        const float subtitleHeight = subtitle ? subtitle->Height() : 0;
        const float padding = (64 - 48) / 2.0f;
        if (navigation) {
            const auto nav = BoundsDp(h, "navigationIcon");
            CHECK(nav && Near(nav->left, 4) && Near(nav->top, bar->bottom - padding - 48));
        }
        CHECK(Near(title->top, (bar->Height() - title->Height() - subtitleHeight) / 2));
        if (centered) {
            CHECK(Near(title->left, (bar->Width() - title->Width()) / 2));
            if (subtitle) CHECK(Near(subtitle->left, (bar->Width() - subtitle->Width()) / 2));
        } else {
            CHECK(Near(title->left, navigation ? 4 + 48 + 4 : 4 + 12));
        }
        CHECK(Near(actions->left, bar->Width() - 4 - 48));
        if (navigation) CHECK(Near(actions->top, bar->bottom - padding - 48));
    }
}

TEST(TopAppBarsFillTheWidthAt64Dp) {
    for (bool centered : { false, true }) {
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "box"), Alignment::TopStart(), false, [&] {
                TopAppBarOptions o;
                o.windowInsets = WindowInsets{};
                if (centered) CenterAlignedTopAppBar([] { Text("Title"); }, {}, o);
                else TopAppBar([] { Text("Title"); }, {}, o);
            });
        });
        const auto b = BoundsDp(h, "box");
        CHECK(b && Near(b->Height(), 64) && Near(b->Width(), 412));
        CHECK(Displayed(h, "Title"));
    }
}

TEST(TopAppBarShowsItsSubtitle) {
    PhoneHarness h;
    Themed(h, [&] {
        TopAppBarOptions o;
        o.subtitle = [] { Text("Subtitle"); };
        TopAppBar([] { Text("Title"); }, {}, o);
    });
    CHECK(Displayed(h, "Subtitle"));
}

TEST(TopAppBarPlacesItsSlots) {
    struct Case { bool navigation, centered, subtitle, twoActions; };
    for (const Case k : { Case{ true, false, false, false }, Case{ false, false, false, false }, Case{ true, true, true, false },
                          Case{ true, true, true, true }, Case{ true, true, false, false }, Case{ false, true, false, false } }) {
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
                TopAppBarOptions o = TaggedBar(k.navigation);
                o.windowInsets = WindowInsets{};
                if (k.twoActions) o.actions = [] { FakeIcon({}); FakeIcon(Tag({}, "actions")); };
                if (k.subtitle) {
                    o.subtitle = [] { Text("Subtitle", Tag({}, "subtitle")); };
                    o.titleHorizontalAlignment = AlignmentH::CenterHorizontally();
                    TopAppBar(TitleText(), {}, o);
                } else if (k.centered) {
                    CenterAlignedTopAppBar(TitleText(), {}, o);
                } else {
                    TopAppBar(TitleText(), {}, o);
                }
            });
        });
        CheckSmallPositioning(h, k.navigation, k.centered);
    }
}

TEST(TopAppBarTextStylesAndColours) {
    for (bool centered : { false, true }) {
        PhoneHarness h;
        text::TextStyle titleStyle, subtitleStyle, expectedTitle, expectedSubtitle;
        Color nav, title, actions;
        TopAppBarColors colors{};
        Themed(h, [&] {
            expectedTitle = Value(tokens::AppBarSmallTokens::TitleFont);
            expectedSubtitle = Value(tokens::TypographyKeyTokens::LabelMedium);
            colors = TopAppBarDefaults::topAppBarColors();
            TopAppBarOptions o;
            o.navigationIcon = [&] { nav = rt::Composition::Current()->Read(LocalContentColor); };
            o.actions = [&] { actions = rt::Composition::Current()->Read(LocalContentColor); };
            o.subtitle = [&] { subtitleStyle = rt::Composition::Current()->Read(LocalTextStyle()); };
            const auto t = [&] {
                titleStyle = rt::Composition::Current()->Read(LocalTextStyle());
                title = rt::Composition::Current()->Read(LocalContentColor);
            };
            if (centered) CenterAlignedTopAppBar(t, {}, o);
            else TopAppBar(t, {}, o);
        });
        CHECK(titleStyle == expectedTitle);
        CHECK(subtitleStyle == expectedSubtitle);
        CHECK(nav == colors.navigationIconContentColor && title == colors.titleContentColor && actions == colors.actionIconContentColor);
    }
}

TEST(TopAppBarSubtitleTakesItsColour) {
    PhoneHarness h;
    Color title, subtitle;
    Themed(h, [&] {
        TopAppBarOptions o;
        o.colors = TopAppBarDefaults::topAppBarColors(Color::Unspecified(), Color::Unspecified(), Color::Unspecified(), Color(0xFFFF0000u),
                                                      Color::Unspecified(), Color(0xFF00FF00u));
        o.subtitle = [&] { subtitle = rt::Composition::Current()->Read(LocalContentColor); };
        TopAppBar([&] { title = rt::Composition::Current()->Read(LocalContentColor); }, {}, o);
    });
    CHECK(title == Color(0xFFFF0000u) && subtitle == Color(0xFF00FF00u));
}

TEST(PinnedTopAppBarCountsTheScrolledContent) {
    // smallTopAppBar_scrolledContentColor: content scrolled under the bar turns it to the
    // scrolled container colour (read here as the overlapped fraction that picks it).
    PhoneHarness h;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    TopAppBarColors colors{};
    Themed(h, [&] {
        behavior = TopAppBarDefaults::pinnedScrollBehavior();
        colors = TopAppBarDefaults::topAppBarColors();
        TopAppBarOptions o;
        o.scrollBehavior = behavior;
        TopAppBar(TitleText(), Tag({}, "topAppBar"), o);
    });
    CHECK(behavior->State().OverlappedFraction() == 0.0f);
    behavior->State().SetContentOffset(-100);
    h.AdvanceTimeBy(1000);
    CHECK(behavior->State().OverlappedFraction() > 0.01f);
    CHECK(colors.ContainerColor(1) == colors.scrolledContainerColor);
}

TEST(TopAppBarShrinksByItsHeightOffset) {
    PhoneHarness h;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    Themed(h, [&] {
        behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
        TopAppBarOptions o;
        o.scrollBehavior = behavior;
        o.windowInsets = WindowInsets{};
        TopAppBar(TitleText(), Tag({}, "topAppBar"), o);
    });
    behavior->State().SetHeightOffset(-40);   // 20 dp
    behavior->State().SetContentOffset(-40);
    h.AdvanceTimeBy(16);
    const auto b = BoundsDp(h, "topAppBar");
    CHECK(b && Near(b->Height(), 64 - 20));
}

TEST(TopAppBarCustomAndTooSmallHeights) {
    {
        PhoneHarness h;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        Themed(h, [&] {
            behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
            TopAppBarOptions o;
            o.scrollBehavior = behavior;
            o.windowInsets = WindowInsets{};
            o.expandedHeight = 50_dp;
            TopAppBar(TitleText(), Tag({}, "topAppBar"), o);
        });
        CHECK_EQ(behavior->State().HeightOffsetLimit(), -100.0f);
        const auto b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 50));
    }
    // A zero height still fits the title and subtitle.
    PhoneHarness h;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    Themed(h, [&] {
        behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
        TopAppBarOptions o;
        o.scrollBehavior = behavior;
        o.windowInsets = WindowInsets{};
        o.expandedHeight = 0_dp;
        o.subtitle = [] { Text("Subtitle", Tag({}, "subtitle")); };
        TopAppBar(TitleText(), Tag({}, "topAppBar"), o);
    });
    const auto title = BoundsDp(h, "title"), subtitle = BoundsDp(h, "subtitle"), bar = BoundsDp(h, "topAppBar");
    CHECK(title && subtitle && bar);
    if (title && subtitle && bar) {
        const float total = title->Height() + subtitle->Height();
        CHECK_EQ(behavior->State().HeightOffsetLimit(), -total * 2);
        CHECK(Near(bar->Height(), total));
    }
}

TEST(TopAppBarLongTitlesStayClearOfTheIcons) {
    {
        PhoneHarness h;
        Themed(h, [&] {
            TopAppBarOptions o;
            o.actions = [] { Row(Tag({}, "actions"), [] { Text("Action"); }); };
            TopAppBar([] { Text("This is a very long title that should be truncated", Tag({}, "title"), { .maxLines = 1 }); }, Tag({}, "topAppBar"), o);
        });
        const auto bar = BoundsDp(h, "topAppBar"), title = BoundsDp(h, "title"), actions = BoundsDp(h, "actions");
        CHECK(bar && title && actions && title->right <= actions->left + 0.01f && title->Width() < bar->Width());
    }
    for (bool navigation : { false, true }) {
        PhoneHarness h;
        Themed(h, [&] {
            TopAppBarOptions o = TaggedBar(navigation, !navigation);
            CenterAlignedTopAppBar([] { Text("This is a very very very very long title", Tag({}, "title"),
                                             { .overflow = text::TextOverflow::Ellipsis, .maxLines = 1 }); },
                                   {}, o);
        });
        const auto title = BoundsDp(h, "title");
        if (navigation) {
            const auto nav = BoundsDp(h, "navigationIcon");
            CHECK(title && nav && title->left > nav->right);
        } else {
            const auto actions = BoundsDp(h, "actions");
            CHECK(title && actions && title->right < actions->left);
        }
    }
}

TEST(CenterAlignedTopAppBarPadsByItsInsets) {
    PhoneHarness h;
    Themed(h, [&] {
        Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [] {
            TopAppBarOptions o = TaggedBar();
            o.windowInsets = WindowInsets::Fixed(10_dp, 10_dp, 10_dp, 10_dp);
            CenterAlignedTopAppBar(TitleText(), {}, o);
        });
    });
    const auto bar = BoundsDp(h, "topAppBar"), nav = BoundsDp(h, "navigationIcon");
    CHECK(bar && nav && Near(nav->left, 4 + 10) && Near(nav->top, bar->bottom - 8 - 10 - 48));
}

TEST(CenterAlignedTopAppBarMeasuresWithAMinimumWidth) {
    PhoneHarness h;
    ui::IntSize size{};
    Themed(h, [&] {
        CenterAlignedTopAppBar([] { Text("Title"); }, LayoutMod({}, [&](ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) {
            ui::Placeable& p = m.Measure(ui::Constraints{ c.maxWidth, c.maxWidth, c.minHeight, c.maxHeight });
            size = { p.Width(), p.Height() };
            return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& place) { place.Place(p, 0, 0); });
        }));
    });
    CHECK(size.width > 0 && size.height > 0);
}

// AppBarTest: medium and large (and flexible) bars.
namespace {
    // getUnclippedBoundsInRoot (dp): where the tag sits, not clipped by the layouts around it.
    std::optional<ui::Rect> UnclippedBoundsDp(Harness& h, const std::string& tag) {
        const auto* n = FindTagged(h, tag);
        if (!n) return std::nullopt;
        const ui::Rect r = TagRectInRoot(*n, tag);
        const float d = h.owner.GetDensity().density;
        return ui::Rect{ r.left / d, r.top / d, r.right / d, r.bottom / d };
    }
    // onAllNodesWithTag(tag, useUnmergedTree = true): placed layout nodes, in tree order.
    void AllTagged(const ui::LayoutNode& n, const std::string& tag, std::vector<const ui::LayoutNode*>& out) {
        if (!n.IsPlaced() && n.Parent()) return;
        ui::SemanticsConfiguration c;
        if (ui::CalculateSemanticsConfiguration(const_cast<ui::LayoutNode&>(n), c) && c.testTag == tag) out.push_back(&n);
        for (const auto* child : n.Children()) AllTagged(*child, tag, out);
    }
    std::vector<ui::Rect> AllTaggedBoundsDp(Harness& h, const std::string& tag) {
        std::vector<const ui::LayoutNode*> nodes;
        AllTagged(h.owner.Root(), tag, nodes);
        std::vector<ui::Rect> out;
        const float d = h.owner.GetDensity().density;
        for (const auto* n : nodes) {
            const ui::Rect r = TagRectInRoot(*n, tag);
            out.push_back({ r.left / d, r.top / d, r.right / d, r.bottom / d });
        }
        return out;
    }
    // onAllNodesWithTag(tag) (merged): the semantics nodes, covered or not.
    int CountMerged(Harness& h, const std::string& tag) {
        const auto tree = h.owner.BuildSemanticsTree(false);
        int n = 0;
        std::function<void(const ui::SemanticsNode&)> walk = [&](const ui::SemanticsNode& s) {
            if (s.config.testTag == tag) ++n;
            for (const auto& c : s.children) walk(c);
        };
        walk(tree);
        return n;
    }
    enum class TwoRow : std::uint8_t { Medium, MediumFlexible, Large, LargeFlexible };
    void TwoRowBar(TwoRow kind, std::function<void()> title, const ui::Modifier& m, const TwoRowsTopAppBarOptions& o) {
        switch (kind) {
            case TwoRow::Medium: MediumTopAppBar(std::move(title), m, o); break;
            case TwoRow::MediumFlexible: MediumFlexibleTopAppBar(std::move(title), m, o); break;
            case TwoRow::Large: LargeTopAppBar(std::move(title), m, o); break;
            case TwoRow::LargeFlexible: LargeFlexibleTopAppBar(std::move(title), m, o); break;
        }
    }
    float ExpandedHeight(TwoRow kind) { return kind == TwoRow::Medium || kind == TwoRow::MediumFlexible ? 112 : kind == TwoRow::Large ? 152 : 120; }
}

TEST(TwoRowTopAppBarsFillTheWidth) {
    for (auto kind : { TwoRow::Medium, TwoRow::Large }) {
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "box"), Alignment::TopStart(), false, [&] {
                TwoRowsTopAppBarOptions o;
                o.windowInsets = WindowInsets{};
                TwoRowBar(kind, [] { Text("Title"); }, {}, o);
            });
        });
        const auto b = BoundsDp(h, "box");
        CHECK(b && Near(b->Height(), ExpandedHeight(kind)) && Near(b->Width(), 412));
    }
}

TEST(TwoRowTopAppBarsPlaceTheirSlotsExpanded) {
    // assertMediumOrLargeDefaultPositioning: the icons and the small title in the top row, the
    // large title's baseline 24 (medium) or 28 (large) dp above the bottom.
    struct Case { TwoRow kind; float collapsed, expanded, bottomPadding; bool custom; };
    for (const Case k : { Case{ TwoRow::Medium, 64, 112, 24, false }, Case{ TwoRow::Medium, 36, 112, 24, true },
                          Case{ TwoRow::Large, 64, 152, 28, false }, Case{ TwoRow::Large, 36, 152, 28, true } }) {
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
                TwoRowsTopAppBarOptions o;
                o.navigationIcon = [] { FakeIcon(Tag({}, "navigationIcon")); };
                o.actions = [] { FakeIcon(Tag({}, "actions")); };
                o.windowInsets = WindowInsets{};
                if (k.custom) {
                    o.collapsedHeight = ui::Dp(k.collapsed);
                    o.expandedHeight = ui::Dp(k.expanded);
                }
                TwoRowBar(k.kind, TitleText(), {}, o);
            });
        });
        const auto bar = BoundsDp(h, "topAppBar");
        const auto titles = AllTaggedBoundsDp(h, "title");
        CHECK(bar && titles.size() == 2);
        if (!bar || titles.size() != 2) continue;
        CHECK(Near(bar->Height(), k.expanded));
        const float topRowBottom = bar->top + k.collapsed, padding = (k.collapsed - 48) / 2;
        // Unclipped: a 36 dp top row leaves the 48 dp icons overflowing it.
        const auto nav = UnclippedBoundsDp(h, "navigationIcon"), actions = UnclippedBoundsDp(h, "actions");
        CHECK(nav && Near(nav->left, 4) && Near(nav->top, topRowBottom - padding - 48));
        CHECK(actions && Near(actions->left, bar->Width() - 4 - 48) && Near(actions->top, topRowBottom - padding - 48));
        CHECK(Near(titles[0].left, 4 + 48 + 4) && Near(titles[0].top, (topRowBottom - titles[0].Height()) / 2));
        CHECK(Near(titles[1].left, 16));
        // The bottom title's baseline (0.8 of a fake text line down) from the bottom. In the
        // default medium bar the test font's title is too tall for its 24 dp padding in the 48 dp
        // row: the padding shrinks to fit it, and the title sits at the row's top.
        const float baselineFromBottom = bar->bottom - (titles[1].top + 0.8f * titles[1].Height());
        if (k.kind == TwoRow::Medium && !k.custom) CHECK(baselineFromBottom < k.bottomPadding && Near(titles[1].top, bar->top + 64));
        else CHECK(Near(baselineFromBottom, k.bottomPadding));
    }
}

TEST(FlexibleTopAppBarsFitTheirTextWhenTooShort) {
    for (auto kind : { TwoRow::MediumFlexible, TwoRow::LargeFlexible }) {
        {
            // Expanded no taller than collapsed: the bottom row still fits the title and subtitle.
            PhoneHarness h;
            std::shared_ptr<TopAppBarScrollBehavior> behavior;
            Themed(h, [&] {
                behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
                Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
                    TwoRowsTopAppBarOptions o;
                    o.subtitle = [] { Text("Subtitle", Tag({}, "subtitle")); };
                    o.collapsedHeight = 64_dp;
                    o.expandedHeight = 64_dp;
                    o.scrollBehavior = behavior;
                    o.windowInsets = WindowInsets{};
                    TwoRowBar(kind, TitleText(), {}, o);
                });
            });
            const auto titles = AllTaggedBoundsDp(h, "title"), subtitles = AllTaggedBoundsDp(h, "subtitle");
            const auto bar = BoundsDp(h, "topAppBar");
            CHECK(bar && titles.size() == 2 && subtitles.size() == 2);
            if (bar && titles.size() == 2 && subtitles.size() == 2) {
                const float textHeight = titles[1].Height() + subtitles[1].Height();
                CHECK_EQ(behavior->State().HeightOffsetLimit(), -textHeight * 2);
                CHECK(Near(bar->Height(), 64 + textHeight));
            }
        }
        // Collapsed to nothing: the top row keeps its text's height.
        PhoneHarness h;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        const float expanded = kind == TwoRow::MediumFlexible ? 136 : 152;
        Themed(h, [&] {
            behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
            Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
                TwoRowsTopAppBarOptions o;
                o.subtitle = [] { Text("Subtitle", Tag({}, "subtitle")); };
                o.collapsedHeight = 0_dp;
                o.expandedHeight = ui::Dp(expanded);
                o.scrollBehavior = behavior;
                o.windowInsets = WindowInsets{};
                TwoRowBar(kind, TitleText(), {}, o);
            });
        });
        behavior->State().SetHeightOffset(-expanded * 2);
        behavior->State().SetContentOffset(-expanded * 2);
        h.AdvanceTimeBy(16);
        const auto titles = AllTaggedBoundsDp(h, "title"), subtitles = AllTaggedBoundsDp(h, "subtitle");
        const auto bar = BoundsDp(h, "topAppBar");
        CHECK(bar && titles.size() == 2 && subtitles.size() == 2);
        if (bar && titles.size() == 2 && subtitles.size() == 2) {
            CHECK_EQ(behavior->State().HeightOffsetLimit(), -expanded * 2);
            CHECK(Near(bar->Height(), titles[0].Height() + subtitles[0].Height()));
        }
    }
}

TEST(TwoRowTopAppBarsCollapseWithTheScroll) {
    // assertMediumOrLargeScrolledHeight, with 13 dp insets all round.
    struct Case { TwoRow kind; float maxHeight, minHeight; bool custom; };
    for (const Case k : { Case{ TwoRow::Medium, 112, 64, false }, Case{ TwoRow::Medium, 120, 40, true }, Case{ TwoRow::Large, 152, 64, false },
                          Case{ TwoRow::Large, 168, 40, true } }) {
        PhoneHarness h;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        Themed(h, [&] {
            behavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
                TwoRowsTopAppBarOptions o;
                o.navigationIcon = [] { FakeIcon(Tag({}, "navigationIcon")); };
                o.actions = [] { FakeIcon(Tag({}, "actions")); };
                o.windowInsets = WindowInsets::Fixed(13_dp, 13_dp, 13_dp, 13_dp);
                o.scrollBehavior = behavior;
                if (k.custom) {
                    o.collapsedHeight = ui::Dp(k.minHeight);
                    o.expandedHeight = ui::Dp(k.maxHeight);
                }
                TwoRowBar(k.kind, TitleText(), {}, o);
            });
        });
        const float full = (k.maxHeight - k.minHeight) * 2, partial = full / 3;
        behavior->State().SetHeightOffset(-partial);
        behavior->State().SetContentOffset(-partial);
        h.AdvanceTimeBy(16);
        auto bar = BoundsDp(h, "topAppBar");
        CHECK(bar && Near(bar->Height(), k.maxHeight - partial / 2 + 26));
        behavior->State().SetHeightOffset(-full);
        behavior->State().SetContentOffset(-full - partial);
        h.AdvanceTimeBy(16);
        bar = BoundsDp(h, "topAppBar");
        CHECK(bar && Near(bar->Height(), k.minHeight + 26));
    }
}

TEST(TwoRowTopAppBarsHideTheBottomTitleWhenCollapsed) {
    // assertMediumOrLargeScrolledColors without the pixels: the title's colour, and the bottom
    // title gone once collapsed.
    for (auto kind : { TwoRow::Medium, TwoRow::MediumFlexible, TwoRow::Large, TwoRow::LargeFlexible }) {
        PhoneHarness h;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        std::vector<Color> seen;
        Themed(h, [&] {
            behavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            TwoRowsTopAppBarOptions o;
            o.scrollBehavior = behavior;
            o.windowInsets = WindowInsets{};
            TwoRowBar(kind, [&] { seen.push_back(rt::Composition::Current()->Read(LocalContentColor)); Text("Title", Tag({}, "title")); },
                      Tag({}, "topAppBar"), o);
        });
        CHECK(seen.size() >= 2);
        CHECK(Displayed(h, "title"));
        const float full = (ExpandedHeight(kind) - 64) * 2;
        behavior->State().SetHeightOffset(-full);
        behavior->State().SetContentOffset(-full);
        h.AdvanceTimeBy(16);
        CHECK_EQ(behavior->State().CollapsedFraction(), 1.0f);
        const auto titles = AllTaggedBoundsDp(h, "title");
        CHECK(titles.size() == 2 && titles[1].Height() >= 0);
        // The bottom row has no height left.
        const auto bar = BoundsDp(h, "topAppBar");
        CHECK(bar && Near(bar->Height(), 64));
    }
}

TEST(TopAppBarColorsTakePositionalArguments) {
    PhoneHarness h;
    TopAppBarColors c{};
    Themed(h, [&] {
        c = TopAppBarDefaults::topAppBarColors(Color(0xFF0000FFu), Color(0xFF00FF00u), Color(0xFFFF0000u), Color(0xFFFFFF00u), Color(0xFF00FFFFu),
                                               Color(0xFFFF00FFu));
    });
    CHECK(c.containerColor == Color(0xFF0000FFu) && c.scrolledContainerColor == Color(0xFF00FF00u));
    CHECK(c.navigationIconContentColor == Color(0xFFFF0000u) && c.titleContentColor == Color(0xFFFFFF00u));
    CHECK(c.actionIconContentColor == Color(0xFF00FFFFu) && c.subtitleContentColor == Color(0xFFFF00FFu));
}

TEST(TwoRowTopAppBarsExposeOneTitle) {
    // assertMediumOrLargeScrolledSemantics: two title nodes laid out, one in the semantics tree,
    // expanded, a third collapsed and fully collapsed.
    for (auto kind : { TwoRow::Medium, TwoRow::MediumFlexible, TwoRow::Large, TwoRow::LargeFlexible }) {
        const bool subtitle = kind == TwoRow::MediumFlexible || kind == TwoRow::LargeFlexible;
        PhoneHarness h;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        Themed(h, [&] {
            behavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            TwoRowsTopAppBarOptions o;
            o.scrollBehavior = behavior;
            if (subtitle) o.subtitle = [] { Text("Subtitle", Tag({}, "subtitle")); };
            TwoRowBar(kind, TitleText(), Tag({}, "topAppBar"), o);
        });
        const float full = (kind == TwoRow::Medium || kind == TwoRow::MediumFlexible ? 112 - 64 : 152 - 64) * 2.0f;
        for (float offset : { 0.0f, full / 3, full }) {
            behavior->State().SetHeightOffset(-offset);
            behavior->State().SetContentOffset(-offset);
            h.AdvanceTimeBy(16);
            CHECK_EQ(AllTaggedBoundsDp(h, "title").size(), std::size_t(2));
            CHECK_EQ(CountMerged(h, "title"), 1);
            if (subtitle) {
                CHECK_EQ(AllTaggedBoundsDp(h, "subtitle").size(), std::size_t(2));
                CHECK_EQ(CountMerged(h, "subtitle"), 1);
            }
        }
    }
}

// AppBarTest: scroll behaviours, drags, intrinsics, restoration.
namespace {
    // A finger from (x0, y0) to (x1, y1) in `steps` moves, `stepMs` apart (px), then up.
    void Swipe(Harness& h, std::int64_t& t, float x0, float y0, float x1, float y1, int steps = 10, std::int64_t stepMs = 10, bool release = true) {
        Touch(h, t, ui::PointerEventType::Press, x0, y0, true);
        for (int i = 1; i <= steps; ++i) {
            const float f = float(i) / float(steps);
            t += stepMs - 16;   // Touch adds 16
            Touch(h, t, ui::PointerEventType::Move, x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, true);
        }
        if (release) Touch(h, t, ui::PointerEventType::Release, x1, y1, false);
    }
    // MultiPageContent: two pages side by side in a lazy row, each a lazy column of 50 items.
    void MultiPageContent(std::shared_ptr<TopAppBarScrollBehavior> behavior, LazyListState& state, std::vector<std::unique_ptr<LazyListState>>& pages) {
        ScaffoldOptions o;
        o.topBar = [behavior] {
            TopAppBarOptions bar;
            bar.scrollBehavior = behavior;
            TopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), bar);
        };
        Scaffold(NestedScroll({}, behavior->NestedScrollConnection()), o, [&](const PaddingValues& padding) {
            LazyRow(Tag(FillMax({}, Axis::Both), "lazyList"), state, [&, padding](LazyListScope& s) {
                s.Items(2, [&, padding](int page) {
                    LazyColumn(FillParentMaxSize({}), *pages[std::size_t(page)], [page](LazyListScope& c) {
                        c.Items(50, [page](int i) { Text("Item #" + std::to_string(page) + " x " + std::to_string(i), FillParentMaxWidth({})); });
                    }, { .contentPadding = padding });
                });
            });
        });
    }
}

TEST(TopAppBarBehavioursLetContentScrollSideways) {
    for (int kind = 0; kind < 3; ++kind) {
        PhoneHarness h;
        std::int64_t t = 1000;
        LazyListState state;
        std::vector<std::unique_ptr<LazyListState>> pages;
        pages.push_back(std::make_unique<LazyListState>());
        pages.push_back(std::make_unique<LazyListState>());
        Themed(h, [&] {
            const auto behavior = kind == 0 ? TopAppBarDefaults::enterAlwaysScrollBehavior()
                                : kind == 1 ? TopAppBarDefaults::exitUntilCollapsedScrollBehavior()
                                            : TopAppBarDefaults::pinnedScrollBehavior();
            MultiPageContent(behavior, state, pages);
        });
        // swipeLeft / swipeRight across the middle.
        Swipe(h, t, 780, 800, 40, 800);
        h.AdvanceTimeBy(2000);
        CHECK_EQ(state.FirstVisibleItemIndex(), 1);
        Swipe(h, t, 40, 800, 780, 800);
        h.AdvanceTimeBy(2000);
        CHECK_EQ(state.FirstVisibleItemIndex(), 0);
    }
}

TEST(TopAppBarDrags) {
    {
        // Pinned: dragging the bar does nothing.
        PhoneHarness h;
        std::int64_t t = 1000;
        Themed(h, [&] {
            TopAppBarOptions o;
            o.scrollBehavior = TopAppBarDefaults::pinnedScrollBehavior();
            o.windowInsets = WindowInsets{};
            TopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), o);
        });
        Swipe(h, t, 10, 64, 10, 0, 10, 10, false);
        h.AdvanceTimeBy(1000);
        const auto b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 64));
    }
    {
        // A medium bar dragged all the way up collapses to the small height.
        PhoneHarness h;
        std::int64_t t = 1000;
        Themed(h, [&] {
            TwoRowsTopAppBarOptions o;
            o.scrollBehavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            o.windowInsets = WindowInsets{};
            MediumTopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), o);
        });
        auto b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 112));
        Swipe(h, t, 10, 224 - 20, 10, 0, 10, 10, false);
        h.AdvanceTimeBy(1000);
        b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 64));
    }
}

TEST(LargeTopAppBarSnapsAfterADrag) {
    {
        PhoneHarness h;
        std::int64_t t = 1000;
        Themed(h, [&] {
            TwoRowsTopAppBarOptions o;
            o.scrollBehavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            o.windowInsets = WindowInsets{};
            LargeTopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), o);
        });
        auto b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 152));
        // A little: back to expanded.
        Swipe(h, t, 10, 304 - 20, 10, 304 - 40, 4, 50);
        h.AdvanceTimeBy(2000);
        b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 152));
        // Most of the way: on to collapsed.
        Swipe(h, t, 10, 304 - 20, 10, 40, 10, 50);
        h.AdvanceTimeBy(2000);
        b = BoundsDp(h, "topAppBar");
        CHECK(b && Near(b->Height(), 64));
    }
    // No snap: it stays where the drag left it.
    PhoneHarness h;
    std::int64_t t = 1000;
    Themed(h, [&] {
        TwoRowsTopAppBarOptions o;
        o.scrollBehavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior(nullptr, {}, anim::SpecPtr(nullptr));
        o.windowInsets = WindowInsets{};
        LargeTopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), o);
    });
    Swipe(h, t, 100, 304 - 20, 100, 304 - 100, 8, 50);
    h.AdvanceTimeBy(2000);
    const auto b = BoundsDp(h, "topAppBar");
    CHECK(b && b->Height() < 152 && b->Height() > 64);
}

TEST(EnterAlwaysTopAppBarMovesTheContentWithIt) {
    // Collapsed by a long swipe up, the bar comes back with a slow swipe down of its height, and
    // the content moves down with it.
    PhoneHarness h;
    std::int64_t t = 1000;
    LazyListState state;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    Themed(h, [&] {
        behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
        ScaffoldOptions o;
        o.contentWindowInsets = WindowInsets{};
        o.topBar = [&] {
            TopAppBarOptions bar;
            bar.scrollBehavior = behavior;
            bar.windowInsets = WindowInsets{};
            TopAppBar([] { Text("Title"); }, {}, bar);
        };
        Scaffold(FillMax({}, Axis::Both), o, [&](const PaddingValues& padding) {
            LazyColumn(Tag(Padding(NestedScroll(FillMax({}, Axis::Both), behavior->NestedScrollConnection()), padding), "lazyList"), state,
                       [](LazyListScope& s) {
                           s.Items(100, [](int i) { Text("Item " + std::to_string(i), Padding(Height(FillMax({}, Axis::Horizontal), 40_dp), 16_dp, 0_dp)); });
                       });
        });
    });
    Swipe(h, t, 400, 1600 * 0.8f, 400, 1600 - 1000, 10, 20);
    h.AdvanceTimeBy(3000);
    CHECK(behavior->State().CollapsedFraction() == 1.0f);
    const auto info = state.LayoutInfo();
    CHECK(!info.visibleItemsInfo.empty());
    if (info.visibleItemsInfo.empty()) return;
    const std::string tracked = "Item " + std::to_string(info.visibleItemsInfo.front().index + 2);
    const auto before = BoundsDp(h, tracked);
    // swipeDown(durationMillis = 2000) by the bar's height.
    Swipe(h, t, 400, 1600 * 0.2f, 400, 1600 * 0.2f + 128, 40, 50);
    h.AdvanceTimeBy(3000);
    const auto after = BoundsDp(h, tracked);
    CHECK(before && after && Near(after->top, before->top + 64));
}

TEST(TopAppBarStateRestores) {
    rt::SaveableStateRegistry::Values saved;
    for (int pass = 0; pass < 2; ++pass) {
        rt::SaveableStateRegistry registry(saved);
        std::shared_ptr<TopAppBarState> state;
        Harness h;
        h.Run([&] { rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, &registry, [&] { state = RememberTopAppBarState(); }); });
        CHECK(state != nullptr);
        if (!state) return;
        if (pass == 0) {
            state->SetHeightOffsetLimit(-350);
            state->SetHeightOffset(-300);
            state->SetContentOffset(-550);
            saved = rt::SaveableStateRegistry::Deserialize(rt::SaveableStateRegistry::Serialize(registry.PerformSave()));
        } else {
            CHECK_EQ(state->HeightOffsetLimit(), -350.0f);
            CHECK_EQ(state->HeightOffset(), -300.0f);
            CHECK_EQ(state->ContentOffset(), -550.0f);
        }
    }
}

TEST(TopAppBarIntrinsicSizes) {
    PhoneHarness h;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    Themed(h, [&] {
        behavior = TopAppBarDefaults::enterAlwaysScrollBehavior();
        Column({}, [&] {
            for (auto size : { IntrinsicSize::Min, IntrinsicSize::Max }) {
                Row(Tag(Height({}, size), size == IntrinsicSize::Min ? "row0" : "row1"), [&] {
                    TopAppBarOptions o;
                    o.expandedHeight = 50_dp;
                    o.scrollBehavior = behavior;
                    o.windowInsets = WindowInsets{};
                    TopAppBar([] { Text("Title"); }, {}, o);
                });
            }
            for (auto size : { IntrinsicSize::Min, IntrinsicSize::Max }) {
                const bool first = size == IntrinsicSize::Min;
                Row(Tag(Width({}, size), first ? "wrow0" : "wrow1"), [&, first] {
                    TopAppBarOptions o;
                    o.navigationIcon = [first] { FakeIcon(first ? Tag({}, "navigationIcon") : ui::Modifier{}); };
                    o.actions = [first] { FakeIcon(first ? Tag({}, "actions") : ui::Modifier{}); };
                    o.expandedHeight = 50_dp;
                    o.scrollBehavior = behavior;
                    o.windowInsets = WindowInsets{};
                    TopAppBar([first] { Text("Title", first ? Tag({}, "title") : ui::Modifier{}); }, FillMax({}, Axis::Horizontal), o);
                });
            }
        });
    });
    CHECK_EQ(behavior->State().HeightOffsetLimit(), -100.0f);
    for (const char* row : { "row0", "row1" }) {
        const auto b = BoundsDp(h, row);
        CHECK(b && Near(b->Height(), 50));
    }
    const auto nav = BoundsDp(h, "navigationIcon"), title = BoundsDp(h, "title"), actions = BoundsDp(h, "actions");
    CHECK(nav && title && actions);
    if (!nav || !title || !actions) return;
    const float intrinsic = 16 + nav->Width() + title->Width() + actions->Width();
    for (const char* row : { "wrow0", "wrow1" }) {
        const auto b = BoundsDp(h, row);
        CHECK(b && std::abs(b->Width() - intrinsic) <= 1);
    }
}

TEST(CustomTwoRowsTopAppBar) {
    {
        // Expanded: the collapsed and expanded titles each where a medium bar puts them.
        PhoneHarness h;
        Themed(h, [&] {
            Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [] {
                TwoRowsTopAppBarOptions o;
                o.navigationIcon = [] { FakeIcon(Tag({}, "navigationIcon")); };
                o.actions = [] { FakeIcon(Tag({}, "actions")); };
                o.collapsedHeight = 36_dp;
                o.expandedHeight = 112_dp;
                o.windowInsets = WindowInsets{};
                TwoRowsTopAppBar([](bool expanded) { Text(expanded ? "Expanded Title" : "Collapsed Title", Tag({}, "title")); }, {}, {}, o);
            });
        });
        const auto bar = BoundsDp(h, "topAppBar");
        const auto titles = AllTaggedBoundsDp(h, "title");
        CHECK(bar && titles.size() == 2 && Near(bar->Height(), 112));
        if (bar && titles.size() == 2) {
            CHECK(Near(titles[0].left, 4 + 48 + 4) && Near(titles[0].top, (bar->top + 36 - titles[0].Height()) / 2));
            CHECK(Near(titles[1].left, 16));
        }
        const auto nav = UnclippedBoundsDp(h, "navigationIcon");
        CHECK(nav && Near(nav->left, 4) && Near(nav->top, 36 - (36 - 48) / 2.0f - 48));
    }
    // Scrolled: from 120 dp to 40 dp, with 13 dp insets.
    PhoneHarness h;
    std::shared_ptr<TopAppBarScrollBehavior> behavior;
    Themed(h, [&] {
        behavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
        Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&] {
            TwoRowsTopAppBarOptions o;
            o.collapsedHeight = 40_dp;
            o.expandedHeight = 120_dp;
            o.windowInsets = WindowInsets::Fixed(13_dp, 13_dp, 13_dp, 13_dp);
            o.scrollBehavior = behavior;
            TwoRowsTopAppBar([](bool expanded) { Text(expanded ? "Expanded Title" : "Collapsed Title", Tag({}, "title")); }, {}, {}, o);
        });
    });
    const float full = 80 * 2, partial = full / 3;
    behavior->State().SetHeightOffset(-partial);
    h.AdvanceTimeBy(16);
    auto bar = BoundsDp(h, "topAppBar");
    CHECK(bar && Near(bar->Height(), 120 - partial / 2 + 26));
    behavior->State().SetHeightOffset(-full);
    h.AdvanceTimeBy(16);
    bar = BoundsDp(h, "topAppBar");
    CHECK(bar && Near(bar->Height(), 40 + 26));
}

TEST(TopAppBarContentPaddingUnderAStatusBar) {
    // The parent pads by the status bar; the bar adds its content padding to its height.
    PhoneHarness h;
    h.owner.Insets().Set(ui::InsetsType::StatusBars, ui::InsetsValue{ 0, 20, 0, 0 });
    Themed(h, [&] {
        Column(WindowInsetsPadding(FillMax({}, Axis::Both), WindowInsets::StatusBars()), [] {
            TopAppBarOptions o;
            o.contentPadding = PaddingValues{ 0_dp, 20_dp, 0_dp, 0_dp };
            TopAppBar([] { Text("Title"); }, Tag({}, "topAppBar"), o);
            Box(Tag(WindowInsetsPadding(Height(FillMax({}, Axis::Horizontal), 40_dp), WindowInsets::StatusBars()), "box"), Alignment::Center(), false,
                [] { Text("Sibling Box"); });
        });
    });
    const auto bar = BoundsDp(h, "topAppBar"), box = BoundsDp(h, "box");
    CHECK(bar && Near(bar->top, 10) && Near(bar->Height(), 64 + 20));
    CHECK(box && Near(box->Height(), 40) && Near(box->top, 20 + 64 + 10));
}

// AppBarTest: bottom app bars.
namespace {
    void BarFab(ui::Modifier m = {}) {
        FloatingActionButtonOptions o;
        o.containerColor = BottomAppBarDefaults::bottomAppBarFabColor();
        o.elevation = FloatingActionButtonDefaults::bottomAppBarFabElevation();
        FloatingActionButton([] {}, std::move(m), o, [] { NavIcon({}, "Localized description"); });
    }
}

TEST(BottomAppBarHeights) {
    const auto measure = [](std::function<void()> bar) {
        PhoneHarness h;
        Themed(h, [&] { Box(Tag({}, "box"), Alignment::TopStart(), false, bar); });
        return BoundsDp(h, "box");
    };
    BottomAppBarOptions none;
    none.windowInsets = WindowInsets{};
    auto b = measure([&] { BottomAppBar([] {}, {}, [] { BarFab(); }, none); });
    CHECK(b && Near(b->Height(), 80) && Near(b->Width(), 412));
    b = measure([&] { BottomAppBar({}, none, [] {}); });
    CHECK(b && Near(b->Height(), 80) && Near(b->Width(), 412));
    FlexibleBottomAppBarOptions flexible;
    flexible.windowInsets = WindowInsets{};
    flexible.horizontalArrangement = BottomAppBarDefaults::FlexibleFixedHorizontalArrangement();
    b = measure([&] { FlexibleBottomAppBar({}, flexible, [] {}); });
    CHECK(b && Near(b->Height(), 64) && Near(b->Width(), 412));
    flexible.expandedHeight = 128_dp;
    b = measure([&] { FlexibleBottomAppBar({}, flexible, [] {}); });
    CHECK(b && Near(b->Height(), 128));
    BottomAppBarOptions inset;
    inset.windowInsets = WindowInsets::Fixed(10_dp, 10_dp, 10_dp, 10_dp);
    b = measure([&] { BottomAppBar([] {}, {}, [] { BarFab(); }, inset); });
    CHECK(b && std::abs(b->Height() - 100) <= 1);
}

TEST(BottomAppBarKeepsItsFabWhenActionsOverflow) {
    PhoneHarness h;
    Themed(h, [&] {
        BottomAppBar([] { for (int i = 0; i < 20; ++i) FakeIcon({}); }, {}, [] { BarFab(Tag({}, "FAB")); });
    });
    CHECK(Displayed(h, "FAB"));
}

TEST(BottomAppBarPlacesItsContent) {
    for (bool customPadding : { false, true }) {
        PhoneHarness h;
        Themed(h, [&] {
            BottomAppBarOptions o;
            o.windowInsets = WindowInsets{};
            if (customPadding) o.contentPadding = PaddingValues{ 3_dp, 5_dp, 0_dp, 0_dp };
            BottomAppBar(Tag({}, "bar"), o, [] { FakeIcon(Tag({}, "icon")); });
        });
        const auto bar = BoundsDp(h, "bar"), icon = BoundsDp(h, "icon");
        CHECK(bar && icon);
        if (!bar || !icon) continue;
        const float top = customPadding ? 5 : 4;
        CHECK(Near(icon->left, customPadding ? 3 : 4));
        CHECK(Near(icon->top, top + (bar->bottom - top - 48) / 2));
    }
    // The FAB 16 dp from the end, 12 dp from the top.
    PhoneHarness h;
    Themed(h, [&] { BottomAppBar([] {}, Tag({}, "bar"), [] { BarFab(Tag({}, "FAB")); }); });
    const auto bar = BoundsDp(h, "bar"), fab = BoundsDp(h, "FAB");
    CHECK(bar && fab && Near(fab->left, bar->Width() - 16 - fab->Width()) && Near(fab->top, bar->top + 12));
}

TEST(ExitAlwaysBottomAppBarInAScaffold) {
    for (bool scrolled : { false, true }) {
        PhoneHarness h;
        std::shared_ptr<BottomAppBarScrollBehavior> behavior;
        Themed(h, [&] {
            behavior = BottomAppBarDefaults::exitAlwaysScrollBehavior();
            ScaffoldOptions o;
            o.contentWindowInsets = WindowInsets{};
            o.bottomBar = [&] {
                BottomAppBarOptions bar;
                bar.windowInsets = WindowInsets{};
                bar.scrollBehavior = behavior;
                BottomAppBar(Tag({}, "bottomAppBar"), bar, [] {});
            };
            o.floatingActionButton = [] { FloatingActionButton([] {}, Tag(OffsetMod({}, 0_dp, 4_dp), "FAB"), {}, [] {}); };
            o.floatingActionButtonPosition = FabPosition::EndOverlay;
            Scaffold(NestedScroll(FillMax({}, Axis::Both), behavior->NestedScrollConnection()), o, [](const PaddingValues&) {});
        });
        if (scrolled) {
            behavior->State().SetHeightOffset(-40);
            behavior->State().SetContentOffset(-40);
            h.AdvanceTimeBy(16);
            const auto bar = BoundsDp(h, "bottomAppBar");
            CHECK(bar && Near(bar->Height(), 80 - 20));
        }
        const auto bar = BoundsDp(h, "bottomAppBar"), fab = BoundsDp(h, "FAB");
        CHECK(bar && fab && Near(fab->left, bar->Width() - 16 - fab->Width()) && Near(fab->top, 800 - 12 - fab->Height()));
    }
}

TEST(ExitAlwaysBottomAppBarLetsContentScrollSideways) {
    PhoneHarness h;
    std::int64_t t = 1000;
    LazyListState state;
    std::vector<std::unique_ptr<LazyListState>> pages;
    pages.push_back(std::make_unique<LazyListState>());
    pages.push_back(std::make_unique<LazyListState>());
    Themed(h, [&] {
        const auto behavior = BottomAppBarDefaults::exitAlwaysScrollBehavior();
        ScaffoldOptions o;
        o.bottomBar = [behavior] {
            BottomAppBarOptions bar;
            bar.scrollBehavior = behavior;
            BottomAppBar(Tag({}, "bottomAppBar"), bar, [] {});
        };
        Scaffold(NestedScroll({}, behavior->NestedScrollConnection()), o, [&](const PaddingValues& padding) {
            LazyRow(FillMax({}, Axis::Both), state, [&, padding](LazyListScope& s) {
                s.Items(2, [&, padding](int page) {
                    LazyColumn(SizeMod({}, 412_dp, 800_dp), *pages[std::size_t(page)], [page](LazyListScope& c) {
                        c.Items(50, [page](int i) { Text("Item #" + std::to_string(page) + " x " + std::to_string(i), FillMax({}, Axis::Horizontal)); });
                    }, { .contentPadding = padding });
                });
            });
        });
    });
    Swipe(h, t, 780, 800, 40, 800);
    h.AdvanceTimeBy(2000);
    CHECK_EQ(state.FirstVisibleItemIndex(), 1);
    Swipe(h, t, 40, 800, 780, 800);
    h.AdvanceTimeBy(2000);
    CHECK_EQ(state.FirstVisibleItemIndex(), 0);
}

TEST(ExitAlwaysBottomAppBarHandlesAnOutOfRangeOffset) {
    PhoneHarness h;
    Themed(h, [&] {
        const auto behavior = BottomAppBarDefaults::exitAlwaysScrollBehavior();
        behavior->State().SetHeightOffset(-1000);
        ScaffoldOptions o;
        o.bottomBar = [behavior] {
            BottomAppBarOptions bar;
            bar.scrollBehavior = behavior;
            BottomAppBar(Tag({}, "bottomAppBar"), bar, [] {});
        };
        Scaffold(NestedScroll({}, behavior->NestedScrollConnection()), o, [](const PaddingValues& p) { Box(Padding({}, p), Alignment::TopStart(), false, {}); });
    });
    h.AdvanceTimeBy(16);
    const auto bar = UnclippedBoundsDp(h, "bottomAppBar");
    CHECK(bar && Near(bar->Height(), 0));
}

// FloatingToolbarTest.
namespace {
    void ToolbarFab() {
        FloatingToolbarDefaults::StandardFloatingActionButton([] {}, Tag({}, "floatingActionButton"), {}, [] { NavIcon({}, "Localized description"); });
    }
    void ToolbarIcon(ui::Modifier m = {}) { IconButton([] {}, std::move(m), {}, [] { NavIcon({}, "Localized description"); }); }
    void ToolbarContent() {
        ToolbarIcon();
        ToolbarIcon();
        ToolbarIcon();
        ToolbarIcon(Tag({}, "floatingToolbarContentLastItem"));
    }
    // setMaterialContentForSizeAssertions: the toolbar's own size, unconstrained by the window.
    std::optional<ui::Rect> ToolbarSize(std::function<void()> toolbar) {
        PhoneHarness h;
        Themed(h, [&] { Box(Tag({}, "sized"), Alignment::TopStart(), false, toolbar); });
        h.AdvanceTimeBy(1000);
        return BoundsDp(h, "sized");
    }
}

TEST(FloatingToolbarScrolledPositioning) {
    // The limit is the toolbar's size plus how far it sits from its parent's exit edge; an offset
    // moves it towards that edge.
    for (bool vertical : { false, true }) {
        for (bool custom : { false, true }) {
            PhoneHarness h;
            std::shared_ptr<FloatingToolbarScrollBehavior> behavior;
            Themed(h, [&] {
                const auto dir = vertical ? FloatingToolbarExitDirection::End : FloatingToolbarExitDirection::Bottom;
                if (custom)
                    behavior = FloatingToolbarDefaults::exitAlwaysScrollBehavior(dir, std::make_shared<FloatingToolbarState>(-FLT_MAX, 0.0f, 0.0f),
                                                                                 anim::Spring(), std::make_shared<anim::SplineDecay>(2.0f));
                else
                    behavior = FloatingToolbarDefaults::exitAlwaysScrollBehavior(dir);
                FloatingToolbarOptions o;
                o.scrollBehavior = behavior;
                o.shape = RectangleShape();
                const auto content = [] { IconButton([] {}, Tag(SizeMod({}, 48_dp, 48_dp), "icon"), {}, [] { NavIcon({}, "Localized description"); }); };
                // setMaterialContent's Surface: a parent the size of the toolbar.
                Box({}, Alignment::TopStart(), false, [=] {
                    if (vertical) VerticalFloatingToolbar(false, OffsetMod(Tag({}, "floatingToolbar"), Dp(-16), 0_dp), o, content);
                    else HorizontalFloatingToolbar(false, OffsetMod(Tag({}, "floatingToolbar"), 0_dp, Dp(-16)), o, content);
                });
            });
            CHECK_EQ(behavior->State().OffsetLimit(), -(64.0f + 16.0f) * 2);
            const auto before = UnclippedBoundsDp(h, "icon");
            behavior->State().SetOffset(-40);
            behavior->State().SetContentOffset(-40);
            h.AdvanceTimeBy(16);
            const auto after = UnclippedBoundsDp(h, "icon");
            CHECK(before && after);
            if (!before || !after) continue;
            if (vertical) CHECK(Near(after->left, before->left + 20) && Near(after->top, before->top));
            else CHECK(Near(after->top, before->top + 20) && Near(after->left, before->left));
        }
    }
}

TEST(FloatingToolbarInteractiveSizeDoesNotResizeToolbar) {
    const auto b = ToolbarSize([] {
        FloatingToolbarOptions o;
        o.collapsedShadowElevation = 5_dp;
        HorizontalFloatingToolbar(false, {}, o, [] {
            for (const char* text : { "Long Text", "Y", "M3", "M", "W", "D" }) Box(MinimumInteractiveComponentSize({}), Alignment::TopStart(), false, [text] { Text(text); });
        });
    });
    CHECK(b && Near(b->Height(), 64));
}

TEST(FloatingToolbarCustomContentPadding) {
    for (bool vertical : { false, true }) {
        PhoneHarness h;
        Themed(h, [&] {
            FloatingToolbarOptions o;
            o.contentPadding = PaddingValues{ 20_dp, 20_dp, 20_dp, 20_dp };
            const auto content = [] { ToolbarIcon(Tag({}, "child")); };
            if (vertical) VerticalFloatingToolbar(false, Tag({}, "floatingToolbar"), o, content);
            else HorizontalFloatingToolbar(false, Tag({}, "floatingToolbar"), o, content);
        });
        // onChild(): the content's touch target.
        const auto child = UnclippedBoundsDp(h, "child");
        CHECK(child && Near(vertical ? child->left : child->top, 20));
    }
}

TEST(FloatingToolbarLeadingAndTrailingContentShowOnlyExpanded) {
    // Padding 8 on each side; a 48 dp icon button for each of leading and trailing content shown.
    for (bool vertical : { false, true }) {
        for (int which = 0; which < 3; ++which) {   // trailing, leading, both
            for (bool expanded : { true, false }) {
                const auto b = ToolbarSize([&] {
                    FloatingToolbarOptions o;
                    if (which != 1) o.trailingContent = [] { ToolbarIcon(); };
                    if (which != 0) o.leadingContent = [] { ToolbarIcon(); };
                    if (vertical) VerticalFloatingToolbar(expanded, {}, o, [] {});
                    else HorizontalFloatingToolbar(expanded, {}, o, [] {});
                });
                CHECK(b.has_value());
                if (!b) continue;
                const float along = vertical ? b->Height() : b->Width(), across = vertical ? b->Width() : b->Height();
                const int shown = expanded ? (which == 2 ? 2 : 1) : 0;
                CHECK(Near(along, 16 + 48.0f * float(shown)));
                if (expanded) CHECK(Near(across, 16 + 48));
            }
        }
    }
}

TEST(FloatingToolbarStateRestores) {
    rt::SaveableStateRegistry::Values saved;
    for (int pass = 0; pass < 2; ++pass) {
        rt::SaveableStateRegistry registry(saved);
        std::shared_ptr<FloatingToolbarState> state;
        Harness h;
        h.Run([&] { rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, &registry, [&] { state = RememberFloatingToolbarState(); }); });
        CHECK(state != nullptr);
        if (!state) return;
        if (pass == 0) {
            state->SetOffsetLimit(-350);
            state->SetOffset(-300);
            state->SetContentOffset(-550);
            saved = rt::SaveableStateRegistry::Deserialize(rt::SaveableStateRegistry::Serialize(registry.PerformSave()));
        } else {
            CHECK_EQ(state->OffsetLimit(), -350.0f);
            CHECK_EQ(state->Offset(), -300.0f);
            CHECK_EQ(state->ContentOffset(), -550.0f);
        }
    }
}

TEST(FloatingToolbarWithFabExpands) {
    // Collapsed: the large FAB and no toolbar; expanded: the toolbar unrolls and the FAB shrinks.
    // The component keeps its size: the widest toolbar, the gap and the small FAB.
    for (bool vertical : { false, true }) {
        PhoneHarness h;
        rt::State<bool> expanded(false);
        Themed(h, [&] {
            if (vertical) VerticalFloatingToolbar(expanded.Get(), ToolbarFab, Tag({}, "floatingToolbar"), {}, ToolbarContent);
            else HorizontalFloatingToolbar(expanded.Get(), ToolbarFab, Tag({}, "floatingToolbar"), {}, ToolbarContent);
        });
        const float component = 80 + 48 * 4;
        auto fab = BoundsDp(h, "floatingActionButton");
        CHECK(fab && Near(fab->Width(), 80) && Near(fab->Height(), 80));
        CHECK(!Displayed(h, "floatingToolbarContentLastItem"));
        auto bar = BoundsDp(h, "floatingToolbar");
        CHECK(bar && Near(vertical ? bar->Height() : bar->Width(), component));
        expanded.Set(true);
        h.AdvanceTimeBy(2000);
        fab = BoundsDp(h, "floatingActionButton");
        CHECK(fab && Near(fab->Width(), 56) && Near(fab->Height(), 56));
        CHECK(Displayed(h, "floatingToolbarContentLastItem"));
        bar = BoundsDp(h, "floatingToolbar");
        CHECK(bar && Near(vertical ? bar->Height() : bar->Width(), component));
    }
}

TEST(FloatingToolbarWithFabContentPadding) {
    for (bool vertical : { false, true }) {
        for (int padding : { -1, vertical ? 64 : 48 }) {   // -1: the default
            PhoneHarness h;
            Themed(h, [&] {
                FloatingToolbarWithFabOptions o;
                o.shape = RectangleShape();
                if (padding >= 0) {
                    const Dp p{ float(padding) };
                    o.contentPadding = vertical ? PaddingValues{ 0_dp, p, 0_dp, p } : PaddingValues{ p, 0_dp, p, 0_dp };
                }
                if (vertical) VerticalFloatingToolbar(true, ToolbarFab, Tag({}, "floatingToolbar"), o, ToolbarContent);
                else HorizontalFloatingToolbar(true, ToolbarFab, Tag({}, "floatingToolbar"), o, ToolbarContent);
            });
            h.AdvanceTimeBy(1000);
            const float pad = padding < 0 ? 16 : float(padding) * 2;
            const auto bar = BoundsDp(h, "floatingToolbar");
            CHECK(bar && Near(vertical ? bar->Height() : bar->Width(), pad + 56 + 48 * 4 + 8));
        }
    }
}

namespace {
    // VerticalNestedScrollTestContent: an 80 dp header over a lazy list of 100 rows.
    void VerticalNestedScrollTestContent(bool* expanded, bool enabled, bool initialValue, bool reverseLayout) {
        ui::Modifier m = FillMax({}, Axis::Both);
        if (enabled)
            m = FloatingToolbarDefaults::FloatingToolbarVerticalNestedScroll(std::move(m), initialValue, [expanded] { *expanded = true; },
                                                                             [expanded] { *expanded = false; }, FloatingToolbarDefaults::ScrollDistanceThreshold,
                                                                             FloatingToolbarDefaults::ScrollDistanceThreshold, reverseLayout);
        Column(m, [reverseLayout] {
            Box(Height(FillMax({}, Axis::Horizontal), 80_dp));
            LazyColumn(Tag(Weight(FillMax({}, Axis::Horizontal), 1), "mainLayout"), RememberLazyListState(), [](LazyListScope& s) {
                s.Items(100, [](int i) {
                    Box(Height(FillMax({}, Axis::Horizontal), 60_dp), Alignment::TopStart(), false, [i] { Text(std::to_string(i)); });
                    Spacer(Height({}, 8_dp));
                });
            }, { .reverseLayout = reverseLayout });
        });
    }
    // performTouchInput { swipeUp / swipeDown(startY, endY) } on the main layout: 200 ms, in its
    // local dp.
    void SwipeMain(Harness& h, std::int64_t& t, float fromY, float toY) {
        const auto b = BoundsDp(h, "mainLayout");
        CHECK(b.has_value());
        if (!b) return;
        const float x = (b->left + b->right) / 2 * 2;
        Swipe(h, t, x, (b->top + fromY) * 2, x, (b->top + toY) * 2, 10, 20);
        h.AdvanceTimeBy(2000);
    }
}

TEST(FloatingToolbarVerticalNestedScrollFollowsSwipes) {
    // Each case: two swipes on the list (up: bottom to centre; down: top to centre) and the
    // expansion after each.
    struct Case { bool enabled, initial, reverse, upFirst, after1, after2; };
    for (const Case k : { Case{ true, true, false, true, false, true },     // verticalSwipesUpdateValue
                          Case{ true, true, true, false, false, true },     // reversed: down scrolls forward
                          Case{ false, true, true, true, true, true },      // disableScrollInterception
                          Case{ true, false, false, true, false, true } }) { // falseInitialValue
        PhoneHarness h;
        std::int64_t t = 1000;
        bool expanded = k.initial;
        Themed(h, [&] { VerticalNestedScrollTestContent(&expanded, k.enabled, k.initial, k.reverse); });
        const auto b = BoundsDp(h, "mainLayout");
        CHECK(b.has_value());
        if (!b) continue;
        const float bottom = b->Height() - 0.5f, centre = b->Height() / 2, top = 0.5f;
        const auto up = [&] { SwipeMain(h, t, bottom, centre); };
        const auto down = [&] { SwipeMain(h, t, top, centre); };
        if (k.upFirst) up(); else down();
        CHECK_EQ(expanded, k.after1);
        if (k.upFirst) down(); else up();
        CHECK_EQ(expanded, k.after2);
    }
}

TEST(FloatingToolbarVerticalNestedScrollThreshold) {
    // Less than the threshold keeps it; more collapses (thresholds in dp; the test's custom one
    // only scales the swipes, as in Compose's).
    for (float threshold : { 40.0f, 100.0f }) {
        PhoneHarness h;
        std::int64_t t = 1000;
        bool expanded = true;
        Themed(h, [&] { VerticalNestedScrollTestContent(&expanded, true, true, false); });
        const auto b = BoundsDp(h, "mainLayout");
        CHECK(b.has_value());
        if (!b) continue;
        const float bottom = b->Height() - 0.5f;
        SwipeMain(h, t, bottom, bottom - threshold / 4);
        CHECK_EQ(expanded, true);
        SwipeMain(h, t, bottom, bottom - (threshold == 40 ? threshold * 2 : threshold));
        CHECK_EQ(expanded, false);
    }
}

TEST(FloatingToolbarScrollBehaviorHidesIt) {
    for (bool vertical : { true, false }) {
        PhoneHarness h;
        std::int64_t t = 1000;
        Themed(h, [&] {
            const auto behavior = FloatingToolbarDefaults::exitAlwaysScrollBehavior(FloatingToolbarExitDirection::End);
            Scaffold(Tag(NestedScroll({}, behavior->NestedScrollConnection()), "mainLayout"), {}, [&, behavior](const PaddingValues& padding) {
                Box(Padding({}, padding), Alignment::TopStart(), false, [&, behavior] {
                    Column(Tag(VerticalScroll(FillMax({}, Axis::Horizontal), RememberScrollState()), "Content"), [] {
                        Box(Height(FillMax({}, Axis::Horizontal), 3000_dp));
                    });
                    FloatingToolbarWithFabOptions o;
                    o.scrollBehavior = behavior;
                    if (vertical)
                        VerticalFloatingToolbar(true, ToolbarFab, OffsetMod(AlignInBox({}, Alignment::CenterEnd()), Dp(-16), 0_dp), o, ToolbarContent);
                    else
                        HorizontalFloatingToolbar(true, ToolbarFab, OffsetMod(AlignInBox({}, Alignment::BottomCenter()), 0_dp, Dp(-16)), o, ToolbarContent);
                });
            });
        });
        h.AdvanceTimeBy(1000);
        CHECK(Displayed(h, "floatingActionButton"));
        CHECK(Displayed(h, "floatingToolbarContentLastItem"));
        const auto content = BoundsDp(h, "Content");
        CHECK(content.has_value());
        if (!content) continue;
        // swipeUp(bottom, bottom - 1000) in px.
        const float x = (content->left + content->right), y = content->bottom * 2 - 1;
        Swipe(h, t, x, y, x, y - 1000, 10, 20);
        h.AdvanceTimeBy(3000);
        CHECK(!Displayed(h, "floatingActionButton"));
        CHECK(!Displayed(h, "floatingToolbarContentLastItem"));
    }
}

TEST(FloatingToolbarOffersToCollapseOrExpand) {
    // The main content (or the FAB) carries one custom action: collapse while expanded, expand
    // while collapsed.
    for (bool vertical : { false, true }) {
        for (bool fab : { false, true }) {
            for (bool expanded : { true, false }) {
                PhoneHarness h;
                std::string label;
                Themed(h, [&] {
                    label = GetString(expanded ? Strings::FloatingToolbarCollapse : Strings::FloatingToolbarExpand);
                    if (fab) {
                        if (vertical) VerticalFloatingToolbar(expanded, ToolbarFab, Tag({}, "floatingToolbar"), {}, ToolbarContent);
                        else HorizontalFloatingToolbar(expanded, ToolbarFab, Tag({}, "floatingToolbar"), {}, ToolbarContent);
                        return;
                    }
                    FloatingToolbarOptions o;
                    o.leadingContent = [] { ToolbarIcon(); };
                    o.trailingContent = [] { ToolbarIcon(); };
                    const auto content = [] { ToolbarIcon(Tag({}, "floatingToolbarMainContentTestTag")); };
                    if (vertical) VerticalFloatingToolbar(expanded, Tag({}, "floatingToolbar"), o, content);
                    else HorizontalFloatingToolbar(expanded, Tag({}, "floatingToolbar"), o, content);
                });
                const auto tree = h.owner.BuildSemanticsTree();
                const auto* node = FindSemantics(tree, fab ? "floatingActionButton" : "floatingToolbarMainContentTestTag");
                CHECK(node != nullptr);
                if (!node) continue;
                CHECK_EQ(node->config.customActions.size(), std::size_t(1));
                if (!node->config.customActions.empty()) CHECK_EQ(node->config.customActions[0].label, label);
            }
        }
    }
}

TEST(FloatingToolbarAccessibilityActionCollapsesIt) {
    PhoneHarness h;
    std::string expandLabel;
    Themed(h, [&] {
        expandLabel = GetString(Strings::FloatingToolbarExpand);
        HorizontalFloatingToolbar(true, ToolbarFab, Tag({}, "floatingToolbar"), {}, ToolbarContent);
    });
    h.AdvanceTimeBy(1000);
    CHECK(Displayed(h, "floatingToolbarContentLastItem"));
    {
        const auto tree = h.owner.BuildSemanticsTree();
        const auto* node = FindSemantics(tree, "floatingActionButton");
        CHECK(node && !node->config.customActions.empty());
        if (node && !node->config.customActions.empty()) CHECK(node->config.customActions[0].action());
    }
    h.AdvanceTimeBy(2000);
    CHECK(!Displayed(h, "floatingToolbarContentLastItem"));
    const auto tree = h.owner.BuildSemanticsTree();
    const auto* node = FindSemantics(tree, "floatingActionButton");
    CHECK(node && !node->config.customActions.empty() && node->config.customActions[0].label == expandLabel);
}

TEST(FloatingToolbarCustomSizeDoesNotCrash) {
    PhoneHarness h;
    Themed(h, [&] {
        HorizontalFloatingToolbar(true, [] {}, Height({}, 40_dp), {}, [] {});
        VerticalFloatingToolbar(true, [] {}, Width({}, 40_dp), {}, [] {});
    });
    h.AdvanceTimeBy(1000);
    CHECK(true);
}

// ScrollbarTest. Compose reads the scrollbar's pixels; these read what the node drew last (its
// colours and rects). A 100 px container, 10 px thick, thumb red, track blue.
namespace {
    struct FakeIndicator final : foundation::ScrollIndicatorState {
        rt::State<int> offset{ 0 }, content{ 200 }, viewport{ 100 };
        int ScrollOffset() const override { return offset.Get(); }
        int ContentSize() const override { return content.Get(); }
        int ViewportSize() const override { return viewport.Get(); }
    };
    const ui::Color kThumb{ 0xFFFF0000u }, kTrack{ 0xFF0000FFu };
    struct ScrollbarCase { foundation::Orientation orientation; bool rtl; };
    constexpr ScrollbarCase kScrollbarCases[] = { { foundation::Orientation::Vertical, false }, { foundation::Orientation::Vertical, true },
                                                  { foundation::Orientation::Horizontal, false }, { foundation::Orientation::Horizontal, true } };
    NonInteractiveScrollbarOptions TestScrollbar() {
        NonInteractiveScrollbarOptions o;
        o.thumbColor = kThumb;
        o.trackColor = kTrack;
        o.thickness = 10_dp;
        o.mainAxisTrackInset = 0_dp;
        o.crossAxisTrackInset = 0_dp;
        return o;
    }
    // ScrollbarTestContainer.
    void ScrollbarContainer(Harness& h, const FakeIndicator& state, ScrollbarCase c, NonInteractiveScrollbarOptions o) {
        h.Run([&state, c, o] {
            rt::Composition::Current()->Provide(ui::LocalLayoutDirection, c.rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                Box({}, Alignment::TopStart(), false, [&] {
                    Box(NonInteractiveScrollbar(Tag(SizeMod({}, 100_dp, 100_dp), "container"), state, c.orientation, o));
                });
            });
        });
    }
    // Frames of 16 ms, each drawn (the scrollbar reads its state while drawing).
    void DrawFor(Harness& h, std::int64_t ms) {
        h.owner.Draw({});
        for (std::int64_t done = 0; done < ms;) {
            const std::int64_t step = std::min<std::int64_t>(16, ms - done);
            h.AdvanceTimeBy(step);
            h.owner.Draw({});
            done += step;
        }
    }
    internal::ScrollbarFrame Frame(Harness& h) {
        const auto* n = FindTagged(h, "container");
        const auto f = n ? internal::LastScrollbarFrame(*n) : std::nullopt;
        return f.value_or(internal::ScrollbarFrame{});
    }
    // computeScrollbarTestColors' sampling: the thumb starts at the visual start (the right of a
    // horizontal RTL bar).
    bool ThumbAtStart(const internal::ScrollbarFrame& f, ScrollbarCase c) {
        if (c.orientation == foundation::Orientation::Vertical) return Near(f.thumb.top, 0);
        return c.rtl ? Near(f.thumb.right, 100) : Near(f.thumb.left, 0);
    }
    bool ThumbAtEnd(const internal::ScrollbarFrame& f, ScrollbarCase c) {
        if (c.orientation == foundation::Orientation::Vertical) return Near(f.thumb.bottom, 100);
        return c.rtl ? Near(f.thumb.left, 0) : Near(f.thumb.right, 100);
    }
    float ThumbLength(const internal::ScrollbarFrame& f, ScrollbarCase c) {
        return c.orientation == foundation::Orientation::Vertical ? f.thumb.Height() : f.thumb.Width();
    }
}

TEST(ScrollbarFadesOutAfterScrolling) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        auto o = TestScrollbar();
        o.fadeDelayMillis = 100;
        o.fadeDurationMillis = 100;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(!Frame(h).drawn);
        state.offset.Set(10);
        DrawFor(h, 20);
        CHECK(Frame(h).drawn && Frame(h).thumbColor == kThumb && Frame(h).trackColor == kTrack);
        // Past the delay and the fade.
        DrawFor(h, 230);
        CHECK(!Frame(h).drawn);
    }
}

TEST(ScrollbarFadesOutAfterItsNodeIsRecreated) {
    // ReusableContent re-attaches the same node; kiln has no reuse, so a key change recreates it.
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        rt::State<bool> key{ true };
        auto o = TestScrollbar();
        o.fadeDelayMillis = 100;
        o.fadeDurationMillis = 100;
        h.Run([&state, &key, c, o] {
            rt::Composition::Current()->Provide(ui::LocalLayoutDirection, c.rtl ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr, [&] {
                Box({}, Alignment::TopStart(), false, [&] {
                    Box({}, Alignment::TopStart(), false, [&] {
                        Box(NonInteractiveScrollbar(Tag(SizeMod({}, 100_dp, 100_dp), "container"), state, c.orientation, o));
                    }, rt::Key(key.Get() ? 1 : 2));
                });
            });
        });
        DrawFor(h, 0);
        key.Set(false);
        DrawFor(h, 16);
        state.offset.Set(10);
        DrawFor(h, 20);
        CHECK(Frame(h).drawn && Frame(h).thumbColor == kThumb && Frame(h).trackColor == kTrack);
        DrawFor(h, 230);
        CHECK(!Frame(h).drawn);
    }
}

TEST(ScrollbarWaitsOutItsDelayWhenAnimationsAreOff) {
    for (const auto c : kScrollbarCases) {
        anim::SetDurationScale(0);
        Harness h;
        FakeIndicator state;
        auto o = TestScrollbar();
        o.fadeDelayMillis = 100;
        o.fadeDurationMillis = 100;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        state.offset.Set(10);
        // The delay is not an animation: still shown before it ends.
        DrawFor(h, 50);
        CHECK(Frame(h).drawn && Frame(h).thumbColor == kThumb);
        // Then gone at once.
        DrawFor(h, 120 + 16);
        CHECK(!Frame(h).drawn);
        anim::SetDurationScale(1);
    }
}

TEST(ScrollbarFollowsTheScrollOffset) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(Frame(h).drawn && Frame(h).thumbColor == kThumb && ThumbAtStart(Frame(h), c));
        state.offset.Set(100);
        DrawFor(h, 16);
        CHECK(Frame(h).drawn && ThumbAtEnd(Frame(h), c));
    }
}

TEST(ScrollbarHidesWhileTheContentFits) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        state.content.Set(100);
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(!Frame(h).drawn);
        state.viewport.Set(50);
        DrawFor(h, 16);
        CHECK(Frame(h).drawn && Frame(h).thumbColor == kThumb && Frame(h).trackColor == kTrack);
        state.viewport.Set(150);
        DrawFor(h, 16);
        CHECK(!Frame(h).drawn);
    }
}

TEST(ScrollbarThumbHasAMinimumLength) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        state.content.Set(1000);
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        o.thumbMinLength = 30_dp;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(Near(ThumbLength(Frame(h), c), 30));
    }
}

TEST(ScrollbarThumbHasAMaximumLength) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        state.content.Set(110);
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        o.thumbMaxLengthFraction = 0.5f;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(Near(ThumbLength(Frame(h), c), 50));
    }
}

TEST(ScrollbarHidesWhenTheTrackIsShorterThanTheThumb) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        o.thumbMinLength = 150_dp;
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        CHECK(!Frame(h).drawn);
    }
}

TEST(ScrollbarTrackKeepsItsInsets) {
    for (const auto c : kScrollbarCases) {
        Harness h;
        FakeIndicator state;
        auto o = TestScrollbar();
        o.isFadeEnabled = false;
        o.mainAxisTrackInset = 10_dp;
        o.crossAxisTrackInset = 5_dp;
        o.thickness = 10_dp;
        o.thumbColor = ui::Color::Transparent();
        ScrollbarContainer(h, state, c, o);
        DrawFor(h, 0);
        const auto f = Frame(h);
        CHECK(f.drawn && f.thumbColor == ui::Color::Transparent() && f.trackColor == kTrack);
        ui::Rect expected;
        if (c.orientation == foundation::Orientation::Vertical) {
            const float left = c.rtl ? 5 : 100 - 5 - 10;
            expected = { left, 10, left + 10, 90 };
        } else {
            expected = { 10, 100 - 5 - 10, 90, 95 };
        }
        CHECK(Near(f.track.left, expected.left) && Near(f.track.top, expected.top) && Near(f.track.right, expected.right)
              && Near(f.track.bottom, expected.bottom));
    }
}

TEST(ScrollbarModifiersCompareByTheirArguments) {
    FakeIndicator state;
    auto o = TestScrollbar();
    o.thickness = 10_dp;
    const auto a = NonInteractiveScrollbar({}, state, foundation::Orientation::Vertical, o);
    const auto b = NonInteractiveScrollbar({}, state, foundation::Orientation::Vertical, o);
    o.thickness = 5_dp;
    const auto other = NonInteractiveScrollbar({}, state, foundation::Orientation::Vertical, o);
    CHECK(a.Elements()[0]->Equals(*b.Elements()[0]));
    CHECK(!a.Elements()[0]->Equals(*other.Elements()[0]));
    o.thumbMaxLengthFraction = 1.5f;
    bool threw = false;
    try { (void)NonInteractiveScrollbar({}, state, foundation::Orientation::Vertical, o); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// AppBarTest's changeColors cases: scroll behaviours given the content's scroll state turn the
// bar to its scrolled colour while the content is away from its visual start (reverse layouts
// start away from it). The colour is read as the overlapped fraction that picks it.
namespace {
    enum class ColourContent : std::uint8_t { LazyColumn, LazyGrid, Column };
    struct ColourCase { const char* name; bool enterAlways; ColourContent content; bool reverse; bool preScrolled; };
    bool ShowsScrolledColour(const TopAppBarScrollBehavior& b) { return b.State().OverlappedFraction() > 0.01f; }
}

TEST(TopAppBarScrolledColourFollowsTheScrollableState) {
    using C = ColourContent;
    const ColourCase cases[] = {
        { "enterAlways scrolledLazyColumn", true, C::LazyColumn, false, false },
        { "enterAlways reverseLayout scrolledLazyColumn", true, C::LazyColumn, true, false },
        { "enterAlways reverseLayout preScrolledLazyColumn", true, C::LazyColumn, true, true },
        { "pinned reverseLayout scrolledLazyColumn", false, C::LazyColumn, true, false },
        { "pinned reverseLayout preScrolledLazyGrid", false, C::LazyGrid, true, true },
        { "pinned reverseLayout scrolledLazyGrid", false, C::LazyGrid, true, false },
        { "pinned scrolledLazyGrid", false, C::LazyGrid, false, false },
        { "enterAlways scrolledLazyGrid", true, C::LazyGrid, false, false },
        { "enterAlways reversedLayout scrolledLazyGrid", true, C::LazyGrid, true, false },
        { "enterAlways reverseLayout preScrolledLazyGrid", true, C::LazyGrid, true, true },
        { "enterAlways scrolledColumn", true, C::Column, false, false },
        { "enterAlways reverseLayout scrolledColumn", true, C::Column, true, false },
        { "enterAlways reverseLayout preScrolledColumn", true, C::Column, true, true },
        { "pinned reverseLayout preScrolledLazyColumn", false, C::LazyColumn, true, true },
        { "pinned scrolledLazyColumn", false, C::LazyColumn, false, false },
        { "pinned preScrolledLazyColumn", false, C::LazyColumn, false, true },
        { "pinned reverseLayout scrolledColumn", false, C::Column, true, false },
        { "pinned scrolledColumn", false, C::Column, false, false },
        { "pinned reverseLayout preScrolledColumn", false, C::Column, true, true },
        { "pinned preScrolledColumn", false, C::Column, false, true },
    };
    for (const auto& k : cases) {
        PhoneHarness h;
        LazyListState list(k.preScrolled ? 30 : 0);
        LazyGridState grid(k.preScrolled ? 30 : 0);
        ScrollState column(k.preScrolled ? 2000 : 0);
        foundation::ScrollableState& scrollable = k.content == C::LazyColumn ? static_cast<foundation::ScrollableState&>(list)
                                                  : k.content == C::LazyGrid ? static_cast<foundation::ScrollableState&>(grid)
                                                                             : static_cast<foundation::ScrollableState&>(column);
        // The reverse grid that enterAlways scrolls both ways holds 100 dp cells.
        const bool tallCells = k.enterAlways && k.content == C::LazyGrid && k.reverse && !k.preScrolled;
        std::shared_ptr<TopAppBarScrollBehavior> behavior;
        Themed(h, [&] {
            behavior = k.enterAlways ? TopAppBarDefaults::enterAlwaysScrollBehavior(scrollable) : TopAppBarDefaults::pinnedScrollBehavior(scrollable);
            ScaffoldOptions o;
            o.topBar = [&] {
                TopAppBarOptions bar;
                bar.scrollBehavior = behavior;
                bar.colors = TopAppBarDefaults::topAppBarColors(Color(0xFFFF0000u), Color(0xFF00FF00u));
                Box(Tag({}, "topAppBar"), Alignment::TopStart(), false, [&, bar] { TopAppBar([] { Text("Title"); }, {}, bar); });
            };
            Scaffold(FillMax({}, Axis::Both), o, [&](const PaddingValues& padding) {
                const ui::Modifier m = NestedScroll(Padding(Tag(FillMax({}, Axis::Both), "content"), padding), behavior->NestedScrollConnection());
                switch (k.content) {
                    case C::LazyColumn: {
                        LazyListOptions lo;
                        lo.reverseLayout = k.reverse;
                        LazyColumn(m, list, [](LazyListScope& s) { s.Items(100, [](int i) { Text(std::to_string(i)); }); }, lo);
                        break;
                    }
                    case C::LazyGrid: {
                        LazyGridOptions go;
                        go.reverseLayout = k.reverse;
                        go.contentPadding = padding;
                        const ui::Modifier gm = NestedScroll(Tag({}, "content"), behavior->NestedScrollConnection());
                        LazyVerticalGrid(gm, GridCells::Adaptive(100_dp), grid, [tallCells](LazyGridScope& s) {
                            s.Items(100, [tallCells](int) { Box(Height(FillMax({}, Axis::Horizontal), tallCells ? 100_dp : 50_dp)); });
                        }, go);
                        break;
                    }
                    case C::Column:
                        Column(VerticalScroll(m, column, true, k.reverse), Arrangement::Bottom(), AlignmentH::Start(), [] {
                            for (int i = 0; i < 50; ++i) Box(Height(FillMax({}, Axis::Horizontal), 80_dp), Alignment::TopStart(), false, [i] { Text(std::to_string(i)); });
                        });
                        break;
                }
            });
        });
        h.AdvanceTimeBy(500);
        const auto content = BoundsDp(h, "content");
        CHECK(content.has_value());
        if (!content) continue;
        // performTouchInput coordinates: px from the content's top.
        const float x = content->left * 2 + 100, top = content->top * 2, height = content->Height() * 2;
        std::int64_t t = 0;
        // swipeUp/swipeDown: 200 ms unless given, moves every 10 ms.
        const auto swipe = [&](float y0, float y1, int durationMs = 200) {
            Swipe(h, t, x, top + y0, x, top + y1, durationMs / 10);
            h.AdvanceTimeBy(3000);
        };
        const auto expect = [&](bool scrolled, const char* when) {
            if (ShowsScrolledColour(*behavior) != scrolled) std::printf("  %s: %s expected the %s colour\n", k.name, when, scrolled ? "scrolled" : "container");
            CHECK(ShowsScrolledColour(*behavior) == scrolled);
        };
        if (!k.reverse && !k.preScrolled) {
            expect(false, "at first");
            swipe(height - 200, height - 1000);
            if (k.enterAlways) {
                CHECK(Near(behavior->State().HeightOffset(), behavior->State().HeightOffsetLimit()));
                swipe(height - 1000, height - 800);
            }
            expect(true, "scrolled");
        } else if (!k.reverse) {
            expect(true, "at first");
            swipe(height - 200, height - 1000);
            expect(true, "scrolled");
        } else {
            expect(true, "at first");
            // Compose swipes 500 px here, short of the visual start on its test device; on this
            // phone (412 x 800 dp at density 2) the content can be nearer, so the partial swipes
            // take half of what is left (capped at Compose's 500 px), slowly enough not to fling.
            const auto partial = [&](float distance) { return std::min(500.0f, distance / 2); };
            if (tallCells) {
                swipe(height - 1000, height - 1000 + partial(float(scrollable.ScrollIndicator()->ScrollOffset())), 600);
                expect(true, "scrolled back a little");
                const auto* i = scrollable.ScrollIndicator();
                swipe(height - 500, height - 500 - partial(float(i->ContentSize() - i->ViewportSize() - i->ScrollOffset())), 600);
                expect(true, "scrolled on");
            } else {
                swipe(0, partial(float(scrollable.ScrollIndicator()->ScrollOffset())), 600);
                expect(true, "scrolled back a little");
            }
            const bool farther = !k.enterAlways && k.content == C::LazyGrid && !k.preScrolled;
            swipe(500, height + (farther ? 2000 : 1000));
            // Long reverse lists need more than one swipe to reach their visual start in Compose's
            // dp; keep swiping as a longer finger would until the content stops.
            for (int i = 0; i < 20 && scrollable.ScrollIndicator()->ScrollOffset() != 0; ++i) swipe(500, height + 1000);
            expect(false, "back at the start");
        }
    }
}

// SnackbarTest, with the styling fix off (the default) and on.
namespace {
    const std::string kLongText = "Message is very long and long and long and long and long and long and long and long and long and long and long";
    struct SnackbarFix {
        bool previous;
        explicit SnackbarFix(bool on) : previous(ComposeMaterial3Flags::isSnackbarStylingFixEnabled) { ComposeMaterial3Flags::isSnackbarStylingFixEnabled = on; }
        ~SnackbarFix() { ComposeMaterial3Flags::isSnackbarStylingFixEnabled = previous; }
    };
    // setMaterialContentForSizeAssertions(parentMaxWidth): the snackbar under a width cap.
    void SizedSnackbar(Harness& h, ui::Dp maxWidth, std::function<void()> snackbar) {
        Themed(h, [&, maxWidth] {
            Box(SizeIn({}, ui::Dp::Unspecified(), ui::Dp::Unspecified(), maxWidth, ui::Dp::Unspecified()), Alignment::TopStart(), false, [&] {
                Box(Tag({}, "snackbar"), Alignment::TopStart(), false, snackbar);
            });
        });
        h.AdvanceTimeBy(100);
    }
    // FakeText baselines: each line's at 0.8 of its height, from the text's top (dp).
    float FirstBaselineDp(Harness& h, const std::string& tag, int lines = 1) {
        const auto* n = FindTagged(h, tag);
        CHECK(n != nullptr);
        if (!n) return 0;
        const float d = h.owner.GetDensity().density;
        return std::round(n->Size().height / float(lines) * 0.8f) / d;
    }
    float LastBaselineDp(Harness& h, const std::string& tag, int lines) {
        const auto* n = FindTagged(h, tag);
        if (!n) return 0;
        const float d = h.owner.GetDensity().density;
        const float lineH = n->Size().height / float(lines);
        return std::round(lineH * float(lines - 1) + lineH * 0.8f) / d;
    }
    struct FakeSnackbarData final : SnackbarData {
        SnackbarVisuals visuals;
        int actions = 0, dismissals = 0;
        const SnackbarVisuals& Visuals() const override { return visuals; }
        void PerformAction() override { ++actions; }
        void Dismiss() override { ++dismissals; }
    };
}

TEST(SnackbarActionsClick) {
    for (bool fix : { false, true }) {
        SnackbarFix flag(fix);
        PhoneHarness h;
        bool clicked = false;
        Themed(h, [&] {
            SnackbarOptions o;
            o.action = [&] { TextButton([&] { clicked = true; }, {}, {}, [] { Text("UNDO"); }); };
            Snackbar({}, o, [] { Text("Message"); });
        });
        CHECK(BoundsDp(h, "Message").has_value());
        CHECK(!clicked);
        std::int64_t t = 0;
        Press(h, t, "UNDO", true);
        CHECK(clicked);
    }
}

TEST(SnackbarDataVersionProxiesItsParameters) {
    for (bool fix : { false, true }) {
        SnackbarFix flag(fix);
        PhoneHarness h;
        auto data = std::make_shared<FakeSnackbarData>();
        data->visuals = { "Data message", "UNDO", false, SnackbarDuration::Short };
        Themed(h, [&] { Snackbar(data); });
        CHECK(BoundsDp(h, "Data message").has_value());
        CHECK_EQ(data->actions, 0);
        std::int64_t t = 0;
        Press(h, t, "UNDO", true);
        CHECK_EQ(data->actions, 1);
    }
}

TEST(SnackbarDismissActionDismisses) {
    for (bool fix : { false, true }) {
        SnackbarFix flag(fix);
        PhoneHarness h;
        auto data = std::make_shared<FakeSnackbarData>();
        data->visuals = { "Data message", std::nullopt, true, SnackbarDuration::Indefinite };
        std::string description;
        Themed(h, [&] {
            description = GetString(Strings::SnackbarDismiss);
            Snackbar(data);
        });
        CHECK_EQ(data->dismissals, 0);
        CHECK(ClickDescribed(h, description));
        CHECK_EQ(data->dismissals, 1);
    }
}

TEST(SnackbarWithoutTextContent) {
    for (bool fix : { false, true }) {
        SnackbarFix flag(fix);
        {
            PhoneHarness h;
            Themed(h, [] { Snackbar({}, {}, [] {}); });   // empty content does not crash
        }
        PhoneHarness h;
        Themed(h, [] { Snackbar({}, {}, [] { Box(Tag(SizeMod({}, 20_dp, 20_dp), "content")); }); });
        const auto content = BoundsDp(h, "content");
        CHECK(content && Near(content->top, (48 - 20) / 2.0f));
    }
}

TEST(SnackbarShortTextIsCentredInOneLine) {
    for (bool fix : { false, true }) {
        for (bool bigFont : { false, true }) {
            SnackbarFix flag(fix);
            PhoneHarness h;
            SizedSnackbar(h, 300_dp, [bigFont] {
                TextOptions to;
                if (bigFont) to.fontSize = text::Sp(30);
                Snackbar({}, {}, [to] { Text("Message", Tag({}, "text"), to); });
            });
            const auto bar = BoundsDp(h, "snackbar"), text = BoundsDp(h, "text");
            CHECK(bar && text);
            if (!bar || !text) continue;
            CHECK(Near(bar->Width(), 300));
            if (!bigFont) CHECK(std::abs(bar->Height() - 48) <= 1);
            CHECK(FirstBaselineDp(h, "text") > 0);
            // The text sits as far from the top as from the bottom.
            CHECK(Near(text->top - bar->top, bar->bottom - text->bottom));
        }
    }
}

TEST(SnackbarShortTextAndButtonShareABaseline) {
    for (bool fix : { false, true }) {
        for (bool bigFont : { false, true }) {
            SnackbarFix flag(fix);
            PhoneHarness h;
            SizedSnackbar(h, bigFont ? 400_dp : 300_dp, [bigFont] {
                TextOptions to;
                if (bigFont) to.fontSize = text::Sp(30);
                SnackbarOptions o;
                o.action = [to] { TextButton([] {}, Tag({}, "button"), {}, [to] { Text("Undo", Tag({}, "buttonText"), to); }); };
                Snackbar({}, o, [to] { Text("Message", Tag({}, "text"), to); });
            });
            const auto bar = BoundsDp(h, "snackbar"), text = BoundsDp(h, "text"), label = BoundsDp(h, "buttonText");
            CHECK(bar && text && label);
            if (!bar || !text || !label) continue;
            if (!bigFont) {
                CHECK(Near(bar->Width(), 300));
                CHECK(std::abs(bar->Height() - 48) <= 1);
            }
            // The button's baseline is its label's.
            CHECK(Near(label->top + FirstBaselineDp(h, "buttonText"), text->top + FirstBaselineDp(h, "text")));
        }
    }
}

TEST(SnackbarLongTextTakesTwoLines) {
    for (bool fix : { false, true }) {
        for (bool button : { false, true }) {
            SnackbarFix flag(fix);
            PhoneHarness h;
            SizedSnackbar(h, 300_dp, [button] {
                SnackbarOptions o;
                if (button) o.action = [] { TextButton([] {}, Tag({}, "button"), {}, [] { Text("Undo"); }); };
                TextOptions to;
                to.maxLines = 2;
                Snackbar({}, o, [to] { Text(kLongText, Tag({}, "text"), to); });
            });
            const auto bar = BoundsDp(h, "snackbar"), text = BoundsDp(h, "text");
            CHECK(bar && text);
            if (!bar || !text) continue;
            CHECK(Near(bar->Width(), 300));
            CHECK(FirstBaselineDp(h, "text", 2) != LastBaselineDp(h, "text", 2));
            if (fix) {
                CHECK(std::abs(bar->Height() - (14 * 2 + text->Height())) <= 1);
            } else {
                CHECK(std::abs(bar->Height() - 68) <= 1);
                if (button) CHECK(Near(text->top - bar->top, 30 - FirstBaselineDp(h, "text", 2)));
            }
            if (button) {
                const auto b = BoundsDp(h, "button");
                CHECK(b && Near(b->top + b->Height() / 2 - bar->top, bar->Height() / 2));
            }
        }
    }
}

TEST(SnackbarActionOnItsOwnLine) {
    for (bool fix : { false, true }) {
        SnackbarFix flag(fix);
        PhoneHarness h;
        SizedSnackbar(h, 300_dp, [] {
            SnackbarOptions o;
            o.actionOnNewLine = true;
            o.action = [] {
                TextButton([] {}, Tag({}, "button"), {}, [] { Text("Undo", SizeIn({}, 48_dp, 48_dp, ui::Dp::Unspecified(), ui::Dp::Unspecified())); });
            };
            Snackbar({}, o, [] { Text("Message", Tag(SizeIn({}, 48_dp, 48_dp, ui::Dp::Unspecified(), ui::Dp::Unspecified()), "text")); });
        });
        const auto bar = BoundsDp(h, "snackbar"), text = BoundsDp(h, "text"), b = BoundsDp(h, "button");
        CHECK(bar && text && b);
        if (!bar || !text || !b) continue;
        if (fix) {
            CHECK(Near(text->top, 14));
            CHECK(std::abs(b->top - (14 * 2 + text->Height())) <= 0.5f);
            CHECK(std::abs(bar->Height() - (4 + b->top + b->Height())) <= 1);
            CHECK(Near(bar->Width(), 8 + b->left + b->Width()));
        } else {
            // One BodyMedium line (20 sp) at the top of the 48 dp text: its baseline at 16.
            const float first = 0.8f * 20, last = first;
            CHECK(Near(text->top, 30 - first));
            const float lastToBottom = std::max(18.0f, 48 - last);
            CHECK(Near(b->top, lastToBottom + text->top + last));
            CHECK(std::abs(bar->Height() - (2 + b->top + b->Height())) <= 1);
            CHECK(Near(bar->Width(), 8 + b->left + b->Width()));
        }
    }
}

// SnackbarHostTest.
TEST(SnackbarHostShowsPushedDataInOrder) {
    PhoneHarness h;
    SnackbarHostState host;
    std::string shown;
    Themed(h, [&] {
        SnackbarHost(host, {}, [&](std::shared_ptr<SnackbarData> data) {
            rt::Composition::Current()->LaunchedEffect(rt::Keys(std::int64_t(reinterpret_cast<std::uintptr_t>(data.get()))), [&shown, data]() -> rt::Task<> {
                return [](std::string* shown, std::shared_ptr<SnackbarData> data) -> rt::Task<> {
                    *shown += data->Visuals().message;
                    data->Dismiss();
                    co_return;
                }(&shown, data);
            });
        });
    });
    std::vector<std::string> seen;
    bool done = false;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, std::string* shown, std::vector<std::string>* seen, bool* done) -> rt::Task<> {
        for (const char* m : { "1", "2", "3" }) {
            co_await host->ShowSnackbar(m);
            seen->push_back(*shown);
        }
        *done = true;
    }(&host, &shown, &seen, &done));
    h.AdvanceTimeBy(500);
    CHECK(done);
    CHECK((seen == std::vector<std::string>{ "1", "12", "123" }));
}

TEST(SnackbarHostQueueIsFirstInFirstOut) {
    PhoneHarness h;
    SnackbarHostState host;
    std::string shown;
    Themed(h, [&] {
        SnackbarHost(host, {}, [&](std::shared_ptr<SnackbarData> data) {
            rt::Composition::Current()->LaunchedEffect(rt::Keys(std::int64_t(reinterpret_cast<std::uintptr_t>(data.get()))), [&shown, data]() -> rt::Task<> {
                return [](std::string* shown, std::shared_ptr<SnackbarData> data) -> rt::Task<> {
                    *shown += data->Visuals().message;
                    co_await rt::Delay(30);
                    data->Dismiss();
                }(&shown, data);
            });
        });
    });
    std::vector<rt::Job> jobs;
    for (int i = 0; i < 10; ++i)
        jobs.push_back(rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, int i) -> rt::Task<> {
            co_await rt::Delay(i * 10);
            co_await host->ShowSnackbar(std::to_string(i));
        }(&host, i)));
    h.AdvanceTimeBy(3000);
    for (const auto& j : jobs) CHECK(j.Done());
    CHECK_EQ(shown, std::string("0123456789"));
}

TEST(SnackbarHostReturnsTheResult) {
    PhoneHarness h;
    SnackbarHostState host;
    Themed(h, [&] { SnackbarHost(host); });
    std::optional<SnackbarResult> first, second;
    rt::Job job1 = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, std::optional<SnackbarResult>* r) -> rt::Task<> {
        *r = co_await host->ShowSnackbar("1", std::string("press"));
    }(&host, &first));
    h.AdvanceTimeBy(500);
    std::int64_t t = 0;
    Press(h, t, "press", true);
    h.AdvanceTimeBy(100);
    CHECK(first == SnackbarResult::ActionPerformed);
    rt::Job job2 = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, std::optional<SnackbarResult>* r) -> rt::Task<> {
        *r = co_await host->ShowSnackbar("1");
    }(&host, &second));
    h.AdvanceTimeBy(5000);
    CHECK(second == SnackbarResult::Dismissed);
}

TEST(SnackbarHostRespectsTheCallersLifetime) {
    PhoneHarness h;
    SnackbarHostState host;
    Themed(h, [&] { SnackbarHost(host); });
    bool resumed = false;
    {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, bool* resumed) -> rt::Task<> {
            co_await host->ShowSnackbar("1");
            *resumed = true;   // the result never comes: the caller is gone
        }(&host, &resumed));
        h.AdvanceTimeBy(10);
        CHECK(host.CurrentSnackbarData() != nullptr);
    }
    h.AdvanceTimeBy(5000);
    CHECK(!resumed);
    CHECK(host.CurrentSnackbarData() == nullptr);
    // The queue moved on.
    std::optional<SnackbarResult> next;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, std::optional<SnackbarResult>* r) -> rt::Task<> {
        *r = co_await host->ShowSnackbar("2");
    }(&host, &next));
    h.AdvanceTimeBy(5000);
    CHECK(next == SnackbarResult::Dismissed);
}

TEST(SnackbarHostSemantics) {
    PhoneHarness h;
    SnackbarHostState host;
    std::string paneTitle;
    Themed(h, [&] {
        SnackbarHost(host);
        paneTitle = GetString(Strings::SnackbarPaneTitle);
    });
    std::optional<SnackbarResult> result;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](SnackbarHostState* host, std::optional<SnackbarResult>* r) -> rt::Task<> {
        *r = co_await host->ShowSnackbar("1", std::string("press"));
    }(&host, &result));
    h.AdvanceTimeBy(500);
    const auto tree = h.owner.BuildSemanticsTree();
    std::function<const ui::SemanticsNode*(const ui::SemanticsNode&)> pane = [&](const ui::SemanticsNode& n) -> const ui::SemanticsNode* {
        if (n.config.paneTitle) return &n;
        for (const auto& c : n.children) if (const auto* r = pane(c)) return r;
        return nullptr;
    };
    const auto* node = pane(tree);
    CHECK(node != nullptr);
    if (!node) return;
    CHECK(node->config.paneTitle == paneTitle);
    CHECK(node->config.liveRegion == ui::LiveRegionMode::Polite);
    CHECK(bool(node->config.dismiss));
    CHECK(FindSemantics(*node, "1") != nullptr);
    if (node->config.dismiss) node->config.dismiss();
    h.AdvanceTimeBy(100);
    CHECK(result == SnackbarResult::Dismissed);
}

TEST(SnackbarDurationsFollowAccessibilitySettings) {
    struct Manager final : ui::AccessibilityManager {
        std::int64_t CalculateRecommendedTimeoutMillis(std::int64_t original, bool, bool, bool controls) const override {
            if (original == INT64_MAX) return INT64_MAX;
            return controls ? 10000 : 5000;
        }
        ui::AccessibilityServiceState ServiceState() const override { return {}; }
    } manager;
    using D = SnackbarDuration;
    CHECK_EQ(internal::SnackbarDurationMillis(D::Indefinite, true, &manager), INT64_MAX);
    CHECK_EQ(internal::SnackbarDurationMillis(D::Indefinite, false, &manager), INT64_MAX);
    CHECK_EQ(internal::SnackbarDurationMillis(D::Long, true, &manager), std::int64_t(10000));
    CHECK_EQ(internal::SnackbarDurationMillis(D::Long, false, &manager), std::int64_t(5000));
    CHECK_EQ(internal::SnackbarDurationMillis(D::Short, true, &manager), std::int64_t(10000));
    CHECK_EQ(internal::SnackbarDurationMillis(D::Short, false, &manager), std::int64_t(5000));
    CHECK_EQ(internal::SnackbarDurationMillis(D::Indefinite, true, nullptr), INT64_MAX);
    CHECK_EQ(internal::SnackbarDurationMillis(D::Long, false, nullptr), std::int64_t(10000));
    CHECK_EQ(internal::SnackbarDurationMillis(D::Short, true, nullptr), std::int64_t(4000));
}

