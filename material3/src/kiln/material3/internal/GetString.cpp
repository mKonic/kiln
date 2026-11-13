#include "kiln/material3/internal/GetString.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Configuration.h"

#include <algorithm>
#include <span>

namespace kiln::material3 {

    namespace internal {
        struct StringsEntry { const char* tag; const char* values[kStringsCount]; };
        extern const StringsEntry kStringsTable[];
        extern const int kStringsTableSize;
    }

    namespace {
        const internal::StringsEntry* Table(std::string_view tag) {
            for (int i = 0; i < internal::kStringsTableSize; ++i)
                if (tag == internal::kStringsTable[i].tag) return &internal::kStringsTable[i];
            return nullptr;
        }
    }

    namespace {
        struct LanguageScript { std::string language, script; };
        // The tag's language, and its script where the tag or its region tells (LocaleList's
        // getLikelyScript, for what the table holds: Chinese and Serbian differ by script).
        LanguageScript Split(std::string_view tag) {
            LanguageScript r;
            std::size_t start = 0;
            std::string region;
            for (int part = 0; start <= tag.size(); ++part) {
                const std::size_t end = std::min(tag.find('-', start), tag.size());
                const std::string_view sub = tag.substr(start, end - start);
                if (part == 0) r.language = std::string(sub);
                else if (sub.size() == 1) break;   // an extension
                else if (sub.size() == 4 && r.script.empty()) r.script = std::string(sub);
                else if ((sub.size() == 2 || sub.size() == 3) && region.empty()) region = std::string(sub);
                start = end + 1;
            }
            if (r.script.empty() && r.language == "zh") r.script = region == "TW" || region == "HK" || region == "MO" ? "Hant" : "Hans";
            if (r.script.empty() && r.language == "sr") r.script = "Cyrl";
            return r;
        }

        // LocaleList.matchesLanguageAndScript.
        bool Matches(const LanguageScript& supported, const LanguageScript& desired) {
            return supported.language == desired.language && supported.script == desired.script;
        }
    }

    // LocaleList.getFirstMatchWithEnglishSupported, as ResourcesImpl.updateConfiguration uses it:
    // one locale of the list is chosen for all resources, the first whose language and script
    // the table has, with English taken as always there (the default values). Strings then
    // resolve for that locale alone, so an English user whose second language is Japanese
    // reads the default English, not the Japanese values.
    std::string_view ResolveString(Strings id, const std::vector<std::string>& locales) {
        const int index = int(id);
        std::size_t best = 0;
        if (locales.size() > 1) {
            best = locales.size();
            for (std::size_t i = 0; i < locales.size() && best == locales.size(); ++i) {
                const LanguageScript desired = Split(locales[i]);
                if (desired.language == "en") { best = i; break; }
                for (int t = 0; t < internal::kStringsTableSize; ++t)
                    if (*internal::kStringsTable[t].tag && Matches(Split(internal::kStringsTable[t].tag), desired)) { best = i; break; }
            }
            if (best == locales.size()) best = 0;   // no match: the first locale
        }
        if (!locales.empty()) {
            for (const std::string& tag : text::ResourceLocaleCandidates(locales[best])) {
                if (tag.empty()) break;
                if (const auto* t = Table(tag); t && t->values[index]) return t->values[index];
            }
        }
        const auto* d = Table("");
        return d && d->values[index] ? d->values[index] : std::string_view();
    }

    std::string GetString(Strings id) {
        const auto& config = rt::Composition::Current()->Read(ui::LocalConfiguration);
        return std::string(ResolveString(id, config.locales));
    }

    std::string GetString(Strings id, std::initializer_list<text::FormatArg> formatArgs) {
        const auto& config = rt::Composition::Current()->Read(ui::LocalConfiguration);
        const std::string_view raw = ResolveString(id, config.locales);
        const std::string locale = config.locales.empty() ? std::string() : config.locales.front();
        return text::FormatJava(raw, std::span(formatArgs.begin(), formatArgs.size()), locale);
    }

    std::string GetString(Strings id, const std::vector<std::string>& locales) { return std::string(ResolveString(id, locales)); }

    std::string GetString(Strings id, const std::vector<std::string>& locales, std::initializer_list<text::FormatArg> formatArgs) {
        const std::string_view raw = ResolveString(id, locales);
        return text::FormatJava(raw, std::span(formatArgs.begin(), formatArgs.size()), locales.empty() ? std::string() : locales.front());
    }

    std::vector<std::string> CurrentLocales() {
        auto* c = rt::Composition::Current();
        return c ? c->Read(ui::LocalConfiguration).locales : std::vector<std::string>{};
    }

}
