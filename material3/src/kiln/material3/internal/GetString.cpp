#include "kiln/material3/internal/GetString.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Configuration.h"

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

    std::string_view ResolveString(Strings id, const std::vector<std::string>& locales) {
        const int index = int(id);
        for (const std::string& locale : locales) {
            for (const std::string& tag : text::ResourceLocaleCandidates(locale)) {
                if (tag.empty()) break;   // the default comes after every locale
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
