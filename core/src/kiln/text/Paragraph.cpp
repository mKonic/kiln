#include "kiln/text/Paragraph.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kiln {

    namespace {
        bool SameFloat(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
    }

    bool TextRunRequest::operator==(const TextRunRequest& o) const {
        return start == o.start && end == o.end && sizePx == o.sizePx && font == o.font &&
               fontFeatureSettings == o.fontFeatureSettings && SameFloat(letterSpacingPx, o.letterSpacingPx) &&
               SameFloat(letterSpacingEm, o.letterSpacingEm) && baselineShift == o.baselineShift && scaleX == o.scaleX &&
               skewX == o.skewX && localeList == o.localeList && underline == o.underline && lineThrough == o.lineThrough;
    }

    bool ParagraphRequest::operator==(const ParagraphRequest& o) const {
        return text == o.text && base == o.base && runs == o.runs && placeholders == o.placeholders && widthPx == o.widthPx && maxHeightPx == o.maxHeightPx &&
               maxLines == o.maxLines && overflow == o.overflow && textAlign == o.textAlign && textDirection == o.textDirection &&
               SameFloat(lineHeightPx, o.lineHeightPx) && lineHeightAlignment == o.lineHeightAlignment &&
               lineHeightTrim == o.lineHeightTrim && lineHeightMode == o.lineHeightMode && hasLineHeightStyle == o.hasLineHeightStyle &&
               indentFirstPx == o.indentFirstPx && indentRestPx == o.indentRestPx && breakStrategy == o.breakStrategy &&
               breakStrictness == o.breakStrictness && wordBreak == o.wordBreak && hyphens == o.hyphens &&
               includeFontPadding == o.includeFontPadding;
    }

    ParagraphData TextBackend::LayoutPlain(std::string_view utf8, float sizePx, float widthPx) {
        ParagraphRequest r;
        r.text = std::string(utf8);
        r.base.sizePx = sizePx;
        r.base.letterSpacingPx = 0;
        TextRunRequest run = r.base;
        run.end = text::Utf16Length(utf8);
        r.runs.push_back(run);
        r.widthPx = widthPx;
        return Layout(r);
    }

    int TextBackend::SurrogateCharacterBreak(std::u16string_view t, int offset, bool following) {
        const int n = int(t.size());
        auto high = [&](int i) { return i >= 0 && i < n && t[std::size_t(i)] >= 0xD800 && t[std::size_t(i)] <= 0xDBFF; };
        auto low = [&](int i) { return i >= 0 && i < n && t[std::size_t(i)] >= 0xDC00 && t[std::size_t(i)] <= 0xDFFF; };
        if (following) {
            if (offset >= n) return -1;
            const int i = std::max(offset, 0);
            return high(i) && low(i + 1) ? i + 2 : i + 1;
        }
        if (offset <= 0) return -1;
        const int i = std::min(offset, n);
        return low(i - 1) && high(i - 2) ? i - 2 : i - 1;
    }

}

namespace kiln::text {

    // --- FontMatcher -------------------------------------------------------------------------

    namespace {
        std::vector<Font> FilterByClosestWeight(const std::vector<Font>& fonts, FontWeight weight, bool preferBelow,
                                                std::optional<FontWeight> minRange = std::nullopt, std::optional<FontWeight> maxRange = std::nullopt) {
            std::optional<FontWeight> above, below;
            for (const Font& f : fonts) {
                const FontWeight w = f.weight;
                if (minRange && w < *minRange) continue;
                if (maxRange && w > *maxRange) continue;
                if (w < weight) {
                    if (!below || w > *below) below = w;
                } else if (w > weight) {
                    if (!above || w < *above) above = w;
                } else {
                    above = below = w;
                    break;
                }
            }
            const std::optional<FontWeight> best = preferBelow ? (below ? below : above) : (above ? above : below);
            std::vector<Font> out;
            for (const Font& f : fonts) if (best && f.weight == *best) out.push_back(f);
            return out;
        }
    }

