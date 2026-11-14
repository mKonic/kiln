// Image decoding through the NDK's AImageDecoder (API 30+): PNG, JPEG, WebP, HEIF, GIF (first
// frame), the same codecs BitmapFactory uses. Output is premultiplied RGBA8.

#include "kiln/graphics/Image.h"

#include <android/imagedecoder.h>

#include <cstring>

namespace kiln::android {

    void InstallImageDecoder() {
        // Coil's StaticImageDecoder: the header's size, then setTargetSize when it differs.
        gfx::SetSizedImageDecoder([](std::span<const std::uint8_t> bytes, const kiln::Function<std::pair<int, int>(int, int)>& size) -> std::optional<gfx::DecodedImage> {
            AImageDecoder* dec = nullptr;
            if (AImageDecoder_createFromBuffer(bytes.data(), bytes.size(), &dec) != ANDROID_IMAGE_DECODER_SUCCESS) return std::nullopt;
            AImageDecoder_setAndroidBitmapFormat(dec, ANDROID_BITMAP_FORMAT_RGBA_8888);
            const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(dec);
            const int sw = AImageDecoderHeaderInfo_getWidth(info), sh = AImageDecoderHeaderInfo_getHeight(info);
            auto [w, h] = size(sw, sh);
            if (w <= 0 || h <= 0) { w = sw; h = sh; }
            if ((w != sw || h != sh) && AImageDecoder_setTargetSize(dec, w, h) != ANDROID_IMAGE_DECODER_SUCCESS) { w = sw; h = sh; }
            const std::size_t stride = AImageDecoder_getMinimumStride(dec);
            std::vector<std::uint8_t> buf(stride * std::size_t(h));
            const int r = AImageDecoder_decodeImage(dec, buf.data(), stride, buf.size());
            AImageDecoder_delete(dec);
            if (r != ANDROID_IMAGE_DECODER_SUCCESS && r != ANDROID_IMAGE_DECODER_INCOMPLETE) return std::nullopt;
            gfx::DecodedImage out;
            out.width = w;
            out.height = h;
            out.sourceWidth = sw;
            out.sourceHeight = sh;
            out.premultiplied = true;
            out.rgba.resize(std::size_t(w) * h * 4);
            for (int y = 0; y < h; ++y) std::memcpy(&out.rgba[std::size_t(y) * w * 4], &buf[std::size_t(y) * stride], std::size_t(w) * 4);
            return out;
        });
        gfx::SetImageDecoder([](std::span<const std::uint8_t> bytes) -> std::optional<gfx::DecodedImage> {
            AImageDecoder* dec = nullptr;
            if (AImageDecoder_createFromBuffer(bytes.data(), bytes.size(), &dec) != ANDROID_IMAGE_DECODER_SUCCESS) return std::nullopt;
            AImageDecoder_setAndroidBitmapFormat(dec, ANDROID_BITMAP_FORMAT_RGBA_8888);
            const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(dec);
            const int w = AImageDecoderHeaderInfo_getWidth(info), h = AImageDecoderHeaderInfo_getHeight(info);
            const std::size_t stride = AImageDecoder_getMinimumStride(dec);
            std::vector<std::uint8_t> buf(stride * std::size_t(h));
            const int r = AImageDecoder_decodeImage(dec, buf.data(), stride, buf.size());
            AImageDecoder_delete(dec);
            if (r != ANDROID_IMAGE_DECODER_SUCCESS) return std::nullopt;
            gfx::DecodedImage out;
            out.width = w;
            out.height = h;
            out.premultiplied = true;   // ANDROID_BITMAP_FLAGS_ALPHA_PREMUL is the default
            out.rgba.resize(std::size_t(w) * h * 4);
            for (int y = 0; y < h; ++y) std::memcpy(&out.rgba[std::size_t(y) * w * 4], &buf[std::size_t(y) * stride], std::size_t(w) * 4);
            return out;
        });
    }

}
