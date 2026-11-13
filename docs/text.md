# Text

This page covers showing text: styles, fonts, text with several styles in it, links,
backgrounds behind part of a text, and selection. Text input is in
[Interactivity and text input](interactivity.md).

## Showing text

`Text` (Material 3) shows a string in the theme's text style and the content color.
`BasicText` (foundation) takes the style you give it and nothing from the theme.

```cpp
Text("Hello, kiln");
Text("Title", Modifier{}, { .fontSize = Sp(22), .fontWeight = FontWeight::Medium() });
BasicText("Plain", Modifier{}, TextStyle(16, Color(0xFF1D1B20u)));
```

`TextOptions` holds what `Text` can change:

| Option | What it sets |
|---|---|
| `color`, `colorProducer` | The color, or a function read when drawing (animate without recomposing) |
| `fontSize`, `fontWeight`, `fontStyle`, `fontFamily`, `letterSpacing` | The font |
| `textAlign`, `lineHeight`, `textDecoration` | The paragraph |
| `maxLines`, `minLines`, `overflow`, `softWrap` | How much shows and what happens past it |
| `autoSize` | A font size that shrinks to fit |
| `style` | A whole `TextStyle` instead of the theme's |

`MaterialTheme::typography()` holds the Material type scale: `bodyLarge`,
`titleMedium`, `headlineSmall` and the rest.

## Several styles in one text

An `AnnotatedString` is text with styles on parts of it. Build one with
`BuildAnnotatedString`:

```cpp
const AnnotatedString styled = BuildAnnotatedString([](AnnotatedStringBuilder& b) {
    b.Append("Mix ");
    SpanStyle bold;
    bold.fontWeight = FontWeight::Bold();
    b.WithStyle(bold, [&] { b.Append("bold"); });
    b.Append(" and ");
    SpanStyle marked;
    marked.background = Color(0x55E8618Cu);
    b.WithStyle(marked, [&] { b.Append("marked"); });
    b.Append(" text.");
});
Text(styled);
```

`SpanStyle` covers color, font, size, letter spacing, decorations, shadows, baseline
shift and a background. `ParagraphStyle` (pushed with `WithStyle` too) covers alignment,
line height, indentation and text direction for whole paragraphs.

## Links

`WithLink` makes part of the text a link: a URL opens in the browser, a clickable link
calls your function. Links are focusable and announced by accessibility services.

<!-- prelude: auto& taps = RememberState(0); -->
```cpp
const AnnotatedString linked = BuildAnnotatedString([&](AnnotatedStringBuilder& b) {
    SpanStyle link;
    link.color = MaterialTheme::colorScheme().primary;
    link.textDecoration = TextDecoration::Underline;
    TextLinkStyles styles;
    styles.style = link;
    b.WithLink(LinkAnnotation::Url("https://developer.android.com", styles), [&] { b.Append("Read the docs"); });
    b.Append(" or ");
    b.WithLink(LinkAnnotation::Clickable("count", styles, [&taps](const LinkAnnotation&) { taps.Set(taps.Get() + 1); }),
               [&] { b.Append("count a tap"); });
});
Text(linked);
```

## Backgrounds behind text

`BackgroundAnnotation` draws a background behind part of a text: a highlight with
rounded corners that follows the text onto the next line, or a pill with a border. It is
drawn in the draw phase, so changing its color does not lay the text out again.

```cpp
const AnnotatedString highlighted = BuildAnnotatedString([](AnnotatedStringBuilder& b) {
    BackgroundAnnotation highlight;
    highlight.color = Color(0x55E8618Cu);
    highlight.cornerRadius = 6_dp;
    highlight.spread = BackgroundAnnotation::SpreadValues::Symmetric(3_dp, 0_dp);
    b.WithDrawAnnotation(MakeDrawAnnotation(highlight), [&] { b.Append("a highlight across a line break"); });
    b.Append(" and a ");
    BackgroundAnnotation pill;
    pill.cornerRadius = 12_dp;
    pill.border = BackgroundAnnotation::Border{ 1_dp, Color(0xFF6750A4u) };
    b.WithDrawAnnotation(MakeDrawAnnotation(pill), [&] { b.Append("pill"); });
});
Text(highlighted);
```

* `color`, `colorProducer` or `brush` fills it.
* `spread` moves its edges outward (positive) or inward (negative) without moving the text.
* `mergeLines` (on by default) joins the boxes of consecutive lines into one shape; off,
  each line gets its own rounded rectangle.

For drawing of your own, `MakeDrawAnnotation(drawBehind, drawOver)` takes two functions
that get the text's layout and the annotated range. `GetBoundingBoxesForRange` and
`FillPathForRange` on the layout give the range's boxes and outline.

## Fonts

A `FontFamily` is a system family, or a list of fonts with their weights and styles:

```cpp
const FontFamily serif = FontFamily::Serif();
const FontFamily lobster = FontFamily::Of({ Font::Google("Lobster Two", FontWeight::Bold()) });
Text("Downloaded", Modifier{}, { .fontWeight = FontWeight::Bold(), .fontFamily = lobster });
```

* `Font::Google` downloads a font from Google Fonts through the system's font provider.
  The text shows in a fallback font until it arrives, then lays out again.
* `Font::Device` uses a font the device has by name, if it has it.
* A font file in the app's assets is `Font{ .source = Font::Source::Asset, .path = "fonts/MyFont.ttf" }`.

## Too much text

`maxLines` limits the lines and `overflow` decides the end: `TextOverflow::Clip` cuts it,
`TextOverflow::Ellipsis` ends it with "…".

```cpp
Text("A long sentence that keeps going past the second line and is cut off with an ellipsis.",
     Modifier{}, { .overflow = TextOverflow::Ellipsis, .maxLines = 2 });
```

## Selection

Text is not selectable by default. `SelectionContainer` makes the texts inside it
selectable together: long press to select a word, drag the handles, and copy, share or
use the actions other apps add to the toolbar. `DisableSelection` turns it off for part of
the content.

```cpp
SelectionContainer(Modifier{}, [] {
    Column(Modifier{}, [] {
        Text("These two lines");
        Text("are selected together.");
        DisableSelection([] { Text("This one is not."); });
    });
});
```
