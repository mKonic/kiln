#pragma once

// BasicText (foundation text/BasicText.kt, modifiers/TextAnnotatedStringNode.kt,
// HeightInLinesModifier.kt): styled text laid out by the platform. Wraps to the max width when
// softWrap, ellipsizes or clips per overflow, keeps between minLines and maxLines of height,
// reports each layout to onTextLayout. Colour-only style changes redraw without a new layout.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>
#include "kiln/foundation/text/TextAutoSize.h"
#include "kiln/text/Paragraph.h"
#include "kiln/text/TextStyleScope.h"

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
        kiln::Function<void(const std::string& alternateText)> children;
    };
    // appendInlineContent: marks text (by default U+FFFD) to be replaced by the inline content
    // with this id.
    inline constexpr const char* kInlineContentTag = "androidx.compose.foundation.text.inlineContent";
    void AppendInlineContent(text::AnnotatedStringBuilder& builder, const std::string& id, const std::string& alternateText = "\xEF\xBF\xBD");

    struct BasicTextOptions {
        kiln::Function<void(const TextLayoutResult&)> onTextLayout;
        TextOverflow overflow = TextOverflow::Clip;
        bool softWrap = true;
        int maxLines = INT_MAX;
        int minLines = 1;
        std::map<std::string, InlineTextContent> inlineContent;
        // ColorProducer: read when drawing, overriding the style's colour when specified (a
        // colour animation redraws without recomposing or a new layout).
        kiln::Function<ui::Color()> color;
        // The font size picked for the constraints (the style's own size is ignored).
        TextAutoSizePtr autoSize;
        // styleProvider (the BasicText(text, styleProvider) overloads): sets the style in place
        // of `style`, run when measuring so the state it reads lays out again only when a
        // layout attribute changes (a colour change only redraws).
        kiln::Function<void(text::TextStyleScope&)> styleProvider;
    };

    void BasicText(const AnnotatedString& text, const Modifier& modifier = {}, TextStyle style = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    void BasicText(const std::string& text, const Modifier& modifier = {}, TextStyle style = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    inline void BasicText(const char* text, const Modifier& modifier = {}, TextStyle style = {},
                          BasicTextOptions options = {}, rt::Key key = {}) {
        BasicText(std::string(text), modifier, std::move(style), std::move(options), key);
    }
    // BasicText(text, styleProvider, ..): the style from a TextStyleScope block.
    void BasicText(const AnnotatedString& text, kiln::Function<void(text::TextStyleScope&)> styleProvider, const Modifier& modifier = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    void BasicText(const std::string& text, kiln::Function<void(text::TextStyleScope&)> styleProvider, const Modifier& modifier = {},
                   BasicTextOptions options = {}, rt::Key key = {});
    inline void BasicText(const char* text, kiln::Function<void(text::TextStyleScope&)> styleProvider, const Modifier& modifier = {},
                          BasicTextOptions options = {}, rt::Key key = {}) {
        BasicText(std::string(text), std::move(styleProvider), modifier, std::move(options), key);
    }

    // Draws a laid-out text at the origin: backgrounds, shadows, glyphs (brushes masked to each
    // paragraph), underlines and strike-throughs. `style` is the current style: run colours
    // that the text's spans leave unset come from it.
    void DrawTextLayout(ui::DrawScope& scope, const TextLayoutResult& layout, const TextStyle& style);
    // The text's draw annotations, behind and over it (BasicText and drawText draw both).
    void DrawAnnotationsBehind(ui::DrawScope& scope, const TextLayoutResult& layout);
    void DrawAnnotationsOver(ui::DrawScope& scope, const TextLayoutResult& layout);

}
