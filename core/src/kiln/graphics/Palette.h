#pragma once

// Palette (androidx palette): the prominent colours of an image, as swatches picked for six
// targets (vibrant, light vibrant, dark vibrant, muted, light muted, dark muted), each with
// text colours that read on it.
//
//   gfx::Palette p = gfx::Palette::From(bitmap).Generate();
//   if (auto* s = p.VibrantSwatch()) { header = s->Rgb(); title = s->TitleTextColor(); }
//
// Generate is CPU work over a downscaled copy (112 x 112 by default): run it off the main
// thread for large images. Colours are ARGB ints (0xAARRGGBB), as upstream.

#include "kiln/graphics/Image.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace kiln::gfx {

    // ColorUtils (androidx core): what Palette uses.
    namespace color_utils {
        std::array<float, 3> ColorToHsl(std::uint32_t argb);   // hue 0..360, saturation, lightness 0..1
        std::uint32_t HslToColor(const std::array<float, 3>& hsl);   // opaque
        double CalculateLuminance(std::uint32_t argb);
        // WCAG contrast; the background must be opaque.
        double CalculateContrast(std::uint32_t foreground, std::uint32_t background);
        // The lowest alpha for foreground to reach the ratio over background; -1 when even
        // opaque it does not.
        int CalculateMinimumAlpha(std::uint32_t foreground, std::uint32_t background, float minContrastRatio);
        std::uint32_t CompositeColors(std::uint32_t foreground, std::uint32_t background);
        std::uint32_t SetAlphaComponent(std::uint32_t argb, int alpha);
    }

    // Target: the saturation and lightness a swatch is scored against, with weights.
    class PaletteTarget {
    public:
        static const PaletteTarget& LightVibrant();
        static const PaletteTarget& Vibrant();
        static const PaletteTarget& DarkVibrant();
        static const PaletteTarget& LightMuted();
        static const PaletteTarget& Muted();
        static const PaletteTarget& DarkMuted();

        float minimumSaturation = 0, targetSaturation = 0.5f, maximumSaturation = 1;
        float minimumLightness = 0, targetLightness = 0.5f, maximumLightness = 1;
        float saturationWeight = 0.24f, lightnessWeight = 0.52f, populationWeight = 0.24f;
        // An exclusive target's swatch is not given to later targets.
        bool exclusive = true;
        bool operator==(const PaletteTarget&) const = default;
    };

    class Palette {
    public:
        class Swatch {
        public:
            Swatch(std::uint32_t argb, int population);
            std::uint32_t Rgb() const { return m_Rgb; }
            std::array<float, 3> Hsl() const { return color_utils::ColorToHsl(m_Rgb); }
            int Population() const { return m_Population; }
            // White or black at the lowest alpha that reaches 3:1 (title) / 4.5:1 (body).
            std::uint32_t TitleTextColor() const;
            std::uint32_t BodyTextColor() const;
            bool operator==(const Swatch& o) const { return m_Rgb == o.m_Rgb && m_Population == o.m_Population; }
        private:
            void EnsureTextColors() const;
            std::uint32_t m_Rgb;
            int m_Population;
            mutable bool m_TextColors = false;
            mutable std::uint32_t m_Title = 0, m_Body = 0;
        };

        // Palette.Filter: whether a colour (with its HSL) may be in the palette.
        using Filter = kiln::Function<bool(std::uint32_t argb, const std::array<float, 3>& hsl)>;
        // Not near black or white, nor on the red "I" line (skin tones).
        static Filter DefaultFilter();

        class Builder {
        public:
            Builder& MaximumColorCount(int colors) { m_MaxColors = colors; return *this; }
            // Downscale so the area is at most this (default 112 x 112); <= 0 keeps the size.
            Builder& ResizeBitmapArea(int area) { m_ResizeArea = area; m_ResizeMaxDimension = -1; return *this; }
            Builder& ClearFilters() { m_Filters.clear(); return *this; }
            Builder& AddFilter(Filter f) { if (f) m_Filters.push_back(std::move(f)); return *this; }
            // Only this region of the image (in its pixels); throws when it misses the image.
            Builder& SetRegion(int left, int top, int right, int bottom);
            Builder& ClearRegion() { m_Region.reset(); return *this; }
            Builder& AddTarget(const PaletteTarget& t);
            Builder& ClearTargets() { m_Targets.clear(); return *this; }
            Palette Generate();

        private:
            friend class Palette;
            Builder() = default;
            std::optional<ImageBitmap> m_Bitmap;
            std::vector<std::uint32_t> m_Pixels;   // ARGB, when built from pixels
            int m_Width = 0, m_Height = 0;
            std::optional<std::vector<Swatch>> m_Swatches;
            std::vector<PaletteTarget> m_Targets;
            int m_MaxColors = 16;
            int m_ResizeArea = 112 * 112;
            int m_ResizeMaxDimension = -1;
            std::vector<Filter> m_Filters;
            struct Region { int left, top, right, bottom; };
            std::optional<Region> m_Region;
        };

        // Palette.from(bitmap): the six default targets and the default filter.
        static Builder From(const ImageBitmap& bitmap);
        // From unpremultiplied ARGB pixels, row by row.
        static Builder FromPixels(std::vector<std::uint32_t> argb, int width, int height);
        // Palette.from(swatches): no targets until added; Generate scores them.
        static Builder FromSwatches(std::vector<Swatch> swatches);

        const std::vector<Swatch>& Swatches() const { return m_Swatches; }
        const std::vector<PaletteTarget>& Targets() const { return m_Targets; }
        const Swatch* SwatchFor(const PaletteTarget& target) const;
        std::uint32_t ColorFor(const PaletteTarget& target, std::uint32_t defaultColor) const;
        const Swatch* VibrantSwatch() const { return SwatchFor(PaletteTarget::Vibrant()); }
        const Swatch* LightVibrantSwatch() const { return SwatchFor(PaletteTarget::LightVibrant()); }
        const Swatch* DarkVibrantSwatch() const { return SwatchFor(PaletteTarget::DarkVibrant()); }
        const Swatch* MutedSwatch() const { return SwatchFor(PaletteTarget::Muted()); }
        const Swatch* LightMutedSwatch() const { return SwatchFor(PaletteTarget::LightMuted()); }
        const Swatch* DarkMutedSwatch() const { return SwatchFor(PaletteTarget::DarkMuted()); }
        // The swatch with the most pixels.
        const Swatch* DominantSwatch() const { return m_Dominant < 0 ? nullptr : &m_Swatches[std::size_t(m_Dominant)]; }
        std::uint32_t DominantColor(std::uint32_t defaultColor) const { auto* s = DominantSwatch(); return s ? s->Rgb() : defaultColor; }

        // ColorCutQuantizer: up to maxColors swatches from ARGB pixels (modified median cut over
        // 5-bit RGB), without colours a filter rejects.
        static std::vector<Swatch> Quantize(std::vector<std::uint32_t> pixels, int maxColors, const std::vector<Filter>& filters);

    private:
        Palette(std::vector<Swatch> swatches, std::vector<PaletteTarget> targets);
        void Generate();
        std::vector<Swatch> m_Swatches;
        std::vector<PaletteTarget> m_Targets;
        std::vector<int> m_Selected;   // per target: the swatch index, -1 for none
        int m_Dominant = -1;
    };

}
