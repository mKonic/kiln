#pragma once

// Units and layout primitives (spec 03 §layout, 07 §A).

#include <algorithm>
#include <climits>
#include <cmath>
#include <compare>
#include <cstdint>

namespace kiln::ui {

    // Java's Math.round (Compose's fastRoundToInt): floor(x + 0.5). std::lround differs for
    // negative halves, which appear when content overflows its space.
    inline int RoundJava(float v) { return int(std::floor(v + 0.5f)); }

    struct Dp {
        float value = 0;
        constexpr Dp() = default;
        constexpr explicit Dp(float v) : value(v) {}
        constexpr Dp operator+(Dp o) const { return Dp(value + o.value); }
        constexpr Dp operator-(Dp o) const { return Dp(value - o.value); }
        constexpr Dp operator*(float f) const { return Dp(value * f); }
        constexpr Dp operator/(float f) const { return Dp(value / f); }
        constexpr Dp operator-() const { return Dp(-value); }
        constexpr bool operator==(const Dp&) const = default;
        constexpr std::partial_ordering operator<=>(const Dp& o) const { return value <=> o.value; }
        constexpr bool Specified() const { return !std::isnan(value); }
        static constexpr Dp Unspecified() { return Dp(NAN); }
        // takeOrElse: this, or `fallback` when unspecified.
        constexpr Dp TakeOrElse(Dp fallback) const { return Specified() ? *this : fallback; }
    };
    struct DpSize {
        Dp width, height;
        bool operator==(const DpSize&) const = default;
    };
    struct DpOffset {
        Dp x, y;
        bool operator==(const DpOffset&) const = default;
    };
    struct DpRect {
        Dp left, top, right, bottom;
        Dp Width() const { return right - left; }
        Dp Height() const { return bottom - top; }
        DpSize GetSize() const { return { Width(), Height() }; }
        bool operator==(const DpRect&) const = default;
    };

    constexpr Dp operator""_dp(long double v) { return Dp(float(v)); }
    constexpr Dp operator""_dp(unsigned long long v) { return Dp(float(v)); }

    enum class LayoutDirection : std::uint8_t { Ltr, Rtl };

    struct Density {
        float density = 1;     // px per dp
        float fontScale = 1;
        // Configuration.fontWeightAdjustment (the "bold text" setting): added to font weights.
        int fontWeightAdjustment = 0;
        float ToPx(Dp d) const { return d.value * density; }
        // roundToPx: Compose rounds half away from zero, and a tiny positive dp never becomes 0.
        int RoundToPx(Dp d) const {
            const float px = ToPx(d);
            if (std::isinf(px)) return INT_MAX;   // Constraints.Infinity
            return RoundJava(px);
        }
        Dp ToDp(float px) const { return Dp(px / density); }
        // sp -> px through the platform's nonlinear font scaling (FontScaleConverterFactory:
        // above 1.03 large sizes grow less than small ones), and back.
        float SpToPx(float sp) const;
        float PxToSp(float px) const;
        bool operator==(const Density&) const = default;
    };

    // FontScaleConverter.convertSpToDp / convertDpToSp for a font scale (linear at <= 1.03).
    float FontScaleSpToDp(float fontScale, float sp);
    float FontScaleDpToSp(float fontScale, float dp);

    struct IntSize {
        int width = 0, height = 0;
        bool operator==(const IntSize&) const = default;
    };
    struct IntOffset {
        int x = 0, y = 0;
        IntOffset operator+(IntOffset o) const { return { x + o.x, y + o.y }; }
        IntOffset operator-(IntOffset o) const { return { x - o.x, y - o.y }; }
        bool operator==(const IntOffset&) const = default;
    };
    struct Offset {
        float x = 0, y = 0;
        Offset operator+(Offset o) const { return { x + o.x, y + o.y }; }
        Offset operator-(Offset o) const { return { x - o.x, y - o.y }; }
        bool operator==(const Offset&) const = default;
        static constexpr Offset Unspecified() { return { NAN, NAN }; }
        bool Specified() const { return !std::isnan(x) && !std::isnan(y); }
        Offset TakeOrElse(Offset fallback) const { return Specified() ? *this : fallback; }
    };
    struct Size {
        float width = 0, height = 0;
        float MinDimension() const { return std::min(width, height); }
        bool operator==(const Size&) const = default;
        static constexpr Size Unspecified() { return { NAN, NAN }; }
        bool Specified() const { return !std::isnan(width) && !std::isnan(height); }
        Size TakeOrElse(Size fallback) const { return Specified() ? *this : fallback; }
    };
    struct IntRect {
        int left = 0, top = 0, right = 0, bottom = 0;
        int Width() const { return right - left; }
        int Height() const { return bottom - top; }
        IntSize GetSize() const { return { Width(), Height() }; }
        bool operator==(const IntRect&) const = default;
        bool Empty() const { return right <= left || bottom <= top; }
    };
    struct Rect {
        float left = 0, top = 0, right = 0, bottom = 0;
        float Width() const { return right - left; }
        float Height() const { return bottom - top; }
        bool Contains(Offset p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
        Rect Translate(Offset o) const { return { left + o.x, top + o.y, right + o.x, bottom + o.y }; }
        Size GetSize() const { return { Width(), Height() }; }
        Rect Intersect(const Rect& o) const {
            return { std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom) };
        }
        bool Empty() const { return right <= left || bottom <= top; }
        bool operator==(const Rect&) const = default;
    };

