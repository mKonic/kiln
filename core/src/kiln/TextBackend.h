#pragma once

#include "kiln/QuadRenderer.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace kiln {

    // One positioned glyph of a laid-out paragraph, already rasterised into an atlas page.
    struct GlyphQuad {
        RectF rect;          // relative to the paragraph's top-left, pixels
        RectF uv;            // normalised within the page
        std::uint32_t page = 0;
        bool color = false;  // colour glyph (emoji): draw with its own colours, not the text tint
    };

    struct ParagraphData {
        float width = 0, height = 0;
        int lineCount = 0;
        std::vector<GlyphQuad> glyphs;
        // Backend handle for position queries (0 = none). Released with TextBackend::Release.
        std::int64_t handle = 0;
    };

    // A zero-width caret rectangle (TextLayoutResult.getCursorRect: the caller applies width).
    struct CursorRect { float x = 0, top = 0, bottom = 0; };

    // Receives atlas pixels from a backend. Pages are created on demand by index.
    class AtlasSink {
    public:
        virtual ~AtlasSink() = default;
        virtual void EnsurePage(std::uint32_t index, std::uint32_t width, std::uint32_t height) = 0;
        virtual void UploadPage(std::uint32_t index, const void* rgba, std::uint32_t strideBytes,
                                std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) = 0;
    };

    // Text layout + glyph rasterisation supplied by the platform (architecture 3.5).
    class TextBackend {
    public:
        virtual ~TextBackend() = default;
        virtual ParagraphData Layout(std::string_view utf8, float sizeSp, float widthPx) = 0;
        // Pushes every atlas region rasterised since the last call.
        virtual void SyncAtlas(AtlasSink& sink) = 0;
        // Queries on a laid-out paragraph; offsets are UTF-16 code units (what IMEs use).
        virtual CursorRect GetCursorRect(std::int64_t handle, int offset) = 0;
        virtual int GetOffsetForPosition(std::int64_t handle, float x, float y) = 0;
        virtual void Release(std::int64_t handle) = 0;
    };

}