    std::vector<Font> MatchFont(const std::vector<Font>& fonts, FontWeight weight, FontStyle style) {
        std::vector<Font> exact;
        for (const Font& f : fonts) if (f.weight == weight && f.style == style) exact.push_back(f);
        if (!exact.empty()) return exact;
        std::vector<Font> search;
        for (const Font& f : fonts) if (f.style == style) search.push_back(f);
        if (search.empty()) search = fonts;
        if (weight < FontWeight::W400()) return FilterByClosestWeight(search, weight, true);
        if (weight > FontWeight::W500()) return FilterByClosestWeight(search, weight, false);
        auto r = FilterByClosestWeight(search, weight, false, std::nullopt, FontWeight::W500());
        if (r.empty()) r = FilterByClosestWeight(search, weight, false, FontWeight::W500(), std::nullopt);
        return r;
    }

    // --- span flattening ---------------------------------------------------------------------

    std::vector<Range<SpanStyle>> FlattenSpanStyles(const AnnotatedString& text, const SpanStyle& base) {
        const auto spans = text.SpanStyles();
        std::set<int> cuts{ 0, text.Length() };
        for (const auto& s : spans) {
            if (s.start >= s.end) continue;
            cuts.insert(s.start);
            cuts.insert(s.end);
        }
        std::vector<Range<SpanStyle>> out;
        if (text.Length() == 0) {
            out.push_back({ base, 0, 0, {} });
            return out;
        }
        for (auto it = cuts.begin(); std::next(it) != cuts.end(); ++it) {
            const int a = *it, b = *std::next(it);
            SpanStyle merged = base;
            for (const auto& s : spans)
                if (s.start < s.end && s.start <= a && s.end >= b) merged = merged.Merge(s.item);
            if (!out.empty() && out.back().item == merged) out.back().end = b;
            else out.push_back({ std::move(merged), a, b, {} });
        }
        return out;
    }

    // --- request -----------------------------------------------------------------------------

    float ResolveLineHeightPx(TextUnit lineHeight, float contextFontSizePx, const ui::Density& density) {
        if (lineHeight.IsSp()) {
            if (!(density.fontScale > 1.05f)) return density.SpToPx(lineHeight.value);
            // Keep the intended multiplier: nonlinear scaling would squash a tall line height.
            const float fontSizeSp = density.PxToSp(contextFontSizePx);
            return lineHeight.value / fontSizeSp * contextFontSizePx;
        }
        if (lineHeight.IsEm()) return lineHeight.value * contextFontSizePx;
        return NAN;
    }

    namespace {
        // TextUnit to px for lengths relative to the font size (em) or absolute (sp).
        float UnitPx(TextUnit u, float fontPx, const ui::Density& d) {
            if (u.IsSp()) return d.SpToPx(u.value);
            if (u.IsEm()) return u.value * fontPx;
            return 0;
        }

