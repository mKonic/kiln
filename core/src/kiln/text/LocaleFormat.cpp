#include "kiln/text/LocaleFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace kiln::text {

    namespace {
        bool Alpha(std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }); }
        bool Digits(std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }); }

        void AppendUtf8(std::string& out, char32_t c) {
            if (c < 0x80) {
                out += char(c);
            } else if (c < 0x800) {
                out += char(0xC0 | (c >> 6));
                out += char(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                out += char(0xE0 | (c >> 12));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            } else {
                out += char(0xF0 | (c >> 18));
                out += char(0x80 | ((c >> 12) & 0x3F));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            }
        }
    }

    std::vector<std::string> ResourceLocaleCandidates(std::string_view tag) {
        // language[-script][-region], stopping at extensions and variants.
        std::string lang, script, region;
        std::size_t at = 0;
        int part = 0;
        while (at <= tag.size()) {
            const std::size_t end = std::min(tag.find_first_of("-_", at), tag.size());
            const std::string_view sub = tag.substr(at, end - at);
            if (part == 0) {
                if (sub.size() < 2 || sub.size() > 3 || !Alpha(sub)) break;
                lang.assign(sub);
                std::transform(lang.begin(), lang.end(), lang.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            } else if (sub.size() == 4 && Alpha(sub) && script.empty() && region.empty()) {
                script.assign(sub);
                script[0] = char(std::toupper(static_cast<unsigned char>(script[0])));
                for (std::size_t i = 1; i < 4; ++i) script[i] = char(std::tolower(static_cast<unsigned char>(script[i])));
            } else if (((sub.size() == 2 && Alpha(sub)) || (sub.size() == 3 && Digits(sub))) && region.empty()) {
                region.assign(sub);
                std::transform(region.begin(), region.end(), region.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            } else {
                break;
            }
            ++part;
            at = end + 1;
        }
        std::vector<std::string> out;
        auto add = [&](std::string t) { if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t)); };
        if (!lang.empty()) {
            if (!script.empty() && !region.empty()) add(lang + "-" + script + "-" + region);
            if (!script.empty()) add(lang + "-" + script);
            if (!region.empty()) add(lang + "-" + region);
            // A script without a region: the region its resources are kept under.
            if (lang == "zh" && region.empty()) {
                if (script == "Hant") add("zh-TW");
                if (script == "Hans") add("zh-CN");
            }
            add(lang);
        }
        add("");
        return out;
    }

    namespace {
        std::string_view LanguageOf(std::string_view tag) {
            const std::size_t dash = tag.find_first_of("-_");
            return dash == std::string_view::npos ? tag : tag.substr(0, dash);
        }
        bool OneOf(std::string_view x, std::initializer_list<std::string_view> set) {
            for (std::string_view s : set) if (s == x) return true;
            return false;
        }
    }

    // CLDR's latn decimal and group symbols for the languages Android ships.
    std::string_view LocaleDecimalSeparator(std::string_view tag) {
        const std::string_view lang = LanguageOf(tag);
        if (OneOf(lang, { "de", "es", "it", "pt", "nl", "da", "nb", "nn", "no", "sv", "fi", "fr", "ru", "uk", "be", "pl", "cs", "sk",
                          "sl", "hr", "sr", "bs", "bg", "ro", "hu", "lt", "lv", "et", "el", "tr", "az", "id", "in", "vi", "ca", "gl",
                          "eu", "is", "ka", "kk", "ky", "uz", "mk", "sq", "hy", "af", "mn", "km" }))
            return ",";
        return ".";
    }
    std::string_view LocaleGroupingSeparator(std::string_view tag) {
        const std::string_view lang = LanguageOf(tag);
        if (lang == "fr") return " ";
        if (OneOf(lang, { "ru", "uk", "be", "pl", "cs", "sk", "sv", "nb", "nn", "no", "fi", "bg", "hu", "lt", "lv", "et", "kk", "ky",
                          "uz", "hy", "ka", "af", "sq", "km" }))
            return " ";
        if (LocaleDecimalSeparator(tag) == ",") return ".";
        return ",";
    }

    namespace {
        struct Spec {
            bool left = false, alt = false, plus = false, space = false, zero = false, group = false, paren = false;
            int width = -1, precision = -1;
            char conversion = 0;
        };

        // Digits in the locale's numbering system, grouping by threes when asked.
        std::string LocalizeDigits(std::string_view ascii, char32_t zero, bool group, std::string_view grouping, std::string_view decimal) {
            const std::size_t dot = ascii.find('.');
            const std::string_view intPart = ascii.substr(0, dot);
            std::string out;
            for (std::size_t i = 0; i < intPart.size(); ++i) {
                if (group && i > 0 && (intPart.size() - i) % 3 == 0) out += grouping;
                const char d = intPart[i];
                if (d >= '0' && d <= '9') AppendUtf8(out, zero + char32_t(d - '0'));
                else out += d;
            }
            if (dot != std::string_view::npos) {
                out += decimal;
                for (char d : ascii.substr(dot + 1)) {
                    if (d >= '0' && d <= '9') AppendUtf8(out, zero + char32_t(d - '0'));
                    else out += d;
                }
            }
            return out;
        }
        std::size_t Utf8Length(std::string_view s) {
            std::size_t n = 0;
            for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
            return n;
        }
        std::string Pad(std::string body, const Spec& sp, std::string_view sign = {}) {
            const std::size_t len = Utf8Length(body) + Utf8Length(sign);
            if (sp.width < 0 || len >= std::size_t(sp.width)) return std::string(sign) + body;
            const std::string fill(std::size_t(sp.width) - len, sp.zero && !sp.left ? '0' : ' ');
            if (sp.left) return std::string(sign) + body + fill;
            if (sp.zero) return std::string(sign) + fill + body;
            return fill + std::string(sign) + body;
        }
        std::string Upper(std::string s) {
            for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }
        // The shortest decimal digits that read back as `v` (v > 0, finite) and the decimal
        // exponent of the first: v = 0.d1d2... x 10^exp10.
        void ShortestDigits(double v, std::string& digits, int& exp10) {
            char buf[64];
            for (int prec = 0; prec <= 17; ++prec) {
                std::snprintf(buf, sizeof buf, "%.*e", prec, v);
                if (std::strtod(buf, nullptr) == v) break;
            }
            const char* e = std::strchr(buf, 'e');
            digits.clear();
            for (const char* p = buf; p < e; ++p) if (*p >= '0' && *p <= '9') digits += *p;
            exp10 = std::atoi(e + 1) + 1;
        }
        // Formatter's %.Nf / %.Ne: the shortest digits rounded half-up (FormattedFloatingDecimal
        // applies RoundingMode.HALF_UP to Double.toString's digits).
        std::string FixedHalfUp(double v, int prec) {
            if (v == 0) return prec > 0 ? "0." + std::string(std::size_t(prec), '0') : "0";
            std::string d;
            int exp10 = 0;
            ShortestDigits(v, d, exp10);
            // Digits as an integer with `prec` decimals: keep exp10 + prec digits.
            const int keep = exp10 + prec;
            std::string kept;
            if (keep <= 0) {
                const bool up = keep == 0 && !d.empty() && d[0] >= '5';
                kept = up ? "1" : "0";
            } else {
                kept = d.substr(0, std::min<std::size_t>(d.size(), std::size_t(keep)));
                if (kept.size() < std::size_t(keep)) kept.append(std::size_t(keep) - kept.size(), '0');
                if (std::size_t(keep) < d.size() && d[std::size_t(keep)] >= '5') {
                    int i = int(kept.size()) - 1;
                    while (i >= 0 && kept[std::size_t(i)] == '9') kept[std::size_t(i--)] = '0';
                    if (i < 0) kept.insert(kept.begin(), '1');
                    else ++kept[std::size_t(i)];
                }
            }
            if (kept.size() <= std::size_t(prec)) kept.insert(0, std::size_t(prec) + 1 - kept.size(), '0');
            std::string out = kept.substr(0, kept.size() - std::size_t(prec));
            if (prec > 0) out += "." + kept.substr(kept.size() - std::size_t(prec));
            return out;
        }
        std::string ScientificHalfUp(double v, int prec) {
            if (v == 0) {
                std::string z = prec > 0 ? "0." + std::string(std::size_t(prec), '0') : "0";
                return z + "e+00";
            }
            std::string d;
            int exp10 = 0;
            ShortestDigits(v, d, exp10);
            std::string kept = d.substr(0, std::min<std::size_t>(d.size(), std::size_t(prec) + 1));
            if (kept.size() < std::size_t(prec) + 1) kept.append(std::size_t(prec) + 1 - kept.size(), '0');
            if (std::size_t(prec) + 1 < d.size() && d[std::size_t(prec) + 1] >= '5') {
                int i = int(kept.size()) - 1;
                while (i >= 0 && kept[std::size_t(i)] == '9') kept[std::size_t(i--)] = '0';
                if (i < 0) { kept.insert(kept.begin(), '1'); kept.pop_back(); ++exp10; }
                else ++kept[std::size_t(i)];
            }
            const int e = exp10 - 1;
            char ebuf[16];
            std::snprintf(ebuf, sizeof ebuf, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
            return kept.substr(0, 1) + (prec > 0 ? "." + kept.substr(1) : std::string()) + ebuf;
        }

        std::string ToString(const FormatArg& a) {
            if (const auto* s = std::get_if<std::string>(&a.value)) return *s;
            if (const auto* i = std::get_if<std::int64_t>(&a.value)) return std::to_string(*i);
            const double d = std::get<double>(a.value);
            // Double.toString: shortest round trip, ".0" for integral values.
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            for (int prec = 1; prec <= 17; ++prec) {
                std::snprintf(buf, sizeof buf, "%.*g", prec, d);
                if (std::strtod(buf, nullptr) == d) break;
            }
            std::string s = buf;
            if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
            return s;
        }
    }

    std::string FormatJava(std::string_view pattern, std::span<const FormatArg> args, std::string_view localeTag) {
        std::string out;
        std::size_t next = 0;   // the ordinary index for conversions without a position
        std::size_t last = 0;   // '<': the previous argument
        const char32_t zero = LocaleZeroDigit(localeTag);
        const std::string_view decimal = LocaleDecimalSeparator(localeTag), grouping = LocaleGroupingSeparator(localeTag);
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const char c = pattern[i];
            if (c != '%' || i + 1 >= pattern.size()) {
                out += c;
                continue;
            }
            std::size_t j = i + 1;
            if (pattern[j] == '%') { out += '%'; i = j; continue; }
            if (pattern[j] == 'n') { out += '\n'; i = j; continue; }
            Spec sp;
            // [index$] or '<'
            std::size_t index = 0;
            bool positional = false, previous = false;
            std::size_t k = j;
            while (k < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[k]))) ++k;
            if (k > j && k < pattern.size() && pattern[k] == '$') {
                index = std::size_t(std::stoul(std::string(pattern.substr(j, k - j))));
                positional = index > 0;
                j = k + 1;
            } else if (pattern[j] == '<') {
                previous = true;
                ++j;
            }
            // flags
            for (; j < pattern.size(); ++j) {
                const char f = pattern[j];
                if (f == '-') sp.left = true;
                else if (f == '#') sp.alt = true;
                else if (f == '+') sp.plus = true;
                else if (f == ' ') sp.space = true;
                else if (f == '0') sp.zero = true;
                else if (f == ',') sp.group = true;
                else if (f == '(') sp.paren = true;
                else break;
            }
            // width, precision
            std::size_t w = j;
            while (j < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[j]))) ++j;
            if (j > w) sp.width = std::stoi(std::string(pattern.substr(w, j - w)));
            if (j < pattern.size() && pattern[j] == '.') {
                const std::size_t p = ++j;
                while (j < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[j]))) ++j;
                sp.precision = j > p ? std::stoi(std::string(pattern.substr(p, j - p))) : 0;
            }
            if (j >= pattern.size() || !std::strchr("sSbBcCdoxXeEfgGhH", pattern[j])) {
                out += c;   // not a conversion: kept as written
                continue;
            }
            sp.conversion = pattern[j];
            const std::size_t argIndex = positional ? index - 1 : previous ? last : next++;
            last = argIndex;
            i = j;
            if (argIndex >= args.size()) continue;   // Java throws MissingFormatArgumentException
            const FormatArg& a = args[argIndex];
            const char conv = sp.conversion;
            const bool upper = std::isupper(static_cast<unsigned char>(conv)) != 0;
            switch (std::tolower(static_cast<unsigned char>(conv))) {
                case 's': {
                    std::string s = ToString(a);
                    if (sp.precision >= 0 && Utf8Length(s) > std::size_t(sp.precision)) {
                        std::size_t n = 0, b = 0;
                        for (; b < s.size(); ++b) if ((static_cast<unsigned char>(s[b]) & 0xC0) != 0x80 && n++ == std::size_t(sp.precision)) break;
                        s.resize(b);
                    }
                    out += Pad(upper ? Upper(s) : s, Spec{ sp.left, false, false, false, false, false, false, sp.width, -1, 's' });
                    break;
                }
                case 'b': {
                    bool v = true;
                    if (const auto* s = std::get_if<std::string>(&a.value)) v = *s == "true";
                    out += Pad(upper ? (v ? "TRUE" : "FALSE") : (v ? "true" : "false"), Spec{ sp.left, false, false, false, false, false, false, sp.width, -1, 'b' });
                    break;
                }
                case 'h': {
                    char buf[32];
                    std::snprintf(buf, sizeof buf, "%x", unsigned(std::hash<std::string>{}(ToString(a))));
                    out += Pad(upper ? Upper(buf) : buf, Spec{ sp.left, false, false, false, false, false, false, sp.width, -1, 'h' });
                    break;
                }
                case 'c': {
                    std::string s;
                    if (const auto* i = std::get_if<std::int64_t>(&a.value)) AppendUtf8(s, char32_t(*i));
                    else s = ToString(a);
                    out += Pad(upper ? Upper(s) : s, Spec{ sp.left, false, false, false, false, false, false, sp.width, -1, 'c' });
                    break;
                }
                case 'd': case 'o': case 'x': {
                    std::int64_t v = 0;
                    if (const auto* i = std::get_if<std::int64_t>(&a.value)) v = *i;
                    else if (const auto* d = std::get_if<double>(&a.value)) v = std::int64_t(*d);
                    else { out += Pad(std::get<std::string>(a.value), sp); break; }   // IllegalFormatConversion in Java
                    if (std::tolower(static_cast<unsigned char>(conv)) == 'd') {
                        const bool neg = v < 0;
                        const std::string body = LocalizeDigits(std::to_string(neg ? -(v + 1) + std::uint64_t(1) : std::uint64_t(v)), zero, sp.group, grouping, decimal);
                        if (neg && sp.paren) { out += Pad("(" + body + ")", sp); break; }
                        out += Pad(body, sp, neg ? "-" : sp.plus ? "+" : sp.space ? " " : "");
                    } else {
                        char buf[32];
                        std::snprintf(buf, sizeof buf, conv == 'o' ? "%llo" : "%llx", static_cast<unsigned long long>(v));
                        std::string body = buf;
                        if (upper) body = Upper(body);
                        out += Pad(body, sp, sp.alt ? (conv == 'o' ? "0" : upper ? "0X" : "0x") : "");
                    }
                    break;
                }
                case 'e': case 'f': case 'g': {
                    double v = 0;
                    if (const auto* d = std::get_if<double>(&a.value)) v = *d;
                    else if (const auto* i = std::get_if<std::int64_t>(&a.value)) v = double(*i);   // Java rejects; be lenient
                    else { out += Pad(std::get<std::string>(a.value), sp); break; }
                    const int prec = sp.precision >= 0 ? sp.precision : 6;
                    if (std::isnan(v) || std::isinf(v)) {
                        out += Pad(std::isnan(v) ? "NaN" : "Infinity", sp, std::isinf(v) && v < 0 ? "-" : sp.plus && !std::isnan(v) ? "+" : "");
                        break;
                    }
                    const bool neg = std::signbit(v);
                    char buf[512];
                    const char lowerConv = char(std::tolower(static_cast<unsigned char>(conv)));
                    std::string ascii;
                    if (lowerConv == 'f') ascii = FixedHalfUp(std::fabs(v), prec);
                    else if (lowerConv == 'e') ascii = ScientificHalfUp(std::fabs(v), prec);
                    else {
                        // %g: scientific when the rounded value is < 10^-4 or >= 10^precision.
                        const int p = prec == 0 ? 1 : prec;
                        const double a = std::fabs(v);
                        if (a != 0 && (a < 1e-4 || a >= std::pow(10.0, p))) ascii = ScientificHalfUp(a, p - 1);
                        else {
                            const int intDigits = a == 0 ? 1 : std::max(1, int(std::floor(std::log10(a))) + 1);
                            ascii = FixedHalfUp(a, std::max(0, p - intDigits));
                        }
                    }
                    (void)buf;
                    std::string expPart;
                    if (const std::size_t e = ascii.find_first_of("eE"); e != std::string::npos) {
                        expPart = ascii.substr(e);
                        ascii.resize(e);
                    }
                    std::string body = LocalizeDigits(ascii, zero, sp.group && lowerConv != 'e', grouping, decimal) + (upper ? Upper(expPart) : expPart);
                    if (neg && sp.paren) { out += Pad("(" + body + ")", sp); break; }
                    out += Pad(body, sp, neg ? "-" : sp.plus ? "+" : sp.space ? " " : "");
                    break;
                }
            }
        }
        return out;
    }

}
