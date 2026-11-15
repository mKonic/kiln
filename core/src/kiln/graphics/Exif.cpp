#include "kiln/graphics/Exif.h"

#include <cstring>

namespace kiln::gfx {

    ExifOrientation OrientationFromTag(int tag) {
        switch (tag) {
            case 2: return { true, 0 };
            case 3: return { false, 180 };
            case 4: return { true, 180 };
            case 5: return { true, 270 };
            case 6: return { false, 90 };
            case 7: return { true, 90 };
            case 8: return { false, 270 };
            default: return {};
        }
    }

    namespace {
        // The orientation tag (0x0112) of IFD0 in a TIFF block (the EXIF payload after "Exif\0\0").
        int TiffOrientation(const std::uint8_t* t, std::size_t n) {
            if (n < 8) return 0;
            const bool little = t[0] == 'I' && t[1] == 'I';
            if (!little && !(t[0] == 'M' && t[1] == 'M')) return 0;
            auto u16 = [&](std::size_t o) -> unsigned { return o + 2 > n ? 0 : little ? t[o] | t[o + 1] << 8 : t[o] << 8 | t[o + 1]; };
            auto u32 = [&](std::size_t o) -> std::uint32_t {
                if (o + 4 > n) return 0;
                return little ? std::uint32_t(t[o]) | std::uint32_t(t[o + 1]) << 8 | std::uint32_t(t[o + 2]) << 16 | std::uint32_t(t[o + 3]) << 24
                              : std::uint32_t(t[o]) << 24 | std::uint32_t(t[o + 1]) << 16 | std::uint32_t(t[o + 2]) << 8 | std::uint32_t(t[o + 3]);
            };
            const std::size_t ifd = u32(4);
            const unsigned count = u16(ifd);
            for (unsigned i = 0; i < count; ++i) {
                const std::size_t e = ifd + 2 + std::size_t(i) * 12;
                if (e + 12 > n) break;
                if (u16(e) == 0x0112) return int(u16(e + 8));   // SHORT, value in the entry
            }
            return 0;
        }
        int ExifPayloadOrientation(const std::uint8_t* p, std::size_t n) {
            // An optional "Exif\0\0" header, then TIFF.
            if (n >= 6 && std::memcmp(p, "Exif\0\0", 6) == 0) return TiffOrientation(p + 6, n - 6);
            return TiffOrientation(p, n);
        }
    }

    ExifOrientation ReadExifOrientation(std::span<const std::uint8_t> b) {
        const std::uint8_t* d = b.data();
        const std::size_t n = b.size();
        if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
            // JPEG: markers until the image data; APP1 holding "Exif".
            std::size_t o = 2;
            while (o + 4 <= n && d[o] == 0xFF) {
                const std::uint8_t marker = d[o + 1];
                if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) { o += 2; continue; }
                if (marker == 0xDA || marker == 0xD9) break;   // start of scan, end of image
                const std::size_t len = std::size_t(d[o + 2]) << 8 | d[o + 3];
                if (len < 2 || o + 2 + len > n) break;
                if (marker == 0xE1 && len >= 8 && std::memcmp(d + o + 4, "Exif\0\0", 6) == 0)
                    return OrientationFromTag(ExifPayloadOrientation(d + o + 4, len - 2));
                o += 2 + len;
            }
            return {};
        }
        if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0) {
            // WebP: RIFF chunks; "EXIF" holds TIFF (with or without the Exif header).
            std::size_t o = 12;
            while (o + 8 <= n) {
                const std::size_t size = std::size_t(d[o + 4]) | std::size_t(d[o + 5]) << 8 | std::size_t(d[o + 6]) << 16 | std::size_t(d[o + 7]) << 24;
                if (o + 8 + size > n) break;
                if (std::memcmp(d + o, "EXIF", 4) == 0) return OrientationFromTag(ExifPayloadOrientation(d + o + 8, size));
                o += 8 + size + (size & 1);
            }
        }
        return {};
    }

    std::vector<std::uint8_t> ApplyExifOrientation(const std::vector<std::uint8_t>& src, int& w, int& h, ExifOrientation o) {
        if (!o.flipped && o.rotationDegrees == 0) return src;
        const int sw = w, sh = h;
        const int dw = o.Swaps() ? sh : sw, dh = o.Swaps() ? sw : sh;
        std::vector<std::uint8_t> out(src.size());
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x) {
                // Flipped (left to right), then turned clockwise.
                const int fx = o.flipped ? sw - 1 - x : x, fy = y;
                int dx = fx, dy = fy;
                switch (o.rotationDegrees) {
                    case 90: dx = sh - 1 - fy; dy = fx; break;
                    case 180: dx = sw - 1 - fx; dy = sh - 1 - fy; break;
                    case 270: dx = fy; dy = sw - 1 - fx; break;
                    default: break;
                }
                std::memcpy(&out[(std::size_t(dy) * dw + std::size_t(dx)) * 4], &src[(std::size_t(y) * sw + std::size_t(x)) * 4], 4);
            }
        w = dw;
        h = dh;
        return out;
    }

}
