#include "kiln/graphics/Image.h"

#include <algorithm>
#include <cmath>
#include <atomic>

namespace kiln::gfx {

    namespace {
        ImageDecoder& Decoder() { static ImageDecoder d; return d; }
        SizedImageDecoder& SizedDecoder() { static SizedImageDecoder d; return d; }
        std::atomic<std::uint64_t> g_NextId{ 1 };
    }

    void SetImageDecoder(ImageDecoder decoder) { Decoder() = std::move(decoder); }
    void SetSizedImageDecoder(SizedImageDecoder decoder) { SizedDecoder() = std::move(decoder); }

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

    ImageBitmap ImageBitmap::Decode(std::span<const std::uint8_t> encoded, const kiln::Function<std::pair<int, int>(int, int)>& size,
                                    std::pair<int, int>* sourceSize) {
        if (SizedDecoder()) {
            auto img = SizedDecoder()(encoded, size);
            if (!img) return {};
            if (sourceSize) *sourceSize = { img->sourceWidth ? img->sourceWidth : img->width, img->sourceHeight ? img->sourceHeight : img->height };
            return FromPixels(img->width, img->height, img->rgba, img->premultiplied);
        }
        ImageBitmap whole = Decode(encoded);
        if (!whole) return {};
        if (sourceSize) *sourceSize = { whole.Width(), whole.Height() };
        const auto [w, h] = size(whole.Width(), whole.Height());
        if (w <= 0 || h <= 0 || (w == whole.Width() && h == whole.Height())) return whole;
        return whole.Scaled(w, h);
    }

    ImageBitmap ImageBitmap::Scaled(int w, int h) const {
        if (!m_Data || w <= 0 || h <= 0) return {};
        if (w == Width() && h == Height()) return *this;
        const int sw = Width(), sh = Height();
        const std::uint8_t* src = Pixels();
        std::vector<std::uint8_t> out(std::size_t(w) * std::size_t(h) * 4);
        const double fx = double(sw) / w, fy = double(sh) / h;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double acc[4] = { 0, 0, 0, 0 };
                if (fx >= 1 && fy >= 1) {
                    // Area average over the source pixels this one covers (partial at the edges).
                    const double x0 = x * fx, x1 = x0 + fx, y0 = y * fy, y1 = y0 + fy;
                    double total = 0;
                    for (int sy = int(y0); sy < std::min(sh, int(std::ceil(y1))); ++sy) {
                        const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                        for (int sx = int(x0); sx < std::min(sw, int(std::ceil(x1))); ++sx) {
                            const double wgt = wy * (std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                            const std::uint8_t* p = src + (std::size_t(sy) * sw + std::size_t(sx)) * 4;
                            for (int c = 0; c < 4; ++c) acc[c] += p[c] * wgt;
                            total += wgt;
                        }
                    }
                    for (double& a : acc) a = total > 0 ? a / total : 0;
                } else {
                    // Bilinear between the four nearest source pixels (pixel centres).
                    const double sx = std::clamp((x + 0.5) * fx - 0.5, 0.0, double(sw - 1)), sy = std::clamp((y + 0.5) * fy - 0.5, 0.0, double(sh - 1));
                    const int ix = int(sx), iy = int(sy), jx = std::min(ix + 1, sw - 1), jy = std::min(iy + 1, sh - 1);
                    const double tx = sx - ix, ty = sy - iy;
                    auto at = [&](int px, int py, int c) { return double(src[(std::size_t(py) * sw + std::size_t(px)) * 4 + std::size_t(c)]); };
                    for (int c = 0; c < 4; ++c)
                        acc[c] = (at(ix, iy, c) * (1 - tx) + at(jx, iy, c) * tx) * (1 - ty) + (at(ix, jy, c) * (1 - tx) + at(jx, jy, c) * tx) * ty;
                }
                std::uint8_t* d = &out[(std::size_t(y) * w + std::size_t(x)) * 4];
                for (int c = 0; c < 4; ++c) d[c] = std::uint8_t(std::clamp(acc[c] + 0.5, 0.0, 255.0));
            }
        }
        return FromPixels(w, h, out, true);
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
