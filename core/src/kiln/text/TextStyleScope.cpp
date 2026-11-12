#include "kiln/text/TextStyleScope.h"

#include <cmath>

namespace kiln::text {

    namespace {
        bool SameAlpha(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }

        bool SameLayout(const TextStyle& a, const TextStyle& b) {
            return a.span.HasSameLayoutAffectingAttributes(b.span) && a.span.background == b.span.background && a.paragraph == b.paragraph;
        }
    }

    void ReusableTextStyleScope::SetTextStyle(const TextStyle& style) {
        m_Style = style;
        if (style.span.brush) {
            m_Style.span.color = Color::Unspecified();
        } else {
            m_Style.span.alpha = NAN;
        }
    }

    void ReusableTextStyleScope::SetColor(Color color) {
        m_Style.span.color = color;
        if (color.Specified()) {
            m_Style.span.brush.reset();
            m_Style.span.alpha = NAN;
        }
    }

    bool ReusableTextStyleScope::HasSameLayoutAffectingAttributes(const ReusableTextStyleScope& other) const {
        return this == &other || SameLayout(m_Style, other.m_Style);
    }

    bool ReusableTextStyleScope::HasSameLayoutAffectingAttributes(const TextStyle& style) const { return SameLayout(m_Style, style); }

    bool ReusableTextStyleScope::HasSameDrawAffectingAttributes(const ReusableTextStyleScope& other) const {
        if (this == &other) return true;
        const SpanStyle &a = m_Style.span, &b = other.m_Style.span;
        return a.color == b.color && a.brush == b.brush && SameAlpha(a.alpha, b.alpha) && a.shadow == b.shadow && a.textDecoration == b.textDecoration &&
               a.drawStyle == b.drawStyle;
    }

    // hasSameDrawAffectingAttributes(style): the style's brush (with its alpha) or its colour.
    bool ReusableTextStyleScope::HasSameDrawAffectingAttributes(const TextStyle& style) const {
        const SpanStyle &a = m_Style.span, &s = style.span;
        if (s.brush) {
            if (a.color.Specified() || a.brush != s.brush || !SameAlpha(a.alpha, s.alpha)) return false;
        } else {
            if (!(a.color == s.color) || a.brush || !std::isnan(a.alpha)) return false;
        }
        return a.shadow == s.shadow && a.textDecoration == s.textDecoration && a.drawStyle == s.drawStyle;
    }

}
