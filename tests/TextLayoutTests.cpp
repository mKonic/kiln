#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/text/Paragraph.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/Semantics.h"
#include "kiln/ui/Pointer.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace kiln;
using namespace kiln::text;

namespace {
    // Monospace stand-in: each UTF-16 unit is half the run's size wide, a line is 1.25 sizes
    // tall (or the line height), wraps anywhere at the width and at '\n'.
    class FakeText final : public TextBackend {
    public:
        int layouts = 0;
        ParagraphData Layout(const ParagraphRequest& r) override {
            ++layouts;
            ParagraphData d;
            const std::u16string text = ToU16(r.text);
            const float width = r.widthPx > 0 ? r.widthPx : INFINITY;
            const float lineH = !std::isnan(r.lineHeightPx) ? r.lineHeightPx : r.base.sizePx * 1.25f;
            int start = 0;
            float x = 0;
            auto push = [&](int end, int next) {
                LineData l;
                l.top = float(d.lines.size()) * lineH;
                l.bottom = l.top + lineH;
                l.baseline = l.top + lineH * 0.8f;
                l.start = start;
                l.end = next;
                l.visibleEnd = end;
                l.right = x;
                d.width = std::max(d.width, x);
                d.lines.push_back(l);
                start = next;
                x = 0;
            };
            for (int i = 0; i < int(text.size()); ++i) {
                if (text[std::size_t(i)] == u'\n') { push(i, i + 1); continue; }
                const float w = Advance(r, i);
                if (x + w > width && i > start) push(i, i);
                x += w;
            }
            push(int(text.size()), int(text.size()));
            // Placeholders: above the baseline, at their first character.
            for (const auto& ph : r.placeholders) {
                std::optional<RectF> rect;
                for (const auto& l : d.lines) {
                    if (ph.start < l.start || ph.start >= std::max(l.end, l.start + 1)) continue;
                    float x = 0;
                    for (int k = l.start; k < ph.start; ++k) x += Advance(r, k);
                    const float h = ph.heightEm ? ph.height * r.base.sizePx : ph.height;
                    rect = RectF{ x, l.baseline - h, ph.widthEm ? ph.width * r.base.sizePx : ph.width, h };
                    break;
                }
                d.placeholderRects.push_back(rect);
            }
            if (int(d.lines.size()) > r.maxLines) {
                d.lines.resize(std::size_t(r.maxLines));
                d.didExceedMaxLines = true;
            }
            d.lineCount = int(d.lines.size());
            d.height = d.lines.back().bottom;
            d.firstBaseline = d.lines.front().baseline;
            d.lastBaseline = d.lines.back().baseline;
            d.handle = ++m_Next;
            m_Laid[d.handle] = { r.base.sizePx * 0.5f, d.lines };
            m_Texts[d.handle] = text;
            return d;
        }
        ParagraphIntrinsicWidths Intrinsics(const ParagraphRequest& r) override {
            const std::u16string text = ToU16(r.text);
            float line = 0, word = 0, maxLine = 0, maxWord = 0;
            for (int i = 0; i < int(text.size()); ++i) {
                const char16_t c = text[std::size_t(i)];
                if (c == u'\n') { line = 0; word = 0; continue; }
                const float w = Advance(r, i);
                line += w;
                word = c == u' ' ? 0 : word + w;
                maxLine = std::max(maxLine, line);
                maxWord = std::max(maxWord, word);
            }
            return { maxWord, maxLine };
        }
        void SyncAtlas(AtlasSink&) override {}
        float GetHorizontalPosition(std::int64_t h, int o, bool) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return float(o - l.start) * cw;
        }
        CursorRect GetCursorRect(std::int64_t h, int o) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return { float(o - l.start) * cw, l.top, l.bottom };
        }
        int GetOffsetForPosition(std::int64_t h, float x, float y) override {
            const auto& [cw, lines] = Laid(h);
            const LineData* l = &lines.back();
            for (const auto& line : lines) if (y < line.bottom) { l = &line; break; }
            return std::clamp(l->start + int(std::lround(x / cw)), l->start, l->visibleEnd);
        }
        RectF GetBoundingBox(std::int64_t h, int o) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return { float(o - l.start) * cw, l.top, cw, l.bottom - l.top };
        }
        std::vector<RectF> GetSelectionRects(std::int64_t h, int start, int end) override {
            std::vector<RectF> out;
            const auto& [cw, lines] = m_Laid[h];
            for (const auto& l : lines) {
                const int s = std::max(start, l.start), e = std::min(end, l.visibleEnd);
                if (s < e) out.push_back({ float(s - l.start) * cw, l.top, float(e - s) * cw, l.bottom - l.top });
            }
            return out;
        }
        // Words are runs of letters and digits; between words the boundary is empty.
        std::pair<int, int> GetWordBoundary(std::int64_t h, int o) override {
            const std::u16string& t = m_Texts[h];
            auto word = [&](int i) { return i >= 0 && i < int(t.size()) && std::isalnum(int(t[std::size_t(i)])); };
            if (!word(o) && !word(o - 1)) return { o, o };
            int a = o, b = o;
            while (word(a - 1)) --a;
            while (word(b)) ++b;
            return { a, b };
        }
        bool IsRtlCharAt(std::int64_t, int) override { return false; }
        void Release(std::int64_t h) override { ++released; m_Laid.erase(h); m_Texts.erase(h); }
        int released = 0;

    private:
        static std::u16string ToU16(const std::string& s) {
            std::u16string out;
            for (unsigned char c : s) out.push_back(c);   // ASCII only in these tests
            return out;
        }
        static float Advance(const ParagraphRequest& r, int i) {
            for (const auto& ph : r.placeholders)
                if (i >= ph.start && i < ph.end) return i == ph.start ? (ph.widthEm ? ph.width * r.base.sizePx : ph.width) : 0;
            for (const auto& run : r.runs)
                if (i >= run.start && i < run.end) return run.sizePx * 0.5f;
            return r.base.sizePx * 0.5f;
        }
        const std::pair<float, std::vector<LineData>>& Laid(std::int64_t h) { return m_Laid[h]; }
        static const LineData& LineOf(const std::vector<LineData>& lines, int o) {
            for (const auto& l : lines) if (o < l.end) return l;
            return lines.back();
        }
        std::int64_t m_Next = 0;
        std::map<std::int64_t, std::pair<float, std::vector<LineData>>> m_Laid;
        std::map<std::int64_t, std::u16string> m_Texts;
    };
}

