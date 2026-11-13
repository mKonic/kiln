#pragma once

// material3 Text.kt and Icon.kt: Text (LocalTextStyle merged with the given values, coloured by
// the content colour, links in the primary colour, underlined) and Icon (an image vector,
// bitmap or painter tinted by the content colour, 24 dp unless it has a size of its own).

#include "kiln/foundation/Image.h"
#include <kiln/Function.h>
#include "kiln/foundation/text/BasicText.h"
#include "kiln/graphics/vector/ImageVector.h"

namespace kiln::material3 {

    struct TextOptions {
        ui::Color color = ui::Color::Unspecified();
        // A colour read at draw time (Text(text, color: ColorProducer)); replaces `color`.
        kiln::Function<ui::Color()> colorProducer;
        foundation::TextAutoSizePtr autoSize;
        text::TextUnit fontSize;
        std::optional<text::FontStyle> fontStyle;
        std::optional<text::FontWeight> fontWeight;
        std::optional<text::FontFamily> fontFamily;
        text::TextUnit letterSpacing;
        std::optional<text::TextDecoration> textDecoration;
        std::optional<text::TextAlign> textAlign;
        text::TextUnit lineHeight;
        text::TextOverflow overflow = text::TextOverflow::Clip;
        bool softWrap = true;
        int maxLines = INT_MAX;
        int minLines = 1;
        std::map<std::string, foundation::InlineTextContent> inlineContent;
        kiln::Function<void(const text::TextLayoutResult&)> onTextLayout;
        std::optional<text::TextStyle> style;   // unset: LocalTextStyle
    };
    void Text(const std::string& text, const ui::Modifier& modifier = {}, const TextOptions& options = {});
    void Text(const text::AnnotatedString& text, const ui::Modifier& modifier = {}, const TextOptions& options = {});
    inline void Text(const char* text, const ui::Modifier& modifier = {}, const TextOptions& options = {}) { Text(std::string(text), modifier, options); }

    // Icon: `tint` Unspecified draws the painter's own colours; with a content description the
    // icon is an image to accessibility.
    void Icon(const gfx::vector::ImageVectorPtr& imageVector, const std::optional<std::string>& contentDescription,
              const ui::Modifier& modifier = {}, std::optional<ui::Color> tint = std::nullopt);
    void Icon(const gfx::ImageBitmap& bitmap, const std::optional<std::string>& contentDescription, const ui::Modifier& modifier = {},
              std::optional<ui::Color> tint = std::nullopt);
    void Icon(std::shared_ptr<foundation::Painter> painter, const std::optional<std::string>& contentDescription,
              const ui::Modifier& modifier = {}, std::optional<ui::Color> tint = std::nullopt);
    // Icon(painter, tint: ColorProducer?): the tint read at draw time, through a layer.
    void Icon(std::shared_ptr<foundation::Painter> painter, kiln::Function<ui::Color()> tint, const std::optional<std::string>& contentDescription,
              const ui::Modifier& modifier = {});

    namespace internal {
        // ProvideContentColorTextStyle: the content colour, and the text style merged over the current one.
        void ProvideContentColorTextStyle(ui::Color contentColor, const text::TextStyle& textStyle, kiln::Function<void()> content);
    }

}
