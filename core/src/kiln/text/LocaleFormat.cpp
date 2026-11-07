#include "kiln/text/LocaleFormat.h"

#include <algorithm>
#include <cctype>

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

    std::string FormatJava(std::string_view pattern, std::span<const FormatArg> args, std::string_view localeTag) {
        std::string out;
        std::size_t next = 0;   // the ordinary index for %s / %d without a position
        const char32_t zero = LocaleZeroDigit(localeTag);
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const char c = pattern[i];
            if (c != '%' || i + 1 >= pattern.size()) {
                out += c;
                continue;
            }
            std::size_t j = i + 1;
            if (pattern[j] == '%') {
                out += '%';
                i = j;
                continue;
            }
            // %[n$]conversion
            std::size_t index = 0;
            std::size_t k = j;
            while (k < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[k]))) ++k;
            bool positional = false;
            if (k > j && k < pattern.size() && pattern[k] == '$') {
                index = std::size_t(std::stoul(std::string(pattern.substr(j, k - j))));
                positional = index > 0;
                j = k + 1;
            }
            if (j >= pattern.size() || (pattern[j] != 's' && pattern[j] != 'd')) {
                out += c;   // not a conversion these strings use: kept as written
                continue;
            }
            const std::size_t argIndex = positional ? index - 1 : next++;
            if (argIndex < args.size()) {
                const FormatArg& a = args[argIndex];
                if (pattern[j] == 's') {
                    if (const auto* s = std::get_if<std::string>(&a)) out += *s;
                    else out += std::to_string(std::get<std::int64_t>(a));
                } else if (const auto* n = std::get_if<std::int64_t>(&a)) {
                    const std::string digits = std::to_string(*n);
                    for (char d : digits) {
                        if (d >= '0' && d <= '9') AppendUtf8(out, zero + char32_t(d - '0'));
                        else out += d;
                    }
                } else {
                    out += std::get<std::string>(a);
                }
            }
            i = j;
        }
        return out;
    }

}
