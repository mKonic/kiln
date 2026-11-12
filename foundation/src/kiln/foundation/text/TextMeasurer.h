#pragma once

// TextMeasurer and drawText (ui-text TextMeasurer.kt, TextPainter.kt, foundation
// TextMeasurerHelper.kt): lay text out outside of a layout node, cached (the last few inputs),
// and draw a laid-out text, or measure and draw in one call, in a DrawScope.

#include "kiln/foundation/text/BasicText.h"
#include "kiln/text/Paragraph.h"

#include <climits>
#include <list>
#include <optional>

namespace kiln { class TextBackend; }

namespace kiln::foundation {

    class TextMeasurer {
    public:
        // cacheSize 0: no cache.
        TextMeasurer(TextBackend* backend, ui::Density density, ui::LayoutDirection direction, int cacheSize = 8)
            : m_Backend(backend), m_Density(density), m_Direction(direction), m_CacheSize(std::max(0, cacheSize)) {}
        // measure: the text laid out within the constraints (unbounded by default).
        TextLayoutResult Measure(const AnnotatedString& text, const TextStyle& style = {}, text::TextOverflow overflow = text::TextOverflow::Clip,
                                 bool softWrap = true, int maxLines = INT_MAX, std::vector<text::Range<text::Placeholder>> placeholders = {},
                                 ui::Constraints constraints = {}, std::optional<ui::LayoutDirection> layoutDirection = std::nullopt,
                                 std::optional<ui::Density> density = std::nullopt, bool skipCache = false);
        TextLayoutResult Measure(const std::string& text, const TextStyle& style = {}, text::TextOverflow overflow = text::TextOverflow::Clip,
                                 bool softWrap = true, int maxLines = INT_MAX, ui::Constraints constraints = {},
                                 std::optional<ui::LayoutDirection> layoutDirection = std::nullopt, std::optional<ui::Density> density = std::nullopt,
                                 bool skipCache = false) {
            return Measure(AnnotatedString(text), style, overflow, softWrap, maxLines, {}, constraints, layoutDirection, density, skipCache);
        }
        TextLayoutResult Measure(const char* text, const TextStyle& style = {}, text::TextOverflow overflow = text::TextOverflow::Clip, bool softWrap = true,
                                 int maxLines = INT_MAX, ui::Constraints constraints = {}, std::optional<ui::LayoutDirection> layoutDirection = std::nullopt,
                                 std::optional<ui::Density> density = std::nullopt, bool skipCache = false) {
            return Measure(AnnotatedString(text), style, overflow, softWrap, maxLines, {}, constraints, layoutDirection, density, skipCache);
        }
        TextBackend* Backend() const { return m_Backend; }

    private:
        TextBackend* m_Backend;
        ui::Density m_Density;
        ui::LayoutDirection m_Direction;
        int m_CacheSize;
        std::list<TextLayoutResult> m_Cache;   // most recent first
    };

    // rememberTextMeasurer(cacheSize): one for the composition's density, direction and text.
    TextMeasurer& RememberTextMeasurer(int cacheSize = 8, rt::Key key = {});

    // TextPainter.paint: a laid-out text at the origin, clipped to its size when it overflows
    // (unless its overflow is Visible).
    struct TextPainter {
        static void Paint(ui::DrawScope& scope, const TextLayoutResult& layout);
    };

    // DrawScope.drawText(textLayoutResult, ...): drawn at topLeft; colour, alpha, shadow,
    // decoration and draw style replace the layout's own when given.
    void DrawText(ui::DrawScope& scope, const TextLayoutResult& layout, ui::Color color = ui::Color::Unspecified(), ui::Offset topLeft = {},
                  float alpha = std::numeric_limits<float>::quiet_NaN(), std::optional<text::Shadow> shadow = std::nullopt,
                  std::optional<text::TextDecoration> textDecoration = std::nullopt, std::optional<text::DrawStyle> drawStyle = std::nullopt,
                  gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
    // DrawScope.drawText(textMeasurer, text, ...): measured to `size` (by default the rest of the
    // scope from topLeft) and drawn at topLeft.
    void DrawText(ui::DrawScope& scope, TextMeasurer& measurer, const AnnotatedString& text, ui::Offset topLeft = {}, const TextStyle& style = {},
                  text::TextOverflow overflow = text::TextOverflow::Clip, bool softWrap = true, int maxLines = INT_MAX,
                  std::vector<text::Range<text::Placeholder>> placeholders = {}, std::optional<ui::Size> size = std::nullopt,
                  gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);

}