TEST(FontScalingIsNonlinearAboveOnePointZeroThree) {
    // Linear at 1 and below the threshold.
    CHECK_NEAR(ui::FontScaleSpToDp(1.0f, 14), 14, 1e-4f);
    CHECK_NEAR(ui::FontScaleSpToDp(1.02f, 10), 10.2f, 1e-4f);
    // The 2.0 table: small sizes double, large ones grow less.
    CHECK_NEAR(ui::FontScaleSpToDp(2.0f, 8), 16, 1e-4f);
    CHECK_NEAR(ui::FontScaleSpToDp(2.0f, 14), 26, 1e-4f);
    CHECK_NEAR(ui::FontScaleSpToDp(2.0f, 100), 100, 1e-4f);
    CHECK_NEAR(ui::FontScaleSpToDp(2.0f, 5), 10, 1e-4f);   // below the table: towards 0
    // Past the last table: linear.
    CHECK_NEAR(ui::FontScaleSpToDp(3.0f, 50), 150, 1e-3f);
    // Between tables: interpolated, under linear for large sizes.
    CHECK_NEAR(ui::FontScaleSpToDp(1.06f, 8), 8 * 1.06f, 0.05f);
    CHECK(ui::FontScaleSpToDp(1.06f, 100) < 106);
    CHECK_NEAR(ui::FontScaleSpToDp(1.9999f, 10), 20, 0.05f);
    // And back.
    CHECK_NEAR(ui::FontScaleDpToSp(2.0f, 26), 14, 1e-4f);
    const ui::Density d{ 2.0f, 1.5f };
    CHECK_NEAR(d.SpToPx(14), 44, 1e-3f);   // 22 dp at 1.5
}

TEST(FontMatcherFollowsCssWeightRules) {
    auto font = [](int w, FontStyle s = FontStyle::Normal) { Font f; f.path = std::to_string(w); f.weight = { w }; f.style = s; return f; };
    const std::vector<Font> fonts{ font(300), font(500), font(700), font(400, FontStyle::Italic) };
    // 400..500 looks up to 500 first.
    CHECK(MatchFont(fonts, FontWeight::W400(), FontStyle::Normal).front().weight == FontWeight::W500());
    // Above 500: heavier first.
    CHECK(MatchFont(fonts, FontWeight::W600(), FontStyle::Normal).front().weight == FontWeight::W700());
    // Below 400: lighter first.
    CHECK(MatchFont(fonts, FontWeight::W200(), FontStyle::Normal).front().weight == FontWeight::W300());
    CHECK(MatchFont(fonts, FontWeight::W100(), FontStyle::Normal).front().weight == FontWeight::W300());
    // The style wins over weight when any font has it.
    const auto italic = MatchFont(fonts, FontWeight::W800(), FontStyle::Italic);
    CHECK(italic.size() == 1 && italic.front().style == FontStyle::Italic);
    CHECK(MatchFont({}, FontWeight::W400(), FontStyle::Normal).empty());
}

