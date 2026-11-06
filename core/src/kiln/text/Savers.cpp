#include "kiln/text/Savers.h"

namespace kiln::text {

    namespace {
        using rt::SavedValue;
        using List = SavedValue::List;

        const List* AsList(const SavedValue& v) { return std::get_if<List>(&v.v); }
        double Num(const List& l, std::size_t i, double fallback = 0) { return i < l.size() ? rt::SavedNumber(l[i]).value_or(fallback) : fallback; }
        bool IsNull(const List& l, std::size_t i) { return i >= l.size() || l[i].IsNull(); }

        SavedValue SaveUnit(const TextUnit& u) { return u.IsSpecified() ? SavedValue(List{ double(u.value), int(u.type) }) : SavedValue(); }
        TextUnit RestoreUnit(const SavedValue& v) {
            const List* l = AsList(v);
            if (!l || l->size() < 2) return {};
            TextUnit u;
            u.value = float(Num(*l, 0));
            u.type = TextUnit::Type(int(Num(*l, 1)));
            return u;
        }
        SavedValue SaveColor(Color c) { return SavedValue(std::int64_t(c.argb)); }
        Color RestoreColor(const SavedValue& v, Color fallback) {
            auto n = rt::SavedNumber(v);
            return n ? Color(std::uint32_t(std::int64_t(*n))) : fallback;
        }
        template<typename T> SavedValue SaveOpt(const std::optional<T>& o, auto&& save) { return o ? SavedValue(save(*o)) : SavedValue(); }

        SavedValue SaveSpan(const SpanStyle& s) {
            return List{
                SaveColor(s.color),
                SaveUnit(s.fontSize),
                SaveOpt(s.fontWeight, [](const FontWeight& w) { return SavedValue(w.weight); }),
                SaveOpt(s.fontStyle, [](FontStyle f) { return SavedValue(int(f)); }),
                SaveOpt(s.fontSynthesis, [](FontSynthesis f) { return SavedValue(int(f)); }),
                SavedValue(-1),   // font family: not saved (as in Compose)
                SaveOpt(s.fontFeatureSettings, [](const std::string& f) { return SavedValue(f); }),
                SaveUnit(s.letterSpacing),
                SaveOpt(s.baselineShift, [](const BaselineShift& b) { return SavedValue(double(b.multiplier)); }),
                SaveOpt(s.textGeometricTransform, [](const TextGeometricTransform& t) { return SavedValue(List{ double(t.scaleX), double(t.skewX) }); }),
                SaveOpt(s.localeList, [](const std::string& l) { return SavedValue(l); }),
                SaveColor(s.background),
                SaveOpt(s.textDecoration, [](TextDecoration d) { return SavedValue(int(d)); }),
                SaveOpt(s.shadow, [](const Shadow& sh) {
                    return SavedValue(List{ SaveColor(sh.color), double(sh.offset.x), double(sh.offset.y), double(sh.blurRadius) });
                }),
            };
        }
        std::optional<SpanStyle> RestoreSpan(const SavedValue& v) {
            const List* lp = AsList(v);
            if (!lp || lp->size() < 14) return std::nullopt;
            const List& l = *lp;
            SpanStyle s;
            s.color = RestoreColor(l[0], Color::Unspecified());
            s.fontSize = RestoreUnit(l[1]);
            if (!IsNull(l, 2)) s.fontWeight = FontWeight{ int(Num(l, 2, 400)) };
            if (!IsNull(l, 3)) s.fontStyle = FontStyle(int(Num(l, 3)));
            if (!IsNull(l, 4)) s.fontSynthesis = FontSynthesis(int(Num(l, 4)));
            if (const auto* f = std::get_if<std::string>(&l[6].v)) s.fontFeatureSettings = *f;
            s.letterSpacing = RestoreUnit(l[7]);
            if (!IsNull(l, 8)) s.baselineShift = BaselineShift{ float(Num(l, 8)) };
            if (const List* t = AsList(l[9])) s.textGeometricTransform = TextGeometricTransform{ float(Num(*t, 0, 1)), float(Num(*t, 1)) };
            if (const auto* loc = std::get_if<std::string>(&l[10].v)) s.localeList = *loc;
            s.background = RestoreColor(l[11], Color::Unspecified());
            if (!IsNull(l, 12)) s.textDecoration = TextDecoration(int(Num(l, 12)));
            if (const List* sh = AsList(l[13]); sh && sh->size() >= 4)
                s.shadow = Shadow{ RestoreColor((*sh)[0], Color(0xFF000000u)), { float(Num(*sh, 1)), float(Num(*sh, 2)) }, float(Num(*sh, 3)) };
            return s;
        }

