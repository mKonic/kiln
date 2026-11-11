#pragma once

// Dates for the date pickers: proleptic Gregorian calendar arithmetic on UTC days, and a
// formatting service. On Android the host installs one backed by ICU and java.time (what
// Compose's CalendarModelImpl uses); elsewhere a built-in English (US) one answers.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::text {

    struct CivilDate {
        int year = 1970, month = 1, day = 1;   // month 1-12
        bool operator==(const CivilDate&) const = default;
    };

    inline constexpr std::int64_t MillisPerDay = 86'400'000;

    // Days since 1970-01-01 for a civil date, and back (H. Hinnant's algorithms).
    std::int64_t DaysFromCivil(int year, int month, int day);
    CivilDate CivilFromDays(std::int64_t days);
    inline std::int64_t UtcMillis(const CivilDate& d) { return DaysFromCivil(d.year, d.month, d.day) * MillisPerDay; }
    // The UTC day a millisecond timestamp falls on (floor, so before 1970 works too).
    CivilDate CivilFromUtcMillis(std::int64_t millis);
    bool IsLeapYear(int year);
    int DaysInMonth(int year, int month);
    // ISO day of week: Monday 1 .. Sunday 7.
    int IsoDayOfWeek(const CivilDate& d);

    class DateFormatBackend {
    public:
        virtual ~DateFormatBackend() = default;
        // The UTC day of `utcMillis` with the best pattern for a skeleton (UTS #35; field order is
        // the locale's), capitalised for standalone use.
        virtual std::string FormatSkeleton(std::int64_t utcMillis, std::string_view skeleton, std::string_view locale) = 0;
        // With a pattern as DateTimeFormatter.ofPattern takes it, in the locale's digits.
        virtual std::string FormatPattern(std::int64_t utcMillis, std::string_view pattern, std::string_view locale) = 0;
        // LocalDate.parse(text, ofPattern(pattern)): nullopt when it does not parse.
        virtual std::optional<CivilDate> Parse(std::string_view text, std::string_view pattern, std::string_view locale) = 0;
        // The locale's SHORT date pattern (DateTimeFormatterBuilder.getLocalizedDateTimePattern).
        virtual std::string ShortDatePattern(std::string_view locale) = 0;
        // WeekFields.of(locale).firstDayOfWeek: ISO, Monday 1 .. Sunday 7.
        virtual int FirstDayOfWeek(std::string_view locale) = 0;
        // Monday first: the full standalone name and the narrow one.
        virtual std::vector<std::pair<std::string, std::string>> WeekdayNames(std::string_view locale) = 0;
        // LocalDate.now(): today in the system time zone.
        virtual CivilDate Today() = 0;
    };

    // The installed backend (the built-in one unless the host set another).
    DateFormatBackend& DateFormat();
    // Installs a backend; null restores the built-in one. Not owned.
    void SetDateFormat(DateFormatBackend* backend);
    // English (US) names and patterns, Sunday-first weeks for US-style regions, the system clock.
    DateFormatBackend& BuiltInDateFormat();

}
