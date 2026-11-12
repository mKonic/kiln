#pragma once

// The text style model (ui-text SpanStyle.kt, ParagraphStyle.kt, TextStyle.kt and style/*):
// character-level SpanStyle, paragraph-level ParagraphStyle, and TextStyle holding both.
// Unset fields fall through on merge (the other style's set fields win); ResolveDefaults fills
// what is still unset with Compose's defaults (14.sp, black, Normal weight, Start, ...).

#include "kiln/graphics/Brush.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/Geometry.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::text {

    using ui::Color;
    using ui::LayoutDirection;
    using ui::Offset;

    // TextUnit: sp, em or unspecified.
    struct TextUnit {
        enum class Type : std::uint8_t { Unspecified, Sp, Em };
        float value = NAN;
        Type type = Type::Unspecified;
        bool IsSpecified() const { return type != Type::Unspecified; }
        bool IsSp() const { return type == Type::Sp; }
        bool IsEm() const { return type == Type::Em; }
        static constexpr TextUnit Unspecified() { return {}; }
        TextUnit TakeOrElse(TextUnit fallback) const { return IsSpecified() ? *this : fallback; }
        bool operator==(const TextUnit& o) const { return type == o.type && (type == Type::Unspecified || value == o.value); }
    };
    constexpr TextUnit Sp(float v) { return { v, TextUnit::Type::Sp }; }
    constexpr TextUnit Em(float v) { return { v, TextUnit::Type::Em }; }
    constexpr TextUnit operator""_sp(long double v) { return Sp(float(v)); }
    constexpr TextUnit operator""_sp(unsigned long long v) { return Sp(float(v)); }
    constexpr TextUnit operator""_em(long double v) { return Em(float(v)); }

    struct FontWeight {
        int weight = 400;   // 1..1000
        bool operator==(const FontWeight&) const = default;
        auto operator<=>(const FontWeight&) const = default;
        static constexpr FontWeight W100() { return { 100 }; }
        static constexpr FontWeight W200() { return { 200 }; }
        static constexpr FontWeight W300() { return { 300 }; }
        static constexpr FontWeight W400() { return { 400 }; }
        static constexpr FontWeight W500() { return { 500 }; }
        static constexpr FontWeight W600() { return { 600 }; }
        static constexpr FontWeight W700() { return { 700 }; }
        static constexpr FontWeight W800() { return { 800 }; }
        static constexpr FontWeight W900() { return { 900 }; }
        static constexpr FontWeight Thin() { return W100(); }
        static constexpr FontWeight ExtraLight() { return W200(); }
        static constexpr FontWeight Light() { return W300(); }
        static constexpr FontWeight Normal() { return W400(); }
        static constexpr FontWeight Medium() { return W500(); }
        static constexpr FontWeight SemiBold() { return W600(); }
        static constexpr FontWeight Bold() { return W700(); }
        static constexpr FontWeight ExtraBold() { return W800(); }
        static constexpr FontWeight Black() { return W900(); }
    };

    enum class FontStyle : std::uint8_t { Normal, Italic };

    // FontSynthesis: which of bold / italic the system may fake when a font lacks them.
    enum class FontSynthesis : std::uint8_t { None = 0, Weight = 1, Style = 2, All = 3 };

    // A downloadable font provider (FontRequest's authority, package and signing certificates,
    // base64 DER; any one may match).
    struct FontProvider {
        std::string authority, package;
        std::vector<std::string> certificates;
        bool operator==(const FontProvider&) const = default;
        // GoogleFont.Provider for Google Play services' font provider.
        static FontProvider GoogleFonts();
    };

    // A font: a file (Font(asset/file, weight, style, variationSettings)), or a downloadable font
    // (path is the provider query) that loads in the background; text uses the default family
    // until it arrives, then lays out again.
    struct Font {
        enum class Source : std::uint8_t { Asset, File, Downloadable };
        Source source = Source::Asset;
        std::string path;
        FontProvider provider;   // Downloadable
        FontWeight weight = FontWeight::Normal();
        FontStyle style = FontStyle::Normal;
        // FontVariation.Settings: "'wght' 650, 'wdth' 90" style axes.
        std::string variationSettings;
        bool operator==(const Font&) const = default;
        // Font(GoogleFont(name, bestEffort), provider, weight, style).
        static Font Google(const std::string& name, FontWeight weight = FontWeight::Normal(), FontStyle style = FontStyle::Normal,
                           bool bestEffort = true, FontProvider provider = FontProvider::GoogleFonts());
    };

    // FontFamily: a generic system family, a named system family, or a list of fonts.
    struct FontFamily {
        enum class Kind : std::uint8_t { Default, SansSerif, Serif, Monospace, Cursive, SystemName, Fonts };
        Kind kind = Kind::Default;
        std::string name;            // SystemName
        std::vector<Font> fonts;     // Fonts
        bool operator==(const FontFamily&) const = default;
        static FontFamily Default() { return {}; }
        static FontFamily SansSerif() { return { Kind::SansSerif }; }
        static FontFamily Serif() { return { Kind::Serif }; }
        static FontFamily Monospace() { return { Kind::Monospace }; }
        static FontFamily Cursive() { return { Kind::Cursive }; }
        static FontFamily System(std::string name) { return { Kind::SystemName, std::move(name) }; }
        static FontFamily Of(std::vector<Font> fonts) { return { Kind::Fonts, {}, std::move(fonts) }; }
    };

    // BaselineShift: multiplier of the font's ascent; Superscript 0.5, Subscript -0.5.
    struct BaselineShift {
        float multiplier = 0;
        bool operator==(const BaselineShift&) const = default;
        static constexpr BaselineShift Superscript() { return { 0.5f }; }
        static constexpr BaselineShift Subscript() { return { -0.5f }; }
        static constexpr BaselineShift None() { return { 0 }; }
    };

    struct TextGeometricTransform {
        float scaleX = 1, skewX = 0;
        bool operator==(const TextGeometricTransform&) const = default;
    };

    // TextDecoration: a mask (Underline | LineThrough).
    enum class TextDecoration : std::uint8_t { None = 0, Underline = 1, LineThrough = 2, Both = 3 };
    constexpr TextDecoration operator|(TextDecoration a, TextDecoration b) { return TextDecoration(std::uint8_t(a) | std::uint8_t(b)); }
    constexpr bool Contains(TextDecoration mask, TextDecoration d) { return (std::uint8_t(mask) & std::uint8_t(d)) == std::uint8_t(d); }

    struct Shadow {
        Color color{ 0xFF000000u };
        Offset offset;
        float blurRadius = 0;
        bool operator==(const Shadow& o) const { return color == o.color && offset == o.offset && blurRadius == o.blurRadius; }
    };

    // DrawStyle: fill, or a stroke of width (px).
    struct DrawStyle {
        bool stroke = false;
        float width = 0, miter = 4;
        bool operator==(const DrawStyle&) const = default;
    };

    struct SpanStyle {
        Color color = Color::Unspecified();
        std::optional<gfx::Brush> brush;       // wins over color
        float alpha = NAN;                     // brush alpha (NaN: as is)
        TextUnit fontSize;
        std::optional<FontWeight> fontWeight;
        std::optional<FontStyle> fontStyle;
        std::optional<FontSynthesis> fontSynthesis;
        std::optional<FontFamily> fontFamily;
        std::optional<std::string> fontFeatureSettings;
        TextUnit letterSpacing;
        std::optional<BaselineShift> baselineShift;
        std::optional<TextGeometricTransform> textGeometricTransform;
        std::optional<std::string> localeList;  // BCP-47 tags, comma separated
        Color background = Color::Unspecified();
        std::optional<TextDecoration> textDecoration;
        std::optional<Shadow> shadow;
        std::optional<DrawStyle> drawStyle;

        // merge(other): other's set fields win. A color never replaces an existing brush.
        SpanStyle Merge(const SpanStyle& other) const;
        bool operator==(const SpanStyle& o) const;
        // The fields that change layout (everything but colour, brush, background, decoration,
        // shadow and draw style).
        bool HasSameLayoutAffectingAttributes(const SpanStyle& o) const;
    };

    enum class TextAlign : std::uint8_t { Unspecified = 0, Left = 1, Right = 2, Center = 3, Justify = 4, Start = 5, End = 6 };
    enum class TextDirection : std::uint8_t { Unspecified = 0, Ltr = 1, Rtl = 2, Content = 3, ContentOrLtr = 4, ContentOrRtl = 5 };
    enum class Hyphens : std::uint8_t { Unspecified = 0, None = 1, Auto = 2 };
    enum class TextMotion : std::uint8_t { Static, Animated };

    struct TextIndent {
        TextUnit firstLine = Sp(0), restLine = Sp(0);
        bool operator==(const TextIndent&) const = default;
    };

    // LineBreak: strategy (1 simple, 2 high quality, 3 balanced), strictness (1 default,
    // 2 loose, 3 normal, 4 strict), word break (1 default, 2 phrase); 0 = unspecified.
    struct LineBreak {
        std::uint8_t strategy = 0, strictness = 0, wordBreak = 0;
        bool IsSpecified() const { return strategy || strictness || wordBreak; }
        bool operator==(const LineBreak&) const = default;
        static constexpr LineBreak Simple() { return { 1, 3, 1 }; }      // text fields: little reflow
        static constexpr LineBreak Heading() { return { 3, 2, 2 }; }     // balanced, loose, phrase
        static constexpr LineBreak Paragraph() { return { 2, 4, 1 }; }   // high quality, strict
        static constexpr LineBreak Unspecified() { return {}; }
    };

    // LineHeightStyle: where the extra line height goes (topRatio 0 top .. 1 bottom, -1
    // proportional to ascent/descent), which ends to trim, and how lines size.
    struct LineHeightStyle {
        enum class Trim : std::uint8_t { None = 0, FirstLineTop = 0x1, LastLineBottom = 0x10, Both = 0x11 };
        enum class Mode : std::uint8_t { Fixed = 0, Minimum = 1, Tight = 2, PerLine = 3 };
        float alignment = -1;   // Top 0, Center 0.5, Bottom 1, Proportional -1
        Trim trim = Trim::Both;
        Mode mode = Mode::Fixed;
        bool operator==(const LineHeightStyle&) const = default;
        static constexpr float Top = 0, Center = 0.5f, Bottom = 1, Proportional = -1;
        static constexpr LineHeightStyle Default() { return {}; }
        bool TrimFirstLineTop() const { return (std::uint8_t(trim) & 0x1) != 0; }
        bool TrimLastLineBottom() const { return (std::uint8_t(trim) & 0x10) != 0; }
    };

    struct ParagraphStyle {
        TextAlign textAlign = TextAlign::Unspecified;
        TextDirection textDirection = TextDirection::Unspecified;
        TextUnit lineHeight;
        std::optional<TextIndent> textIndent;
        std::optional<LineHeightStyle> lineHeightStyle;
        LineBreak lineBreak;
        Hyphens hyphens = Hyphens::Unspecified;
        std::optional<TextMotion> textMotion;
        std::optional<bool> includeFontPadding;   // PlatformParagraphStyle (default false)

        ParagraphStyle Merge(const ParagraphStyle& other) const;
        bool operator==(const ParagraphStyle&) const = default;
    };

    struct TextStyle {
        SpanStyle span;
        ParagraphStyle paragraph;

        TextStyle() = default;
        TextStyle(SpanStyle s, ParagraphStyle p) : span(std::move(s)), paragraph(std::move(p)) {}
        // TextStyle(fontSize = .., color = ..): the two most set fields.
        TextStyle(TextUnit fontSize, Color color = Color::Unspecified()) { span.fontSize = fontSize; span.color = color; }
        TextStyle(float fontSizeSp, Color color = Color::Unspecified()) : TextStyle(Sp(fontSizeSp), color) {}

        TextStyle Merge(const TextStyle& other) const { return { span.Merge(other.span), paragraph.Merge(other.paragraph) }; }
        TextStyle Merge(const SpanStyle& other) const { return { span.Merge(other), paragraph }; }
        TextStyle Merge(const ParagraphStyle& other) const { return { span, paragraph.Merge(other) }; }
        bool operator==(const TextStyle&) const = default;
        // Everything that changes layout is equal (only colours, decorations and the like differ).
        bool HasSameLayoutAffectingAttributes(const TextStyle& o) const {
            return paragraph == o.paragraph && span.HasSameLayoutAffectingAttributes(o.span);
        }

        // Shorthands for the common fields.
        TextStyle& WithColor(ui::Color c) { span.color = c; return *this; }
        TextStyle& WithFontSize(TextUnit s) { span.fontSize = s; return *this; }
        TextStyle& WithWeight(FontWeight w) { span.fontWeight = w; return *this; }
        TextStyle& WithLineHeight(TextUnit h) { paragraph.lineHeight = h; return *this; }
        TextStyle& WithAlign(TextAlign a) { paragraph.textAlign = a; return *this; }
        TextStyle& WithLetterSpacing(TextUnit s) { span.letterSpacing = s; return *this; }
    };

    // resolveDefaults: unset fields become Compose's defaults for the layout direction.
    // lerp(start, stop, fraction) (SpanStyle.kt / ParagraphStyle.kt): numbers interpolate (sizes
    // only when both are given), everything else switches at the halfway point.
    TextStyle Lerp(const TextStyle& start, const TextStyle& stop, float fraction);

    TextStyle ResolveDefaults(const TextStyle& style, LayoutDirection direction);
    // resolveTextDirection: Unspecified follows the layout direction; Content becomes
    // ContentOrLtr / ContentOrRtl.
    TextDirection ResolveTextDirection(LayoutDirection layoutDirection, TextDirection textDirection);

}