    // Measurement constraints (androidx.compose.ui.unit.Constraints).
    struct Constraints {
        static constexpr int Infinity = INT_MAX;
        int minWidth = 0, maxWidth = Infinity, minHeight = 0, maxHeight = Infinity;

        static Constraints Fixed(int w, int h) { return { w, w, h, h }; }
        static Constraints FixedWidth(int w) { return { w, w, 0, Infinity }; }
        static Constraints FixedHeight(int h) { return { 0, Infinity, h, h }; }

        bool HasBoundedWidth() const { return maxWidth != Infinity; }
        bool HasBoundedHeight() const { return maxHeight != Infinity; }
        bool HasFixedWidth() const { return minWidth == maxWidth; }
        bool HasFixedHeight() const { return minHeight == maxHeight; }
        bool IsZero() const { return maxWidth == 0 || maxHeight == 0; }

        int ConstrainWidth(int w) const { return std::clamp(w, minWidth, maxWidth); }
        int ConstrainHeight(int h) const { return std::clamp(h, minHeight, maxHeight); }
        IntSize Constrain(IntSize s) const { return { ConstrainWidth(s.width), ConstrainHeight(s.height) }; }
        // The intersection with `other`, coerced into this (Constraints.constrain(Constraints)).
        Constraints Constrain(const Constraints& o) const {
            return { std::clamp(o.minWidth, minWidth, maxWidth), std::clamp(o.maxWidth, minWidth, maxWidth),
                     std::clamp(o.minHeight, minHeight, maxHeight), std::clamp(o.maxHeight, minHeight, maxHeight) };
        }
        // Shrinks by horizontal/vertical insets (offset()): min coerced at 0, Infinity kept.
        Constraints Offset(int horizontal, int vertical) const {
            auto add = [](int v, int d) { return v == Infinity ? Infinity : std::max(0, v + d); };
            return { std::max(0, minWidth + horizontal), add(maxWidth, horizontal),
                     std::max(0, minHeight + vertical), add(maxHeight, vertical) };
        }
        Constraints CopyMaxDimensions() const { return { 0, maxWidth, 0, maxHeight }; }
        // isSatisfiedBy: `s` fits within these bounds.
        bool IsSatisfiedBy(IntSize s) const {
            return s.width >= minWidth && s.width <= maxWidth && s.height >= minHeight && s.height <= maxHeight;
        }
        bool operator==(const Constraints&) const = default;
    };

    // Conversions between the integer and float types (toOffset, toSize, roundToIntSize, ...):
    // To* truncates towards zero as Kotlin's toInt does, RoundTo* rounds as roundToInt.
    inline Offset ToOffset(IntOffset o) { return { float(o.x), float(o.y) }; }
    inline IntOffset RoundToIntOffset(Offset o) { return { RoundJava(o.x), RoundJava(o.y) }; }
    inline Size ToSize(IntSize s) { return { float(s.width), float(s.height) }; }
    inline IntSize ToIntSize(Size s) { return { int(s.width), int(s.height) }; }
    inline IntSize RoundToIntSize(Size s) { return { RoundJava(s.width), RoundJava(s.height) }; }
    inline Rect ToRect(Size s) { return { 0, 0, s.width, s.height }; }
    inline Rect ToRect(const IntRect& r) { return { float(r.left), float(r.top), float(r.right), float(r.bottom) }; }
    inline IntRect ToIntRect(IntSize s) { return { 0, 0, s.width, s.height }; }
    inline IntRect RoundToIntRect(const Rect& r) { return { RoundJava(r.left), RoundJava(r.top), RoundJava(r.right), RoundJava(r.bottom) }; }

