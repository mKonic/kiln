#include "kiln/graphics/Image.h"

#include <algorithm>
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

    PixelMap ImageBitmap::ToPixelMap(int startX, int startY, int width, int height) const {
        if (width < 0) width = Width() - startX;
        if (height < 0) height = Height() - startY;
        width = std::max(0, width);
        height = std::max(0, height);
        std::vector<ui::Color> out(std::size_t(width) * std::size_t(height), ui::Color::Transparent());
        const std::uint8_t* px = Pixels();
        for (int y = 0; px && y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int sx = startX + x, sy = startY + y;
                if (sx < 0 || sy < 0 || sx >= Width() || sy >= Height()) continue;
                const std::uint8_t* p = px + (std::size_t(sy) * std::size_t(Width()) + std::size_t(sx)) * 4;
                // Stored premultiplied: un-premultiply for the colour.
                const unsigned a = p[3];
                auto un = [a](unsigned c) { return a == 0 ? 0u : std::min(255u, (c * 255u + a / 2) / a); };
                out[std::size_t(y) * std::size_t(width) + std::size_t(x)] = ui::Color((a << 24) | (un(p[0]) << 16) | (un(p[1]) << 8) | un(p[2]));
            }
        }
        return PixelMap(std::move(out), width, height);
    }

}
