#include "HostText.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace kiln::hostdev {

    namespace {

        constexpr std::uint32_t kPageSize = 1024;

        struct Page {
            std::vector<std::uint8_t> rgba = std::vector<std::uint8_t>(kPageSize * kPageSize * 4, 0);
            std::uint32_t shelfX = 0, shelfY = 0, shelfH = 0;
            std::uint32_t dirtyY0 = kPageSize, dirtyY1 = 0;
            bool created = false;
        };

        struct Slot { std::uint32_t page; float x, y, w, h; float left, top; float scale; bool color; bool empty; };

        struct Face {
            FT_Face ft = nullptr;
            hb_font_t* hb = nullptr;
            bool bitmapOnly = false;   // CBDT emoji: fixed strikes, scaled at draw time
        };

        struct G { Face* face; unsigned id; float adv; float xoff, yoff; std::uint32_t cluster; };

        class HostTextBackend final : public TextBackend {
        public:
            HostTextBackend(float density, float fontScale) : m_Density(density), m_FontScale(fontScale) {
                FT_Init_FreeType(&m_Lib);
                Open(m_Main, { "/usr/share/fonts/TTF/Roboto-Regular.ttf",
                               "/usr/share/fonts/noto/NotoSans-Regular.ttf" });
                Open(m_Emoji, { "/usr/share/fonts/noto/NotoColorEmoji.ttf" });
            }

            ParagraphData Layout(std::string_view utf8, float sizeSp, float widthPx) override {
                const float px = sizeSp * m_FontScale * m_Density;
                ParagraphData out;
                if (!m_Main.ft) return out;
                SetSize(m_Main, px);
                const float ascent = m_Main.ft->size->metrics.ascender / 64.0f;
                const float lineHeight = m_Main.ft->size->metrics.height / 64.0f;

                // Shape the whole paragraph with the main face, then substitute missing glyphs
                // (glyph 0) cluster by cluster from the emoji face.
                auto glyphs = Shape(m_Main, utf8, px);
                // Greedy wrap at spaces.
                float x = 0, y = ascent;
                int lines = 1;
                size_t lastBreak = 0;
                float lastBreakX = 0;
                std::vector<std::tuple<size_t, float, float>> pos(glyphs.size());
                for (size_t i = 0; i < glyphs.size(); ++i) {
                    const char c = glyphs[i].cluster < utf8.size() ? utf8[glyphs[i].cluster] : 0;
                    if (c == '\n') { x = 0; y += lineHeight; ++lines; pos[i] = std::make_tuple(i, -1e9f, 0.0f); continue; }
                    if (c == ' ') { lastBreak = i; lastBreakX = x; }
                    if (widthPx > 0 && x + glyphs[i].adv > widthPx && lastBreak > 0 && c != ' ') {
                        // Move everything after the last space to the next line.
                        y += lineHeight; ++lines;
                        float shift = lastBreakX + glyphs[lastBreak].adv;
                        for (size_t j = lastBreak + 1; j < i; ++j) std::get<1>(pos[j]) -= shift, std::get<2>(pos[j]) = y;
                        x -= shift;
                        lastBreak = 0;
                    }
                    pos[i] = std::make_tuple(i, x + glyphs[i].xoff, y - glyphs[i].yoff);
                    x += glyphs[i].adv;
                    out.width = std::max(out.width, x);
                }
                out.lineCount = lines;
                out.firstBaseline = ascent;
                out.lastBaseline = ascent + float(lines - 1) * lineHeight;
                // Position data for cursor queries: one stop per glyph cluster plus the end.
                Para para;
                para.text = std::string(utf8);
                for (size_t i = 0; i < glyphs.size(); ++i) {
                    auto [idx, gx, gy] = pos[i];
                    if (gx < -1e8f) continue;
                    para.stops.push_back({ glyphs[i].cluster, gx - glyphs[i].xoff, gy - ascent, gy - ascent + lineHeight, glyphs[i].adv });
                }
                para.lineHeight = lineHeight;
                out.handle = ++m_NextHandle;
                m_Paras[out.handle] = std::move(para);
                out.height = (lines - 1) * lineHeight + m_Main.ft->size->metrics.height / 64.0f;
                for (size_t i = 0; i < glyphs.size(); ++i) {
                    auto [idx, gx, gy] = pos[i];
                    if (gx < -1e8f) continue;
                    const Slot s = Rasterize(*glyphs[i].face, glyphs[i].id, px);
                    if (s.empty) continue;
                    GlyphQuad q;
                    q.page = s.page;
                    q.color = s.color;
                    q.rect = { gx + s.left * s.scale, gy - s.top * s.scale, s.w * s.scale, s.h * s.scale };
                    q.uv = { s.x / kPageSize, s.y / kPageSize, s.w / kPageSize, s.h / kPageSize };
                    out.glyphs.push_back(q);
                }
                return out;
            }

            CursorRect GetCursorRect(std::int64_t handle, int offset16) override {
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return {};
                const Para& p = it->second;
                const std::size_t byte = Utf16ToByte(p.text, offset16);
                for (const Stop& st : p.stops)
                    if (st.cluster >= byte) return { st.x, st.top, st.bottom };
                if (p.stops.empty()) return { 0, 0, p.lineHeight };
                const Stop& last = p.stops.back();
                return { last.x + last.advance, last.top, last.bottom };
            }
            int GetOffsetForPosition(std::int64_t handle, float x, float y) override {
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return 0;
                const Para& p = it->second;
                std::size_t best = p.text.size();
                float bestLineTop = -1;
                for (const Stop& st : p.stops) {
                    if (y < st.top || y >= st.bottom) continue;
                    bestLineTop = st.top;
                    if (x < st.x + st.advance / 2) { best = st.cluster; break; }
                }
                if (bestLineTop < 0 && !p.stops.empty() && y < p.stops.front().top) best = 0;
                return ByteToUtf16(p.text, best);
            }
            void Release(std::int64_t handle) override { m_Paras.erase(handle); }

            void SyncAtlas(AtlasSink& sink) override {
                for (std::uint32_t i = 0; i < m_Pages.size(); ++i) {
                    Page& p = m_Pages[i];
                    if (!p.created) { sink.EnsurePage(i, kPageSize, kPageSize); p.created = true; }
                    if (p.dirtyY1 > p.dirtyY0) {
                        sink.UploadPage(i, p.rgba.data() + std::size_t(p.dirtyY0) * kPageSize * 4, kPageSize * 4,
                                        0, p.dirtyY0, kPageSize, p.dirtyY1 - p.dirtyY0);
                        p.dirtyY0 = kPageSize; p.dirtyY1 = 0;
                    }
                }
            }

        private:
            void Open(Face& f, std::initializer_list<const char*> paths) {
                for (const char* path : paths) {
                    if (FT_New_Face(m_Lib, path, 0, &f.ft) == 0) {
                        f.bitmapOnly = !FT_IS_SCALABLE(f.ft);
                        f.hb = hb_ft_font_create_referenced(f.ft);
                        return;
                    }
                    f.ft = nullptr;
                }
            }

            void SetSize(Face& f, float px) {
                if (f.bitmapOnly) {
                    if (f.ft->num_fixed_sizes > 0) FT_Select_Size(f.ft, 0);
                } else {
                    FT_Set_Pixel_Sizes(f.ft, 0, unsigned(std::lround(px)));
                }
                hb_ft_font_changed(f.hb);
            }

            std::vector<G> Shape(Face& f, std::string_view text, float px) {
                std::vector<G> out;
                hb_buffer_t* buf = hb_buffer_create();
                hb_buffer_add_utf8(buf, text.data(), int(text.size()), 0, int(text.size()));
                hb_buffer_guess_segment_properties(buf);
                hb_shape(f.hb, buf, nullptr, 0);
                unsigned n = 0;
                auto* info = hb_buffer_get_glyph_infos(buf, &n);
                auto* gp = hb_buffer_get_glyph_positions(buf, &n);
                for (unsigned i = 0; i < n; ++i) {
                    if (info[i].codepoint == 0 && m_Emoji.ft) {
                        // Fallback: shape this cluster alone with the emoji face.
                        const std::uint32_t start = info[i].cluster;
                        std::uint32_t end = i + 1 < n ? info[i + 1].cluster : std::uint32_t(text.size());
                        if (end <= start) end = std::uint32_t(text.size());
                        SetSize(m_Emoji, px);
                        const float scale = px / (m_Emoji.ft->size->metrics.y_ppem ? m_Emoji.ft->size->metrics.y_ppem : px);
                        hb_buffer_t* eb = hb_buffer_create();
                        hb_buffer_add_utf8(eb, text.data(), int(text.size()), start, int(end - start));
                        hb_buffer_guess_segment_properties(eb);
                        hb_shape(m_Emoji.hb, eb, nullptr, 0);
                        unsigned en = 0;
                        auto* ei = hb_buffer_get_glyph_infos(eb, &en);
                        auto* ep = hb_buffer_get_glyph_positions(eb, &en);
                        for (unsigned k = 0; k < en; ++k)
                            out.push_back({ &m_Emoji, ei[k].codepoint, ep[k].x_advance / 64.0f * scale, 0, 0, ei[k].cluster });
                        hb_buffer_destroy(eb);
                        while (i + 1 < n && info[i + 1].cluster < end) ++i;
                        continue;
                    }
                    out.push_back({ &f, info[i].codepoint, gp[i].x_advance / 64.0f, gp[i].x_offset / 64.0f,
                                    gp[i].y_offset / 64.0f, info[i].cluster });
                }
                hb_buffer_destroy(buf);
                return out;
            }

            Slot Rasterize(Face& f, unsigned id, float px) {
                const auto key = std::make_tuple(&f, id, f.bitmapOnly ? 0 : long(std::lround(px * 4)));
                if (auto it = m_Slots.find(key); it != m_Slots.end()) return it->second;
                SetSize(f, px);
                Slot s{};
                const float scale = f.bitmapOnly ? px / float(f.ft->size->metrics.y_ppem ? f.ft->size->metrics.y_ppem : 1) : 1.0f;
                if (FT_Load_Glyph(f.ft, id, FT_LOAD_DEFAULT | FT_LOAD_COLOR | FT_LOAD_RENDER) != 0 ||
                    f.ft->glyph->bitmap.width == 0) {
                    s.empty = true;
                    return m_Slots[key] = s;
                }
                const FT_Bitmap& bm = f.ft->glyph->bitmap;
                const std::uint32_t w = bm.width, h = bm.rows;
                // Shelf packing with a 1px gutter.
                if (m_Pages.empty() || m_Pages.back().shelfX + w + 1 > kPageSize) {
                    if (!m_Pages.empty()) { auto& p = m_Pages.back(); p.shelfY += p.shelfH + 1; p.shelfX = 0; p.shelfH = 0; }
                }
                if (m_Pages.empty() || m_Pages.back().shelfY + h + 1 > kPageSize) m_Pages.emplace_back();
                Page& p = m_Pages.back();
                const std::uint32_t ox = p.shelfX, oy = p.shelfY;
                p.shelfX += w + 1;
                p.shelfH = std::max(p.shelfH, h);
                s.color = bm.pixel_mode == FT_PIXEL_MODE_BGRA;
                for (std::uint32_t row = 0; row < h; ++row) {
                    std::uint8_t* dst = &p.rgba[(std::size_t(oy + row) * kPageSize + ox) * 4];
                    const std::uint8_t* src = bm.buffer + std::ptrdiff_t(row) * bm.pitch;
                    for (std::uint32_t col = 0; col < w; ++col, dst += 4) {
                        if (s.color) { dst[0] = src[col * 4 + 2]; dst[1] = src[col * 4 + 1]; dst[2] = src[col * 4]; dst[3] = src[col * 4 + 3]; }
                        else { dst[0] = dst[1] = dst[2] = dst[3] = src[col]; }
                    }
                }
                p.dirtyY0 = std::min(p.dirtyY0, oy);
                p.dirtyY1 = std::max(p.dirtyY1, oy + h);
                s.page = std::uint32_t(m_Pages.size() - 1);
                s.x = float(ox); s.y = float(oy); s.w = float(w); s.h = float(h);
                s.left = float(f.ft->glyph->bitmap_left); s.top = float(f.ft->glyph->bitmap_top);
                s.scale = scale;
                return m_Slots[key] = s;
            }

            struct Stop { std::uint32_t cluster; float x, top, bottom, advance; };
            struct Para { std::string text; std::vector<Stop> stops; float lineHeight = 0; };
            static std::size_t Utf16ToByte(const std::string& s, int u16) {
                std::size_t i = 0;
                int count = 0;
                while (i < s.size() && count < u16) {
                    const unsigned char c = s[i];
                    const int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                    count += len == 4 ? 2 : 1;
                    i += len;
                }
                return i;
            }
            static int ByteToUtf16(const std::string& s, std::size_t byte) {
                std::size_t i = 0;
                int count = 0;
                while (i < s.size() && i < byte) {
                    const unsigned char c = s[i];
                    const int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                    count += len == 4 ? 2 : 1;
                    i += len;
                }
                return count;
            }
            std::map<std::int64_t, Para> m_Paras;
            std::int64_t m_NextHandle = 0;
            FT_Library m_Lib = nullptr;
            Face m_Main, m_Emoji;
            float m_Density, m_FontScale;
            std::vector<Page> m_Pages;
            std::map<std::tuple<Face*, unsigned, long>, Slot> m_Slots;
        };

    }

    std::unique_ptr<TextBackend> CreateHostText(float density, float fontScale) {
        return std::make_unique<HostTextBackend>(density, fontScale);
    }

}
