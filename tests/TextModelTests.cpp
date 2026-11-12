#include "Check.h"

#include "kiln/text/AnnotatedString.h"
#include "kiln/text/CaseMapping.h"

using namespace kiln;
using namespace kiln::text;

TEST(TextStylesMergeAndResolve) {
    SpanStyle a;
    a.fontSize = Sp(20);
    a.color = ui::Color(0xFFFF0000u);
    a.fontWeight = FontWeight::Bold();
    SpanStyle b;
    b.color = ui::Color(0xFF00FF00u);
    b.letterSpacing = Em(0.1f);
    const SpanStyle m = a.Merge(b);
    CHECK(m.fontSize == Sp(20));                  // unset in b: kept
    CHECK(m.color == ui::Color(0xFF00FF00u));     // set in b: wins
    CHECK(m.fontWeight == FontWeight::Bold());
    CHECK(m.letterSpacing == Em(0.1f));
    // A colour never replaces a brush.
    SpanStyle brushed;
    brushed.brush = gfx::Brush::SolidColor(ui::Color(0xFF0000FFu));
    CHECK(brushed.Merge(b).brush.has_value());
    // Defaults: 14sp black, Start, Ltr from the layout direction, Simple line breaking.
    TextStyle r = ResolveDefaults({}, ui::LayoutDirection::Rtl);
    CHECK(r.span.fontSize == Sp(14));
    CHECK(r.span.color == ui::Color(0xFF000000u));
    CHECK(r.paragraph.textAlign == TextAlign::Start);
    CHECK(r.paragraph.textDirection == TextDirection::Rtl);
    CHECK(r.paragraph.lineBreak == LineBreak::Simple());
    CHECK(!*r.paragraph.includeFontPadding);
    ParagraphStyle content;
    content.textDirection = TextDirection::Content;
    CHECK(ResolveDefaults({ {}, content }, ui::LayoutDirection::Ltr).paragraph.textDirection == TextDirection::ContentOrLtr);
    // Layout-affecting comparison ignores colours.
    TextStyle red; red.WithColor(ui::Color(0xFFFF0000u));
    TextStyle blue; blue.WithColor(ui::Color(0xFF0000FFu));
    CHECK(red.HasSameLayoutAffectingAttributes(blue));
    blue.WithFontSize(Sp(30));
    CHECK(!red.HasSameLayoutAffectingAttributes(blue));
}

TEST(AnnotatedStringBuildsRangesInUtf16) {
    SpanStyle bold; bold.fontWeight = FontWeight::Bold();
    SpanStyle red; red.color = ui::Color(0xFFFF0000u);
    const AnnotatedString s = BuildAnnotatedString([&](AnnotatedStringBuilder& b) {
        b.Append("Hi ");
        b.WithStyle(bold, [&] {
            b.Append("bold ");
            b.WithStyle(red, [&] { b.Append("\xF0\x9F\x98\x80"); });   // an emoji: two UTF-16 units
        });
        b.WithLink(LinkAnnotation::Url("https://example.com"), [&] { b.Append(" link"); });
        b.PushStringAnnotation("tag", "value");
        b.Append("!");
    });
    CHECK_EQ(s.Length(), 3 + 5 + 2 + 5 + 1);
    const auto spans = s.SpanStyles();
    CHECK_EQ(spans.size(), std::size_t(2));
    CHECK(spans[0].start == 3 && spans[0].end == 10);
    CHECK(spans[1].start == 8 && spans[1].end == 10);
    const auto links = s.GetLinkAnnotations(0, s.Length());
    CHECK(links.size() == 1 && links[0].item.url == "https://example.com" && links[0].start == 10 && links[0].end == 15);
    const auto tags = s.GetStringAnnotations("tag", 15, 16);
    CHECK(tags.size() == 1 && tags[0].item == "value" && tags[0].end == 16);   // still open: to the end
    CHECK(s.GetStringAnnotations("other", 0, 16).empty());
    // UTF-16 to UTF-8 offsets around the emoji.
    CHECK_EQ(Utf16ToUtf8Offset(s.Text(), 10), std::size_t(12));
    CHECK_EQ(Utf8ToUtf16Offset(s.Text(), 12), 10);
    // A sub-sequence keeps what it covers, shifted.
    const AnnotatedString sub = s.SubSequence(5, 12);
    CHECK_EQ(sub.Length(), 7);
    CHECK(sub.SpanStyles()[0].start == 0 && sub.SpanStyles()[0].end == 5);
    CHECK(sub.Text() == "ld \xF0\x9F\x98\x80 l");
}

