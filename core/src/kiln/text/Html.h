#pragma once

// AnnotatedString.fromHtml (ui-text Html.android.kt): HTML markup as an AnnotatedString, as
// Android's Html.fromHtml(FROM_HTML_MODE_COMPACT) reads it (HtmlToSpannedConverter over a
// lenient, TagSoup-like parse) with Compose's own handling of <annotation>, <ul> and <li>.
//
// Tags: b strong, i em cite dfn, u, s del strike, big (1.25em), small (0.8em), sup, sub, tt
// (monospace), font (color, face), span and p / li style="color / background(-color) /
// text-decoration: line-through", a href (a LinkAnnotation.Url with linkStyles and listener),
// h1..h6 (bold, 1.5em .. 1em), p div blockquote h* (one line break around them, text-align:
// start / center / end), br, img (U+FFFC), <annotation key=value ..> (string annotations), and
// <ul><li> (Bullet.Default indented 1em per list level; a nested list goes inside its <li>).
// Whitespace collapses to single spaces; entities and character references are decoded.

#include "kiln/text/AnnotatedString.h"

#include <string_view>

namespace kiln::text {

    AnnotatedString FromHtml(std::string_view html, const TextLinkStyles& linkStyles = {}, LinkInteractionListener listener = {});

}
