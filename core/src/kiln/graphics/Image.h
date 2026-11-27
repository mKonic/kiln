#pragma once

#include <utility>

// ImageBitmap: immutable premultiplied RGBA8 pixels shared by handle. Decoding goes through a
// platform decoder (AImageDecoder on Android, libpng/libjpeg on the dev host).

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kiln::gfx {

    // Sampling of a scaled image (FilterQuality.kt). Android filters bitmaps bilinearly or not at
    // all: None is nearest-neighbour, the others bilinear (Paint.setNativeFilterQuality).
    enum class FilterQuality : std::uint8_t { None, Low, Medium, High };

    // ImageBitmapConfig: how pixels are stored. kiln keeps every bitmap as 8-bit RGBA.
    enum class ImageBitmapConfig : std::uint8_t { Argb8888, Alpha8, Rgb565, F16, Gpu };

    // PixelMap (toPixelMap): colours read back from a bitmap's region, addressed from its corner.
    class PixelMap {
    public:
        PixelMap(std::vector<ui::Color> buffer, int width, int height, int bufferOffset = 0, int stride = -1)
            : m_Buffer(std::move(buffer)), m_Width(width), m_Height(height), m_Offset(bufferOffset), m_Stride(stride < 0 ? width : stride) {}
        ui::Color operator()(int x, int y) const { return m_Buffer[std::size_t(m_Offset + y * m_Stride + x)]; }
        ui::Color Get(int x, int y) const { return (*this)(x, y); }
        int Width() const { return m_Width; }
        int Height() const { return m_Height; }
        const std::vector<ui::Color>& Buffer() const { return m_Buffer; }
    private:
        std::vector<ui::Color> m_Buffer;
        int m_Width, m_Height, m_Offset, m_Stride;
    };

    class ImageBitmap {
    public:
        ImageBitmap() = default;
        // `rgba` is width*height*4 bytes; straight alpha is premultiplied here.
        static ImageBitmap FromPixels(int width, int height, std::span<const std::uint8_t> rgba, bool premultiplied = false);
        // Encoded PNG/JPEG/WebP/... bytes; empty bitmap when undecodable.
        static ImageBitmap Decode(std::span<const std::uint8_t> encoded);
        // Decoded at the size `size` picks for the source's width and height: through the sized
        // decoder (AImageDecoder's target size) when the host has one, else decoded whole and
        // resampled. `sourceSize`, when given, receives the source's size.
        static ImageBitmap Decode(std::span<const std::uint8_t> encoded, const kiln::Function<std::pair<int, int>(int, int)>& size,
                                  std::pair<int, int>* sourceSize = nullptr);
        // The bitmap resampled to width x height (area average down, bilinear up).
        ImageBitmap Scaled(int width, int height) const;
        // Bytes of pixel memory.
        std::size_t ByteCount() const { return m_Data ? m_Data->rgba.size() : 0; }

        explicit operator bool() const { return m_Data != nullptr; }
        int Width() const { return m_Data ? m_Data->width : 0; }
        int Height() const { return m_Data ? m_Data->height : 0; }
        const std::uint8_t* Pixels() const { return m_Data ? m_Data->rgba.data() : nullptr; }
        ImageBitmapConfig Config() const { return ImageBitmapConfig::Argb8888; }
        bool HasAlpha() const { return true; }
        // toPixelMap / readPixels: the region's colours (straight alpha), row by row.
        PixelMap ToPixelMap(int startX = 0, int startY = 0, int width = -1, int height = -1) const;
        std::uint64_t Id() const { return m_Data ? m_Data->id : 0; }   // unique per pixel buffer
        bool operator==(const ImageBitmap& o) const { return m_Data == o.m_Data; }

        // Lets the renderer notice when the last handle is gone.
        std::weak_ptr<const void> Token() const { return m_Data; }

    private:
        struct Data { int width = 0, height = 0; std::vector<std::uint8_t> rgba; std::uint64_t id = 0; };
        std::shared_ptr<const Data> m_Data;
    };

    // Decoded straight-alpha RGBA, or nullopt.
    struct DecodedImage { int width = 0, height = 0; std::vector<std::uint8_t> rgba; bool premultiplied = false; int sourceWidth = 0, sourceHeight = 0; };
    using ImageDecoder = kiln::Function<std::optional<DecodedImage>(std::span<const std::uint8_t>)>;
    void SetImageDecoder(ImageDecoder decoder);
    // A decoder that reads the header, asks `size` for the output size and decodes at it.
    // DecodedImage holds the output; sourceWidth / sourceHeight the encoded size.
    using SizedImageDecoder = kiln::Function<std::optional<DecodedImage>(std::span<const std::uint8_t>, const kiln::Function<std::pair<int, int>(int, int)>&)>;
    void SetSizedImageDecoder(SizedImageDecoder decoder);

}
