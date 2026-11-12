#include "Check.h"

#include "kiln/text/Html.h"

using namespace kiln;
using namespace kiln::text;

namespace {
    SpanStyle Weight() {
        SpanStyle s;
        s.fontWeight = FontWeight::Bold();
        return s;
    }
}

// AnnotatedStringFromHtmlTest.buildAnnotatedString_fromHtml: the test string resource as aapt
// leaves it (whitespace collapsed to single spaces).
TEST(FromHtmlMatchesComposeSample) {
    const AnnotatedString s = FromHtml(
        " <a href=\"https://example.com\">a</a> <annotation Foo=Bar>a</annotation> <b>a</b> <big>a</big> <div>a</div> "
        "<font face=\"serif\">a</font> <font color=\"#00ff00\">a</font> <i>a</i> <p>a</p> <s>a</s> <small>a</small> "
        "<span style=\"background-color:red\">a</span> <sub>a</sub> <sup>a</sup> <tt>a</tt> <u>a</u> "
        "<ul><li>a<ul><li>a</li></ul></li><li>a</li></ul> ");
    CHECK_EQ(s.Text(), std::string("a a a a \na\na a a \na\na a a a a a a aaa "));

    std::vector<SpanStyle> expected(12);
    expected[0].fontWeight = FontWeight::Bold();
    expected[1].fontSize = Em(1.25f);
    expected[2].fontFamily = FontFamily::Serif();
    expected[3].color = ui::Color(0xFF00FF00u);
    expected[4].fontStyle = FontStyle::Italic;
    expected[5].textDecoration = TextDecoration::LineThrough;
    expected[6].fontSize = Em(0.8f);
    expected[7].background = ui::Color(0xFFFF0000u);
    expected[8].baselineShift = BaselineShift::Subscript();
    expected[9].baselineShift = BaselineShift::Superscript();
    expected[10].fontFamily = FontFamily::Monospace();
    expected[11].textDecoration = TextDecoration::Underline;
    const int starts[] = { 4, 6, 11, 13, 15, 20, 22, 24, 26, 28, 30, 32 };
    const auto spans = s.SpanStyles();
    CHECK_EQ(spans.size(), expected.size());
    for (std::size_t i = 0; i < spans.size() && i < expected.size(); ++i)
        CHECK(spans[i].item == expected[i] && spans[i].start == starts[i] && spans[i].end == starts[i] + 1);

    // Bullets: level 1, 2, 1, indented 1em per level.
    const auto paragraphs = s.ParagraphStyles();
    const auto bullets = s.Bullets();
    CHECK(paragraphs.size() == 3 && bullets.size() == 3);
    const float levels[] = { 1, 2, 1 };
    for (std::size_t i = 0; i < 3 && i < paragraphs.size() && i < bullets.size(); ++i) {
        CHECK(paragraphs[i].item.textIndent && paragraphs[i].item.textIndent->firstLine == Em(levels[i]) &&
              paragraphs[i].item.textIndent->restLine == Em(levels[i]));
        CHECK(paragraphs[i].start == 34 + int(i) && paragraphs[i].end == 35 + int(i));
        CHECK(bullets[i].item == Bullet::Default() && bullets[i].start == 34 + int(i));
    }
    const auto annotations = s.GetStringAnnotations(0, s.Length());
    CHECK(annotations.size() == 1 && annotations[0].tag == "foo" && annotations[0].item == "Bar" && annotations[0].start == 2 &&
          annotations[0].end == 3);
    const auto links = s.GetLinkAnnotations(0, s.Length());
    CHECK(links.size() == 1 && links[0].item.url == "https://example.com" && links[0].start == 0 && links[0].end == 1);
}

