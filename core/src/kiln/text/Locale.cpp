// java.util.Locale.forLanguageTag's splitting (language, script, region), for Compose's Locale.
#include "kiln/text/Locale.h"

#include "kiln/ui/PlatformServices.h"

#include <algorithm>
#include <cctype>

namespace kiln::text {

    namespace {
        bool Alpha(std::string_view s) { return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalpha(c); }); }
        bool Digits(std::string_view s) { return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }
        std::string Lower(std::string_view s) {
            std::string r(s);
            for (char& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
            return r;
        }
        std::string Upper(std::string_view s) {
            std::string r(s);
            for (char& c : r) c = char(std::toupper(static_cast<unsigned char>(c)));
            return r;
        }
    }

    Locale::Locale(std::string_view tag) {
        std::vector<std::string_view> parts;
        std::size_t start = 0;
        while (start <= tag.size()) {
            const std::size_t end = std::min(tag.find_first_of("-_", start), tag.size());
            parts.push_back(tag.substr(start, end - start));
            start = end + 1;
        }
        std::size_t i = 0;
        if (i < parts.size() && parts[i].size() >= 2 && parts[i].size() <= 8 && Alpha(parts[i])) m_Language = Lower(parts[i++]);
        else if (i < parts.size() && parts[i].empty()) ++i;
        if (i < parts.size() && parts[i].size() == 4 && Alpha(parts[i])) {
            m_Script = Lower(parts[i++]);
            m_Script[0] = char(std::toupper(static_cast<unsigned char>(m_Script[0])));
        }
        if (i < parts.size() && ((parts[i].size() == 2 && Alpha(parts[i])) || (parts[i].size() == 3 && Digits(parts[i])))) m_Region = Upper(parts[i++]);
        for (; i < parts.size(); ++i) {
            if (parts[i].empty()) continue;
            if (!m_Rest.empty()) m_Rest += '-';
            m_Rest += parts[i];
        }
        if (m_Language == "und") m_Language.clear();
    }

    Locale Locale::Current() { return Locale(ui::CurrentLocale()); }

    std::string Locale::ToLanguageTag() const {
        std::string out = m_Language.empty() ? "und" : m_Language;
        if (!m_Script.empty()) out += "-" + m_Script;
        if (!m_Region.empty()) out += "-" + m_Region;
        if (!m_Rest.empty()) out += "-" + m_Rest;
        return out;
    }

    LocaleList::LocaleList(std::string_view tags) {
        std::size_t start = 0;
        while (start <= tags.size()) {
            const std::size_t end = std::min(tags.find(',', start), tags.size());
            std::string_view t = tags.substr(start, end - start);
            while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
            while (!t.empty() && t.back() == ' ') t.remove_suffix(1);
            if (!t.empty()) m_Locales.emplace_back(t);
            start = end + 1;
        }
    }

    LocaleList LocaleList::Current() {
        std::vector<Locale> out;
        for (const std::string& t : ui::CurrentLocaleList()) out.emplace_back(t);
        return LocaleList(std::move(out));
    }

    bool LocaleList::Contains(const Locale& l) const { return std::find(m_Locales.begin(), m_Locales.end(), l) != m_Locales.end(); }
    bool LocaleList::ContainsAll(const std::vector<Locale>& ls) const {
        return std::all_of(ls.begin(), ls.end(), [&](const Locale& l) { return Contains(l); });
    }

    std::string LocaleList::ToLanguageTags() const {
        std::string out;
        for (const Locale& l : m_Locales) {
            if (!out.empty()) out += ",";
            out += l.ToLanguageTag();
        }
        return out;
    }

}
