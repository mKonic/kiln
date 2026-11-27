#pragma once

// Locale and LocaleList (androidx.compose.ui.text.intl): a BCP-47 tag split as java.util.Locale
// forLanguageTag does (language, script, region), and a list of them (a SpanStyle's localeList
// is the list's tags joined by commas).

#include <string>
#include <string_view>
#include <vector>

namespace kiln::text {

    class Locale {
    public:
        explicit Locale(std::string_view languageTag);
        // Locale.current: the system's first locale.
        static Locale Current();

        const std::string& Language() const { return m_Language; }   // "en"; "" when not given
        const std::string& Script() const { return m_Script; }       // "Hans"
        const std::string& Region() const { return m_Region; }       // "US", "419"
        // The well-formed tag ("und" without a language), extensions and variants kept.
        std::string ToLanguageTag() const;
        bool operator==(const Locale& o) const { return ToLanguageTag() == o.ToLanguageTag(); }

    private:
        std::string m_Language, m_Script, m_Region, m_Rest;
    };

    class LocaleList {
    public:
        LocaleList() = default;
        explicit LocaleList(std::vector<Locale> locales) : m_Locales(std::move(locales)) {}
        // LocaleList(languageTags): comma separated tags.
        explicit LocaleList(std::string_view languageTags);
        // LocaleList.current: the system's locales.
        static LocaleList Current();

        const std::vector<Locale>& Locales() const { return m_Locales; }
        const Locale& operator[](std::size_t i) const { return m_Locales[i]; }
        std::size_t Size() const { return m_Locales.size(); }
        bool IsEmpty() const { return m_Locales.empty(); }
        bool Contains(const Locale& l) const;
        bool ContainsAll(const std::vector<Locale>& ls) const;
        auto begin() const { return m_Locales.begin(); }
        auto end() const { return m_Locales.end(); }
        // The tags joined by commas (SpanStyle::localeList).
        std::string ToLanguageTags() const;
        bool operator==(const LocaleList&) const = default;

    private:
        std::vector<Locale> m_Locales;
    };

}