        TextFontRequest ResolveFont(const SpanStyle& s, int weightAdjustment) {
            TextFontRequest f;
            // AndroidFontResolveInterceptor: the bold-text setting shifts every weight.
            FontWeight weight = s.fontWeight.value_or(FontWeight::Normal());
            if (weightAdjustment != 0) weight.weight = std::clamp(weight.weight + weightAdjustment, 1, 1000);
            const FontStyle style = s.fontStyle.value_or(FontStyle::Normal);
            const FontSynthesis synthesis = s.fontSynthesis.value_or(FontSynthesis::All);
            f.weight = weight.weight;
            f.italic = style == FontStyle::Italic;
            const FontFamily family = s.fontFamily.value_or(FontFamily::Default());
            switch (family.kind) {
                case FontFamily::Kind::Default: f.kind = TextFontRequest::Kind::Default; break;
                case FontFamily::Kind::SansSerif: f.kind = TextFontRequest::Kind::SansSerif; break;
                case FontFamily::Kind::Serif: f.kind = TextFontRequest::Kind::Serif; break;
                case FontFamily::Kind::Monospace: f.kind = TextFontRequest::Kind::Monospace; break;
                case FontFamily::Kind::Cursive: f.kind = TextFontRequest::Kind::Cursive; break;
                case FontFamily::Kind::SystemName:
                    f.kind = TextFontRequest::Kind::SystemName;
                    f.name = family.name;
                    break;
                case FontFamily::Kind::Fonts: {
                    const auto matched = MatchFont(family.fonts, weight, style);
                    if (matched.empty()) break;   // default family
                    const Font& font = matched.front();
                    if (font.source == Font::Source::Downloadable) {
                        f.kind = TextFontRequest::Kind::Downloadable;
                        f.name = font.provider.authority + "\n" + font.provider.package + "\n" + font.path + "\n";
                        for (std::size_t i = 0; i < font.provider.certificates.size(); ++i)
                            f.name += (i ? "," : "") + font.provider.certificates[i];
                    } else {
                        f.kind = font.source == Font::Source::Asset ? TextFontRequest::Kind::Asset : TextFontRequest::Kind::File;
                        f.name = font.path;
                    }
                    // synthesizeTypeface: fake bold only from below 600 to 600+, fake slant when
                    // the slant differs; otherwise the font's own weight and slant.
                    const bool weightOn = synthesis == FontSynthesis::All || synthesis == FontSynthesis::Weight;
                    const bool styleOn = synthesis == FontSynthesis::All || synthesis == FontSynthesis::Style;
                    const bool synthWeight = weightOn && font.weight != weight && weight >= FontWeight::W600() && font.weight < FontWeight::W600();
                    const bool synthStyle = styleOn && style != font.style;
                    f.weight = synthWeight ? weight.weight : font.weight.weight;
                    f.italic = synthStyle ? style == FontStyle::Italic : font.style == FontStyle::Italic;
                    f.fakeBold = f.weight >= 600 && f.weight - font.weight.weight >= 200;
                    f.fakeItalic = f.italic && font.style != FontStyle::Italic;
                    // FontVariation.Settings(weight, style) is a font's default.
                    f.variationSettings = !font.variationSettings.empty()
                        ? font.variationSettings
                        : "'wght' " + std::to_string(font.weight.weight) + ", 'ital' " + (font.style == FontStyle::Italic ? "1" : "0");
                    break;
                }
            }
            return f;
        }

        TextRunRequest ResolveRun(const SpanStyle& s, const SpanStyle& base, float basePx, const ui::Density& d) {
            TextRunRequest r;
            // The paragraph size is exact; a span's size rounds to whole pixels (AbsoluteSizeSpan).
            if (s.fontSize == base.fontSize) r.sizePx = basePx;
            else if (s.fontSize.IsSp()) r.sizePx = float(ui::RoundJava(d.SpToPx(s.fontSize.value)));
            else if (s.fontSize.IsEm()) r.sizePx = basePx * s.fontSize.value;
            else r.sizePx = basePx;
            r.font = ResolveFont(s, d.fontWeightAdjustment);
            r.fontFeatureSettings = s.fontFeatureSettings.value_or("");
            if (s.letterSpacing.IsSp()) r.letterSpacingPx = d.SpToPx(s.letterSpacing.value);
            else if (s.letterSpacing.IsEm()) r.letterSpacingEm = s.letterSpacing.value;
            if (s.baselineShift) r.baselineShift = s.baselineShift->multiplier;
            if (s.textGeometricTransform) {
                r.scaleX = s.textGeometricTransform->scaleX;
                r.skewX = s.textGeometricTransform->skewX;
            }
            r.localeList = s.localeList.value_or("");
            if (s.textDecoration) {
                r.underline = Contains(*s.textDecoration, TextDecoration::Underline);
                r.lineThrough = Contains(*s.textDecoration, TextDecoration::LineThrough);
            }
            return r;
        }