TEST(SpanStylesFlattenIntoRuns) {
    AnnotatedStringBuilder b;
    b.Append("abcdef");
    SpanStyle bold;
    bold.fontWeight = FontWeight::Bold();
    SpanStyle red;
    red.color = ui::Color(0xFFFF0000u);
    b.AddStyle(bold, 1, 4);
    b.AddStyle(red, 2, 5);
    const auto runs = FlattenSpanStyles(b.ToAnnotatedString(), {});
    CHECK(runs.size() == 5);
    CHECK(runs[0].start == 0 && runs[0].end == 1 && !runs[0].item.fontWeight);
    CHECK(runs[1].start == 1 && runs[1].end == 2 && runs[1].item.fontWeight == FontWeight::Bold() && !runs[1].item.color.Specified());
    CHECK(runs[2].start == 2 && runs[2].end == 4 && runs[2].item.fontWeight && runs[2].item.color == red.color);
    CHECK(runs[3].start == 4 && runs[3].end == 5 && !runs[3].item.fontWeight && runs[3].item.color == red.color);
    CHECK(runs[4].start == 5 && runs[4].end == 6);
    // Equal neighbours join; empty text is one empty run.
    AnnotatedStringBuilder same;
    same.Append("abcd");
    same.AddStyle(bold, 0, 2);
    same.AddStyle(bold, 2, 4);
    CHECK(FlattenSpanStyles(same.ToAnnotatedString(), {}).size() == 1);
    CHECK(FlattenSpanStyles(AnnotatedString(""), {}).size() == 1);
}

TEST(ParagraphRequestsResolveSizesFontsAndLineHeight) {
    const ui::Density d{ 2.0f, 1.0f };
    AnnotatedStringBuilder b;
    b.Append("Hello world");
    SpanStyle big;
    big.fontSize = Sp(15.3f);
    b.AddStyle(big, 6, 11);
    TextStyle style = ResolveDefaults(TextStyle(Sp(14.3f)), ui::LayoutDirection::Ltr);
    style.paragraph.lineHeight = Sp(20);
    std::vector<SpanStyle> runStyles;
    const auto r = BuildParagraphRequest(b.ToAnnotatedString(), style, d, {}, &runStyles);
    CHECK(r.runs.size() == 2 && runStyles.size() == 2);
    CHECK_NEAR(r.base.sizePx, 28.6f, 1e-4f);         // the paragraph size is exact
    CHECK_NEAR(r.runs[0].sizePx, 28.6f, 1e-4f);
    CHECK_NEAR(r.runs[1].sizePx, 31.0f, 1e-4f);      // a span's rounds (AbsoluteSizeSpan)
    CHECK_NEAR(r.lineHeightPx, 40, 1e-4f);
    CHECK(!r.hasLineHeightStyle && r.lineHeightTrim == 0x11);
    CHECK(r.widthPx == 0 && r.textAlign == std::uint8_t(TextAlign::Start));
    // Run styles hold only what the spans set (colours come from the style when drawn).
    CHECK(!runStyles[0].fontSize.IsSpecified() && runStyles[1].fontSize == Sp(15.3f));
    // Em line heights scale the font size; nonlinear scaling keeps the sp multiplier.
    CHECK_NEAR(ResolveLineHeightPx(Em(1.5f), 20, d), 30, 1e-4f);
    const ui::Density big2{ 1.0f, 2.0f };
    CHECK_NEAR(ResolveLineHeightPx(Sp(28), big2.SpToPx(14), big2), 52, 1e-3f);   // 2x the 26 px font

    // Font files: the matched file, with synthesis as Compose and the platform apply it.
    Font regular;
    regular.path = "fonts/r.ttf";
    TextStyle bold = ResolveDefaults(TextStyle(Sp(14)), ui::LayoutDirection::Ltr);
    bold.span.fontFamily = FontFamily::Of({ regular });
    bold.span.fontWeight = FontWeight::Bold();
    bold.span.fontStyle = FontStyle::Italic;
    auto f = BuildParagraphRequest(AnnotatedString("x"), bold, d, {}).base.font;
    CHECK(f.kind == TextFontRequest::Kind::Asset && f.name == "fonts/r.ttf");
    CHECK(f.weight == 700 && f.italic && f.fakeBold && f.fakeItalic);
    CHECK(f.variationSettings == "'wght' 400, 'ital' 0");
    bold.span.fontSynthesis = FontSynthesis::None;
    f = BuildParagraphRequest(AnnotatedString("x"), bold, d, {}).base.font;
    CHECK(f.weight == 400 && !f.italic && !f.fakeBold && !f.fakeItalic);
    // A 600 request from a 500 file is asked for but not faked (under 200 apart).
    Font medium = regular;
    medium.weight = FontWeight::Medium();
    bold.span.fontFamily = FontFamily::Of({ medium });
    bold.span.fontSynthesis = FontSynthesis::All;
    bold.span.fontWeight = FontWeight::SemiBold();
    f = BuildParagraphRequest(AnnotatedString("x"), bold, d, {}).base.font;
    CHECK(f.weight == 600 && !f.fakeBold);
    // The bold-text setting shifts weights before matching.
    const ui::Density boldText{ 1.0f, 1.0f, 300 };
    CHECK(BuildParagraphRequest(AnnotatedString("x"), ResolveDefaults(TextStyle(Sp(14)), ui::LayoutDirection::Ltr), boldText, {}).base.font.weight == 700);
}

