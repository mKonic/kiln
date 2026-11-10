// M2 sample: the declarative C++ layer. Rows, columns, weights, arrangements, padding,
// backgrounds and wrapped text, plus a counter driven by a coroutine to show recomposition.
#include <kiln/kiln.hpp>
#include "MaterialComponents.h"

#include <cmath>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/text/BasicSecureTextField.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/foundation/draganddrop/DragAndDrop.h>
#include <kiln/foundation/activity/ActivityResult.h>
#include <kiln/foundation/Popup.h>
#include <kiln/foundation/Dialog.h>
#include <kiln/material3/DynamicColorScheme.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Ripple.h>
#include <kiln/material3/Surface.h>
#include <kiln/material3/Text.h>
#include <kiln/material3/MaterialShapes.h>
#include <kiln/material3/internal/Icons.h>
#include <kiln/graphics/PathEffect.h>
#include <kiln/foundation/MeshGradient.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/Insets.h>
#include <kiln/foundation/Back.h>
#include <kiln/foundation/Draw.h>
#include <kiln/foundation/AnimatedVisibility.h>
#include <kiln/foundation/AnchoredDraggable.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Pager.h>
#include <kiln/foundation/Image.h>
#include <kiln/foundation/Shapes.h>
#include <kiln/foundation/Shadow.h>
#include <kiln/foundation/PolygonShape.h>
#include <kiln/graphics/shapes/Shapes.h>

#include <kiln/Assets.h>
#include <kiln/foundation/text/BasicTextField.h>
#include <kiln/foundation/text/Selection.h>
#include <kiln/runtime/Task.h>
#include <kiln/runtime/Saveable.h>