        float BasePx(TextUnit size, const ui::Density& d) {
            if (size.IsSp()) return d.SpToPx(size.value);
            if (size.IsEm()) return d.SpToPx(14) * size.value;
            return d.SpToPx(14);
        }
    }

    ParagraphRequest BuildParagraphRequest(const AnnotatedString& text, const TextStyle& style, const ui::Density& density,
                                           const ParagraphConstraints& c, std::vector<SpanStyle>* runStyles,
                                           std::span<const Range<Placeholder>> placeholders) {
        ParagraphRequest r;
        r.text = text.Text();
        const float basePx = BasePx(style.span.fontSize, density);
        r.base = ResolveRun(style.span, style.span, basePx, density);
        r.base.start = 0;
        r.base.end = text.Length();
        // Run styles keep only what spans set, so drawing can take the rest from the current
        // style (a colour change needs no new layout); the request gets them merged.
        for (auto& run : FlattenSpanStyles(text, SpanStyle{})) {
            TextRunRequest rr = ResolveRun(style.span.Merge(run.item), style.span, basePx, density);
            rr.start = run.start;
            rr.end = run.end;
            r.runs.push_back(std::move(rr));
            if (runStyles) runStyles->push_back(std::move(run.item));
        }
        for (const auto& ph : placeholders) {
            PlaceholderRequest pr;
            pr.start = ph.start;
            pr.end = ph.end;
            pr.widthEm = ph.item.width.IsEm();
            pr.heightEm = ph.item.height.IsEm();
            pr.width = pr.widthEm ? ph.item.width.value : density.SpToPx(ph.item.width.value);
            pr.height = pr.heightEm ? ph.item.height.value : density.SpToPx(ph.item.height.value);
            pr.verticalAlign = std::uint8_t(int(ph.item.verticalAlign) - 1);
            r.placeholders.push_back(pr);
        }
        r.widthPx = std::isinf(c.width) ? 0 : c.width;
        r.maxHeightPx = c.maxHeight;
        r.maxLines = std::max(1, c.maxLines);
        r.overflow = ParagraphRequest::Overflow(c.overflow);
        const ParagraphStyle& p = style.paragraph;
        r.textAlign = std::uint8_t(p.textAlign == TextAlign::Unspecified ? TextAlign::Start : p.textAlign);
        r.textDirection = std::uint8_t(p.textDirection);
        r.lineHeightPx = ResolveLineHeightPx(p.lineHeight, basePx, density);
        const LineHeightStyle lhs = p.lineHeightStyle.value_or(LineHeightStyle::Default());
        r.hasLineHeightStyle = p.lineHeightStyle.has_value();
        r.lineHeightAlignment = lhs.alignment;
        r.lineHeightTrim = std::uint8_t(lhs.trim);
        r.lineHeightMode = std::uint8_t(lhs.mode);
        if (p.textIndent) {
            r.indentFirstPx = UnitPx(p.textIndent->firstLine, basePx, density);
            r.indentRestPx = UnitPx(p.textIndent->restLine, basePx, density);
        }
        const LineBreak lb = p.lineBreak.IsSpecified() ? p.lineBreak : LineBreak::Simple();
        r.breakStrategy = lb.strategy;
        r.breakStrictness = lb.strictness;
        r.wordBreak = lb.wordBreak;
        r.hyphens = std::uint8_t(p.hyphens == Hyphens::Unspecified ? Hyphens::None : p.hyphens);
        r.includeFontPadding = p.includeFontPadding.value_or(false);
        return r;
    }

    // --- Paragraph ---------------------------------------------------------------------------

    Paragraph::Paragraph(TextBackend* backend, ParagraphRequest request, std::vector<SpanStyle> runStyles, float width)
        : m_Backend(backend), m_Request(std::move(request)), m_RunStyles(std::move(runStyles)),
          m_Length(Utf16Length(m_Request.text)) {
        if (m_Backend) m_Data = m_Backend->Layout(m_Request);
        m_Width = std::isinf(width) || width <= 0 ? m_Data.width : width;
    }

