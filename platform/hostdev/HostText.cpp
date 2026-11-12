#include "HostText.h"

#include "kiln/text/AnnotatedString.h"

#include <cstdio>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cctype>
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

            // Approximate: each run shaped on its own at its size, greedy wrap at spaces, line
            // height from the tallest run on the line (or the requested line height), no bidi.
            ParagraphData Layout(const ParagraphRequest& req) override {
                ParagraphData out;
                if (!m_Main.ft) return out;
                const std::string& utf8 = req.text;
                struct PG { G g; int run; float px; int placeholder = -1; bool skip = false; };
                std::vector<PG> glyphs;
                auto runFor = [&](std::uint32_t byte) {
                    const int u16 = ByteToUtf16(utf8, byte);
                    for (std::size_t r = 0; r < req.runs.size(); ++r)
                        if (u16 >= req.runs[r].start && u16 < req.runs[r].end) return int(r);
                    return req.runs.empty() ? -1 : int(req.runs.size()) - 1;
                };
                for (std::size_t r = 0; r < req.runs.size(); ++r) {
                    const auto& run = req.runs[r];
                    const std::size_t b0 = Utf16ToByte(utf8, run.start), b1 = Utf16ToByte(utf8, run.end);
                    if (b1 <= b0) continue;
                    SetSize(m_Main, run.sizePx);
                    const float spacing = !std::isnan(run.letterSpacingPx) ? run.letterSpacingPx
                                        : !std::isnan(run.letterSpacingEm) ? run.letterSpacingEm * run.sizePx : 0;
                    for (G g : Shape(m_Main, std::string_view(utf8).substr(0, b1), run.sizePx, b0)) {
                        g.adv = g.adv * run.scaleX + spacing;
                        glyphs.push_back({ g, int(r), run.sizePx });
                    }
                }
                // Placeholders: their first glyph takes the placeholder's width, the rest none.
                for (std::size_t ph = 0; ph < req.placeholders.size(); ++ph) {
                    const auto& p = req.placeholders[ph];
                    bool first = true;
                    for (PG& pg : glyphs) {
                        const int u16 = ByteToUtf16(utf8, pg.g.cluster);
                        if (u16 < p.start || u16 >= p.end) continue;
                        pg.skip = true;
                        pg.g.adv = first ? std::ceil(p.widthEm ? p.width * pg.px : p.width) : 0;
                        if (first) pg.placeholder = int(ph);
                        first = false;
                    }
                }
                out.placeholderRects.assign(req.placeholders.size(), std::nullopt);
                const float basePx = req.base.sizePx;
                auto metrics = [&](float px) {
                    SetSize(m_Main, px);
                    return std::pair{ m_Main.ft->size->metrics.ascender / 64.0f, -m_Main.ft->size->metrics.descender / 64.0f };
                };
                const float width = req.widthPx > 0 ? req.widthPx : INFINITY;
                // Break into lines: [first glyph, end glyph).
                struct L { std::size_t g0, g1; };
                std::vector<L> lines;
                {
                    std::size_t lineStart = 0, lastSpace = std::size_t(-1);
                    float x = req.indentFirstPx;
                    for (std::size_t i = 0; i < glyphs.size(); ++i) {
                        const char c = utf8[glyphs[i].g.cluster];
                        if (c == '\n') { lines.push_back({ lineStart, i + 1 }); lineStart = i + 1; lastSpace = std::size_t(-1); x = req.indentRestPx; continue; }
                        if (c == ' ') lastSpace = i;
                        if (x + glyphs[i].g.adv > width && c != ' ' && lastSpace != std::size_t(-1) && lastSpace >= lineStart) {
                            lines.push_back({ lineStart, lastSpace + 1 });
                            lineStart = lastSpace + 1;
                            lastSpace = std::size_t(-1);
                            x = req.indentRestPx;
                            for (std::size_t j = lineStart; j < i; ++j) x += glyphs[j].g.adv;
                        } else if (x + glyphs[i].g.adv > width && c != ' ' && i > lineStart && glyphs[i].g.cluster != glyphs[i - 1].g.cluster) {
                            // A word wider than the line breaks between characters (StaticLayout).
                            lines.push_back({ lineStart, i });
                            lineStart = i;
                            lastSpace = std::size_t(-1);
                            x = req.indentRestPx;
                        }
                        x += glyphs[i].g.adv;
                    }
                    lines.push_back({ lineStart, glyphs.size() });
                }
                const int textLength = text::Utf16Length(utf8);
                const bool exceeded = int(lines.size()) > req.maxLines;
                using Overflow = ParagraphRequest::Overflow;
                const bool endEllipsis = req.overflow == Overflow::Ellipsis;
                const bool lineEllipsis = req.overflow == Overflow::StartEllipsis || req.overflow == Overflow::MiddleEllipsis;
                // Start and middle ellipsis apply to one line (TextUtils.ellipsize): the text stays whole.
                if (exceeded && lineEllipsis && req.maxLines == 1) lines = { { 0, glyphs.size() } };
                else if (exceeded) lines.resize(std::size_t(req.maxLines));
                out.didExceedMaxLines = exceeded;
                // The ellipsized line: glyphs [cut0, cut1) give way to "…" (drawn at cut0).
                std::size_t cutLine = std::size_t(-1), cut0 = 0, cut1 = 0;
                PG dots{};
                if ((endEllipsis || (lineEllipsis && lines.size() == 1)) && !lines.empty() && !std::isinf(width)) {
                    const std::size_t li = lines.size() - 1;
                    const L& l = lines[li];
                    const float indent = li == 0 ? req.indentFirstPx : req.indentRestPx;
                    float lw = 0;
                    for (std::size_t i = l.g0; i < l.g1; ++i) lw += glyphs[i].g.adv;
                    if ((exceeded && endEllipsis) || lw + indent > width) {
                        const std::size_t at = l.g1 > l.g0 ? l.g1 - 1 : l.g0;
                        dots.run = at < glyphs.size() ? glyphs[at].run : 0;
                        dots.px = at < glyphs.size() ? glyphs[at].px : basePx;
                        const auto shaped = Shape(m_Main, "\xE2\x80\xA6", dots.px);
                        if (!shaped.empty()) dots.g = shaped.front();
                        else dots.skip = true;
                        dots.g.adv = 0;
                        for (const G& g : shaped) dots.g.adv += g.adv;
                        const float budget = width - indent - dots.g.adv;
                        float w = lw;
                        cut0 = l.g0; cut1 = l.g1;
                        if (req.overflow == Overflow::StartEllipsis) {
                            while (cut1 > cut0 && w > budget) w -= glyphs[cut0++].g.adv;
                            cut1 = cut0;   // removed: [l.g0, cut1)
                            cut0 = l.g0;
                        } else if (req.overflow == Overflow::MiddleEllipsis) {
                            std::size_t mid = (l.g0 + l.g1) / 2;
                            cut0 = cut1 = mid;
                            bool right = true;
                            while (w > budget && (cut0 > l.g0 || cut1 < l.g1)) {
                                if ((right && cut1 < l.g1) || cut0 == l.g0) w -= glyphs[cut1++].g.adv;
                                else w -= glyphs[--cut0].g.adv;
                                right = !right;
                            }
                        } else {
                            cut0 = l.g1;
                            while (cut0 > l.g0 && w > budget) w -= glyphs[--cut0].g.adv;
                            // Keep a trailing space out of the visible text.
                            while (cut0 > l.g0 && utf8[glyphs[cut0 - 1].g.cluster] == ' ') --cut0;
                        }
                        cutLine = li;
                    }
                }
                Para para;
                para.text = utf8;
                float y = 0;
                for (std::size_t li = 0; li < lines.size(); ++li) {
                    const L& l = lines[li];
                    auto [asc, desc] = metrics(basePx);
                    for (std::size_t i = l.g0; i < l.g1; ++i) {
                        auto [a, d] = metrics(glyphs[i].px);
                        asc = std::max(asc, a);
                        desc = std::max(desc, d);
                    }
                    if (!std::isnan(req.lineHeightPx)) {
                        const float extra = req.lineHeightPx - (asc + desc);
                        asc += extra / 2;
                        desc += extra / 2;
                    }
                    LineData ld;
                    ld.top = y;
                    ld.baseline = y + asc;
                    ld.bottom = y + asc + desc;
                    ld.ascent = -asc;
                    ld.descent = desc;
                    ld.start = l.g0 < glyphs.size() ? ByteToUtf16(utf8, glyphs[l.g0].g.cluster) : textLength;
                    ld.end = l.g1 < glyphs.size() ? ByteToUtf16(utf8, glyphs[l.g1].g.cluster) : textLength;
                    if (li == 0) ld.start = 0;
                    ld.visibleEnd = ld.end;
                    const bool cut = li == cutLine;
                    auto u16At = [&](std::size_t g) { return g < glyphs.size() ? ByteToUtf16(utf8, glyphs[g].g.cluster) : textLength; };
                    if (cut) {
                        ld.ellipsisStart = u16At(cut0) - ld.start;
                        ld.ellipsisCount = (endEllipsis ? textLength : u16At(cut1)) - u16At(cut0);
                        if (ld.ellipsisCount == 0) ld.ellipsisCount = 1;   // the ellipsis shows: the line is ellipsized
                    }
                    float lineWidth = 0;
                    for (std::size_t i = l.g0; i < l.g1; ++i)
                        if (!cut || i < cut0 || i >= cut1) lineWidth += glyphs[i].g.adv;
                    if (cut) lineWidth += dots.g.adv;
                    const float indent = li == 0 ? req.indentFirstPx : req.indentRestPx;
                    float x = indent;
                    const float avail = std::isinf(width) ? lineWidth + indent : width;
                    if (req.textAlign == 3) x = (avail - lineWidth) / 2;
                    else if (req.textAlign == 2 || req.textAlign == 6) x = avail - lineWidth;
                    ld.left = x;
                    ld.right = x + lineWidth;
                    out.width = std::max(out.width, lineWidth + indent);
                    int segRun = -1;
                    for (std::size_t i = l.g0; i <= l.g1; ++i) {
                        const bool dotsHere = cut && i == cut0;
                        if (!dotsHere && (i == l.g1 || (cut && i > cut0 && i < cut1))) continue;
                        const PG& pg = dotsHere ? dots : glyphs[i];
                        const auto& run = req.runs[std::size_t(pg.run)];
                        const float shift = run.baselineShift != 0 ? -metrics(pg.px).first * run.baselineShift : 0;
                        if (pg.run != segRun) { out.segments.push_back({ pg.run, int(li), x, x, shift }); segRun = pg.run; }
                        out.segments.back().right = x + pg.g.adv;
                        if (!dotsHere) para.stops.push_back({ pg.g.cluster, x, ld.top, ld.bottom, pg.g.adv });
                        if (pg.placeholder >= 0) {
                            const auto& p = req.placeholders[std::size_t(pg.placeholder)];
                            const float h = std::ceil(p.heightEm ? p.height * pg.px : p.height);
                            out.placeholderRects[std::size_t(pg.placeholder)] = RectF{ x, ld.baseline - h, pg.g.adv, h };
                        }
                        if ((dotsHere || utf8[pg.g.cluster] != '\n') && !pg.skip) {
                            const Slot sl = Rasterize(*pg.g.face, pg.g.id, pg.px);
                            if (!sl.empty) {
                                GlyphQuad q;
                                q.page = sl.page;
                                q.color = sl.color;
                                q.run = pg.run;
                                const float gx = x + pg.g.xoff, gy = ld.baseline - pg.g.yoff + shift;
                                q.rect = { gx + sl.left * sl.scale, gy - sl.top * sl.scale, sl.w * sl.scale, sl.h * sl.scale };
                                q.uv = { sl.x / kPageSize, sl.y / kPageSize, sl.w / kPageSize, sl.h / kPageSize };
                                out.glyphs.push_back(q);
                            }
                        }
                        x += pg.g.adv;
                    }
                    out.lines.push_back(ld);
                    y = ld.bottom;
                }
                out.lineCount = int(out.lines.size());
                out.height = y;
                out.firstBaseline = out.lines.empty() ? 0 : out.lines.front().baseline;
                out.lastBaseline = out.lines.empty() ? 0 : out.lines.back().baseline;
                for (const auto& run : req.runs) {
                    SetSize(m_Main, run.sizePx);
                    const float scale = run.sizePx / float(m_Main.ft->units_per_EM ? m_Main.ft->units_per_EM : 1);
                    const float thick = std::max(1.0f, m_Main.ft->underline_thickness * scale);
                    out.runMetrics.push_back({ -m_Main.ft->underline_position * scale, thick, -metrics(run.sizePx).first * 0.3f, thick });
                }
                para.lineHeight = out.lines.empty() ? basePx : out.lines.front().bottom - out.lines.front().top;
                para.lines = out.lines;
                out.handle = ++m_NextHandle;
                m_Paras[out.handle] = std::move(para);
                return out;
            }

            ParagraphIntrinsicWidths Intrinsics(const ParagraphRequest& req) override {
                ParagraphRequest unbounded = req;
                unbounded.widthPx = 0;
                unbounded.maxLines = 0x7fffffff;
                ParagraphData d = Layout(unbounded);
                Release(d.handle);
                // Widest word: lay out each space-separated word.
                float minWidth = 0;
                std::size_t i = 0;
                while (i < req.text.size()) {
                    std::size_t j = req.text.find_first_of(" \n", i);
                    if (j == std::string::npos) j = req.text.size();
                    if (j > i) {
                        ParagraphRequest w = unbounded;
                        const int s16 = ByteToUtf16(req.text, i), e16 = ByteToUtf16(req.text, j);
                        w.text = req.text.substr(i, j - i);
                        w.runs.clear();
                        for (auto run : req.runs) {
                            run.start = std::clamp(run.start, s16, e16) - s16;
                            run.end = std::clamp(run.end, s16, e16) - s16;
                            if (run.end > run.start) w.runs.push_back(run);
                        }
                        ParagraphData wd = Layout(w);
                        Release(wd.handle);
                        minWidth = std::max(minWidth, wd.width);
                    }
                    i = j + 1;
                }
                return { minWidth, d.width };
            }

            float GetHorizontalPosition(std::int64_t handle, int offset16, bool) override { return GetCursorRect(handle, offset16).x; }

            CursorRect GetCursorRect(std::int64_t handle, int offset16) override {
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return {};
                const Para& p = it->second;
                const std::size_t byte = Utf16ToByte(p.text, offset16);
                for (const Stop& st : p.stops)
                    if (st.cluster >= byte) return { st.x, st.top, st.bottom };
                if (p.stops.empty()) return { 0, 0, p.lineHeight };
                const Stop& last = p.stops.back();
                if (!p.text.empty() && p.text.back() == '\n' && !p.lines.empty())
                    return { 0, p.lines.back().bottom, p.lines.back().bottom + p.lineHeight };
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
            RectF GetBoundingBox(std::int64_t handle, int offset16) override {
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return {};
                const std::size_t byte = Utf16ToByte(it->second.text, offset16);
                for (const Stop& st : it->second.stops)
                    if (st.cluster >= byte) return { st.x, st.top, st.advance, st.bottom - st.top };
                const CursorRect c = GetCursorRect(handle, offset16);
                return { c.x, c.top, 0, c.bottom - c.top };
            }
            std::vector<RectF> GetSelectionRects(std::int64_t handle, int start, int end) override {
                std::vector<RectF> out;
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return out;
                const Para& p = it->second;
                const std::size_t b0 = Utf16ToByte(p.text, start), b1 = Utf16ToByte(p.text, end);
                for (const Stop& st : p.stops) {
                    if (st.cluster < b0 || st.cluster >= b1) continue;
                    if (!out.empty() && out.back().y == st.top && std::abs(out.back().x + out.back().w - st.x) < 0.5f) out.back().w += st.advance;
                    else out.push_back({ st.x, st.top, st.advance, st.bottom - st.top });
                }
                return out;
            }
            std::pair<int, int> GetWordBoundary(std::int64_t handle, int offset16) override {
                auto it = m_Paras.find(handle);
                if (it == m_Paras.end()) return { offset16, offset16 };
                const std::string& t = it->second.text;
                auto word = [&](std::size_t i) { return i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || (static_cast<unsigned char>(t[i]) & 0x80)); };
                std::size_t at = Utf16ToByte(t, offset16);
                if (!word(at) && at > 0 && word(at - 1)) --at;
                std::size_t s = at, e = at;
                while (s > 0 && word(s - 1)) --s;
                while (word(e)) ++e;
                return { ByteToUtf16(t, s), std::max(ByteToUtf16(t, e), offset16) };
            }
            bool IsRtlCharAt(std::int64_t, int) override { return false; }
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

            // Shapes text[from..]; clusters are byte offsets into text.
            std::vector<G> Shape(Face& f, std::string_view text, float px, std::size_t from = 0) {
                std::vector<G> out;
                hb_buffer_t* buf = hb_buffer_create();
                hb_buffer_add_utf8(buf, text.data(), int(text.size()), unsigned(from), int(text.size() - from));
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
            struct Para { std::string text; std::vector<Stop> stops; float lineHeight = 0; std::vector<LineData> lines; };
            static std::size_t Utf16ToByte(std::string_view s, int u16) {
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
            static int ByteToUtf16(std::string_view s, std::size_t byte) {
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
