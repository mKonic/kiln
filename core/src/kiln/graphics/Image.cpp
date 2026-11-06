#include "kiln/graphics/Image.h"

#include <atomic>

namespace kiln::gfx {

    namespace {
        ImageDecoder& Decoder() { static ImageDecoder d; return d; }
        std::atomic<std::uint64_t> g_NextId{ 1 };
    }

    void SetImageDecoder(ImageDecoder decoder) { Decoder() = std::move(decoder); }

    ImageBitmap ImageBitmap::FromPixels(int w, int h, std::span<const std::uint8_t> rgba, bool premultiplied) {
        ImageBitmap b;
        if (w <= 0 || h <= 0 || rgba.size() < std::size_t(w) * h * 4) return b;
        auto d = std::make_shared<Data>();
        d->width = w;
        d->height = h;
        d->rgba.assign(rgba.begin(), rgba.begin() + std::ptrdiff_t(std::size_t(w) * h * 4));
        if (!premultiplied)
            for (std::size_t i = 0; i < d->rgba.size(); i += 4) {
                const unsigned a = d->rgba[i + 3];
                for (int c = 0; c < 3; ++c) d->rgba[i + c] = std::uint8_t((d->rgba[i + c] * a + 127) / 255);
            }
        d->id = g_NextId++;
        b.m_Data = std::move(d);
        return b;
    }

    ImageBitmap ImageBitmap::Decode(std::span<const std::uint8_t> encoded) {
        if (!Decoder()) return {};
        auto img = Decoder()(encoded);
        if (!img) return {};
        return FromPixels(img->width, img->height, img->rgba, img->premultiplied);
    }

}
