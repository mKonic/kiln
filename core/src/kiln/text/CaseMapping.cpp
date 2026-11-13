#include "kiln/text/CaseMapping.h"
#include <kiln/Function.h>

#include <cwctype>
#include <map>
#include <set>

namespace kiln::text {

    namespace {
        CaseMapper& Mapper() { static CaseMapper m; return m; }

        std::u16string ToU16(std::string_view s) {
            std::u16string out;
            for (std::size_t i = 0; i < s.size();) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                char32_t cp;
                int n;
                if (c < 0x80) { cp = c; n = 1; }
                else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
                else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
                else { cp = c & 0x07; n = 4; }
                for (int k = 1; k < n && i + std::size_t(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3F);
                i += std::size_t(n);
                if (cp >= 0x10000) { cp -= 0x10000; out += char16_t(0xD800 + (cp >> 10)); out += char16_t(0xDC00 + (cp & 0x3FF)); }
                else out += char16_t(cp);
            }
            return out;
        }
        std::string ToU8(std::u16string_view s) {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                char32_t cp = s[i];
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            }
            return out;
        }

        // Without a host: the C library's mapping per UTF-16 unit (no length changes).
        char16_t Map(char16_t c, bool upper) {
            if (c >= 0xD800 && c < 0xE000) return c;
            return char16_t(upper ? std::towupper(wint_t(c)) : std::towlower(wint_t(c)));
        }

        AnnotatedString Transform(const AnnotatedString& s, const kiln::Function<std::u16string(std::u16string_view, int start)>& fn) {
            const std::u16string text = ToU16(s.Text());
            std::set<int> cuts{ 0, int(text.size()) };
            for (const auto& a : s.Annotations()) { cuts.insert(a.start); cuts.insert(a.end); }
            std::u16string out;
            std::map<int, int> offsets{ { 0, 0 } };
            int prev = -1;
            for (int c : cuts) {
                if (prev >= 0) {
                    out += fn(std::u16string_view(text).substr(std::size_t(prev), std::size_t(c - prev)), prev);
                    offsets[c] = int(out.size());
                }
                prev = c;
            }
            std::vector<Range<Annotation>> annotations;
            for (const auto& a : s.Annotations()) {
                Range<Annotation> r = a;
                r.start = offsets[a.start];
                r.end = offsets[a.end];
                annotations.push_back(std::move(r));
            }
            return AnnotatedString(ToU8(out), std::move(annotations));
        }
    }

    void SetCaseMapper(CaseMapper mapper) { Mapper() = std::move(mapper); }

    std::u16string MapCase(std::u16string_view text, const std::string& localeTag, CaseOp op) {
        if (Mapper()) return Mapper()(text, localeTag, op);
        std::u16string out(text);
        if (out.empty()) return out;
        switch (op) {
            case CaseOp::Upper: for (char16_t& c : out) c = Map(c, true); break;
            case CaseOp::Lower: for (char16_t& c : out) c = Map(c, false); break;
            case CaseOp::Capitalize: if (std::iswlower(wint_t(out[0]))) out[0] = Map(out[0], true); break;
            case CaseOp::Decapitalize: out[0] = Map(out[0], false); break;
        }
        return out;
    }

    std::string ToUpperCase(std::string_view t, const std::string& tag) { return ToU8(MapCase(ToU16(t), tag, CaseOp::Upper)); }
    std::string ToLowerCase(std::string_view t, const std::string& tag) { return ToU8(MapCase(ToU16(t), tag, CaseOp::Lower)); }
    std::string Capitalize(std::string_view t, const std::string& tag) { return ToU8(MapCase(ToU16(t), tag, CaseOp::Capitalize)); }
    std::string Decapitalize(std::string_view t, const std::string& tag) { return ToU8(MapCase(ToU16(t), tag, CaseOp::Decapitalize)); }

    AnnotatedString ToUpperCase(const AnnotatedString& s, const std::string& tag) {
        return Transform(s, [&](std::u16string_view part, int) { return MapCase(part, tag, CaseOp::Upper); });
    }
    AnnotatedString ToLowerCase(const AnnotatedString& s, const std::string& tag) {
        return Transform(s, [&](std::u16string_view part, int) { return MapCase(part, tag, CaseOp::Lower); });
    }
    AnnotatedString Capitalize(const AnnotatedString& s, const std::string& tag) {
        return Transform(s, [&](std::u16string_view part, int start) { return start == 0 ? MapCase(part, tag, CaseOp::Capitalize) : std::u16string(part); });
    }
    AnnotatedString Decapitalize(const AnnotatedString& s, const std::string& tag) {
        return Transform(s, [&](std::u16string_view part, int start) { return start == 0 ? MapCase(part, tag, CaseOp::Decapitalize) : std::u16string(part); });
    }

}
