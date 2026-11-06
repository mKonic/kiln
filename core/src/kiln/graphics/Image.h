#pragma once

// ImageBitmap: immutable premultiplied RGBA8 pixels shared by handle. Decoding goes through a
// platform decoder (AImageDecoder on Android, libpng/libjpeg on the dev host).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kiln::gfx {

    class ImageBitmap {
    public:
        ImageBitmap() = default;
        // `rgba` is width*height*4 bytes; straight alpha is premultiplied here.
        static ImageBitmap FromPixels(int width, int height, std::span<const std::uint8_t> rgba, bool premultiplied = false);
        // Encoded PNG/JPEG/WebP/... bytes; empty bitmap when undecodable.
        static ImageBitmap Decode(std::span<const std::uint8_t> encoded);

        explicit operator bool() const { return m_Data != nullptr; }
        int Width() const { return m_Data ? m_Data->width : 0; }
        int Height() const { return m_Data ? m_Data->height : 0; }
        const std::uint8_t* Pixels() const { return m_Data ? m_Data->rgba.data() : nullptr; }
        std::uint64_t Id() const { return m_Data ? m_Data->id : 0; }   // unique per pixel buffer
        bool operator==(const ImageBitmap& o) const { return m_Data == o.m_Data; }

        // Lets the renderer notice when the last handle is gone.
        std::weak_ptr<const void> Token() const { return m_Data; }

    private:
        struct Data { int width = 0, height = 0; std::vector<std::uint8_t> rgba; std::uint64_t id = 0; };
        std::shared_ptr<const Data> m_Data;
    };

    // Decoded straight-alpha RGBA, or nullopt.
    struct DecodedImage { int width = 0, height = 0; std::vector<std::uint8_t> rgba; bool premultiplied = false; };
    using ImageDecoder = std::function<std::optional<DecodedImage>(std::span<const std::uint8_t>)>;
    void SetImageDecoder(ImageDecoder decoder);

}
