#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/material3/MaterialTheme.h"
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
