#pragma once

// TextStyleScope (ui-text TextStyleScope.kt): text attributes set one by one, as BasicText's
// styleProvider does. ReusableTextStyleScope keeps them (in a TextStyle: unset fields stay
// unset) and compares what changes layout and what changes only drawing, so a provider whose
// state changes only a colour redraws without a new layout.

#include "kiln/text/TextStyle.h"

namespace kiln::text {

    class TextStyleScope {
    public:
        virtual ~TextStyleScope() = default;
        virtual void SetTextStyle(const TextStyle& style) = 0;
        virtual void SetColor(Color color) = 0;
        virtual void SetBrush(gfx::Brush brush) = 0;
        virtual void SetAlpha(float alpha) = 0;
        virtual void SetFontSize(TextUnit fontSize) = 0;
        virtual void SetFontWeight(FontWeight fontWeight) = 0;
        virtual void SetFontStyle(FontStyle fontStyle) = 0;
        virtual void SetFontSynthesis(FontSynthesis fontSynthesis) = 0;
        virtual void SetFontFamily(FontFamily fontFamily) = 0;
        virtual void SetFontFeatureSettings(std::string fontFeatureSettings) = 0;
        virtual void SetLetterSpacing(TextUnit letterSpacing) = 0;
        virtual void SetBaselineShift(BaselineShift baselineShift) = 0;
        virtual void SetTextGeometricTransform(TextGeometricTransform transform) = 0;
        virtual void SetLocaleList(std::string localeList) = 0;
        virtual void SetBackground(Color background) = 0;
        virtual void SetTextDecoration(TextDecoration textDecoration) = 0;
        virtual void SetShadow(Shadow shadow) = 0;
        virtual void SetDrawStyle(DrawStyle drawStyle) = 0;
        virtual void SetTextAlign(TextAlign textAlign) = 0;
        virtual void SetTextDirection(TextDirection textDirection) = 0;
        virtual void SetLineHeight(TextUnit lineHeight) = 0;
        virtual void SetTextIndent(TextIndent textIndent) = 0;
        virtual void SetPlatformStyle(const PlatformTextStyle& platformStyle) = 0;
        virtual void SetLineHeightStyle(LineHeightStyle lineHeightStyle) = 0;
        virtual void SetLineBreak(LineBreak lineBreak) = 0;
        virtual void SetHyphens(Hyphens hyphens) = 0;
        virtual void SetTextMotion(TextMotion textMotion) = 0;
    };

    class ReusableTextStyleScope final : public TextStyleScope {
    public:
        // setTextStyle: every attribute from the style (a brush clears the colour; a colour
        // clears the brush and its alpha).
        void SetTextStyle(const TextStyle& style) override;
        // setColor: a specified colour replaces the brush.
        void SetColor(Color color) override;
        void SetBrush(gfx::Brush brush) override { m_Style.span.brush = std::move(brush); m_Style.span.color = Color::Unspecified(); }
        void SetAlpha(float alpha) override { m_Style.span.alpha = alpha; }
        void SetFontSize(TextUnit v) override { m_Style.span.fontSize = v; }
        void SetFontWeight(FontWeight v) override { m_Style.span.fontWeight = v; }
        void SetFontStyle(FontStyle v) override { m_Style.span.fontStyle = v; }
        void SetFontSynthesis(FontSynthesis v) override { m_Style.span.fontSynthesis = v; }
        void SetFontFamily(FontFamily v) override { m_Style.span.fontFamily = std::move(v); }
        void SetFontFeatureSettings(std::string v) override { m_Style.span.fontFeatureSettings = std::move(v); }
        void SetLetterSpacing(TextUnit v) override { m_Style.span.letterSpacing = v; }
        void SetBaselineShift(BaselineShift v) override { m_Style.span.baselineShift = v; }
        void SetTextGeometricTransform(TextGeometricTransform v) override { m_Style.span.textGeometricTransform = v; }
        void SetLocaleList(std::string v) override { m_Style.span.localeList = std::move(v); }
        void SetBackground(Color v) override { m_Style.span.background = v; }
        void SetTextDecoration(TextDecoration v) override { m_Style.span.textDecoration = v; }
        void SetShadow(Shadow v) override { m_Style.span.shadow = v; }
        void SetDrawStyle(DrawStyle v) override { m_Style.span.drawStyle = v; }
        void SetTextAlign(TextAlign v) override { m_Style.paragraph.textAlign = v; }
        void SetTextDirection(TextDirection v) override { m_Style.paragraph.textDirection = v; }
        void SetLineHeight(TextUnit v) override { m_Style.paragraph.lineHeight = v; }
        void SetTextIndent(TextIndent v) override { m_Style.paragraph.textIndent = v; }
        void SetPlatformStyle(const PlatformTextStyle& v) override { m_Style.SetPlatformStyle(v); }
        void SetLineHeightStyle(LineHeightStyle v) override { m_Style.paragraph.lineHeightStyle = v; }
        void SetLineBreak(LineBreak v) override { m_Style.paragraph.lineBreak = v; }
        void SetHyphens(Hyphens v) override { m_Style.paragraph.hyphens = v; }
        void SetTextMotion(TextMotion v) override { m_Style.paragraph.textMotion = v; }

        // The attributes set so far (unset ones unset; the getters' values).
        const TextStyle& Style() const { return m_Style; }
        Color GetColor() const { return m_Style.span.color; }
        const std::optional<gfx::Brush>& GetBrush() const { return m_Style.span.brush; }
        float GetAlpha() const { return m_Style.span.alpha; }
        std::optional<PlatformTextStyle> GetPlatformStyle() const { return m_Style.PlatformStyle(); }
        // toTextStyle (foundation): the style BasicText lays out and draws with.
        TextStyle ToTextStyle() const { return m_Style; }

        // reset: nothing set.
        void Reset() { m_Style = {}; }
        // Every attribute but colour, brush, alpha, shadow, decoration and draw style is equal
        // (the background counts here, as Compose has it).
        bool HasSameLayoutAffectingAttributes(const ReusableTextStyleScope& other) const;
        bool HasSameLayoutAffectingAttributes(const TextStyle& style) const;
        // Colour, brush, alpha, shadow, decoration and draw style are equal.
        bool HasSameDrawAffectingAttributes(const ReusableTextStyleScope& other) const;
        bool HasSameDrawAffectingAttributes(const TextStyle& style) const;
        void CopyFrom(const ReusableTextStyleScope& other) { m_Style = other.m_Style; }

    private:
        TextStyle m_Style;
    };

}
