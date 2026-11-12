#include "kiln/foundation/text/TextMeasurer.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        bool SameInput(const text::TextLayoutInput& a, const text::TextLayoutInput& b) {
            return a.text == b.text && a.style == b.style && a.maxLines == b.maxLines && a.softWrap == b.softWrap && a.overflow == b.overflow &&
                   a.placeholders == b.placeholders && a.density.density == b.density.density && a.density.fontScale == b.density.fontScale &&
                   a.layoutDirection == b.layoutDirection && a.constraints == b.constraints;
        }
    }

    TextLayoutResult TextMeasurer::Measure(const AnnotatedString& text, const TextStyle& style, text::TextOverflow overflow, bool softWrap, int maxLines,
                                           std::vector<text::Range<text::Placeholder>> placeholders, ui::Constraints constraints,
                                           std::optional<ui::LayoutDirection> layoutDirection, std::optional<ui::Density> density, bool skipCache) {
        text::TextLayoutInput input;
        input.text = text;
        input.style = style;
        input.overflow = overflow;
        input.softWrap = softWrap;
        input.maxLines = std::max(1, maxLines);
        input.placeholders = std::move(placeholders);
        input.constraints = constraints;
        input.layoutDirection = layoutDirection.value_or(m_Direction);
        input.density = density.value_or(m_Density);
        const bool useCache = !skipCache && m_CacheSize > 0;
        if (useCache) {
            for (auto it = m_Cache.begin(); it != m_Cache.end(); ++it) {
                if (!SameInput(it->LayoutInput(), input)) continue;
                m_Cache.splice(m_Cache.begin(), m_Cache, it);   // most recently used
                return m_Cache.front();
            }
        }
        TextLayoutResult result = text::LayoutText(m_Backend, std::move(input));
        if (useCache) {
            m_Cache.push_front(result);
            while (int(m_Cache.size()) > m_CacheSize) m_Cache.pop_back();
        }
        return result;
    }

    TextMeasurer& RememberTextMeasurer(int cacheSize, rt::Key key) {
        rt::Composition* c = rt::Composition::Current();
        const ui::Density density = c->Read(ui::LocalDensity);
        const ui::LayoutDirection direction = c->Read(ui::LocalLayoutDirection);
        ui::Owner* owner = c->Read(ui::LocalOwner);
        TextBackend* backend = owner ? owner->Text() : nullptr;
        return c->RememberNew<TextMeasurer>(rt::Keys(density.density, density.fontScale, int(direction), reinterpret_cast<std::uintptr_t>(backend), cacheSize),
                                            key, backend, density, direction, cacheSize);
    }

    void TextPainter::Paint(ui::DrawScope& s, const TextLayoutResult& layout) {
        DrawText(s, layout);
    }

    void DrawText(ui::DrawScope& s, const TextLayoutResult& layout, ui::Color color, ui::Offset topLeft, float alpha, std::optional<text::Shadow> shadow,
                  std::optional<text::TextDecoration> textDecoration, std::optional<text::DrawStyle> drawStyle, gfx::BlendMode blendMode) {
        TextStyle style = layout.LayoutInput().style;
        if (color.Specified()) {
            style.span.color = std::isnan(alpha) ? color : color.WithAlpha(color.Alpha() * std::clamp(alpha, 0.0f, 1.0f));
            style.span.brush.reset();
        } else if (!std::isnan(alpha)) {
            if (style.span.brush) style.span.alpha = std::clamp(alpha, 0.0f, 1.0f);
            else if (style.span.color.Specified()) style.span.color = style.span.color.WithAlpha(style.span.color.Alpha() * std::clamp(alpha, 0.0f, 1.0f));
        }
        if (shadow) style.span.shadow = shadow;
        if (textDecoration) style.span.textDecoration = textDecoration;
        if (drawStyle) style.span.drawStyle = drawStyle;
        s.Save();
        if (blendMode != gfx::BlendMode::SrcOver) s.SetBlendMode(blendMode);
        s.Translate(topLeft);
        const ui::IntSize size = layout.Size();
        if (layout.HasVisualOverflow() && layout.LayoutInput().overflow != text::TextOverflow::Visible) s.ClipRect({ 0, 0, float(size.width), float(size.height) });
        DrawTextLayout(s, layout, style);
        s.Restore();
    }

    void DrawText(ui::DrawScope& s, TextMeasurer& measurer, const AnnotatedString& text, ui::Offset topLeft, const TextStyle& style,
                  text::TextOverflow overflow, bool softWrap, int maxLines, std::vector<text::Range<text::Placeholder>> placeholders,
                  std::optional<ui::Size> size, gfx::BlendMode blendMode) {
        // textLayoutConstraints: the size given, or the rest of the scope from topLeft.
        ui::Constraints c;
        const ui::Size scope = s.GetSize();
        const float w = size ? size->width : scope.width - topLeft.x;
        const float h = size ? size->height : scope.height - topLeft.y;
        if (size && std::isfinite(w)) { c.minWidth = c.maxWidth = std::max(0, int(std::ceil(w))); }
        else if (std::isfinite(w)) c.maxWidth = std::max(0, int(std::ceil(w)));
        if (size && std::isfinite(h)) { c.minHeight = c.maxHeight = std::max(0, int(std::ceil(h))); }
        else if (std::isfinite(h)) c.maxHeight = std::max(0, int(std::ceil(h)));
        const TextLayoutResult layout = measurer.Measure(text, style, overflow, softWrap, maxLines, std::move(placeholders), c, s.Direction(), ui::Density(s));
        DrawText(s, layout, ui::Color::Unspecified(), topLeft, std::numeric_limits<float>::quiet_NaN(), std::nullopt, std::nullopt, std::nullopt, blendMode);
    }

}