TEST(FromHtmlAnnotationsAndFormatting) {
    // formattedString_withStyling
    AnnotatedString s = FromHtml("Hello, <b>computer</b>!");
    CHECK(s.Text() == "Hello, computer!" && s.SpanStyles().size() == 1 && s.SpanStyles()[0].item == Weight() &&
          s.SpanStyles()[0].start == 7 && s.SpanStyles()[0].end == 15);
    // annotationTag_*: none without text or attributes; one per attribute (names lower-cased).
    s = FromHtml("a<annotation key1=value1></annotation>");
    CHECK(s.Text() == "a" && s.GetStringAnnotations(0, 1).empty());
    s = FromHtml("<annotation>a</annotation>");
    CHECK(s.Text() == "a" && s.GetStringAnnotations(0, 1).empty());
    s = FromHtml("<annotation key1=\"value1\" key2=value2 keyThree=\"valueThree\">a</annotation>");
    auto a = s.GetStringAnnotations(0, 1);
    CHECK(a.size() == 3 && a[0].tag == "key1" && a[0].item == "value1" && a[1].tag == "key2" && a[1].item == "value2" &&
          a[2].tag == "keythree" && a[2].item == "valueThree");
    s = FromHtml("<annotation key1=val1>a</annotation>a<annotation key2=\"val2\">a</annotation>");
    a = s.GetStringAnnotations(0, 3);
    CHECK(s.Text() == "aaa" && a.size() == 2 && a[0].start == 0 && a[0].end == 1 && a[1].tag == "key2" && a[1].start == 2 && a[1].end == 3);
    s = FromHtml("<b><annotation key1=\"value1\">a</annotation></b>");
    CHECK(s.SpanStyles().size() == 1 && s.GetStringAnnotations(0, 1).size() == 1);

    // Link styles and a listener go to each link; a link's own colour comes from the markup.
    int clicked = 0;
    TextLinkStyles styles;
    styles.style = SpanStyle{};
    styles.style->color = ui::Color(0xFF00FF00u);
    s = FromHtml("<span style=\"color:blue\">text<a href=\"url\">link</a></span>", styles, [&](const LinkAnnotation&) { ++clicked; });
    const auto links = s.GetLinkAnnotations(0, s.Length());
    CHECK(links.size() == 1 && links[0].item.styles == styles && links[0].item.listener && links[0].start == 4 && links[0].end == 8);
    (*links[0].item.listener)(links[0].item);
    CHECK(clicked == 1 && s.SpanStyles().size() == 1 && s.SpanStyles()[0].item.color == ui::Color(0xFF0000FFu) && s.SpanStyles()[0].start == 0);
}

TEST(FromHtmlBlocksEntitiesAndLenientMarkup) {
    // Headings: bold and sized up to the line break after them; p / div align by text-align.
    AnnotatedString s = FromHtml("x<h1>Title</h1>body");
    CHECK_EQ(s.Text(), std::string("x\nTitle\nbody"));
    auto spans = s.SpanStyles();
    CHECK(spans.size() == 2 && spans[0].item.fontSize == Em(1.5f) && spans[1].item == Weight() && spans[0].start == 2 && spans[0].end == 7);
    s = FromHtml("<p style=\"text-align:center\">mid</p>end");
    auto paragraphs = s.ParagraphStyles();
    CHECK(s.Text() == "mid\nend" && paragraphs.size() == 1 && paragraphs[0].item.textAlign == TextAlign::Center && paragraphs[0].start == 0 &&
          paragraphs[0].end == 4);
    // Whitespace collapses; br breaks; entities and references decode; img is U+FFFC.
    s = FromHtml("  a \n\n b<br>c &amp; &lt;d&gt; &#233;&#x1F600;&nbsp;<img src=x>");
    CHECK_EQ(s.Text(), std::string("a b\nc & <d> \xC3\xA9\xF0\x9F\x98\x80\xC2\xA0\xEF\xBF\xBC"));
    // Colours: names, #rrggbb, and what getHtmlColor rejects (white is -1; #fff is 0x000fff).
    s = FromHtml("<font color=\"teal\">a</font><font color=\"white\">b</font><span style=\"color:#fff\">c</span><font color=\"nope\">d</font>");
    spans = s.SpanStyles();
    CHECK(spans.size() == 2 && spans[0].item.color == ui::Color(0xFF008080u) && spans[1].item.color == ui::Color(0xFF000FFFu) && spans[1].start == 2);
    // A face that is not generic: an installed family by that name, the default when missing.
    s = FromHtml("<font face=\"Roboto Flex\">a</font>");
    CHECK(s.SpanStyles().size() == 1 && s.SpanStyles()[0].item.fontFamily == FontFamily::Of({ Font::Device("Roboto Flex") }));
    // Unclosed and stray tags: everything open closes at the end, a stray end tag is ignored, a
    // second <p> closes the first, a bare '<' is text, comments drop out.
    s = FromHtml("<b>bold<i>both</b> plain</i><!-- note -->a < b<p>one<p>two");
    CHECK_EQ(s.Text(), std::string("boldboth plaina < b\none\ntwo\n"));
    spans = s.SpanStyles();
    CHECK(spans.size() == 2 && spans[0].item.fontStyle == FontStyle::Italic && spans[0].start == 4 && spans[0].end == 8 && spans[1].item == Weight() &&
          spans[1].start == 0 && spans[1].end == 8);
    // An <li> outside a list sits in an implied <ul> (indent 1em); a new <li> closes the last.
    s = FromHtml("<li>a<li>b");
    const auto bullets = s.Bullets();
    paragraphs = s.ParagraphStyles();
    CHECK(s.Text() == "ab" && bullets.size() == 2 && bullets[1].start == 1 && paragraphs.size() == 2 && paragraphs[0].item.textIndent->firstLine == Em(1));
}