TEST(MultiParagraphStacksParagraphStyles) {
    FakeText backend;
    const ui::Density d{ 1.0f, 1.0f };
    AnnotatedStringBuilder b;
    b.Append("Hello\n");
    ParagraphStyle centred;
    centred.textAlign = TextAlign::Center;
    b.WithStyle(centred, [&] { b.Append("World wide"); });
    const AnnotatedString text = b.ToAnnotatedString();
    {
        MultiParagraph mp(&backend, text, TextStyle(Sp(20)), d, ui::LayoutDirection::Ltr, { 90, INFINITY, 0x7fffffff, TextOverflow::Clip });
        CHECK(mp.Paragraphs().size() == 2);
        CHECK(mp.Paragraphs()[1].startIndex == 6 && mp.Paragraphs()[1].endIndex == 16);
        CHECK(mp.Paragraphs()[1].paragraph->Request().textAlign == std::uint8_t(TextAlign::Center));
        CHECK(mp.Paragraphs()[0].paragraph->Request().textAlign == std::uint8_t(TextAlign::Start));
        // "Hello\n" has its own (empty) last line; "World wide" wraps at 90 px (10 px chars).
        CHECK(mp.LineCount() == 4);
        CHECK_NEAR(mp.Paragraphs()[1].top, 50, 1e-4f);
        CHECK_NEAR(mp.Height(), 100, 1e-4f);
        CHECK(mp.LineForOffset(0) == 0 && mp.LineForOffset(7) == 2 && mp.LineForOffset(16) == 3);
        CHECK(mp.LineStart(2) == 6 && mp.LineStart(3) == 15);
        CHECK_NEAR(mp.LineTop(3), 75, 1e-4f);
        CHECK(mp.LineForVerticalPosition(80) == 3);
        CHECK_NEAR(mp.FirstBaseline(), 20, 1e-4f);
        CHECK_NEAR(mp.LastBaseline(), 95, 1e-4f);
        CHECK(!mp.DidExceedMaxLines());
        // maxLines: a paragraph that fills them ends the layout.
        MultiParagraph two(&backend, text, TextStyle(Sp(20)), d, ui::LayoutDirection::Ltr, { 90, INFINITY, 2, TextOverflow::Clip });
        CHECK(two.Paragraphs().size() == 1 && two.DidExceedMaxLines() && two.LineCount() == 2);
    }
    // Paragraphs release their handles.
    CHECK(backend.released == backend.layouts);
}

TEST(LayoutTextSizesToIntrinsicsAndConstraints) {
    FakeText backend;
    TextLayoutInput in;
    in.text = AnnotatedString("Hi there");
    in.style = TextStyle(Sp(20));
    in.constraints = { 0, 200, 0, ui::Constraints::Infinity };
    // Short text: as wide as it is (8 chars of 10 px).
    auto r = LayoutText(&backend, in);
    CHECK(r.Size().width == 80 && r.Size().height == 25);
    CHECK(!r.HasVisualOverflow());
    // Long text: the max width, wrapped.
    in.text = AnnotatedString("Hi there, this wraps onto more lines");
    r = LayoutText(&backend, in);
    CHECK(r.Size().width == 200 && r.LineCount() == 2);
    // No soft wrap: one line, wider than allowed.
    in.softWrap = false;
    r = LayoutText(&backend, in);
    CHECK(r.LineCount() == 1 && r.Size().width == 200 && r.DidOverflowWidth());
    // Ellipsis without soft wrap: one line at the max width.
    in.overflow = TextOverflow::Ellipsis;
    r = LayoutText(&backend, in);
    CHECK(r.LineCount() == 1 && r.Paragraphs().Width() == 200);
    // maxLines and a height limit: overflow reported.
    in.softWrap = true;
    in.overflow = TextOverflow::Clip;
    in.maxLines = 1;
    r = LayoutText(&backend, in);
    CHECK(r.LineCount() == 1 && r.DidOverflowHeight());
    in.maxLines = 0x7fffffff;
    in.constraints.maxHeight = 30;
    r = LayoutText(&backend, in);
    CHECK(r.Size().height == 30 && r.DidOverflowHeight());
    // A fixed width is used as is.
    in.constraints = ui::Constraints::FixedWidth(150);
    r = LayoutText(&backend, in);
    CHECK(r.Size().width == 150 && r.Paragraphs().Width() == 150);
    // Intrinsics are measured once and reused.
    std::optional<ParagraphIntrinsicWidths> intr;
    in.constraints = {};
    LayoutText(&backend, in, &intr);
    CHECK(intr && std::abs(intr->maxWidth - 360) < 1e-3f && std::abs(intr->minWidth - 60) < 1e-3f);
}

