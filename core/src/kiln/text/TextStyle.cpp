#include "kiln/text/TextStyle.h"
#include "kiln/ui/ColorSpace.h"

#include <algorithm>
#include <cmath>

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


    namespace {
        template<typename T> T Discrete(const T& a, const T& b, float t) { return t < 0.5f ? a : b; }
        float LerpF(float a, float b, float t) { return a + (b - a) * t; }
        // lerpTextUnitInheritable
        TextUnit LerpUnit(TextUnit a, TextUnit b, float t) {
            if (!a.IsSpecified() || !b.IsSpecified()) return Discrete(a, b, t);
            return TextUnit{ LerpF(a.value, b.value, t), a.type };
        }
    }

    TextStyle Lerp(const TextStyle& start, const TextStyle& stop, float t) {
        const SpanStyle& a = start.span;
        const SpanStyle& b = stop.span;
        SpanStyle s;
        // TextForegroundStyle: two colours interpolate; brushes and unspecified switch.
        if (a.brush || b.brush || !a.color.Specified() || !b.color.Specified()) {
            s.color = Discrete(a.color, b.color, t);
            s.brush = Discrete(a.brush, b.brush, t);
            s.alpha = Discrete(a.alpha, b.alpha, t);
        } else {
            s.color = ui::Lerp(a.color, b.color, t);
        }
        s.fontFamily = Discrete(a.fontFamily, b.fontFamily, t);
        s.fontSize = LerpUnit(a.fontSize, b.fontSize, t);
        const int wa = a.fontWeight.value_or(FontWeight{}).weight, wb = b.fontWeight.value_or(FontWeight{}).weight;
        s.fontWeight = FontWeight{ std::clamp(wa + int(std::floor(double(wb - wa) * t + 0.5)), 1, 1000) };
        s.fontStyle = Discrete(a.fontStyle, b.fontStyle, t);
        s.fontSynthesis = Discrete(a.fontSynthesis, b.fontSynthesis, t);
        s.fontFeatureSettings = Discrete(a.fontFeatureSettings, b.fontFeatureSettings, t);
        s.letterSpacing = LerpUnit(a.letterSpacing, b.letterSpacing, t);
        s.baselineShift = BaselineShift{ LerpF(a.baselineShift.value_or(BaselineShift{}).multiplier, b.baselineShift.value_or(BaselineShift{}).multiplier, t) };
        const TextGeometricTransform ga = a.textGeometricTransform.value_or(TextGeometricTransform{}),
                                     gb = b.textGeometricTransform.value_or(TextGeometricTransform{});
        s.textGeometricTransform = TextGeometricTransform{ LerpF(ga.scaleX, gb.scaleX, t), LerpF(ga.skewX, gb.skewX, t) };
        s.background = a.background.Specified() && b.background.Specified() ? ui::Lerp(a.background, b.background, t) : Discrete(a.background, b.background, t);
        s.textDecoration = Discrete(a.textDecoration, b.textDecoration, t);
        const Shadow sa = a.shadow.value_or(Shadow{}), sb = b.shadow.value_or(Shadow{});
        Shadow sh;
        sh.color = ui::Lerp(sa.color, sb.color, t);
        sh.offset = { LerpF(sa.offset.x, sb.offset.x, t), LerpF(sa.offset.y, sb.offset.y, t) };
        sh.blurRadius = LerpF(sa.blurRadius, sb.blurRadius, t);
        s.shadow = sh;
        s.drawStyle = Discrete(a.drawStyle, b.drawStyle, t);

        const ParagraphStyle& pa = start.paragraph;
        const ParagraphStyle& pb = stop.paragraph;
        ParagraphStyle p;
        p.textAlign = Discrete(pa.textAlign, pb.textAlign, t);
        p.textDirection = Discrete(pa.textDirection, pb.textDirection, t);
        p.lineHeight = LerpUnit(pa.lineHeight, pb.lineHeight, t);
        const TextIndent ia = pa.textIndent.value_or(TextIndent{}), ib = pb.textIndent.value_or(TextIndent{});
        p.textIndent = TextIndent{ LerpUnit(ia.firstLine, ib.firstLine, t), LerpUnit(ia.restLine, ib.restLine, t) };
        p.lineHeightStyle = Discrete(pa.lineHeightStyle, pb.lineHeightStyle, t);
        p.lineBreak = Discrete(pa.lineBreak, pb.lineBreak, t);
        p.hyphens = Discrete(pa.hyphens, pb.hyphens, t);
        p.textMotion = Discrete(pa.textMotion, pb.textMotion, t);
        return { s, p };
    }

}