TEST(ParagraphStylesNormalize) {
    ParagraphStyle center; center.textAlign = TextAlign::Center;
    ParagraphStyle tall; tall.lineHeight = Sp(30);
    AnnotatedStringBuilder b;
    b.Append("aaaa");
    b.WithStyle(center, [&] {
        b.Append("bbbb");
        b.WithStyle(tall, [&] { b.Append("cc"); });
        b.Append("dd");
    });
    b.Append("ee");
    ParagraphStyle def; def.textDirection = TextDirection::Ltr;
    const auto ranges = b.ToAnnotatedString().NormalizedParagraphStyles(def);
    // Default, centre, centre + tall, centre, default; each over the default.
    CHECK_EQ(ranges.size(), std::size_t(5));
    CHECK(ranges[0].start == 0 && ranges[0].end == 4 && ranges[0].item.textAlign == TextAlign::Unspecified);
    CHECK(ranges[1].start == 4 && ranges[1].end == 8 && ranges[1].item.textAlign == TextAlign::Center);
    CHECK(ranges[2].start == 8 && ranges[2].end == 10 && ranges[2].item.lineHeight == Sp(30) && ranges[2].item.textAlign == TextAlign::Center);
    CHECK(ranges[3].start == 10 && ranges[3].end == 12 && !ranges[3].item.lineHeight.IsSpecified());
    CHECK(ranges[4].start == 12 && ranges[4].end == 14);
    for (const auto& r : ranges) CHECK(r.item.textDirection == TextDirection::Ltr);
}

TEST(TextStylesLerpLikeCompose) {
    // lerp(TextStyle): sizes and colours interpolate, discrete values switch halfway.
    text::TextStyle a(text::Sp(16), ui::Color(0xFF000000u)), b(text::Sp(12), ui::Color(0xFFFFFFFFu));
    a.paragraph.lineHeight = text::Sp(24);
    b.paragraph.lineHeight = text::Sp(16);
    a.span.fontWeight = text::FontWeight{ 400 };
    b.span.fontWeight = text::FontWeight{ 700 };
    a.paragraph.textAlign = text::TextAlign::Start;
    b.paragraph.textAlign = text::TextAlign::Center;
    const text::TextStyle q = text::Lerp(a, b, 0.25f);
    CHECK(std::abs(q.span.fontSize.value - 15) < 1e-4f && q.span.fontSize.IsSp());
    CHECK(std::abs(q.paragraph.lineHeight.value - 22) < 1e-4f);
    CHECK_EQ(q.span.fontWeight->weight, 475);
    CHECK(q.paragraph.textAlign == text::TextAlign::Start);
    CHECK(text::Lerp(a, b, 0.5f).paragraph.textAlign == text::TextAlign::Center);
    // An unspecified size switches rather than interpolating.
    text::TextStyle c = a;
    c.span.fontSize = text::TextUnit::Unspecified();
    CHECK(text::Lerp(a, c, 0.4f).span.fontSize == text::Sp(16));
    CHECK(!text::Lerp(a, c, 0.6f).span.fontSize.IsSpecified());
}

TEST(CaseMappingKeepsAnnotationsOnTheirRuns) {
    using namespace kiln::text;
    CHECK(ToUpperCase(std::string_view("abc")) == "ABC" && ToLowerCase(std::string_view("ABC")) == "abc");
    CHECK(Capitalize(std::string_view("hello world")) == "Hello world" && Decapitalize(std::string_view("Hello")) == "hello");
    CHECK(Capitalize(std::string_view("Hello")) == "Hello");
    // A host mapping that changes length ("ß" -> "SS") moves the following annotations along.
    SetCaseMapper([](std::u16string_view t, const std::string&, CaseOp op) {
        std::u16string out;
        for (char16_t c : t) {
            if (op == CaseOp::Upper && c == u'ß') out += u"SS";
            else out += op == CaseOp::Upper && c >= u'a' && c <= u'z' ? char16_t(c - 32) : c;
        }
        return out;
    });
    AnnotatedStringBuilder b;
    b.Append("stra\xC3\x9F" "e ");
    SpanStyle bold;
    bold.fontWeight = FontWeight::Bold();
    b.WithStyle(bold, [&] { b.Append("big"); });
    const AnnotatedString up = ToUpperCase(b.ToAnnotatedString());
    SetCaseMapper(nullptr);
    CHECK(up.Text() == "STRASSE BIG");
    const auto spans = up.SpanStyles();
    CHECK(spans.size() == 1u && spans[0].start == 8 && spans[0].end == 11);
}