TEST(BasicTextLinksClickHoverAndOpenUris) {
    FakeText backend;
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.SetText(&backend);
    std::vector<std::string> opened;
    owner.SetUriHandler([&](const std::string& uri) { opened.push_back(uri); return true; });
    std::vector<int> icons;
    owner.SetPointerIconHost([&](int type) { icons.push_back(type); });
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    int clicks = 0;
    AnnotatedStringBuilder b;
    b.Append("Go to ");
    TextLinkStyles styles;
    SpanStyle underline;
    underline.textDecoration = TextDecoration::Underline;
    styles.hoveredStyle = underline;
    SpanStyle focusBg;
    focusBg.background = ui::Color(0xFF00FF00u);
    styles.focusedStyle = focusBg;
    b.WithLink(LinkAnnotation::Clickable("kiln", styles, [&](const LinkAnnotation& l) { if (l.tag == "kiln") ++clicks; }), [&] { b.Append("kiln"); });
    b.Append(" or ");
    b.WithLink(LinkAnnotation::Url("https://example.com"), [&] { b.Append("site"); });
    const AnnotatedString text = b.ToAnnotatedString();
    std::shared_ptr<TextLayoutResult> last;
    foundation::BasicTextOptions options;
    options.onTextLayout = [&](const TextLayoutResult& r) { last = std::make_shared<TextLayoutResult>(r); };
    comp.SetContent([&] { foundation::Column(ui::Modifier{}, [&] { foundation::BasicText(text, {}, TextStyle(Sp(20)), options); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 300, 100 });
    CHECK(last && last->Size().width == 180);   // 18 chars of 10 px

    std::int64_t t = 1000;
    auto send = [&](ui::PointerEventType type, float x, bool down) {
        ui::RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t += 20;
        e.buttons = down ? 1u : 0u;
        e.pointers.push_back({ 1, { x, 10 }, down, ui::PointerType::Mouse, {} });
        owner.ProcessPointer(e);
        rt::Dispatcher::Main().RunPending();
    };
    auto tap = [&](float x) { send(ui::PointerEventType::Press, x, true); send(ui::PointerEventType::Release, x, false); };
    tap(80);    // "kiln" spans 60..100
    CHECK_EQ(clicks, 1);
    tap(20);    // plain text
    CHECK_EQ(clicks, 1);
    tap(155);   // "site" spans 140..180: no listener, so the UriHandler opens it
    CHECK(opened.size() == 1 && opened[0] == "https://example.com");

    // Hover: the hand icon, and the hovered style (a re-layout with an underline span).
    send(ui::PointerEventType::Move, 80, false);
    CHECK(owner.CurrentPointerIcon() == 1002);
    comp.Recompose();
    owner.MeasureAndLayout({ 300, 100 });
    bool underlined = false;
    for (const auto& span : last->LayoutInput().text.SpanStyles())
        if (span.start == 6 && span.end == 10 && span.item.textDecoration == TextDecoration::Underline) underlined = true;
    CHECK(underlined);
    send(ui::PointerEventType::Move, 20, false);
    CHECK(owner.CurrentPointerIcon() == 1000);
    CHECK(icons.size() >= 2 && icons.back() == 1000);

    // Accessibility: the text node carries the link spans; each link is its own node.
    const ui::SemanticsNode root = owner.BuildSemanticsTree();
    CHECK(root.children.size() == 1);
    if (root.children.size() == 1) {
        const auto& textNode = root.children[0];
        CHECK(textNode.config.text.size() == 1 && textNode.config.textSpans.size() == 2);
        if (textNode.config.textSpans.size() == 2) {
            const auto& site = textNode.config.textSpans[1];
            CHECK(site.kind == ui::TextSpanSemantics::Kind::Url && site.value == "https://example.com" && site.start == 14 && site.end == 18);
            const auto& kiln = textNode.config.textSpans[0];
            CHECK(kiln.kind == ui::TextSpanSemantics::Kind::Clickable && kiln.start == 6);
            kiln.onClick();
        }
        CHECK(textNode.children.size() == 2 && textNode.children[0].config.text[0] == "kiln");
    }
    CHECK_EQ(clicks, 2);
    clicks = 1;

    // Keys: Tab focuses the first link (with its focused style), Enter follows it.
    auto key = [&](int code) {
        owner.DispatchKeyEvent(ui::KeyEvent{ code, ui::KeyEventType::KeyDown, 0 });
        owner.DispatchKeyEvent(ui::KeyEvent{ code, ui::KeyEventType::KeyUp, 0 });
        rt::Dispatcher::Main().RunPending();
        comp.Recompose();
        owner.MeasureAndLayout({ 300, 100 });
    };
    key(ui::keys::Tab);
    CHECK(owner.ActiveFocus() != nullptr);
    bool focusStyled = false;
    for (const auto& span : last->LayoutInput().text.SpanStyles())
        if (span.start == 6 && span.end == 10 && span.item.background == focusBg.background) focusStyled = true;
    CHECK(focusStyled);
    key(ui::keys::Enter);
    CHECK_EQ(clicks, 2);
    key(ui::keys::Tab);
    key(ui::keys::Enter);
    CHECK(opened.size() == 2);
}

TEST(BasicTextPlacesInlineContentInPlaceholders) {
    FakeText backend;
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.SetText(&backend);
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    AnnotatedStringBuilder b;
    b.Append("ab ");
    foundation::AppendInlineContent(b, "icon", "[i]");
    b.Append(" cd");
    const AnnotatedString text = b.ToAnnotatedString();
    std::string alternate;
    foundation::BasicTextOptions options;
    options.inlineContent["icon"] = { Placeholder{ Sp(30), Sp(16), PlaceholderVerticalAlign::AboveBaseline },
                                      [&](const std::string& alt) {
                                          alternate = alt;
                                          foundation::Box(foundation::Semantics(foundation::SizeMod(ui::Modifier{}, ui::Dp(30), ui::Dp(16)),
                                                                               [](ui::SemanticsConfiguration& c) { c.text.push_back("icon"); }));
                                      } };
    std::shared_ptr<TextLayoutResult> last;
    options.onTextLayout = [&](const TextLayoutResult& r) { last = std::make_shared<TextLayoutResult>(r); };
    comp.SetContent([&] { foundation::Column(ui::Modifier{}, [&] { foundation::BasicText(text, {}, TextStyle(Sp(20)), options); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 300, 100 });
    CHECK(alternate == "[i]");
    // "ab " is 30 px, the placeholder 30 px (its other two characters none), " cd" 30 px.
    CHECK(last && last->Size().width == 90);
    CHECK(last && last->PlaceholderRects().size() == 1 && last->PlaceholderRects()[0]);
    if (last && !last->PlaceholderRects().empty() && last->PlaceholderRects()[0]) {
        const RectF r = *last->PlaceholderRects()[0];
        CHECK_NEAR(r.x, 30, 1e-3f);
        CHECK_NEAR(r.y, 20 - 16, 1e-3f);   // above the baseline (at 20)
    }
    // The content sits in it.
    const ui::SemanticsNode root = owner.BuildSemanticsTree();
    bool found = false;
    std::function<void(const ui::SemanticsNode&)> walk = [&](const ui::SemanticsNode& n) {
        if (!n.config.text.empty() && n.config.text[0] == "icon") {
            found = true;
            CHECK_NEAR(n.boundsInRoot.left, 30, 1e-3f);
            CHECK_NEAR(n.boundsInRoot.top, 4, 1e-3f);
            CHECK_NEAR(n.boundsInRoot.Width(), 30, 1e-3f);
        }
        for (const auto& c : n.children) walk(c);
    };
    walk(root);
    CHECK(found);
}

TEST(DownloadableFontsQueryTheProviderAndRelayoutOnArrival) {
    const Font lobster = Font::Google("Lobster Two", FontWeight::Bold(), FontStyle::Italic);
    CHECK(lobster.source == Font::Source::Downloadable);
    CHECK(lobster.path == "name=Lobster Two&weight=700&italic=1&besteffort=true");
    CHECK(lobster.provider.authority == "com.google.android.gms.fonts" && lobster.provider.certificates.size() == 2);
    TextStyle style = ResolveDefaults(TextStyle(Sp(14)), ui::LayoutDirection::Ltr);
    style.span.fontFamily = FontFamily::Of({ lobster });
    style.span.fontWeight = FontWeight::Bold();
    style.span.fontStyle = FontStyle::Italic;
    const auto f = BuildParagraphRequest(AnnotatedString("x"), style, {}, {}).base.font;
    CHECK(f.kind == TextFontRequest::Kind::Downloadable);
    CHECK(f.name.rfind("com.google.android.gms.fonts\ncom.google.android.gms\nname=Lobster Two", 0) == 0);
    CHECK(f.weight == 700 && f.italic && !f.fakeBold && !f.fakeItalic);

    // When fonts arrive, text lays out again.
    FakeText backend;
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.SetText(&backend);
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    comp.SetContent([&] { foundation::Column(ui::Modifier{}, [&] { foundation::BasicText("Hello", {}, style); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 300, 100 });
    const int before = backend.layouts;
    owner.MeasureAndLayout({ 300, 100 });
    CHECK_EQ(backend.layouts, before);
    owner.OnFontsChanged();
    owner.MeasureAndLayout({ 300, 100 });
    CHECK(backend.layouts > before);
}

#include "kiln/foundation/text/Selection.h"

#include <thread>

namespace {
    // Two texts in a SelectionContainer: "Hello world" at y 0..25 and "Second line" at 25..50,
    // 10 px per character.
    struct SelectionHarness {
        FakeText backend;
        ui::Owner owner;
        rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t t = 1000;
        SelectionHarness() {
            owner.SetDensity({ 1, 1 });
            owner.SetText(&backend);
            owner.Config().longPressTimeoutMillis = 40;
        }
        void Pump() {
            rt::Dispatcher::Main().RunPending();
            comp.Recompose();
            owner.MeasureAndLayout({ 300, 200 });
        }
        void Run(std::function<void()> content) {
            comp.SetContent(std::move(content));
            Pump();
        }
        void Send(ui::PointerType type, ui::PointerEventType kind, float x, float y, bool down, std::int64_t dt = 16, std::uint32_t meta = 0) {
            ui::RawPointerEvent e;
            e.type = kind;
            e.uptimeMillis = t += dt;
            e.buttons = down && type == ui::PointerType::Mouse ? 1u : 0u;
            e.metaState = meta;
            e.pointers.push_back({ 1, { x, y }, down, type, {} });
            owner.ProcessPointer(e);
            Pump();
        }
        void Wait(int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            t += ms;
            Pump();
        }
        // The container's handle layer (its last child) and the handles in it.
        std::vector<ui::LayoutNode*> Handles() {
            ui::LayoutNode* container = owner.Root().Children()[0];
            ui::LayoutNode* layer = container->Children().back();
            return layer->Children();
        }
    };
    void TwoTexts() {
        foundation::Column(ui::Modifier{}, [] {
            foundation::BasicText("Hello world", {}, TextStyle(Sp(20)));
            foundation::BasicText("Second line", {}, TextStyle(Sp(20)));
        });
    }
}

TEST(SelectionLongPressSelectsWordsAcrossTextsAndCopies) {
    SelectionHarness h;
    foundation::SelectionState* state = nullptr;
    h.Run([&] {
        auto& s = foundation::RememberSelectionState();
        state = &s;
        foundation::SelectionContainer(s, ui::Modifier{}, TwoTexts);
    });
    using ui::PointerEventType;
    using ui::PointerType;
    // Long press on "world": the word is selected, the toolbar waits for the gesture to end.
    h.Send(PointerType::Touch, PointerEventType::Press, 75, 10, true);
    h.Wait(80);
    CHECK(state->SelectedTexts().size() == 1 && state->SelectedTexts()[0].Text() == "world");
    CHECK(!h.owner.TextToolbarShown());
    // Dragging into the second text extends by words.
    h.Send(PointerType::Touch, PointerEventType::Move, 75, 30, true);
    h.Send(PointerType::Touch, PointerEventType::Move, 45, 35, true);
    CHECK(state->SelectedTexts().size() == 2);
    if (state->SelectedTexts().size() == 2) {
        CHECK(state->SelectedTexts()[0].Text() == "world");
        CHECK(state->SelectedTexts()[1].Text() == "Second");
    }
    h.Send(PointerType::Touch, PointerEventType::Release, 45, 35, false);
    // Released: the toolbar with Copy and Select all over the selection.
    CHECK(h.owner.TextToolbarShown());
    CHECK_EQ(h.owner.TextToolbarItems(), unsigned(ui::Owner::kToolbarCopy | ui::Owner::kToolbarSelectAll));
    CHECK(h.owner.TextToolbarRect().top >= 0 && h.owner.TextToolbarRect().bottom > 50);
    // Two handles, at the ends of the selection.
    const auto handles = h.Handles();
    CHECK_EQ(int(handles.size()), 2);
    if (handles.size() == 2) {
        CHECK_EQ(handles[0]->Position().x, 60 - 25);   // the start handle's point at x 60, left of it
        CHECK_EQ(handles[0]->Position().y, 25);
        CHECK_EQ(handles[1]->Position().x, 60);        // after "Second", on its line's bottom
        CHECK_EQ(handles[1]->Position().y, 50);
    }
    // Copy: the texts joined by a line break; touch mode clears the selection.
    h.owner.OnTextToolbarItem(ui::Owner::kToolbarCopy);
    h.Pump();
    CHECK(h.owner.ClipboardText() == std::optional<std::string>("world\nSecond"));
    CHECK(state->SelectedTexts().empty());
    CHECK(!h.owner.TextToolbarShown());
    CHECK(h.Handles().empty());
}

TEST(SelectionHandlesDragAndTapClears) {
    SelectionHarness h;
    foundation::SelectionState* state = nullptr;
    h.Run([&] {
        auto& s = foundation::RememberSelectionState();
        state = &s;
        foundation::SelectionContainer(s, ui::Modifier{}, TwoTexts);
    });
    using ui::PointerEventType;
    using ui::PointerType;
    h.Send(PointerType::Touch, PointerEventType::Press, 25, 10, true);
    h.Wait(80);
    h.Send(PointerType::Touch, PointerEventType::Release, 25, 10, false);
    CHECK(state->SelectedTexts().size() == 1 && state->SelectedTexts()[0].Text() == "Hello");
    // The end handle hangs below "Hello" (point at 50, 25); drag it down into the second text.
    const auto handles = h.Handles();
    CHECK_EQ(int(handles.size()), 2);
    h.Send(PointerType::Touch, PointerEventType::Press, 60, 35, true, 400);
    h.Send(PointerType::Touch, PointerEventType::Move, 60, 50, true);
    h.Send(PointerType::Touch, PointerEventType::Move, 32, 60, true);
    CHECK(state->SelectedTexts().size() == 2);
    if (state->SelectedTexts().size() == 2) CHECK(state->SelectedTexts()[0].Text() == "Hello world");
    CHECK(!h.owner.TextToolbarShown());   // hidden while a handle drags
    h.Send(PointerType::Touch, PointerEventType::Release, 32, 60, false);
    CHECK(h.owner.TextToolbarShown());
    // A tap (no slop) on the container clears the selection.
    h.Send(PointerType::Touch, PointerEventType::Press, 100, 40, true, 400);
    h.Send(PointerType::Touch, PointerEventType::Release, 100, 40, false);
    CHECK(state->SelectedTexts().empty());
    CHECK(!h.owner.TextToolbarShown());
}

TEST(SelectionMouseClicksShiftExtendAndCtrlC) {
    SelectionHarness h;
    foundation::SelectionState* state = nullptr;
    h.Run([&] {
        auto& s = foundation::RememberSelectionState();
        state = &s;
        foundation::SelectionContainer(s, ui::Modifier{}, TwoTexts);
    });
    using ui::PointerEventType;
    using ui::PointerType;
    // A double click selects a word.
    h.Send(PointerType::Mouse, PointerEventType::Press, 25, 10, true);
    h.Send(PointerType::Mouse, PointerEventType::Release, 25, 10, false);
    h.Send(PointerType::Mouse, PointerEventType::Press, 25, 10, true, 60);
    h.Send(PointerType::Mouse, PointerEventType::Release, 25, 10, false);
    CHECK(state->SelectedTexts().size() == 1 && state->SelectedTexts()[0].Text() == "Hello");
    CHECK(!h.owner.TextToolbarShown());   // no toolbar for a mouse
    CHECK(h.Handles().empty());
    // Shift-click extends by characters into the second text.
    h.Send(PointerType::Mouse, PointerEventType::Press, 30, 35, true, 400, ui::kMetaShift);
    h.Send(PointerType::Mouse, PointerEventType::Release, 30, 35, false, 16, ui::kMetaShift);
    CHECK(state->SelectedTexts().size() == 2);
    if (state->SelectedTexts().size() == 2) CHECK(state->SelectedTexts()[1].Text() == "Sec");
    // Ctrl+C copies.
    h.owner.DispatchKeyEvent(ui::KeyEvent{ 31, ui::KeyEventType::KeyDown, ui::kMetaCtrl });
    h.owner.DispatchKeyEvent(ui::KeyEvent{ 31, ui::KeyEventType::KeyUp, ui::kMetaCtrl });
    CHECK(h.owner.ClipboardText() == std::optional<std::string>("Hello world\nSec"));
    // A triple click selects the paragraph.
    h.Send(PointerType::Mouse, PointerEventType::Press, 25, 35, true, 400);
    h.Send(PointerType::Mouse, PointerEventType::Release, 25, 35, false);
    h.Send(PointerType::Mouse, PointerEventType::Press, 25, 35, true, 60);
    h.Send(PointerType::Mouse, PointerEventType::Release, 25, 35, false);
    h.Send(PointerType::Mouse, PointerEventType::Press, 25, 35, true, 60);
    h.Send(PointerType::Mouse, PointerEventType::Release, 25, 35, false);
    CHECK(state->SelectedTexts().size() == 1 && state->SelectedTexts()[0].Text() == "Second line");
}

TEST(SelectionStateSelectAllSkipsDisabledTexts) {
    SelectionHarness h;
    foundation::SelectionState* state = nullptr;
    h.Run([&] {
        auto& s = foundation::RememberSelectionState();
        state = &s;
        foundation::SelectionContainer(s, ui::Modifier{}, [] {
            foundation::Column(ui::Modifier{}, [] {
                foundation::BasicText("One", {}, TextStyle(Sp(20)));
                foundation::DisableSelection([] { foundation::BasicText("Two", {}, TextStyle(Sp(20))); });
                foundation::BasicText("Three", {}, TextStyle(Sp(20)));
            });
        });
    });
    CHECK(state->GetSelectableTexts().size() == 2);
    state->SelectAll();
    h.Pump();
    CHECK(state->SelectedTexts().size() == 2);
    if (state->SelectedTexts().size() == 2) {
        CHECK(state->SelectedTexts()[0].Text() == "One");
        CHECK(state->SelectedTexts()[1].Text() == "Three");
    }
    // select(range): over the texts end to end ("OneThree").
    state->Select(1, 5);
    h.Pump();
    CHECK(state->SelectedTexts().size() == 2);
    if (state->SelectedTexts().size() == 2) {
        CHECK(state->SelectedTexts()[0].Text() == "ne");
        CHECK(state->SelectedTexts()[1].Text() == "Th");
    }
    state->Clear();
    h.Pump();
    CHECK(state->SelectedTexts().empty());
}

TEST(SelectionGesturesSurviveParentRecomposition) {
    // A parent that recomposes (a ticking clock) hands the container new handler lambdas; the
    // running gesture handlers must not depend on the old ones.
    SelectionHarness h;
    rt::State<int> tick(0);
    foundation::SelectionState* state = nullptr;
    h.Run([&] {
        (void)tick.Get();
        auto& s = foundation::RememberSelectionState();
        state = &s;
        foundation::SelectionContainer(s, ui::Modifier{}, TwoTexts);
    });
    using ui::PointerEventType;
    using ui::PointerType;
    h.Send(PointerType::Touch, PointerEventType::Press, 100, 60, true);
    h.Send(PointerType::Touch, PointerEventType::Release, 100, 60, false);
    for (int i = 1; i <= 3; ++i) {
        tick.Set(i);
        h.Pump();
    }
    h.Send(PointerType::Touch, PointerEventType::Press, 75, 10, true, 400);
    h.Wait(80);
    h.Send(PointerType::Touch, PointerEventType::Release, 75, 10, false);
    CHECK(state->SelectedTexts().size() == 1 && state->SelectedTexts()[0].Text() == "world");
}
