#pragma once

// BasicText (foundation text/BasicText.kt, modifiers/TextAnnotatedStringNode.kt,
// HeightInLinesModifier.kt): styled text laid out by the platform. Wraps to the max width when
// softWrap, ellipsizes or clips per overflow, keeps between minLines and maxLines of height,
// reports each layout to onTextLayout. Colour-only style changes redraw without a new layout.

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/text/TextAutoSize.h"
#include "kiln/text/Paragraph.h"

#include <climits>
#include <functional>
#include <map>
#include <string>

namespace kiln::foundation {

    using text::AnnotatedString;
    using text::TextLayoutResult;
    using text::TextOverflow;
    using text::TextStyle;

    // InlineTextContent: a composable shown in the text in place of an AppendInlineContent
    // range, in a box the placeholder sizes; it gets the range's alternate text.
    struct InlineTextContent {
        text::Placeholder placeholder;
        std::function<void(const std::string& alternateText)> children;
    };
    // appendInlineContent: marks text (by default U+FFFD) to be replaced by the inline content
    // with this id.
    inline constexpr const char* kInlineContentTag = "androidx.compose.foundation.text.inlineContent";
    void AppendInlineContent(text::AnnotatedStringBuilder& builder, const std::string& id, const std::string& alternateText = "\xEF\xBF\xBD");

    struct BasicTextOptions {
        std::function<void(const TextLayoutResult&)> onTextLayout;
        TextOverflow overflow = TextOverflow::Clip;
        bool softWrap = true;
        int maxLines = INT_MAX;
        int minLines = 1;
        std::map<std::string, InlineTextContent> inlineContent;
        // ColorProducer: read when drawing, overriding the style's colour when specified (a
        // colour animation redraws without recomposing or a new layout).
        std::function<ui::Color()> color;
        // The font size picked for the constraints (the style's own size is ignored).
        TextAutoSizePtr autoSize;
    };

    void BasicText(const AnnotatedString& text, const Modifier& modifier = {}, TextStyle style = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    void BasicText(const std::string& text, const Modifier& modifier = {}, TextStyle style = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    inline void BasicText(const char* text, const Modifier& modifier = {}, TextStyle style = {},
                          BasicTextOptions options = {}, rt::Key key = {}) {
        BasicText(std::string(text), modifier, std::move(style), std::move(options), key);
    }

    // Draws a laid-out text at the origin: backgrounds, shadows, glyphs (brushes masked to each
    // paragraph), underlines and strike-throughs. `style` is the current style: run colours
    // that the text's spans leave unset come from it.
    void DrawTextLayout(ui::DrawScope& scope, const TextLayoutResult& layout, const TextStyle& style);

}
