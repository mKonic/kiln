// ui-text API: FontVariationTest, LocaleTest, TextRangeTest, AnnotatedStringTest (annotation
// comparisons, map / flatMap, get), TextStyle plus / draw-affecting, Paragraph paths and boxes.
#include "Check.h"
#include "FakeText.h"

#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/text/Locale.h"
#include "kiln/text/Paragraph.h"

using namespace kiln;
using namespace kiln::text;

TEST(FontVariationSettingsMergeAndScale) {
    using namespace FontVariation;
    const Settings s = MakeSettings(FontWeight::Bold(), FontStyle::Italic, { Width(90), OpticalSizing(Sp(18)) });
    CHECK(s.NeedsDensity());
    ui::Density d{ 2, 1.5f };
    CHECK_EQ(s.ToString(&d), std::string("'wght' 700, 'ital' 1, 'wdth' 90, 'opsz' 27"));
    CHECK_THROWS(s.ToString(nullptr));   // opsz needs the font scale
    // merge: same axis replaced in place, new ones appended.
    const Settings m = s.Merge(std::vector<Setting>{ Weight(300), Grade(-50) });
    CHECK_EQ(m.settings[0], Weight(300));
    CHECK_EQ(m.settings.back(), Grade(-50));
    CHECK_EQ(m.settings.size(), std::size_t(5));
    CHECK_THROWS(Settings({ Weight(100), Weight(200) }));
    CHECK_THROWS(Weight(0));
    CHECK_THROWS(Italic(2));
    CHECK_THROWS(MakeSetting("abc", 1));
    CHECK_THROWS(OpticalSizing(Em(1)));
}

TEST(LocaleParsesLanguageScriptRegion) {
    const Locale zh("zh-hans-cn");
    CHECK(zh.Language() == "zh" && zh.Script() == "Hans" && zh.Region() == "CN");
    CHECK_EQ(zh.ToLanguageTag(), std::string("zh-Hans-CN"));
    CHECK_EQ(Locale("en_US").ToLanguageTag(), std::string("en-US"));
    CHECK_EQ(Locale("es-419").Region(), std::string("419"));
    CHECK_EQ(Locale("").ToLanguageTag(), std::string("und"));
    const LocaleList list("en-US, ja");
    CHECK(list.Size() == 2 && list[1].Language() == "ja" && list.Contains(Locale("en-us")));
    CHECK_EQ(list.ToLanguageTags(), std::string("en-US,ja"));
}

TEST(TextRangeIntersects) {
    using foundation::TextRange;
    CHECK(TextRange(0, 5).Intersects(TextRange(4, 8)));
    CHECK(!TextRange(0, 5).Intersects(TextRange(5, 8)));
    CHECK(TextRange(5, 0).Intersects(TextRange(2, 3)));   // reversed ranges by min / max
    CHECK(TextRange(2, 6).Contains(5) && !TextRange(2, 6).Contains(6));
}

TEST(AnnotatedStringAnnotationsCompareAndMap) {
    AnnotatedStringBuilder b;
    b.Append("a\xF0\x9F\x98\x80" "b");   // a, U+1F600 (two UTF-16 units), b
    b.AddStyle(SpanStyle{ .color = ui::Color::Red() }, 0, 1);
    b.AddStringAnnotation("tag", "v", 1, 3);
    const AnnotatedString s = b.ToAnnotatedString();
    CHECK(s.Get(0) == u'a' && s.Get(1) == char16_t(0xD83D) && s.Get(2) == char16_t(0xDE00) && s.Get(3) == u'b');
    CHECK(s.HasStringAnnotations("tag", 0, 2) && !s.HasStringAnnotations("tag", 3, 4) && !s.HasStringAnnotations("x", 0, 4));
    AnnotatedStringBuilder b2(s);
    b2.AddDrawAnnotation(MakeDrawAnnotation(), 0, 1);
    const AnnotatedString withDraw = b2.ToAnnotatedString();
    CHECK(!s.HasEqualAnnotations(withDraw));
    CHECK(s.HasSameLayoutAffectingAnnotations(withDraw));
    CHECK(!s.HasSameDrawAffectingAnnotations(withDraw));
    const AnnotatedString blue = s.MapAnnotations([](const Range<Annotation>& r) {
        Range<Annotation> out = r;
        if (std::holds_alternative<SpanStyle>(out.item)) std::get<SpanStyle>(out.item).color = ui::Color::Blue();
        return out;
    });
    CHECK(blue.SpanStyles()[0].item.color == ui::Color::Blue() && blue.Text() == s.Text());
    const AnnotatedString none = s.FlatMapAnnotations([](const Range<Annotation>&) { return std::vector<Range<Annotation>>{}; });
    CHECK(none.Annotations().empty());
}

TEST(TextStylePlusAndDrawAffecting) {
    TextStyle a(Sp(14));
    TextStyle red = a + SpanStyle{ .color = ui::Color::Red() };
    CHECK(red.span.color == ui::Color::Red() && red.span.fontSize == Sp(14));
    CHECK(a.HasSameLayoutAffectingAttributes(red) && !a.HasSameDrawAffectingAttributes(red));
    TextStyle big = a + TextStyle(Sp(20));
    CHECK(big.HasSameDrawAffectingAttributes(a) && !big.HasSameLayoutAffectingAttributes(a));
    CHECK((a + ParagraphStyle{ .textAlign = TextAlign::Center }).ToParagraphStyle().textAlign == TextAlign::Center);
}

TEST(ParagraphPathAndBoxesForRange) {
    test::FakeText backend;
    TextLayoutInput in;
    in.text = AnnotatedString("Hello");
    in.style = TextStyle(Sp(20));
    in.constraints = { 0, 500, 0, ui::Constraints::Infinity };
    const TextLayoutResult r = LayoutText(&backend, in);
    const gfx::Path p = r.GetPathForRange(1, 3);
    CHECK(!p.IsEmpty());
    std::vector<float> boxes;
    r.Paragraphs().FillBoundingBoxes(0, 2, boxes);
    CHECK_EQ(boxes.size(), std::size_t(8));
    CHECK(boxes[2] <= boxes[4] + 0.01f);   // the second box starts where the first ends
}
