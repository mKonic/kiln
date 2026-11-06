// Dev-host image decoding: PNG through libpng, JPEG through libjpeg(-turbo).

#include "kiln/graphics/Image.h"

#include <png.h>
#include <jpeglib.h>

#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace kiln::hostdev {

    namespace {
        std::optional<gfx::DecodedImage> DecodePng(std::span<const std::uint8_t> bytes) {
            png_image img;
            std::memset(&img, 0, sizeof(img));
            img.version = PNG_IMAGE_VERSION;
            if (!png_image_begin_read_from_memory(&img, bytes.data(), bytes.size())) return std::nullopt;
            img.format = PNG_FORMAT_RGBA;
            gfx::DecodedImage out;
            out.width = int(img.width);
            out.height = int(img.height);
            out.rgba.resize(PNG_IMAGE_SIZE(img));
            if (!png_image_finish_read(&img, nullptr, out.rgba.data(), 0, nullptr)) { png_image_free(&img); return std::nullopt; }
            return out;
        }

        struct JpegError { jpeg_error_mgr mgr; std::jmp_buf jump; };

        std::optional<gfx::DecodedImage> DecodeJpeg(std::span<const std::uint8_t> bytes) {
            jpeg_decompress_struct cinfo;
            JpegError err;
            cinfo.err = jpeg_std_error(&err.mgr);
            err.mgr.error_exit = [](j_common_ptr c) { std::longjmp(reinterpret_cast<JpegError*>(c->err)->jump, 1); };
            if (setjmp(err.jump)) { jpeg_destroy_decompress(&cinfo); return std::nullopt; }
            jpeg_create_decompress(&cinfo);
            jpeg_mem_src(&cinfo, bytes.data(), static_cast<unsigned long>(bytes.size()));
            jpeg_read_header(&cinfo, TRUE);
            cinfo.out_color_space = JCS_EXT_RGBA;
            jpeg_start_decompress(&cinfo);
            gfx::DecodedImage out;
            out.width = int(cinfo.output_width);
            out.height = int(cinfo.output_height);
            out.rgba.resize(std::size_t(out.width) * out.height * 4);
            while (cinfo.output_scanline < cinfo.output_height) {
                JSAMPROW row = &out.rgba[std::size_t(cinfo.output_scanline) * out.width * 4];
                jpeg_read_scanlines(&cinfo, &row, 1);
            }
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return out;
        }
    }

    void InstallImageDecoder() {
        gfx::SetImageDecoder([](std::span<const std::uint8_t> b) -> std::optional<gfx::DecodedImage> {
            if (b.size() >= 8 && png_sig_cmp(b.data(), 0, 8) == 0) return DecodePng(b);
            if (b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return DecodeJpeg(b);
            return std::nullopt;
        });
    }

}