    // Alignment (androidx.compose.ui.Alignment): bias in [-1, 1], RTL mirrors horizontal bias.
    struct Alignment {
        float horizontal = -1, vertical = -1;
        IntOffset Align(IntSize size, IntSize space, LayoutDirection dir) const {
            const float cx = (space.width - size.width) / 2.0f;
            const float cy = (space.height - size.height) / 2.0f;
            const float h = dir == LayoutDirection::Ltr ? horizontal : -horizontal;
            return { RoundJava(cx * (1 + h)), RoundJava(cy * (1 + vertical)) };
        }
        static constexpr Alignment TopStart() { return { -1, -1 }; }
        static constexpr Alignment TopCenter() { return { 0, -1 }; }
        static constexpr Alignment TopEnd() { return { 1, -1 }; }
        static constexpr Alignment CenterStart() { return { -1, 0 }; }
        static constexpr Alignment Center() { return { 0, 0 }; }
        static constexpr Alignment CenterEnd() { return { 1, 0 }; }
        static constexpr Alignment BottomStart() { return { -1, 1 }; }
        static constexpr Alignment BottomCenter() { return { 0, 1 }; }
        static constexpr Alignment BottomEnd() { return { 1, 1 }; }
        bool operator==(const Alignment&) const = default;
    };
    // One-axis alignments used by Row (vertical) and Column (horizontal).
    struct AlignmentH {
        float bias = -1;
        int Align(int size, int space, LayoutDirection dir) const {
            const float c = (space - size) / 2.0f;
            return RoundJava(c * (1 + (dir == LayoutDirection::Ltr ? bias : -bias)));
        }
        static constexpr AlignmentH Start() { return { -1 }; }
        static constexpr AlignmentH CenterHorizontally() { return { 0 }; }
        static constexpr AlignmentH End() { return { 1 }; }
        bool operator==(const AlignmentH&) const = default;
    };
    struct AlignmentV {
        float bias = -1;
        int Align(int size, int space) const { return RoundJava((space - size) / 2.0f * (1 + bias)); }
        static constexpr AlignmentV Top() { return { -1 }; }
        static constexpr AlignmentV CenterVertically() { return { 0 }; }
        static constexpr AlignmentV Bottom() { return { 1 }; }
        bool operator==(const AlignmentV&) const = default;
    };

    // ARGB colour, 8 bits per channel (sRGB). Wide-gamut colour comes with the renderer work.
    struct Color {
        std::uint32_t argb = 0xFF000000u;
        constexpr Color() = default;
        constexpr explicit Color(std::uint32_t v) : argb(v) {}
        constexpr float Alpha() const { return ((argb >> 24) & 0xff) / 255.0f; }
        Color WithAlpha(float a) const {
            return Color((std::uint32_t(RoundJava(std::clamp(a, 0.0f, 1.0f) * 255)) << 24) | (argb & 0xFFFFFFu));
        }
        static constexpr Color Transparent() { return Color(0); }
        static constexpr Color Unspecified() { return Color(0x00000001u); }   // sentinel
        // Color.Black .. Color.Magenta.
        static constexpr Color Black() { return Color(0xFF000000u); }
        static constexpr Color DarkGray() { return Color(0xFF444444u); }
        static constexpr Color Gray() { return Color(0xFF888888u); }
        static constexpr Color LightGray() { return Color(0xFFCCCCCCu); }
        static constexpr Color White() { return Color(0xFFFFFFFFu); }
        static constexpr Color Red() { return Color(0xFFFF0000u); }
        static constexpr Color Green() { return Color(0xFF00FF00u); }
        static constexpr Color Blue() { return Color(0xFF0000FFu); }
        static constexpr Color Yellow() { return Color(0xFFFFFF00u); }
        static constexpr Color Cyan() { return Color(0xFF00FFFFu); }
        static constexpr Color Magenta() { return Color(0xFFFF00FFu); }
        constexpr bool Specified() const { return argb != 0x00000001u; }
        bool operator==(const Color&) const = default;
    };

    // Color.compositeOver: this colour drawn over `background` (sRGB, straight alpha), packed
    // like Compose's UncheckedColor (each component * 255 + 0.5, truncated).
    inline Color CompositeOver(Color foreground, Color background) {
        auto ch = [](Color c, int shift) { return float((c.argb >> shift) & 0xff) / 255.0f; };
        const float fgA = ch(foreground, 24), bgA = ch(background, 24);
        const float a = fgA + (bgA * (1.0f - fgA));
        auto comp = [&](int shift) {
            const float fgC = ch(foreground, shift), bgC = ch(background, shift);
            return a == 0.0f ? 0.0f : ((fgC * fgA) + ((bgC * bgA) * (1.0f - fgA))) / a;
        };
        auto pack = [](float v) { return std::uint32_t(int(v * 255.0f + 0.5f)); };
        return Color((pack(a) << 24) | (pack(comp(16)) << 16) | (pack(comp(8)) << 8) | pack(comp(0)));
    }

}
