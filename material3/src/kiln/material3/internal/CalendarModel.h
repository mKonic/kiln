#pragma once

// CalendarModel (internal/CalendarModel.kt, CalendarModelImpl.android.kt): the date pickers'
// calendar over kiln::text's date service, in UTC days.

#include "kiln/text/DateFormat.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kiln::material3::internal {

    inline constexpr int DaysInWeek = 7;
    inline constexpr std::int64_t MillisecondsIn24Hours = text::MillisPerDay;

    struct CalendarDate {
        int year = 1970, month = 1, dayOfMonth = 1;
        std::int64_t utcTimeMillis = 0;
        bool operator==(const CalendarDate&) const = default;
        auto operator<=>(const CalendarDate& o) const { return utcTimeMillis <=> o.utcTimeMillis; }
    };

    struct CalendarMonth {
        int year = 1970, month = 1;   // month 1-12
        int numberOfDays = 31;
        int daysFromStartOfWeekToFirstOfMonth = 0;
        std::int64_t startUtcTimeMillis = 0;
        // The last millisecond of the month's last day.
        std::int64_t EndUtcTimeMillis() const { return startUtcTimeMillis + std::int64_t(numberOfDays) * MillisecondsIn24Hours - 1; }
        // The month's position in a year range (first..last).
        int IndexIn(int firstYear) const { return (year - firstYear) * 12 + month - 1; }
        bool operator==(const CalendarMonth&) const = default;
    };

    // The locale's short date pattern as two-digit day and month and four-digit year, with its
    // delimiter (e.g. MM/dd/yyyy).
    struct DateInputFormat {
        std::string patternWithDelimiters;
        char delimiter = '/';
        std::string PatternWithoutDelimiters() const;
        bool operator==(const DateInputFormat&) const = default;
    };
    DateInputFormat DatePatternAsInputFormat(std::string_view localeFormat);

    class CalendarModel {
    public:
        explicit CalendarModel(std::string locale);
        // Pickers hold pointers to their state's model (copies in lambdas would dangle).
        CalendarModel(const CalendarModel&) = delete;
        CalendarModel& operator=(const CalendarModel&) = delete;
        const std::string& Locale() const { return m_Locale; }

        CalendarDate Today() const;
        // ISO: Monday 1 .. Sunday 7.
        int FirstDayOfWeek() const { return m_FirstDayOfWeek; }
        // Monday first: full and short names.
        const std::vector<std::pair<std::string, std::string>>& WeekdayNames() const { return m_WeekdayNames; }
        DateInputFormat GetDateInputFormat() const;
        // The start of the UTC day of a timestamp.
        CalendarDate GetCanonicalDate(std::int64_t timeInMillis) const;
        CalendarMonth GetMonth(std::int64_t timeInMillis) const;
        CalendarMonth GetMonth(const CalendarDate& date) const { return GetMonth(date.year, date.month); }
        CalendarMonth GetMonth(int year, int month) const;
        int GetDayOfWeek(const CalendarDate& date) const;
        CalendarMonth PlusMonths(const CalendarMonth& from, int count) const;
        CalendarMonth MinusMonths(const CalendarMonth& from, int count) const;
        std::string FormatWithSkeleton(std::int64_t utcTimeMillis, std::string_view skeleton) const;
        std::string FormatWithSkeleton(const CalendarMonth& m, std::string_view skeleton) const { return FormatWithSkeleton(m.startUtcTimeMillis, skeleton); }
        std::string FormatWithSkeleton(const CalendarDate& d, std::string_view skeleton) const { return FormatWithSkeleton(d.utcTimeMillis, skeleton); }
        std::string FormatWithPattern(std::int64_t utcTimeMillis, std::string_view pattern) const;
        std::optional<CalendarDate> Parse(std::string_view date, std::string_view pattern) const;

    private:
        std::string m_Locale;
        int m_FirstDayOfWeek = 7;
        std::vector<std::pair<std::string, std::string>> m_WeekdayNames;
    };

}