using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    const Color kSurface{ 0xFFFEF7FFu }, kContainer{ 0xFFF3EDF7u }, kPrimary{ 0xFF6750A4u },
                kOnPrimary{ 0xFFFFFFFFu }, kOnSurface{ 0xFF1D1B20u }, kVariant{ 0xFF49454Fu },
                kPrimaryContainer{ 0xFFEADDFFu }, kTertiary{ 0xFF7D5260u };

    struct RingShadowShape final : kiln::gfx::Shape {
        kiln::gfx::Outline CreateOutline(Size size, LayoutDirection, const Density&) const override {
            kiln::gfx::Path path;
            path.fillType = kiln::gfx::FillType::EvenOdd;
            path.AddOval({ 0, 0, size.width, size.height });
            path.AddOval({ size.width * 0.25f, size.height * 0.25f, size.width * 0.75f, size.height * 0.75f });
            return { path };
        }
        bool Equals(const kiln::gfx::Shape& other) const override { return dynamic_cast<const RingShadowShape*>(&other) != nullptr; }
    };
    struct NotchedShadowShape final : kiln::gfx::Shape {
        kiln::gfx::Outline CreateOutline(Size size, LayoutDirection, const Density&) const override {
            kiln::gfx::Path path;
            path.MoveTo(0, 0); path.LineTo(size.width, 0); path.LineTo(size.width * 0.55f, size.height * 0.5f);
            path.LineTo(size.width, size.height); path.LineTo(0, size.height); path.Close();
            return { path };
        }
        bool Equals(const kiln::gfx::Shape& other) const override { return dynamic_cast<const NotchedShadowShape*>(&other) != nullptr; }
    };

    kiln::rt::Task<> Tick(kiln::rt::State<int>& seconds) {
        for (;;) {
            co_await kiln::rt::Delay(1000);
            seconds.Set(seconds.Peek() + 1);
        }
    }

    // A coroutine must not be a capturing lambda: its frame would outlive the captures.
    kiln::rt::Task<> Spin(kiln::rt::State<float>& angle) {
        const std::int64_t start = co_await kiln::rt::NextFrame();
        for (;;) {
            const std::int64_t t = co_await kiln::rt::NextFrame();
            angle.Set(float(double(t - start) * 1e-9 * 45.0));   // 45 degrees per second
        }
    }

    kiln::rt::Task<> Breathe(kiln::rt::State<float>& t) {
        const std::int64_t start = co_await kiln::rt::NextFrame();
        for (;;) {
            const std::int64_t now = co_await kiln::rt::NextFrame();
            t.Set(0.5f - 0.5f * std::cos(float(double(now - start) * 1e-9 * 2.0)));   // 0..1..0
        }
    }

    void Chip(const std::string& label, Color bg, Color fg) {
        Box(Background(Padding(Modifier{}, 0_dp), bg, CornerShape::Rounded(8_dp)), Alignment::Center(), false, [=] {
            BasicText(label, Padding(Modifier{}, 12_dp, 6_dp), { 14, fg });
        });
    }

    void Gallery() {
        auto& c = *kiln::rt::Composition::Current();
        auto& seconds = RememberState(0);
        c.LaunchedEffect(kiln::rt::Keys(0), [&seconds] { return Tick(seconds); });

        auto& scroll = RememberScrollState();
        // One Material section alone (for tools/gallery-smoke): nothing else in the tree.
        static const std::string section = kiln::gallery::RequestedSection();
        if (!section.empty()) {
            Column(VerticalScroll(SafeDrawingPadding(Background(FillMax(Modifier{}, Axis::Both), kSurface)), scroll), Arrangement::Top(), AlignmentH::Start(),
                   [] { kiln::gallery::MaterialComponents(section); });
            return;
        }
        // Edge to edge: the background fills the window, content stays inside the safe area and
        // above the keyboard.
        Column(VerticalScroll(ImeNestedScroll(SafeDrawingPadding(Background(FillMax(Modifier{}, Axis::Both), kSurface))), scroll), Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {
            Spacer(Height(Modifier{}, 8_dp));
            BasicText("kiln layout", Padding(Modifier{}, 16_dp, 0_dp), { 28, kOnSurface });
            kiln::gallery::MaterialComponents();
            {
                using namespace kiln::text;
                const AnnotatedString styled = BuildAnnotatedString([&](AnnotatedStringBuilder& b) {
                    b.Append("Styled ");
                    SpanStyle bold; bold.fontWeight = FontWeight::Bold();
                    b.WithStyle(bold, [&] { b.Append("bold"); });
                    b.Append(", ");
                    SpanStyle italic; italic.fontStyle = FontStyle::Italic; italic.color = kPrimary;
                    b.WithStyle(italic, [&] { b.Append("italic"); });
                    b.Append(", ");
                    SpanStyle under; under.textDecoration = TextDecoration::Underline;
                    b.WithStyle(under, [&] { b.Append("underlined"); });
                    b.Append(", ");
                    SpanStyle strike; strike.textDecoration = TextDecoration::LineThrough;
                    b.WithStyle(strike, [&] { b.Append("struck"); });
                    b.Append(", ");
                    SpanStyle mark; mark.background = Color(0xFFFFD8E4u);
                    b.WithStyle(mark, [&] { b.Append("marked"); });
                    b.Append(" and E = mc");
                    SpanStyle sup; sup.baselineShift = BaselineShift::Superscript(); sup.fontSize = Em(0.6f);
                    b.WithStyle(sup, [&] { b.Append("2"); });
                    b.Append(". ");
                    SpanStyle big; big.fontSize = Sp(24); big.letterSpacing = Em(0.1f);
                    b.WithStyle(big, [&] { b.Append("Spaced"); });
                });
                TextStyle body(16, kOnSurface);
                body.paragraph.lineHeight = Sp(24);
                BasicText(styled, Padding(Modifier{}, 16_dp, 8_dp), body);
                TextStyle shadowed(22, kOnSurface);
                shadowed.span.shadow = Shadow{ Color(0x806750A4u), { 4, 4 }, 6 };
                shadowed.span.fontWeight = FontWeight::Medium();
                BasicText("Soft shadow", Padding(Modifier{}, 16_dp, 4_dp), shadowed);
                TextStyle gradient(28);
                gradient.span.brush = kiln::gfx::Brush::LinearGradient({ kPrimary, Color(0xFFE8618Cu) }, { 0, 0 }, { 400, 0 });
                gradient.span.fontWeight = FontWeight::Bold();
                BasicText("Gradient brush", Padding(Modifier{}, 16_dp, 4_dp), gradient);
                auto& linkTaps = RememberState(0);
                const AnnotatedString linked = BuildAnnotatedString([&](AnnotatedStringBuilder& b) {
                    b.Append("Links: ");
                    TextLinkStyles ls;
                    SpanStyle linkStyle; linkStyle.color = kPrimary; linkStyle.textDecoration = TextDecoration::Underline;
                    SpanStyle pressed; pressed.background = Color(0x336750A4u);
                    ls.style = linkStyle;
                    ls.pressedStyle = pressed;
                    b.WithLink(LinkAnnotation::Url("https://developer.android.com/develop/ui/compose/text", ls), [&] { b.Append("Compose text docs"); });
                    b.Append(" or ");
                    b.WithLink(LinkAnnotation::Clickable("count", ls, [&linkTaps](const LinkAnnotation&) { linkTaps.Set(linkTaps.Get() + 1); }),
                               [&] { b.Append("tapped " + std::to_string(linkTaps.Get())); });
                });
                BasicText(linked, Padding(Modifier{}, 16_dp, 4_dp), { 16, kOnSurface });
                BasicTextOptions inlineOpts;
                static const auto star = PolygonShape::Star(5, 0.5f, PolygonCornerRounding::Fraction(0.1f));
                inlineOpts.inlineContent["star"] = { Placeholder{ Em(1.2f), Em(1.2f), PlaceholderVerticalAlign::TextCenter },
                                                     [](const std::string&) { Box(Background(FillMax(Modifier{}, Axis::Both), Color(0xFFE8618Cu), star)); } };
                BasicText(BuildAnnotatedString([](AnnotatedStringBuilder& b) {
                              b.Append("Inline content: a ");
                              AppendInlineContent(b, "star");
                              b.Append(" sits in the line like a glyph.");
                          }), Padding(Modifier{}, 16_dp, 4_dp), { 16, kOnSurface }, inlineOpts);
                TextStyle downloaded(26, kPrimary);
                downloaded.span.fontFamily = FontFamily::Of({ Font::Google("Lobster Two", FontWeight::Bold()) });
                downloaded.span.fontWeight = FontWeight::Bold();
                BasicText("Downloaded: Lobster Two", Padding(Modifier{}, 16_dp, 4_dp), downloaded);
                BasicTextOptions two;
                two.maxLines = 2;
                two.overflow = TextOverflow::Ellipsis;
                BasicText("Two lines at most, then an ellipsis: this sentence keeps going well past the second line so "
                          "the end of it is cut off and replaced.", Padding(Modifier{}, 16_dp, 4_dp), { 14, kVariant }, two);
                SelectionContainer(Modifier{}, [] {
                    Column(Padding(Modifier{}, 16_dp, 4_dp), [] {
                        // A copy keeps the bold (Compose's styled clip).
                        BasicText(kiln::text::BuildAnnotatedString([](kiln::text::AnnotatedStringBuilder& b) {
                                      b.Append("Selection runs across these texts: from the ");
                                      kiln::text::SpanStyle bold;
                                      bold.fontWeight = kiln::text::FontWeight::Bold();
                                      b.WithStyle(bold, [&] { b.Append("first sentence"); });
                                  }),
                                  {}, { 16, kOnSurface });
                        BasicText("into the second one, and copies them on separate lines. Smart selection: call 650-253-0000.", {}, { 16, kOnSurface });
                        DisableSelection([] { BasicText("This line is not selectable.", {}, { 14, kVariant }); });
                    });
                });
            }
            Column(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), kContainer, CornerShape::Rounded(12_dp)),
                   Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&] {
                BasicText("Row with weights 1 : 2 : 1", Padding(Modifier{}, PaddingValues{ 16_dp, 16_dp, 16_dp, 0_dp }), { 16, kVariant });
                Row(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 48_dp), Arrangement::SpacedBy(8_dp), AlignmentV::Top(), [&] {
                    Box(Background(Height(Weight(Modifier{}, 1), 48_dp), kPrimary, CornerShape::Rounded(8_dp)));
                    Box(Background(Height(Weight(Modifier{}, 2), 48_dp), kTertiary, CornerShape::Rounded(8_dp)));
                    Box(Background(Height(Weight(Modifier{}, 1), 48_dp), kPrimary, CornerShape::Rounded(8_dp)));
                });
                BasicText("SpaceBetween, centred vertically", Padding(Modifier{}, 16_dp, 0_dp), { 16, kVariant });
                Row(Padding(FillMax(Modifier{}, Axis::Horizontal), PaddingValues{ 16_dp, 0_dp, 16_dp, 16_dp }), Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [&] {
                    Chip("one", kPrimaryContainer, kOnSurface);
                    Chip("two", kPrimary, kOnPrimary);
                    Chip("three", kPrimaryContainer, kOnSurface);
                });
            });
            Box(Background(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 120_dp), kPrimaryContainer, CornerShape::Rounded(28_dp)),
                Alignment::Center(), false, [&] {
                BasicText("Running for " + std::to_string(seconds.Get()) + " s", {}, { 22, kOnSurface });
            });
            // A text field: focus starts the platform input session (keyboard, InputConnection).
            auto& field = RememberTextFieldState("Edit me");
            Box(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), kContainer, CornerShape::Rounded(12_dp)),
                Alignment::CenterStart(), false, [&] {
                BasicTextFieldOptions opt;
                opt.textStyle = { 16, kOnSurface };
                opt.lineLimits = TextFieldLineLimits::SingleLine();
                BasicTextField(field, Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 16_dp), opt);
            });
            BasicText("Field text: " + field.TextUtf8(), Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
            // Long press "Drag me" and drop it on a field: the text goes in at the drop point.
            Box(DragAndDropSource(Background(Padding(Modifier{}, 16_dp, 0_dp), kContainer, CornerShape::Rounded(8_dp)), [](Offset) {
                    kiln::ui::DragAndDropTransferData d;
                    d.clip.items.push_back({ std::string(" dropped "), std::nullopt, std::nullopt });
                    d.metadata = { "kiln", { "text/plain" } };
                    d.flags = 256;   // View.DRAG_FLAG_GLOBAL: other apps may take it
                    return std::optional(d);
                }),
                Alignment::Center(), false, [] { BasicText("Drag me", Padding(Modifier{}, 12_dp, 8_dp), { 16, kOnSurface }); });
            // Opens the system document picker; the picked URI shows below.
            auto& pickedUri = kiln::rt::RememberSaveableState(std::string("none"));
            auto openDocument = RememberLauncherForActivityResult<std::vector<std::string>, contracts::UriResult>(
                std::make_shared<contracts::OpenDocument>(),
                [&pickedUri](const contracts::UriResult& uri) { pickedUri.Set(uri.value_or("cancelled")); });
            Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                          [openDocument] { openDocument.Launch({ "*/*" }); }),
                Alignment::Center(), false, [] { BasicText("Pick a document", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface }); });
            BasicText("Picked: " + pickedUri.Get(), Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
            // The configuration surface, and FLAG_SECURE on the window (screenshots come out black).
            {
                auto* c = kiln::rt::Composition::Current();
                const auto& config = c->Read(LocalConfiguration);
                BasicText(std::string("Dark theme: ") + (IsSystemInDarkTheme() ? "on" : "off") + ", 24-hour: " +
                              (c->Read(LocalIs24HourFormat) ? "on" : "off") + ", locale: " +
                              (config.locales.empty() ? std::string("?") : config.locales.front()),
                          Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
                kiln::ui::Window* window = c->Read(LocalWindow);
                const bool secure = window && window->IsFlagSecure();
                Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                              [window, secure] { if (window) window->SetFlagSecure(!secure); }),
                    Alignment::Center(), false, [secure] {
                    BasicText(secure ? "Secure window: on" : "Secure window: off", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface });
                });
            }
            // enableEdgeToEdge styles: the defaults (icons follow dark mode), or dark bars with a scrim.
            {
                auto& darkBars = kiln::rt::RememberSaveableState(false);
                kiln::ui::Owner* owner = kiln::rt::Composition::Current()->Read(LocalOwner);
                Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()), [&darkBars, owner] {
                        darkBars.Set(!darkBars.Peek());
                        if (!owner) return;
                        if (darkBars.Peek()) owner->EnableEdgeToEdge(kiln::ui::SystemBarStyle::Dark(Color(0x80000000u)), kiln::ui::SystemBarStyle::Dark(Color(0x80000000u)));
                        else owner->EnableEdgeToEdge();
                    }),
                    Alignment::Center(), false, [&darkBars] {
                    BasicText(darkBars.Get() ? "System bars: dark" : "System bars: auto", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface });
                });
            }
            // A popup menu under its button: focusable, so it takes key focus and the content under
            // it gets no touches; a press outside or Back dismisses it.
            {
                auto& menuOpen = kiln::rt::RememberSaveableState(false);
                auto& picked = kiln::rt::RememberSaveableState(std::string("nothing"));
                Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                              [&menuOpen] { menuOpen.Set(true); }),
                    Alignment::Center(), false, [&] {
                    BasicText("Open menu", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface });
                    if (menuOpen.Get()) {
                        PopupProperties props;
                        props.focusable = true;
                        // Below the button, its start edges lined up.
                        struct Below final : kiln::ui::PopupPositionProvider {
                            kiln::ui::IntOffset CalculatePosition(kiln::ui::IntRect a, kiln::ui::IntSize, LayoutDirection,
                                                                  kiln::ui::IntSize) const override { return { a.left, a.bottom + 16 }; }
                        };
                        static const auto below = std::make_shared<const Below>();
                        Popup(below, [&menuOpen] { menuOpen.Set(false); }, props, [&] {
                            Column(Background(Width(Modifier{}, 180_dp), kContainer, CornerShape::Rounded(12_dp)), [&] {
                                for (const char* item : { "Copy", "Share", "Delete" }) {
                                    Box(Clickable(FillMax(Modifier{}, Axis::Horizontal), [&menuOpen, &picked, item] {
                                            picked.Set(item);
                                            menuOpen.Set(false);
                                        }),
                                        Alignment::CenterStart(), false, [item] {
                                        BasicText(item, Padding(Modifier{}, 16_dp, 12_dp), { 16, kOnSurface });
                                    }, kiln::rt::Key(std::int64_t(std::hash<std::string>{}(item))));
                                }
                            });
                        });
                    }
                });
                BasicText("Menu picked: " + picked.Get(), Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
            }
            // Elevation shadows under the window's light (Modifier.shadow): a card at 6 dp, a circle
            // at 12 dp and a translucent card at 8 dp, whose shadow shows through it.
            Row(Padding(Modifier{}, 16_dp, 16_dp), Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                Box(Background(ShadowMod(SizeMod(Modifier{}, 96_dp, 64_dp), 6_dp, RoundedCorner(12_dp)), Color(0xFFFFFFFFu)),
                    Alignment::Center(), false, [] { BasicText("6 dp", {}, { 14, kOnSurface }); });
                Box(Background(ShadowMod(SizeMod(Modifier{}, 56_dp, 56_dp), 12_dp, CircleShape()), kPrimaryContainer));
                GraphicsLayerParams p;
                p.alpha = 0.6f;
                p.shadowElevation = 8 * kiln::rt::Composition::Current()->Read(LocalConfiguration).densityDpi / 160.0f;
                p.clip = true;
                p.clipShape = RoundedCorner(12_dp);
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 96_dp, 64_dp), p), Color(0xFFFFFFFFu)),
                    Alignment::Center(), false, [] { BasicText("alpha 0.6", {}, { 14, kOnSurface }); });
            });
            Row(Padding(Modifier{}, 16_dp, 24_dp), Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                const auto corners = RoundedCorner(CornerSize::Of(24_dp), CornerSize::Of(4_dp), CornerSize::Of(16_dp), CornerSize::Of(0_dp));
                const auto notch = std::make_shared<const NotchedShadowShape>();
                const auto ring = std::make_shared<const RingShadowShape>();
                for (const ShapePtr& shape : { corners, ShapePtr(notch), ShapePtr(ring) })
                    Box(Background(ShadowMod(SizeMod(Modifier{}, 64_dp, 64_dp), 8_dp, shape), Color(0xFFFFFFFFu), shape));
                GraphicsLayerParams p;
                p.rotationX = 18;
                p.rotationY = -24;
                p.shadowElevation = 12 * kiln::rt::Composition::Current()->Read(LocalConfiguration).densityDpi / 160.0f;
                p.clip = true;
                p.clipShape = RoundedCorner(12_dp);
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 64_dp, 64_dp), p), kPrimaryContainer, CornerShape::Rounded(12_dp)));
            });
            // Painted shadows: bitmap-sized gradient, inner gradient, a path with a hole, and
            // a block whose offset changes without recomposing the gallery.
            Row(Padding(Modifier{}, 16_dp, 16_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [] {
                using PaintedShadow = kiln::gfx::shadow::Shadow;
                const auto rounded = RoundedCorner(12_dp);
                const auto gradient = kiln::gfx::Brush::HorizontalGradient({ Color(0xb36750a4), Color(0xb3e8618c) });
                Box(Background(DropShadow(SizeMod(Modifier{}, 72_dp, 64_dp), rounded,
                                          PaintedShadow(6_dp, gradient, 2_dp, { 0_dp, 4_dp })), Color(0xffffffff), rounded),
                    Alignment::Center(), false, [] { BasicText("Drop", {}, { 14, kOnSurface }); });
                Box(InnerShadow(Background(SizeMod(Modifier{}, 72_dp, 64_dp), Color(0xffffffff), rounded), rounded,
                                PaintedShadow(6_dp, gradient, 2_dp, { 3_dp, 4_dp })),
                    Alignment::Center(), false, [] { BasicText("Inner", {}, { 14, kOnSurface }); });
                static const auto ring = std::make_shared<const RingShadowShape>();
                Box(InnerShadow(Background(DropShadow(SizeMod(Modifier{}, 64_dp, 64_dp), ring,
                                                       PaintedShadow(4_dp, Color(0x806750a4), 1_dp, { 0_dp, 3_dp })),
                                           Color(0xffffffff), ring), ring, PaintedShadow(4_dp, Color(0x80000000), 1_dp, { 2_dp, 2_dp })));
                auto& moved = RememberState(false);
                Box(Clickable(Background(DropShadow(SizeMod(Modifier{}, 72_dp, 64_dp), rounded, [&moved](DropShadowScope& s) {
                        s.radius = s.ToPx(6_dp);
                        s.spread = s.ToPx(2_dp);
                        s.offset = { s.ToPx(moved.Get() ? Dp(-6) : 6_dp), s.ToPx(4_dp) };
                        s.color = Color(0x806750a4);
                    }), Color(0xffffffff), rounded), [&moved] { moved.Set(!moved.Peek()); }),
                    Alignment::Center(), false, [] { BasicText("Move", {}, { 14, kOnSurface }); });
            });
            // Painter blending keeps bitmap alpha separate from the circle's coverage: Clear
            // removes the whole circle, Src leaves a soft ring, and an alpha-raising filter fills it.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::Top(), [] {
                for (int kind = 0; kind < 3; ++kind) {
                    Column(Modifier{}, Arrangement::SpacedBy(4_dp), AlignmentH::CenterHorizontally(), [kind] {
                        GraphicsLayerParams layer;
                        layer.compositingStrategy = CompositingStrategy::Offscreen;
                        Canvas(GraphicsLayer(SizeMod(Modifier{}, 72_dp, 64_dp), layer), [kind](DrawScope& d) {
                            d.DrawRect(kPrimary, {}, d.GetSize());
                            kiln::gfx::shadow::Shadow shadow(6_dp, Color(0x80000000), 2_dp);
                            shadow.blendMode = kind == 0 ? kiln::gfx::BlendMode::Clear : kiln::gfx::BlendMode::Src;
                            InnerShadowPainter painter(CircleShape(), shadow);
                            const auto filter = kiln::gfx::ColorFilter::ColorMatrix({ 0, 0, 0, 0, 64,
                                                                                   0, 0, 0, 0, 192,
                                                                                   0, 0, 0, 0, 192,
                                                                                   0, 0, 0, 0, 255 });
                            painter.Draw(d, d.GetSize(), 1, kind == 2 ? &filter : nullptr);
                        });
                        BasicText(kind == 0 ? "Clear" : kind == 1 ? "Src" : "Filter", {}, { 14, kOnSurface });
                    }, kiln::rt::Key(kind));
                }
            });
            // Ripples (MaterialTheme's indication): a bounded card clipped to its shape, an
            // unbounded 24 dp ripple, and a card whose focus shows the inset ring (Tab to it).
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [] {
                const auto rounded = RoundedCorner(12_dp);
                Box(Clickable(Background(Clip(SizeMod(Modifier{}, 96_dp, 64_dp), rounded), kPrimaryContainer), [] {}),
                    Alignment::Center(), false, [] { BasicText("Bounded", {}, { 14, kOnSurface }); });
                ClickableOptions unbounded;
                unbounded.indication = kiln::material3::Ripple({ .bounded = false, .radius = 24_dp });
                Box(Clickable(SizeMod(Modifier{}, 48_dp, 48_dp), [] {}, unbounded), Alignment::Center(), false, [] {
                    Box(Background(SizeMod(Modifier{}, 20_dp, 20_dp), kPrimary, CornerShape::Pill()));
                });
                auto* c = kiln::rt::Composition::Current();
                c->Provide(kiln::material3::LocalRippleThemeConfiguration, kiln::material3::RippleDefaults::InsetFocusRingThemeConfiguration(), [rounded] {
                    ClickableOptions ring;
                    ring.indication = kiln::material3::Ripple({ .focusRingShape = rounded });
                    Box(Clickable(Background(Clip(SizeMod(Modifier{}, 96_dp, 64_dp), rounded), kContainer), [] {}, ring),
                        Alignment::Center(), false, [] { BasicText("Focus ring", {}, { 14, kOnSurface }); });
                });
            });
            // Material foundation: surfaces (tonal elevation 3 dp with a border, a clickable one
            // at 2 dp shadow), the components' icons tinted by the content colour, and Material
            // Expressive shapes.
            {
                namespace m3 = kiln::material3;
                Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(12_dp), AlignmentV::CenterVertically(), [] {
                    m3::SurfaceOptions tonal;
                    tonal.shape = RoundedCorner(12_dp);
                    tonal.tonalElevation = 3_dp;
                    tonal.border = BorderStroke(1_dp, m3::MaterialTheme::colorScheme().outlineVariant);
                    m3::Surface(SizeMod(Modifier{}, 72_dp, 56_dp), tonal, [] {
                        Box(FillMax(Modifier{}, Axis::Both), Alignment::Center(), false, [] {
                            m3::Icon(m3::internal::Icons::Filled::Edit(), std::string("Edit"));
                        });
                    });
                    m3::SurfaceOptions raised;
                    raised.shape = RoundedCorner(16_dp);
                    raised.color = m3::MaterialTheme::colorScheme().primaryContainer;
                    raised.shadowElevation = 2_dp;
                    m3::Surface([] {}, SizeMod(Modifier{}, 72_dp, 56_dp), raised, [] {
                        Box(FillMax(Modifier{}, Axis::Both), Alignment::Center(), false, [] {
                            m3::Icon(m3::internal::Icons::Filled::DateRange(), std::string("Date"));
                        });
                    });
                    for (const auto* shape : { &m3::MaterialShapes::Cookie9Sided(), &m3::MaterialShapes::Clover4Leaf(), &m3::MaterialShapes::Heart() })
                        Box(Background(SizeMod(Modifier{}, 40_dp, 40_dp), m3::MaterialTheme::colorScheme().tertiary, ToShape(*shape)));
                });
                m3::TextOptions title;
                title.style = m3::MaterialTheme::typography().titleMedium;
                m3::Text("Material Text: titleMedium in the content colour", Padding(Modifier{}, 16_dp, 0_dp), title);
            }
            // The platform's Material You colours as a dynamic colour scheme (baseline when the
            // platform has none): primary, secondary, tertiary, surface container.
            {
                const bool dark = IsSystemInDarkTheme();
                const auto dynamic = dark ? kiln::material3::DynamicDarkColorScheme() : kiln::material3::DynamicLightColorScheme();
                const auto scheme = dynamic ? *dynamic : (dark ? kiln::material3::DarkColorScheme() : kiln::material3::LightColorScheme());
                Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [scheme, dynamic] {
                    for (Color c : { scheme.primary, scheme.secondary, scheme.tertiary, scheme.surfaceContainerHighest })
                        Box(Background(SizeMod(Modifier{}, 32_dp, 32_dp), c, CornerShape::Pill()));
                    BasicText(dynamic ? "Dynamic colour" : "Baseline colour", {}, { 14, kVariant });
                });
            }
            // A dialog in a window of its own: the app behind it dims; Back, Escape or a press
            // outside the card dismisses it.
            {
                auto& dialogOpen = kiln::rt::RememberSaveableState(false);
                auto& answer = kiln::rt::RememberSaveableState(std::string("none"));
                auto& dialogName = RememberTextFieldState("Draft");
                Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                              [&dialogOpen] { dialogOpen.Set(true); }),
                    Alignment::Center(), false, [] { BasicText("Open dialog", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface }); });
                if (dialogOpen.Get()) {
                    Dialog([&dialogOpen, &answer] { answer.Set("dismissed"); dialogOpen.Set(false); }, {}, [&] {
                        Column(Padding(Background(Width(Modifier{}, 300_dp), kContainer, CornerShape::Rounded(28_dp)), 24_dp, 24_dp), [&] {
                            BasicText("Rename", Padding(Modifier{}, 0_dp, 0_dp), { 24, kOnSurface });
                            // A field in the dialog window: it has its own keyboard session.
                            Box(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 0_dp, 16_dp), kPrimaryContainer,
                                           CornerShape::Rounded(8_dp)),
                                Alignment::CenterStart(), false, [&] {
                                BasicTextFieldOptions opt;
                                opt.textStyle = { 16, kOnSurface };
                                opt.lineLimits = TextFieldLineLimits::SingleLine();
                                BasicTextField(dialogName, Padding(FillMax(Modifier{}, Axis::Horizontal), 12_dp, 12_dp), opt);
                            });
                            Row(Modifier{}, Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [&] {
                                for (const char* choice : { "Cancel", "Save" }) {
                                    Box(Clickable(Modifier{}, [&dialogOpen, &answer, &dialogName, choice] {
                                            answer.Set(std::string(choice) == "Save" ? dialogName.TextUtf8() : std::string(choice));
                                            dialogOpen.Set(false);
                                        }),
                                        Alignment::Center(), false, [choice] {
                                        BasicText(choice, Padding(Modifier{}, 12_dp, 10_dp), { 14, kPrimary });
                                    }, kiln::rt::Key(std::int64_t(std::hash<std::string>{}(choice))));
                                }
                            });
                        });
                    });
                }
                BasicText("Dialog answer: " + answer.Get(), Padding(Modifier{}, 16_dp, 0_dp), { 14, kVariant });
            }
            // A password field: the last typed character shows briefly when the setting allows.
            auto& password = RememberTextFieldState("");
            Box(Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), kContainer, CornerShape::Rounded(12_dp)),
                Alignment::CenterStart(), false, [&] {
                BasicSecureTextFieldOptions opt;
                opt.textStyle = { 16, kOnSurface };
                // Credential Manager offers saved passwords for it (Android 15+).
                static const auto credentialRequest = [] {
                    auto r = std::make_shared<kiln::ui::CredentialRequestData>();
                    r->options = { kiln::ui::GetPasswordOption() };
                    return r;
                }();
                BasicSecureTextField(password,
                                     Semantics(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 16_dp),
                                               [](kiln::ui::SemanticsConfiguration& c) { c.credentialRequest = credentialRequest; }),
                                     opt);
            });
            auto& taps = kiln::rt::RememberSaveableState(0);
            auto& pressed = RememberState(false);
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                // Pressed colour animates in Oklab with a spring.
                const Color buttonColor = AnimateColorAsState(pressed.Get() ? Color(0xFF21005Du) : kPrimary,
                                                              kiln::anim::Spring(1.0f, kiln::anim::spring::StiffnessMediumLow));
                Box(Clickable(Background(Height(Modifier{}, 40_dp), buttonColor, CornerShape::Pill()),
                              [&taps] { taps.Set(taps.Peek() + 1); }, [&pressed](bool p) { pressed.Set(p); }),
                    Alignment::Center(), false, [&] {
                    BasicText("Tap me", Padding(Modifier{}, 24_dp, 0_dp), { 14, kOnPrimary });
                });
                BasicText("Tapped " + std::to_string(taps.Get()) + " times", {}, { 16, kOnSurface });
            });
            // Predictive back: the panel follows the gesture and closes when it completes.
            auto& panelOpen = kiln::rt::RememberSaveableState(false);
            auto& backProgress = RememberState(0.0f);
            PredictiveBackHandler(panelOpen.Get(), [&](BackProgress& p) -> kiln::rt::Task<> {
                while (auto e = co_await p.Next()) backProgress.Set(e->progress);
                backProgress.Set(0.0f);
                panelOpen.Set(false);
            });
            Box(Clickable(Background(Padding(Modifier{}, 16_dp, 0_dp), kPrimaryContainer, CornerShape::Pill()),
                          [&panelOpen] { panelOpen.Set(!panelOpen.Peek()); }),
                Alignment::Center(), false, [&] {
                BasicText(panelOpen.Get() ? "Close panel" : "Open panel", Padding(Modifier{}, 24_dp, 10_dp), { 14, kOnSurface });
            });
            AnimatedVisibility(panelOpen.Get(), Modifier{}, [&] {
                GraphicsLayerParams layer;
                layer.scaleX = layer.scaleY = 1.0f - 0.1f * backProgress.Get();
                Box(Background(GraphicsLayer(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), layer),
                               kTertiary, CornerShape::Rounded(16_dp)), Alignment::Center(), false, [&] {
                    BasicText("Panel: back closes it", Padding(Modifier{}, 16_dp, 24_dp), { 16, kOnPrimary });
                });
            }, ExpandVertically() + FadeIn(), ShrinkVertically() + FadeOut());
            // Rotation: a clipped card with text and a path, spinning.
            {
                auto& angle = RememberState(0.0f);
                c.LaunchedEffect({}, [&angle] { return Spin(angle); });
                // Read in the layer block: the spin redraws the card without recomposing the gallery.
                auto* anglePtr = &angle;
                Box(Padding(Modifier{}, 32_dp, 8_dp), Alignment::TopStart(), false, [&, anglePtr] {
                    Box(Background(GraphicsLayer(SizeMod(Modifier{}, 120_dp, 64_dp), [anglePtr](GraphicsLayerParams& spin) {
                            spin.rotationZ = anglePtr->Get();
                            spin.clip = true;
                            spin.shape = CornerShape::Rounded(12_dp);
                        }), kPrimaryContainer),
                        Alignment::Center(), false, [&] {
                        BasicText("Rotated", {}, { 18, kOnSurface });
                    });
                });
            }
            // 3D tilts: rotationX / rotationY in perspective (the default camera distance).
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                GraphicsLayerParams tiltX;
                tiltX.rotationX = 45;
                tiltX.clip = true;
                tiltX.shape = CornerShape::Rounded(12_dp);
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 140_dp, 80_dp), tiltX), kPrimary), Alignment::Center(), false, [] {
                    BasicText("rotationX 45", {}, { 16, kOnPrimary });
                });
                GraphicsLayerParams tiltY;
                tiltY.rotationY = 50;
                tiltY.clip = true;
                tiltY.shape = CornerShape::Rounded(12_dp);
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 140_dp, 80_dp), tiltY), kTertiary), Alignment::Center(), false, [] {
                    BasicText("rotationY 50", {}, { 16, kOnPrimary });
                });
            });
            // An unclipped tilt: the child hanging out of its parent turns with it.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::Start(), AlignmentV::CenterVertically(), [] {
                GraphicsLayerParams tilt;
                tilt.rotationY = -40;
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 80_dp, 60_dp), tilt), kPrimaryContainer), Alignment::TopStart(), false, [] {
                    Box(Background(SizeMod(OffsetMod(Modifier{}, 60_dp, 15_dp), 100_dp, 30_dp), kPrimary, CornerShape::Pill()),
                        Alignment::Center(), false, [] { BasicText("overflow", {}, { 12, kOnPrimary }); });
                });
            });
            // Group alpha: overlapping circles fade as one image (offscreen layer).
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                Box(Alpha(SizeMod(Modifier{}, 80_dp, 48_dp), 0.5f), Alignment::TopStart(), false, [&] {
                    Box(Background(SizeMod(Modifier{}, 48_dp, 48_dp), kPrimary, CornerShape::Pill()));
                    Box(Background(SizeMod(OffsetMod(Modifier{}, 32_dp, 0_dp), 48_dp, 48_dp), kTertiary, CornerShape::Pill()));
                });
                // CompositingStrategy::ModulateAlpha: each circle fades on its own; the overlap shows.
                GraphicsLayerParams modulate;
                modulate.alpha = 0.5f;
                modulate.compositingStrategy = CompositingStrategy::ModulateAlpha;
                Box(GraphicsLayer(SizeMod(Modifier{}, 80_dp, 48_dp), modulate), Alignment::TopStart(), false, [&] {
                    Box(Background(SizeMod(Modifier{}, 48_dp, 48_dp), kPrimary, CornerShape::Pill()));
                    Box(Background(SizeMod(OffsetMod(Modifier{}, 32_dp, 0_dp), 48_dp, 48_dp), kTertiary, CornerShape::Pill()));
                });
            });
            // Modifier.blur: Rectangle keeps the edges inside the bounds; Unbounded lets them spread.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                // RenderEffect chain: an offset of a decal blur, like a soft shadow.
                GraphicsLayerParams shadow;
                shadow.renderEffect = RenderEffect::Offset(24, 24, std::make_shared<RenderEffect>(RenderEffect::Blur(16, 16, kiln::gfx::TileMode::Decal)));
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 56_dp, 56_dp), shadow), kPrimary, CornerShape::Rounded(12_dp)));
                for (const bool bounded : { true, false }) {
                    Box(Blur(SizeMod(Modifier{}, 120_dp, 64_dp), 6_dp, bounded ? BlurredEdgeTreatment::Rectangle() : BlurredEdgeTreatment::Unbounded()),
                        Alignment::Center(), false, [bounded] {
                        Box(Background(FillMax(Modifier{}, Axis::Both), bounded ? kPrimary : kTertiary), Alignment::Center(), false, [bounded] {
                            BasicText(bounded ? "Rectangle" : "Unbounded", {}, { 14, kOnPrimary });
                        });
                    });
                }
            });
            // Nested clips: a rounded card around a full-size clipped child keeps its corners;
            // a rotated clipped child crossing the card's corners clips through a masked layer.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                Box(Clip(SizeMod(Modifier{}, 120_dp, 72_dp), CornerShape::Rounded(24_dp)), Alignment::Center(), false, [] {
                    Box(Background(ClipToBounds(FillMax(Modifier{}, Axis::Both)), kPrimary));
                });
                Box(Background(Clip(SizeMod(Modifier{}, 120_dp, 72_dp), CornerShape::Rounded(24_dp)), kContainer), Alignment::Center(), false, [] {
                    Box(Background(Clip(Rotate(SizeMod(Modifier{}, 150_dp, 40_dp), 25), CornerShape::Rounded(8_dp)), kTertiary));
                });
            });
            // Blend modes: a hole punched with DstOut in an offscreen layer; a layer that
            // composites with Difference; a layer through a desaturating colour filter.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [] {
                GraphicsLayerParams offscreen;
                offscreen.compositingStrategy = CompositingStrategy::Offscreen;
                Canvas(GraphicsLayer(SizeMod(Modifier{}, 72_dp, 72_dp), offscreen), [](DrawScope& d) {
                    const Size sz = d.GetSize();
                    d.DrawRoundRect(kPrimary, { 0, 0 }, sz, sz.width * 0.2f);
                    d.SetBlendMode(kiln::gfx::BlendMode::DstOut);
                    d.DrawRoundRect(kPrimary, { sz.width * 0.25f, sz.height * 0.25f }, { sz.width * 0.5f, sz.height * 0.5f }, sz.width * 0.25f);
                });
                Box(Background(SizeMod(Modifier{}, 72_dp, 72_dp), kTertiary, CornerShape::Rounded(12_dp)), Alignment::Center(), false, [] {
                    GraphicsLayerParams difference;
                    difference.blendMode = kiln::gfx::BlendMode::Difference;
                    Box(Background(GraphicsLayer(SizeMod(Modifier{}, 48_dp, 48_dp), difference), Color(0xffffffff), CornerShape::Pill()));
                });
                GraphicsLayerParams gray;
                gray.colorFilter = kiln::gfx::ColorFilter::ColorMatrix({ 0.2126f, 0.7152f, 0.0722f, 0, 0,
                                                                         0.2126f, 0.7152f, 0.0722f, 0, 0,
                                                                         0.2126f, 0.7152f, 0.0722f, 0, 0,
                                                                         0, 0, 0, 1, 0 });
                Row(GraphicsLayer(Modifier{}, gray), Arrangement::SpacedBy(4_dp), AlignmentV::CenterVertically(), [] {
                    Box(Background(SizeMod(Modifier{}, 32_dp, 72_dp), kPrimary, CornerShape::Rounded(8_dp)));
                    Box(Background(SizeMod(Modifier{}, 32_dp, 72_dp), kTertiary, CornerShape::Rounded(8_dp)));
                });
            });
            // graphics-shapes: a rounded square morphing into a rounded star and back.
            {
                static const kiln::gfx::shapes::Morph morph(
                    kiln::gfx::shapes::Rectangle(2, 2, { 0.4f }).Normalized(),
                    kiln::gfx::shapes::Star(8, 1, 0.7f, { 0.15f }).Normalized());
                auto& morphT = RememberState(0.0f);
                c.LaunchedEffect({}, [&morphT] { return Breathe(morphT); });
                Canvas(SizeMod(Padding(Modifier{}, 16_dp, 0_dp), 72_dp, 72_dp), [&morphT](DrawScope& d) {
                    kiln::gfx::Path p = MorphToPath(morph, morphT.Get(), 0);
                    p.Transform(kiln::gfx::Matrix::Scale(d.GetSize().width, d.GetSize().height));
                    d.DrawPath(p, kiln::gfx::Brush::LinearGradient({ kPrimary, kTertiary }));
                });
            }
            // PolygonShape: a rounded rectangle morphing into a rotated, rounded pill star, as a
            // background shape (the morph progress is read at draw time).
            {
                static const auto rect = PolygonShape::Rectangle(PolygonCornerRounding::Fraction(0.25f));
                static const auto star = [] {
                    auto s = PolygonShape::PillStar(8, 0.6f, 0.5f, 0, PolygonCornerRounding::Fraction(0.1f));
                    s->Transform([](PolygonShapeTransformScope& t) { t.Rotate(22.5f); });
                    return s;
                }();
                auto& polyT = RememberState(0.0f);
                c.LaunchedEffect({}, [&polyT] { return Breathe(polyT); });
                auto& shape = c.Remember<ShapePtr>([&polyT] { return MorphPolygonShape(rect, star, [&polyT] { return polyT.Get(); }); });
                Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                    Box(Background(SizeMod(Modifier{}, 120_dp, 72_dp), kTertiary, shape));
                    Box(Background(SizeMod(Modifier{}, 72_dp, 72_dp), kPrimary, star));
                });
            }
            // anchoredDraggable: a thumb that snaps to either end of its track.
            {
                auto& knob = c.RememberNew<AnchoredDraggableState<bool>>({}, {}, false);
                Box(AnchoredDraggable(OnSizeChanged(Background(SizeMod(Padding(Modifier{}, 16_dp, 0_dp), 200_dp, 48_dp), kContainer, CornerShape::Pill()),
                                                    [&knob](IntSize s) { knob.UpdateAnchors({ { false, 0 }, { true, float(s.width - s.height) } }); }),
                                      knob, Orientation::Horizontal),
                    Alignment::TopStart(), false, [&knob] {
                    Box(Background(SizeMod(OffsetMod(Modifier{}, [&knob](const Density&) {
                            const float o = knob.Offset();
                            return IntOffset{ std::isnan(o) ? 0 : int(std::lround(o)), 0 };
                        }), 48_dp, 48_dp), knob.SettledValue() ? kPrimary : kVariant, CornerShape::Pill()));
                });
            }
            // A lazy horizontal grid: two rows, every fifth cell spanning both.
            {
                auto& gridState = RememberLazyGridState();
                LazyGridOptions go;
                go.contentPadding = { 16_dp, 0_dp, 16_dp, 0_dp };
                go.mainAxisArrangement = Arrangement::SpacedBy(8_dp);
                go.crossAxisArrangement = Arrangement::SpacedBy(8_dp);
                LazyHorizontalGrid(Height(FillMax(Modifier{}, Axis::Horizontal), 120_dp), GridCells::Fixed(2), gridState, [](LazyGridScope& s) {
                    s.Items(60, [](int i) {
                        Box(Background(Width(Modifier{}, i % 5 == 0 ? 96_dp : 56_dp), i % 5 == 0 ? kTertiary : kPrimaryContainer, CornerShape::Rounded(12_dp)),
                            Alignment::Center(), false, [i] {
                            BasicText(std::to_string(i), {}, { 14, i % 5 == 0 ? kOnPrimary : kOnSurface });
                        });
                    }, {}, [](const GridItemSpanScope& sc, int i) { return i % 5 == 0 ? sc.maxLineSpan : 1; });
                }, go);
            }
            // A horizontal pager of cards with page dots.
            {
                auto& pager = RememberPagerState([] { return 6; });
                PagerOptions po;
                po.contentPadding = { 16_dp, 0_dp, 16_dp, 0_dp };
                po.pageSpacing = 8_dp;
                HorizontalPager(Height(FillMax(Modifier{}, Axis::Horizontal), 140_dp), pager, [](int page) {
                    const Color colors[] = { kPrimary, kTertiary, kPrimaryContainer, kVariant, Color(0xFFB3261Eu), Color(0xFF386A20u) };
                    Box(Background(FillMax(Modifier{}, Axis::Both), colors[page % 6], CornerShape::Rounded(24_dp)), Alignment::Center(), false, [page] {
                        BasicText("Page " + std::to_string(page + 1), {}, { 24, page == 2 ? kOnSurface : kOnPrimary });
                    });
                }, po);
                Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(6_dp), AlignmentV::CenterVertically(), [&pager] {
                    for (int i = 0; i < 6; ++i)
                        Box(Background(SizeMod(Modifier{}, 8_dp, 8_dp), pager.CurrentPage() == i ? kPrimary : kPrimaryContainer, CornerShape::Pill()));
                });
            }
            // A staggered grid: three lanes of tiles of different heights, a full-width banner.
            {
                auto& stagState = c.RememberNew<LazyStaggeredGridState>({}, {});
                LazyStaggeredGridOptions so;
                so.contentPadding = { 16_dp, 0_dp, 16_dp, 0_dp };
                so.mainAxisItemSpacing = 8_dp;
                so.crossAxisArrangement = Arrangement::SpacedBy(8_dp);
                LazyVerticalStaggeredGrid(Height(FillMax(Modifier{}, Axis::Horizontal), 220_dp), GridCells::Fixed(3), stagState,
                                          [](LazyStaggeredGridScope& s) {
                    s.Items(40, [](int i) {
                        const Dp h = i == 6 ? 40_dp : Dp(float(48 + (i * 37) % 64));
                        Box(Background(Height(Modifier{}, h), i == 6 ? kTertiary : (i % 3 == 0 ? kPrimary : kPrimaryContainer), CornerShape::Rounded(12_dp)),
                            Alignment::Center(), false, [i] {
                            BasicText(i == 6 ? "Full line" : std::to_string(i), {}, { 14, i == 6 || i % 3 == 0 ? kOnPrimary : kOnSurface });
                        });
                    }, {}, [](int i) { return i == 6; });
                }, so);
            }
            // Mixed text sizes sharing a baseline (alignByBaseline).
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(8_dp), AlignmentV::Top(), [&] {
                BasicText("Baseline", AlignByBaseline({}), { 32, kOnSurface });
                BasicText("shared by", AlignByBaseline({}), { 14, kVariant });
                BasicText("three sizes", AlignByBaseline({}), { 20, kPrimary });
            });
            // Paths: even-odd star, filled heart, stroked wave with round caps, bevel-joined zigzag.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    kiln::gfx::Path star;
                    star.fillType = kiln::gfx::FillType::EvenOdd;
                    const float cx = d.GetSize().width / 2, cy = d.GetSize().height / 2, r = d.GetSize().width / 2;
                    for (int i = 0; i < 5; ++i) {
                        const float a = -1.5707964f + float(i) * 2.5132742f;   // 144 degrees: a pentagram
                        if (i == 0) star.MoveTo(cx + r * std::cos(a), cy + r * std::sin(a));
                        else star.LineTo(cx + r * std::cos(a), cy + r * std::sin(a));
                    }
                    star.Close();
                    d.DrawPath(star, kPrimary);
                });
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height;
                    kiln::gfx::Path heart;
                    heart.MoveTo(w / 2, h * 0.9f);
                    heart.CubicTo(w * -0.1f, h * 0.45f, w * 0.15f, h * -0.05f, w / 2, h * 0.28f);
                    heart.CubicTo(w * 0.85f, h * -0.05f, w * 1.1f, h * 0.45f, w / 2, h * 0.9f);
                    heart.Close();
                    d.DrawPath(heart, kTertiary);
                });
                Canvas(SizeMod(Modifier{}, 96_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height;
                    kiln::gfx::Path wave;
                    wave.MoveTo(d.ToPx(6_dp), h / 2);
                    for (int i = 0; i < 4; ++i) {
                        const float x0 = d.ToPx(6_dp) + (w - d.ToPx(12_dp)) * float(i) / 4, step = (w - d.ToPx(12_dp)) / 4;
                        wave.QuadraticTo(x0 + step / 2, i % 2 ? h * 0.85f : h * 0.15f, x0 + step, h / 2);
                    }
                    kiln::gfx::Stroke stroke{ d.ToPx(4_dp), 4, kiln::gfx::StrokeCap::Round, kiln::gfx::StrokeJoin::Round };
                    d.DrawPath(wave, kPrimary, &stroke);
                });
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height, m = d.ToPx(6_dp);
                    kiln::gfx::Path tri;
                    tri.MoveTo(w / 2, m); tri.LineTo(w - m, h - m); tri.LineTo(m, h - m); tri.Close();
                    kiln::gfx::Stroke stroke{ d.ToPx(3_dp), 4, kiln::gfx::StrokeCap::Butt, kiln::gfx::StrokeJoin::Bevel };
                    d.DrawPath(tri, kVariant, &stroke);
                });
            });
            // Progressive blur: sharp at the top fading to blurred (vertical), and around a centre.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                for (int k = 0; k < 2; ++k) {
                    const BlurRadiusSpec spec = k == 0 ? BlurRadiusSpec::VerticalGradient(0_dp, 12_dp)
                                                       : BlurRadiusSpec::RadialGradient(0_dp, 12_dp);
                    Column(Blur(Clip(SizeMod(Modifier{}, 120_dp, 80_dp), CornerShape::Rounded(12_dp)), spec), [&] {
                        for (int i = 0; i < 4; ++i)
                            Box(Background(SizeMod(Modifier{}, 120_dp, 20_dp), i % 2 ? kTertiary : kPrimary), Alignment::Center(), false, [&] {
                                BasicText("progressive", {}, { 12, kOnPrimary });
                            });
                    });
                }
            });
            // Mesh gradients: a 2x2 grid of Bezier patches, bilinear and Catmull-Rom colours.
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                for (const bool bicubic : { false, true }) {
                    auto& c2 = *kiln::rt::Composition::Current();
                    auto& mesh = c2.Remember<std::shared_ptr<Painter>>([bicubic] {
                        return std::make_shared<MeshGradientPainter>(2, 2, bicubic, [](MeshGradientScope& m) {
                            const Color colors[3][3] = { { Color(0xffffd8e4), Color(0xffeaddff), Color(0xffd0bcff) },
                                                         { Color(0xff7d5260), Color(0xffb69df8), Color(0xff6750a4) },
                                                         { Color(0xff21005d), Color(0xff4f378b), Color(0xffffb4ab) } };
                            for (int r = 0; r <= 2; ++r)
                                for (int c = 0; c <= 2; ++c) {
                                    // The middle vertex pulled off-centre bends the patches.
                                    const Offset p = r == 1 && c == 1 ? Offset{ 0.6f, 0.4f } : Offset{ c / 2.0f, r / 2.0f };
                                    m.SetVertex(r, c, p, colors[r][c]);
                                }
                        });
                    }, kiln::rt::Keys(int(bicubic)));
                    Box(Paint(Clip(SizeMod(Modifier{}, 120_dp, 80_dp), CornerShape::Rounded(16_dp)), mesh, false, Alignment::Center(), ContentScale::FillBounds()));
                }
            });
            // Stamped path effects: dots along a wave (Translate), arrows along an arc (Rotate).
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                Canvas(SizeMod(Modifier{}, 128_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height;
                    kiln::gfx::Path wave;
                    wave.MoveTo(d.ToPx(6_dp), h / 2);
                    for (int i = 0; i < 4; ++i) {
                        const float x0 = d.ToPx(6_dp) + (w - d.ToPx(12_dp)) * float(i) / 4, step = (w - d.ToPx(12_dp)) / 4;
                        wave.QuadraticTo(x0 + step / 2, i % 2 ? h * 0.85f : h * 0.15f, x0 + step, h / 2);
                    }
                    kiln::gfx::Path dot;
                    const float r = d.ToPx(3_dp);
                    dot.AddOval({ -r, -r, r, r });
                    kiln::gfx::Stroke stroke{ 1 };
                    stroke.pathEffect = kiln::gfx::PathEffect::Stamped(dot, d.ToPx(10_dp), 0, kiln::gfx::PathEffect::StampStyle::Translate);
                    d.DrawPath(wave, kPrimary, &stroke);
                });
                Canvas(SizeMod(Modifier{}, 72_dp, 56_dp), [](DrawScope& d) {
                    const float w = d.GetSize().width, h = d.GetSize().height, m = d.ToPx(8_dp);
                    kiln::gfx::Path arc;
                    arc.ArcTo({ m, m, w - m, h * 2 - m }, 180, 180, true);
                    kiln::gfx::Path arrow;
                    const float a = d.ToPx(5_dp);
                    arrow.MoveTo(a, 0); arrow.LineTo(-a, -a * 0.7f); arrow.LineTo(-a, a * 0.7f); arrow.Close();
                    kiln::gfx::Stroke stroke{ 1 };
                    stroke.pathEffect = kiln::gfx::PathEffect::Stamped(arrow, d.ToPx(14_dp), 0, kiln::gfx::PathEffect::StampStyle::Rotate);
                    d.DrawPath(arc, kTertiary, &stroke);
                });
            });
            // Gradients: linear background, radial circle, sweep ring stroke, repeated diagonal.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                using kiln::gfx::Brush;
                Box(Background(SizeMod(Modifier{}, 96_dp, 56_dp), Brush::HorizontalGradient({ kPrimary, kTertiary }), CornerShape::Pill()));
                Box(Background(SizeMod(Modifier{}, 56_dp, 56_dp), Brush::RadialGradient({ kPrimaryContainer, kPrimary }), CornerShape::Pill()));
                Canvas(SizeMod(Modifier{}, 56_dp, 56_dp), [](DrawScope& d) {
                    kiln::gfx::Path ring;
                    const float w = d.GetSize().width, inset = d.ToPx(6_dp);
                    ring.AddOval({ inset, inset, w - inset, w - inset });
                    kiln::gfx::Stroke stroke{ d.ToPx(6_dp) };
                    d.DrawPath(ring, Brush::SweepGradient({ kPrimary, kTertiary, Color(0xFFFFB4ABu), kPrimary }), &stroke);
                });
                Box(Background(SizeMod(Modifier{}, 56_dp, 56_dp),
                               Brush::LinearGradient({ kPrimaryContainer, kPrimary }, { 0, 0 }, { 24, 24 }, kiln::gfx::TileMode::Mirror),
                               CornerShape::Rounded(12_dp)));
            });
            // Images: a decoded PNG at its intrinsic size, scaled down with Fit, and cropped.
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                auto& c2 = *kiln::rt::Composition::Current();
                const auto& orb = c2.Remember<kiln::gfx::ImageBitmap>([] {
                    auto bytes = kiln::ReadAsset("orb.png");
                    return bytes ? kiln::gfx::ImageBitmap::Decode(*bytes) : kiln::gfx::ImageBitmap{};
                });
                Image(orb, "Orb");
                Image(orb, "Small orb", SizeMod(Modifier{}, 32_dp, 32_dp));
                Image(orb, "Cropped orb", SizeMod(Modifier{}, 96_dp, 32_dp), Alignment::Center(), ContentScale::Crop());
                // Clip by a path (GenericShape): a hexagon over a gradient, a star over the image.
                static const auto hexagon = std::make_shared<kiln::gfx::GenericShape>([](kiln::gfx::Path& p, Size s, LayoutDirection) {
                    for (int i = 0; i < 6; ++i) {
                        const float a = 3.14159265f / 3 * float(i);
                        const float x = s.width / 2 + s.width / 2 * std::cos(a), y = s.height / 2 + s.height / 2 * std::sin(a);
                        if (i == 0) p.MoveTo(x, y); else p.LineTo(x, y);
                    }
                    p.Close();
                });
                static const auto star = std::make_shared<kiln::gfx::GenericShape>([](kiln::gfx::Path& p, Size s, LayoutDirection) {
                    for (int i = 0; i < 10; ++i) {
                        const float a = -1.5707964f + 3.14159265f / 5 * float(i), r = (i % 2 ? 0.45f : 1.0f) * s.width / 2;
                        const float x = s.width / 2 + r * std::cos(a), y = s.height / 2 + r * std::sin(a);
                        if (i == 0) p.MoveTo(x, y); else p.LineTo(x, y);
                    }
                    p.Close();
                });
                Box(Background(Clip(SizeMod(Modifier{}, 48_dp, 48_dp), hexagon),
                               kiln::gfx::Brush::LinearGradient({ kPrimary, kTertiary })));
                Image(orb, "Star orb", Clip(SizeMod(Modifier{}, 48_dp, 48_dp), star), Alignment::Center(), ContentScale::Crop());
                // ColorFilter.tint (SrcIn) and a greyscale colour matrix.
                Image(orb, "Tinted orb", SizeMod(Modifier{}, 32_dp, 32_dp), Alignment::Center(), ContentScale::Fit(), 1,
                      kiln::gfx::ColorFilter::Tint(kTertiary));
                Image(orb, "Grey orb", SizeMod(Modifier{}, 32_dp, 32_dp), Alignment::Center(), ContentScale::Fit(), 1,
                      kiln::gfx::ColorFilter::ColorMatrix({ 0.3f, 0.59f, 0.11f, 0, 0, 0.3f, 0.59f, 0.11f, 0, 0,
                                                            0.3f, 0.59f, 0.11f, 0, 0, 0, 0, 0, 1, 0 }));
            });
            // FilterQuality: a 4x4 checkerboard scaled up, nearest (None) and bilinear (Low).
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                auto& c2 = *kiln::rt::Composition::Current();
                const auto& checker = c2.Remember<kiln::gfx::ImageBitmap>([] {
                    std::vector<std::uint8_t> px(4 * 4 * 4);
                    for (int i = 0; i < 16; ++i) {
                        const bool dark = ((i % 4) + (i / 4)) % 2 == 0;
                        px[i * 4 + 0] = dark ? 0x67 : 0xff; px[i * 4 + 1] = dark ? 0x50 : 0xff; px[i * 4 + 2] = dark ? 0xa4 : 0xff; px[i * 4 + 3] = 0xff;
                    }
                    return kiln::gfx::ImageBitmap::FromPixels(4, 4, px);
                });
                for (const auto q : { kiln::gfx::FilterQuality::None, kiln::gfx::FilterQuality::Low })
                    Image(checker, q == kiln::gfx::FilterQuality::None ? "Nearest" : "Bilinear", SizeMod(Modifier{}, 48_dp, 48_dp),
                          Alignment::Center(), ContentScale::FillBounds(), 1, std::nullopt, q);
            });
            Row(Padding(Modifier{}, 16_dp, 0_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [&] {
                // Rounded clip around a child that overflows it.
                // Modifiers wrap what follows them: clip before the background so both are clipped.
                Box(Background(Clip(SizeMod(Modifier{}, 64_dp, 64_dp), CornerShape::Rounded(20_dp)), kPrimaryContainer),
                    Alignment::TopStart(), false, [&] {
                    Box(Background(OffsetMod(RequiredSize(Modifier{}, 80_dp, 80_dp), 24_dp, 24_dp), kTertiary));
                });
                Box(Background(Alpha(SizeMod(Modifier{}, 64_dp, 64_dp), 0.5f), kPrimary, CornerShape::Rounded(12_dp)));
                GraphicsLayerParams scaled;
                scaled.scaleX = scaled.scaleY = 0.75f;
                Box(Background(GraphicsLayer(SizeMod(Modifier{}, 64_dp, 64_dp), scaled), kTertiary, CornerShape::Pill()));
            });
            BasicText("Text wraps to the width it is given. The paragraph below the counter is laid out by the "
                      "same platform path as before, now measured by a layout node.",
                      Padding(Modifier{}, 16_dp, 0_dp), { 16, kVariant });
            for (int i = 1; i <= 30; ++i) {
                Box(Background(Height(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), 56_dp), kContainer, CornerShape::Rounded(12_dp)),
                    Alignment::CenterStart(), false, [i] {
                    BasicText("Row " + std::to_string(i), Padding(Modifier{}, 16_dp, 0_dp), { 16, kOnSurface });
                }, kiln::rt::Key(i));
            }
            Spacer(Height(Modifier{}, 32_dp));
        });
    }
}

extern "C" void kiln_main(kiln_app* app) {
    kiln::material3::ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled = true;
    kiln::SetContent(app, [] { kiln::material3::MaterialTheme([] { Gallery(); }); });
}
