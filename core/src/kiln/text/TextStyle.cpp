#include "kiln/text/TextStyle.h"

namespace kiln::text {

    namespace {
        template<typename T> std::optional<T> Or(const std::optional<T>& a, const std::optional<T>& b) { return a ? a : b; }
        bool SameAlpha(float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; }
    }

    SpanStyle SpanStyle::Merge(const SpanStyle& o) const {
        SpanStyle r = *this;
        // TextForegroundStyle.merge: a brush wins; a colour never overrides an existing brush.
        if (o.brush) { r.brush = o.brush; r.alpha = o.alpha; r.color = Color::Unspecified(); }
        else if (o.color.Specified() && !brush) r.color = o.color;
        if (o.fontSize.IsSpecified()) r.fontSize = o.fontSize;
        r.fontWeight = Or(o.fontWeight, fontWeight);
        r.fontStyle = Or(o.fontStyle, fontStyle);
        r.fontSynthesis = Or(o.fontSynthesis, fontSynthesis);
        r.fontFamily = Or(o.fontFamily, fontFamily);
        r.fontFeatureSettings = Or(o.fontFeatureSettings, fontFeatureSettings);
        if (o.letterSpacing.IsSpecified()) r.letterSpacing = o.letterSpacing;
        r.baselineShift = Or(o.baselineShift, baselineShift);
        r.textGeometricTransform = Or(o.textGeometricTransform, textGeometricTransform);
        r.localeList = Or(o.localeList, localeList);
        if (o.background.Specified()) r.background = o.background;
        r.textDecoration = Or(o.textDecoration, textDecoration);
        r.shadow = Or(o.shadow, shadow);
        r.drawStyle = Or(o.drawStyle, drawStyle);
        return r;
    }

    bool SpanStyle::HasSameLayoutAffectingAttributes(const SpanStyle& o) const {
        return fontSize == o.fontSize && fontWeight == o.fontWeight && fontStyle == o.fontStyle && fontSynthesis == o.fontSynthesis &&
               fontFamily == o.fontFamily && fontFeatureSettings == o.fontFeatureSettings && letterSpacing == o.letterSpacing &&
               baselineShift == o.baselineShift && textGeometricTransform == o.textGeometricTransform && localeList == o.localeList;
    }

    bool SpanStyle::operator==(const SpanStyle& o) const {
        return HasSameLayoutAffectingAttributes(o) && color == o.color && brush == o.brush && SameAlpha(alpha, o.alpha) &&
               background == o.background && textDecoration == o.textDecoration && shadow == o.shadow && drawStyle == o.drawStyle;
    }

    ParagraphStyle ParagraphStyle::Merge(const ParagraphStyle& o) const {
        ParagraphStyle r = *this;
        if (o.textAlign != TextAlign::Unspecified) r.textAlign = o.textAlign;
        if (o.textDirection != TextDirection::Unspecified) r.textDirection = o.textDirection;
        if (o.lineHeight.IsSpecified()) r.lineHeight = o.lineHeight;
        r.textIndent = Or(o.textIndent, textIndent);
        r.lineHeightStyle = Or(o.lineHeightStyle, lineHeightStyle);
        if (o.lineBreak.IsSpecified()) r.lineBreak = o.lineBreak;
        if (o.hyphens != Hyphens::Unspecified) r.hyphens = o.hyphens;
        r.textMotion = Or(o.textMotion, textMotion);
        r.includeFontPadding = Or(o.includeFontPadding, includeFontPadding);
        return r;
    }

    TextDirection ResolveTextDirection(LayoutDirection dir, TextDirection d) {
        switch (d) {
            case TextDirection::Content: return dir == LayoutDirection::Ltr ? TextDirection::ContentOrLtr : TextDirection::ContentOrRtl;
            case TextDirection::Unspecified: return dir == LayoutDirection::Ltr ? TextDirection::Ltr : TextDirection::Rtl;
            default: return d;
        }
    }

    TextStyle ResolveDefaults(const TextStyle& style, LayoutDirection dir) {
        TextStyle r = style;
        SpanStyle& s = r.span;
        if (!s.brush && !s.color.Specified()) s.color = Color(0xFF000000u);
        if (!s.fontSize.IsSpecified()) s.fontSize = Sp(14);
        if (!s.fontWeight) s.fontWeight = FontWeight::Normal();
        if (!s.fontStyle) s.fontStyle = FontStyle::Normal;
        if (!s.fontSynthesis) s.fontSynthesis = FontSynthesis::All;
        if (!s.fontFamily) s.fontFamily = FontFamily::Default();
        if (!s.fontFeatureSettings) s.fontFeatureSettings = "";
        if (!s.letterSpacing.IsSpecified()) s.letterSpacing = Sp(0);
        if (!s.baselineShift || std::isnan(s.baselineShift->multiplier)) s.baselineShift = BaselineShift::None();
        if (!s.textGeometricTransform) s.textGeometricTransform = TextGeometricTransform{};
        if (!s.background.Specified()) s.background = Color::Transparent();
        if (!s.textDecoration) s.textDecoration = TextDecoration::None;
        if (!s.shadow) s.shadow = Shadow{ Color::Transparent(), {}, 0 };
        if (!s.drawStyle) s.drawStyle = DrawStyle{};
        ParagraphStyle& p = r.paragraph;
        if (p.textAlign == TextAlign::Unspecified) p.textAlign = TextAlign::Start;
        p.textDirection = ResolveTextDirection(dir, p.textDirection);
        if (!p.textIndent) p.textIndent = TextIndent{};
        if (!p.lineBreak.IsSpecified()) p.lineBreak = LineBreak::Simple();
        if (p.hyphens == Hyphens::Unspecified) p.hyphens = Hyphens::None;
        if (!p.textMotion) p.textMotion = TextMotion::Static;
        if (!p.includeFontPadding) p.includeFontPadding = false;
        return r;
    }

}