    Paragraph::~Paragraph() {
        if (m_Backend && m_Data.handle) m_Backend->Release(m_Data.handle);
    }

    int Paragraph::LineForOffset(int offset) const {
        const auto& lines = m_Data.lines;
        if (lines.empty()) return 0;
        auto it = std::upper_bound(lines.begin(), lines.end(), offset, [](int o, const LineData& l) { return o < l.start; });
        return std::max(0, int(it - lines.begin()) - 1);
    }
    int Paragraph::LineForVertical(float y) const {
        const auto& lines = m_Data.lines;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (y < lines[i].bottom) return int(i);
        return std::max(0, int(lines.size()) - 1);
    }
    float Paragraph::HorizontalPosition(int offset, bool primary) const {
        return m_Backend && m_Data.handle ? m_Backend->GetHorizontalPosition(m_Data.handle, std::clamp(offset, 0, m_Length), primary) : 0;
    }
    CursorRect Paragraph::Cursor(int offset) const {
        return m_Backend && m_Data.handle ? m_Backend->GetCursorRect(m_Data.handle, std::clamp(offset, 0, m_Length)) : CursorRect{};
    }
    int Paragraph::OffsetForPosition(float x, float y) const {
        return m_Backend && m_Data.handle ? m_Backend->GetOffsetForPosition(m_Data.handle, x, y) : 0;
    }
    RectF Paragraph::BoundingBox(int offset) const {
        return m_Backend && m_Data.handle ? m_Backend->GetBoundingBox(m_Data.handle, std::clamp(offset, 0, m_Length)) : RectF{};
    }
    std::vector<RectF> Paragraph::SelectionRects(int start, int end) const {
        if (!m_Backend || !m_Data.handle || start >= end) return {};
        return m_Backend->GetSelectionRects(m_Data.handle, std::clamp(start, 0, m_Length), std::clamp(end, 0, m_Length));
    }
    std::pair<int, int> Paragraph::WordBoundary(int offset) const {
        if (!m_Backend || !m_Data.handle) return { offset, offset };
        return m_Backend->GetWordBoundary(m_Data.handle, std::clamp(offset, 0, m_Length));
    }
    bool Paragraph::IsRtlCharAt(int offset) const {
        return m_Backend && m_Data.handle && m_Backend->IsRtlCharAt(m_Data.handle, std::clamp(offset, 0, m_Length));
    }

    // --- MultiParagraph ----------------------------------------------------------------------

    namespace {
        // substringWithoutParagraphStyles.
        AnnotatedString WithoutParagraphStyles(const AnnotatedString& text, int start, int end) {
            const AnnotatedString sub = text.SubSequence(start, end);
            std::vector<Range<Annotation>> kept;
            for (const auto& a : sub.Annotations())
                if (!std::holds_alternative<ParagraphStyle>(a.item)) kept.push_back(a);
            return AnnotatedString(sub.Text(), std::move(kept));
        }

        // getLocalPlaceholders: the placeholders inside [start, end), shifted.
        std::vector<Range<Placeholder>> LocalPlaceholders(std::span<const Range<Placeholder>> all, int start, int end) {
            std::vector<Range<Placeholder>> out;
            for (const auto& p : all)
                if (p.start >= start && p.end <= end && p.start < p.end && !(p.start == end)) out.push_back({ p.item, p.start - start, p.end - start, p.tag });
            return out;
        }

        template<typename F>
        void ForEachParagraph(const AnnotatedString& text, const TextStyle& style, ui::LayoutDirection dir, F&& f) {
            for (const auto& range : text.NormalizedParagraphStyles(style.paragraph)) {
                const TextStyle merged = ResolveDefaults(TextStyle{ style.span, range.item }, dir);
                f(range.start, range.end, WithoutParagraphStyles(text, range.start, range.end), merged);
            }
        }
    }

