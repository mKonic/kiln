#include "kiln/material3/internal/CalendarModel.h"

#include <algorithm>

namespace kiln::material3::internal {

    namespace {
        CalendarDate DateOf(const text::CivilDate& c) { return { c.year, c.month, c.day, text::UtcMillis(c) }; }
    }

    std::string DateInputFormat::PatternWithoutDelimiters() const {
        std::string s;
        for (char c : patternWithDelimiters) if (c != delimiter) s += c;
        return s;
    }

    // datePatternAsInputFormat: keep d, M, y and the delimiters; two-digit day and month, four-
    // digit year.
    DateInputFormat DatePatternAsInputFormat(std::string_view localeFormat) {
        std::string kept;
        for (char c : localeFormat) if (c == 'd' || c == 'M' || c == 'y' || c == '/' || c == '-' || c == '.') kept += c;
        std::string out;
        for (std::size_t i = 0; i < kept.size();) {
            const char c = kept[i];
            std::size_t n = 1;
            while (i + n < kept.size() && kept[i + n] == c) ++n;
            if (c == 'd' || c == 'M') {
                // d{1,2} / M{1,2}: each run of up to two becomes two.
                for (std::size_t k = 0; k < n; k += 2) out += std::string(2, c);
            } else if (c == 'y') {
                for (std::size_t k = 0; k < n; k += 4) out += "yyyy";
            } else {
                out.append(n, c);
            }
            i += n;
        }
        // "My" -> "M/y" (the Kako locale).
        for (std::size_t p; (p = out.find("My")) != std::string::npos;) out.insert(p + 1, "/");
        if (!out.empty() && out.back() == '.') out.pop_back();
        const auto d = out.find_first_of("/-.");
        return { out, d == std::string::npos ? '/' : out[d] };
    }

    CalendarModel::CalendarModel(std::string locale) : m_Locale(std::move(locale)) {
        auto& df = text::DateFormat();
        m_FirstDayOfWeek = df.FirstDayOfWeek(m_Locale);
        m_WeekdayNames = df.WeekdayNames(m_Locale);
    }

    CalendarDate CalendarModel::Today() const { return DateOf(text::DateFormat().Today()); }

    DateInputFormat CalendarModel::GetDateInputFormat() const {
        return DatePatternAsInputFormat(text::DateFormat().ShortDatePattern(m_Locale));
    }

    CalendarDate CalendarModel::GetCanonicalDate(std::int64_t ms) const { return DateOf(text::CivilFromUtcMillis(ms)); }

    CalendarMonth CalendarModel::GetMonth(std::int64_t ms) const {
        const auto c = text::CivilFromUtcMillis(ms);
        return GetMonth(c.year, c.month);
    }

    CalendarMonth CalendarModel::GetMonth(int year, int month) const {
        const text::CivilDate first{ year, month, 1 };
        int difference = text::IsoDayOfWeek(first) - m_FirstDayOfWeek;
        if (difference < 0) difference += DaysInWeek;
        return { year, month, text::DaysInMonth(year, month), difference, text::UtcMillis(first) };
    }

    int CalendarModel::GetDayOfWeek(const CalendarDate& d) const { return text::IsoDayOfWeek({ d.year, d.month, d.dayOfMonth }); }

    CalendarMonth CalendarModel::PlusMonths(const CalendarMonth& from, int count) const {
        if (count <= 0) return from;
        const int index = from.year * 12 + (from.month - 1) + count;
        return GetMonth(index / 12, index % 12 + 1);
    }

    CalendarMonth CalendarModel::MinusMonths(const CalendarMonth& from, int count) const {
        if (count <= 0) return from;
        const int index = from.year * 12 + (from.month - 1) - count;
        const int year = index >= 0 ? index / 12 : (index - 11) / 12;
        return GetMonth(year, index - year * 12 + 1);
    }

    std::string CalendarModel::FormatWithSkeleton(std::int64_t ms, std::string_view skeleton) const {
        return text::DateFormat().FormatSkeleton(ms, skeleton, m_Locale);
    }

    std::string CalendarModel::FormatWithPattern(std::int64_t ms, std::string_view pattern) const {
        return text::DateFormat().FormatPattern(ms, pattern, m_Locale);
    }

    std::optional<CalendarDate> CalendarModel::Parse(std::string_view date, std::string_view pattern) const {
        const auto c = text::DateFormat().Parse(date, pattern, m_Locale);
        if (!c) return std::nullopt;
        return DateOf(*c);
    }

}