        SavedValue SaveParagraph(const ParagraphStyle& p) {
            return List{
                int(p.textAlign),
                int(p.textDirection),
                SaveUnit(p.lineHeight),
                SaveOpt(p.textIndent, [](const TextIndent& t) { return SavedValue(List{ SaveUnit(t.firstLine), SaveUnit(t.restLine) }); }),
                SaveOpt(p.includeFontPadding, [](bool b) { return SavedValue(b); }),   // PlatformParagraphStyle
                SaveOpt(p.lineHeightStyle, [](const LineHeightStyle& s) {
                    return SavedValue(List{ double(s.alignment), int(s.trim), int(s.mode) });
                }),
                List{ int(p.lineBreak.strategy), int(p.lineBreak.strictness), int(p.lineBreak.wordBreak) },
                int(p.hyphens),
                SaveOpt(p.textMotion, [](TextMotion m) { return SavedValue(int(m)); }),
            };
        }
        std::optional<ParagraphStyle> RestoreParagraph(const SavedValue& v) {
            const List* lp = AsList(v);
            if (!lp || lp->size() < 9) return std::nullopt;
            const List& l = *lp;
            ParagraphStyle p;
            p.textAlign = TextAlign(int(Num(l, 0)));
            p.textDirection = TextDirection(int(Num(l, 1)));
            p.lineHeight = RestoreUnit(l[2]);
            if (const List* t = AsList(l[3]); t && t->size() >= 2) p.textIndent = TextIndent{ RestoreUnit((*t)[0]), RestoreUnit((*t)[1]) };
            if (const auto* b = std::get_if<bool>(&l[4].v)) p.includeFontPadding = *b;
            if (const List* s = AsList(l[5]); s && s->size() >= 3)
                p.lineHeightStyle = LineHeightStyle{ float(Num(*s, 0, -1)), LineHeightStyle::Trim(int(Num(*s, 1))), LineHeightStyle::Mode(int(Num(*s, 2))) };
            if (const List* b = AsList(l[6]); b && b->size() >= 3)
                p.lineBreak = LineBreak{ std::uint8_t(Num(*b, 0)), std::uint8_t(Num(*b, 1)), std::uint8_t(Num(*b, 2)) };
            p.hyphens = Hyphens(int(Num(l, 7)));
            if (!IsNull(l, 8)) p.textMotion = TextMotion(int(Num(l, 8)));
            return p;
        }

        SavedValue SaveLinkStyles(const TextLinkStyles& s) {
            auto opt = [](const std::optional<SpanStyle>& o) { return o ? SaveSpan(*o) : SavedValue(); };
            return List{ opt(s.style), opt(s.focusedStyle), opt(s.hoveredStyle), opt(s.pressedStyle) };
        }
        TextLinkStyles RestoreLinkStyles(const SavedValue& v) {
            TextLinkStyles s;
            const List* l = AsList(v);
            if (!l || l->size() < 4) return s;
            s.style = RestoreSpan((*l)[0]);
            s.focusedStyle = RestoreSpan((*l)[1]);
            s.hoveredStyle = RestoreSpan((*l)[2]);
            s.pressedStyle = RestoreSpan((*l)[3]);
            return s;
        }

        // AnnotationType markers, in Compose's order.
        enum Marker { kParagraph = 0, kSpan = 1, kVerbatimTts = 2, kUrl = 3, kLink = 4, kClickable = 5, kString = 6 };