    ParagraphIntrinsicWidths MeasureIntrinsics(TextBackend* backend, const AnnotatedString& text, const TextStyle& style,
                                               const ui::Density& density, ui::LayoutDirection dir,
                                               std::span<const Range<Placeholder>> placeholders) {
        ParagraphIntrinsicWidths out;
        if (!backend) return out;
        ForEachParagraph(text, style, dir, [&](int start, int end, const AnnotatedString& sub, const TextStyle& merged) {
            const auto local = LocalPlaceholders(placeholders, start, end);
            const auto w = backend->Intrinsics(BuildParagraphRequest(sub, merged, density, {}, nullptr, local));
            out.minWidth = std::max(out.minWidth, w.minWidth);
            out.maxWidth = std::max(out.maxWidth, w.maxWidth);
        });
        return out;
    }

    MultiParagraph::MultiParagraph(TextBackend* backend, const AnnotatedString& text, const TextStyle& style, const ui::Density& density,
                                   ui::LayoutDirection dir, const ParagraphConstraints& c,
                                   std::span<const Range<Placeholder>> placeholders)
        : m_Text(text) {
        float height = 0;
        int lineCount = 0;
        const int maxLines = std::max(1, c.maxLines);
        const auto ranges = text.NormalizedParagraphStyles(style.paragraph);
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            const TextStyle merged = ResolveDefaults(TextStyle{ style.span, range.item }, dir);
            ParagraphConstraints pc = c;
            pc.maxLines = maxLines - lineCount;
            if (!std::isinf(c.maxHeight)) pc.maxHeight = std::max(0.0f, c.maxHeight - std::ceil(height));
            std::vector<SpanStyle> runStyles;
            const auto local = LocalPlaceholders(placeholders, range.start, range.end);
            ParagraphRequest request = BuildParagraphRequest(WithoutParagraphStyles(text, range.start, range.end), merged, density, pc, &runStyles, local);
            auto paragraph = std::make_shared<Paragraph>(backend, std::move(request), std::move(runStyles), c.width);
            ParagraphInfo info;
            info.startIndex = range.start;
            info.endIndex = range.end;
            info.startLineIndex = lineCount;
            info.endLineIndex = lineCount + paragraph->LineCount();
            info.top = height;
            info.bottom = height + paragraph->Height();
            height = info.bottom;
            lineCount = info.endLineIndex;
            const bool exceeded = paragraph->Data().didExceedMaxLines || (info.endLineIndex == maxLines && i + 1 != ranges.size());
            m_Width = std::max(m_Width, paragraph->Width());
            for (const auto& r : paragraph->Data().placeholderRects)
                m_PlaceholderRects.push_back(r ? std::optional(RectF{ r->x, r->y + info.top, r->w, r->h }) : std::nullopt);
            info.paragraph = std::move(paragraph);
            m_Infos.push_back(std::move(info));
            if (exceeded) {
                m_DidExceedMaxLines = true;
                break;
            }
        }
        if (!std::isinf(c.width) && c.width > 0) m_Width = c.width;
        m_Height = height;
        m_LineCount = lineCount;
        m_PlaceholderRects.resize(std::max(m_PlaceholderRects.size(), placeholders.size()));
    }

    float MultiParagraph::FirstBaseline() const {
        return m_Infos.empty() ? 0 : m_Infos.front().paragraph->Data().firstBaseline;
    }
    float MultiParagraph::LastBaseline() const {
        if (m_Infos.empty()) return 0;
        const auto& last = m_Infos.back();
        return last.top + last.paragraph->Data().lastBaseline;
    }

    const ParagraphInfo& MultiParagraph::InfoForOffset(int offset) const {
        if (offset >= m_Text.Length()) return m_Infos.back();
        for (const auto& info : m_Infos)
            if (offset < info.endIndex) return info;
        return m_Infos.back();
    }
    const ParagraphInfo& MultiParagraph::InfoForLine(int line) const {
        for (const auto& info : m_Infos)
            if (line < info.endLineIndex) return info;
        return m_Infos.back();
    }
    const ParagraphInfo& MultiParagraph::InfoForY(float y) const {
        if (y <= 0) return m_Infos.front();
        if (y >= m_Height) return m_Infos.back();
        for (const auto& info : m_Infos)
            if (y < info.bottom) return info;
        return m_Infos.back();
    }

    namespace {
        const LineData* LocalLine(const ParagraphInfo& info, int line) {
            const auto& lines = info.paragraph->Data().lines;
            const int local = line - info.startLineIndex;
            if (lines.empty()) return nullptr;
            return &lines[std::size_t(std::clamp(local, 0, int(lines.size()) - 1))];
        }
    }

    int MultiParagraph::LineForOffset(int offset) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForOffset(offset);
        return info.startLineIndex + info.paragraph->LineForOffset(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex);
    }
    int MultiParagraph::LineForVerticalPosition(float y) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForY(y);
        if (info.paragraph->LineCount() == 0) return info.startLineIndex;
        return info.startLineIndex + info.paragraph->LineForVertical(y - info.top);
    }
    int MultiParagraph::LineStart(int line) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForLine(line);
        const LineData* l = LocalLine(info, line);
        return info.startIndex + (l ? l->start : 0);
    }
    int MultiParagraph::LineEnd(int line, bool visibleEnd) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForLine(line);
        const LineData* l = LocalLine(info, line);
        if (!l) return info.startIndex;
        // TextLayout.getLineEnd / getLineVisibleEnd: an ellipsized line ends at the text's end,
        // or (visible) where the ellipsis starts.
        int end;
        if (l->ellipsisCount == 0 || l->ellipsisStart == 0) end = visibleEnd ? l->visibleEnd : l->end;
        else end = visibleEnd ? l->start + l->ellipsisStart : info.paragraph->Length();
        return info.startIndex + end;
    }
    bool MultiParagraph::IsLineEllipsized(int line) const {
        if (m_Infos.empty()) return false;
        const LineData* l = LocalLine(InfoForLine(line), line);
        return l && l->ellipsisCount > 0;
    }
    float MultiParagraph::LineTop(int line) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForLine(line);
        const LineData* l = LocalLine(info, line);
        return info.top + (l ? l->top : 0);
    }
    float MultiParagraph::LineBottom(int line) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForLine(line);
        const LineData* l = LocalLine(info, line);
        return info.top + (l ? l->bottom : 0);
    }
    float MultiParagraph::LineBaseline(int line) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForLine(line);
        const LineData* l = LocalLine(info, line);
        return info.top + (l ? l->baseline : 0);
    }
    float MultiParagraph::LineLeft(int line) const {
        if (m_Infos.empty()) return 0;
        const LineData* l = LocalLine(InfoForLine(line), line);
        return l ? l->left : 0;
    }
    float MultiParagraph::LineRight(int line) const {
        if (m_Infos.empty()) return 0;
        const LineData* l = LocalLine(InfoForLine(line), line);
        return l ? l->right : 0;
    }
    float MultiParagraph::HorizontalPosition(int offset, bool primary) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForOffset(offset);
        return info.paragraph->HorizontalPosition(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex, primary);
    }
    CursorRect MultiParagraph::Cursor(int offset) const {
        if (m_Infos.empty()) return {};
        const auto& info = InfoForOffset(offset);
        CursorRect r = info.paragraph->Cursor(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex);
        r.top += info.top;
        r.bottom += info.top;
        return r;
    }
    RectF MultiParagraph::CursorRectWithWidth(int offset, float width) const {
        const CursorRect c = Cursor(offset);
        const float left = std::clamp(c.x - width / 2, 0.0f, std::max(0.0f, m_Width - width));
        return { left, c.top, width, c.bottom - c.top };
    }
    int MultiParagraph::OffsetForPosition(ui::Offset p) const {
        if (m_Infos.empty()) return 0;
        const auto& info = InfoForY(p.y);
        if (info.endIndex == info.startIndex) return info.startIndex;
        return info.startIndex + info.paragraph->OffsetForPosition(p.x, p.y - info.top);
    }
    RectF MultiParagraph::BoundingBox(int offset) const {
        if (m_Infos.empty()) return {};
        const auto& info = InfoForOffset(offset);
        RectF r = info.paragraph->BoundingBox(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex);
        r.y += info.top;
        return r;
    }
    std::vector<RectF> MultiParagraph::SelectionRects(int start, int end) const {
        std::vector<RectF> out;
        if (start >= end) return out;
        for (const auto& info : m_Infos) {
            const int s = std::max(start, info.startIndex), e = std::min(end, info.endIndex);
            if (s >= e) continue;
            for (RectF r : info.paragraph->SelectionRects(s - info.startIndex, e - info.startIndex)) {
                r.y += info.top;
                out.push_back(r);
            }
        }
        return out;
    }
    std::pair<int, int> MultiParagraph::WordBoundary(int offset) const {
        if (m_Infos.empty()) return { offset, offset };
        const auto& info = InfoForOffset(offset);
        const auto [s, e] = info.paragraph->WordBoundary(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex);
        return { info.startIndex + s, info.startIndex + e };
    }
    ResolvedTextDirection MultiParagraph::ParagraphDirection(int offset) const {
        if (m_Infos.empty()) return ResolvedTextDirection::Ltr;
        const auto& info = InfoForOffset(offset);
        const auto& lines = info.paragraph->Data().lines;
        if (lines.empty()) return ResolvedTextDirection::Ltr;
        const int line = info.paragraph->LineForOffset(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex);
        return lines[std::size_t(line)].rtl ? ResolvedTextDirection::Rtl : ResolvedTextDirection::Ltr;
    }
    ResolvedTextDirection MultiParagraph::BidiRunDirection(int offset) const {
        if (m_Infos.empty()) return ResolvedTextDirection::Ltr;
        const auto& info = InfoForOffset(offset);
        return info.paragraph->IsRtlCharAt(std::clamp(offset, info.startIndex, info.endIndex) - info.startIndex) ? ResolvedTextDirection::Rtl
                                                                                                                  : ResolvedTextDirection::Ltr;
    }

    // --- layoutText --------------------------------------------------------------------------

    TextLayoutResult LayoutText(TextBackend* backend, TextLayoutInput input, std::optional<ParagraphIntrinsicWidths>* intrinsics) {
        const ui::Constraints& c = input.constraints;
        std::optional<ParagraphIntrinsicWidths> local;
        auto& intr = intrinsics ? *intrinsics : local;
        if (!intr) intr = MeasureIntrinsics(backend, input.text, input.style, input.density, input.layoutDirection, input.placeholders);
        // finalMaxWidth
        const bool widthMatters = input.softWrap || IsEllipsis(input.overflow);
        const int maxWidth = widthMatters && c.HasBoundedWidth() ? c.maxWidth : ui::Constraints::Infinity;
        int width;
        if (c.minWidth == maxWidth) width = maxWidth;
        else width = std::clamp(int(std::ceil(intr->maxWidth)), c.minWidth, maxWidth);
        // finalMaxLines
        const int maxLines = !input.softWrap && IsEllipsis(input.overflow) ? 1 : std::max(1, input.maxLines);
        ParagraphConstraints pc;
        pc.width = width == ui::Constraints::Infinity ? INFINITY : float(width);
        pc.maxHeight = c.HasBoundedHeight() ? float(c.maxHeight) : INFINITY;
        pc.maxLines = maxLines;
        pc.overflow = input.overflow;
        auto mp = std::make_shared<MultiParagraph>(backend, input.text, input.style, input.density, input.layoutDirection, pc, input.placeholders);
        const ui::IntSize size{ std::clamp(int(std::ceil(mp->Width())), c.minWidth, c.maxWidth),
                                std::clamp(int(std::ceil(mp->Height())), c.minHeight, c.maxHeight) };
        return TextLayoutResult(std::move(input), std::move(mp), size);
    }

}