        SavedValue SaveAnnotation(const Annotation& a) {
            if (const auto* p = std::get_if<ParagraphStyle>(&a)) return List{ int(kParagraph), SaveParagraph(*p) };
            if (const auto* s = std::get_if<SpanStyle>(&a)) return List{ int(kSpan), SaveSpan(*s) };
            if (const auto* t = std::get_if<TtsAnnotation>(&a)) return List{ int(kVerbatimTts), t->verbatim };
            if (const auto* str = std::get_if<StringAnnotation>(&a)) return List{ int(kString), str->value };
            const auto& link = std::get<LinkAnnotation>(a);
            if (link.kind == LinkAnnotation::Kind::Url) return List{ int(kLink), List{ link.url, SaveLinkStyles(link.styles) } };
            return List{ int(kClickable), List{ link.tag, SaveLinkStyles(link.styles) } };
        }
        std::optional<Annotation> RestoreAnnotation(const SavedValue& v) {
            const List* l = AsList(v);
            if (!l || l->size() < 2) return std::nullopt;
            switch (int(Num(*l, 0, -1))) {
                case kParagraph: if (auto p = RestoreParagraph((*l)[1])) return Annotation(*p); break;
                case kSpan: if (auto s = RestoreSpan((*l)[1])) return Annotation(*s); break;
                case kVerbatimTts: if (const auto* t = std::get_if<std::string>(&(*l)[1].v)) return Annotation(TtsAnnotation{ *t }); break;
                case kString: if (const auto* t = std::get_if<std::string>(&(*l)[1].v)) return Annotation(StringAnnotation{ *t }); break;
                case kUrl: if (const auto* t = std::get_if<std::string>(&(*l)[1].v)) return Annotation(LinkAnnotation::Url(*t)); break;
                case kLink:
                case kClickable: {
                    const List* d = AsList((*l)[1]);
                    const auto* s = d && !d->empty() ? std::get_if<std::string>(&(*d)[0].v) : nullptr;
                    if (!s) break;
                    TextLinkStyles styles = d->size() > 1 ? RestoreLinkStyles((*d)[1]) : TextLinkStyles{};
                    return Annotation(int(Num(*l, 0)) == kLink ? LinkAnnotation::Url(*s, styles) : LinkAnnotation::Clickable(*s, styles));
                }
            }
            return std::nullopt;
        }
    }

    rt::Saver<SpanStyle> SpanStyleSaver() { return { [](const SpanStyle& s) { return SaveSpan(s); }, RestoreSpan }; }
    rt::Saver<ParagraphStyle> ParagraphStyleSaver() { return { [](const ParagraphStyle& p) { return SaveParagraph(p); }, RestoreParagraph }; }

    rt::Saver<AnnotatedString> AnnotatedStringSaver() {
        return {
            [](const AnnotatedString& s) {
                List ranges;
                for (const auto& r : s.Annotations()) ranges.emplace_back(List{ SaveAnnotation(r.item), r.start, r.end, r.tag });
                return SavedValue(List{ s.Text(), ranges.empty() ? SavedValue() : SavedValue(std::move(ranges)) });
            },
            [](const SavedValue& v) -> std::optional<AnnotatedString> {
                const List* l = AsList(v);
                const auto* text = l && !l->empty() ? std::get_if<std::string>(&(*l)[0].v) : nullptr;
                if (!text) return std::nullopt;
                std::vector<Range<Annotation>> annotations;
                if (const List* ranges = l->size() > 1 ? AsList((*l)[1]) : nullptr)
                    for (const auto& r : *ranges) {
                        const List* rl = AsList(r);
                        if (!rl || rl->size() < 4) continue;
                        auto item = RestoreAnnotation((*rl)[0]);
                        const auto* tag = std::get_if<std::string>(&(*rl)[3].v);
                        if (!item) continue;
                        annotations.push_back({ std::move(*item), int(Num(*rl, 1)), int(Num(*rl, 2)), tag ? *tag : std::string() });
                    }
                return AnnotatedString(*text, std::move(annotations));
            },
        };
    }

}
