#include "Check.h"

// CalendarModel (CalendarModelTest.kt) over the built-in date service, and the civil-date math.

#include "kiln/material3/internal/CalendarModel.h"

using namespace kiln;
using namespace kiln::material3::internal;

namespace {
    constexpr std::int64_t January2022Millis = 1640995200000;
}

TEST(CivilDateArithmetic) {
    CHECK(text::DaysFromCivil(1970, 1, 1) == 0);
    CHECK(text::UtcMillis({ 2022, 1, 1 }) == January2022Millis);
    CHECK((text::CivilFromDays(text::DaysFromCivil(2000, 2, 29)) == text::CivilDate{ 2000, 2, 29 }));
    CHECK((text::CivilFromUtcMillis(-1) == text::CivilDate{ 1969, 12, 31 }));
    CHECK(text::DaysInMonth(2024, 2) == 29 && text::DaysInMonth(1900, 2) == 28 && text::DaysInMonth(2000, 2) == 29);
    CHECK(text::IsoDayOfWeek({ 2022, 1, 1 }) == 6);   // a Saturday
    CHECK(text::IsoDayOfWeek({ 1900, 1, 1 }) == 1);   // a Monday
}

TEST(CalendarModelDatesAndMonths) {
    const CalendarModel model("en-US");
    const CalendarDate date = model.GetCanonicalDate(January2022Millis);
    CHECK((date == CalendarDate{ 2022, 1, 1, January2022Millis }));
    // Rounded down to the start of the day.
    CHECK((model.GetCanonicalDate(January2022Millis + 30000) == date));
    CHECK((model.GetMonth(date) == model.GetMonth(January2022Millis)));
    CHECK((model.GetMonth(date) == model.GetMonth(2022, 1)));
    CHECK((model.GetMonth(January2022Millis + 10000) == model.GetMonth(date)));
    const CalendarMonth december = model.GetMonth(1999, 12);
    CHECK((model.GetMonth(december.startUtcTimeMillis) == december));
    const CalendarMonth january = model.GetMonth(January2022Millis);
    const CalendarMonth plus = model.PlusMonths(january, 1);
    CHECK((plus == model.GetMonth(january.EndUtcTimeMillis() + 1)));
    CHECK((model.MinusMonths(plus, 1) == january));
    CHECK((model.MinusMonths(january, 13) == model.GetMonth(2020, 12)));
    // January 2022 starts on a Saturday: six days after a Sunday week start.
    CHECK(model.FirstDayOfWeek() == 7 && january.daysFromStartOfWeekToFirstOfMonth == 6 && january.numberOfDays == 31);
    CHECK(CalendarModel("en-GB").GetMonth(2022, 1).daysFromStartOfWeekToFirstOfMonth == 5);
    CHECK(model.GetDayOfWeek(date) == 6);
}

TEST(CalendarModelParsesAndFormats) {
    const CalendarModel model("en-US");
    CHECK((model.Parse("1/1/2022", "M/d/yyyy") == CalendarDate{ 2022, 1, 1, January2022Millis }));
    CHECK((model.Parse("01012022", "MMddyyyy") == CalendarDate{ 2022, 1, 1, January2022Millis }));
    CHECK(!model.Parse("99272030", "MMddyyyy"));
    CHECK(!model.Parse("0101202", "MMddyyyy"));
    // A day past the month's end resolves to its last day (ResolverStyle.SMART).
    CHECK((model.Parse("02302023", "MMddyyyy") == model.GetCanonicalDate(text::UtcMillis({ 2023, 2, 28 }))));

    const CalendarDate date{ 2022, 1, 1, January2022Millis };
    CHECK(model.FormatWithSkeleton(date, "yMMMd") == "Jan 1, 2022");
    CHECK(model.FormatWithSkeleton(date, "dMMMy") == "Jan 1, 2022");
    CHECK(model.FormatWithSkeleton(date, "yMMMMEEEEd") == "Saturday, January 1, 2022");
    CHECK(model.FormatWithSkeleton(model.GetMonth(2022, 3), "yMMMM") == "March 2022");
    CHECK(model.FormatWithSkeleton(model.GetMonth(2022, 3), "MMMMy") == "March 2022");
    CHECK(model.FormatWithSkeleton(January2022Millis - 1000000000, "yMMMMEEEEd") == "Monday, December 20, 2021");
    CHECK(model.FormatWithSkeleton(January2022Millis, "YY") == "22");
    CHECK(model.FormatWithPattern(January2022Millis, "MM/dd/yyyy") == "01/01/2022");

    const auto& names = model.WeekdayNames();
    CHECK(names.size() == 7 && names.front().first == "Monday");
    for (const auto& [full, narrow] : names) CHECK(!narrow.empty() && narrow[0] == full[0]);
}

TEST(DateInputFormatFromTheLocalesShortPattern) {
    // The SHORT patterns java.time gives for en-US, zh-CN, en-GB, ko-KR and es-CL.
    const auto check = [](std::string_view shortPattern, std::string_view with, std::string_view without, char delimiter) {
        const DateInputFormat f = DatePatternAsInputFormat(shortPattern);
        return f.patternWithDelimiters == with && f.PatternWithoutDelimiters() == without && f.delimiter == delimiter;
    };
    CHECK(check("M/d/yy", "MM/dd/yyyy", "MMddyyyy", '/'));
    CHECK(check("y/M/d", "yyyy/MM/dd", "yyyyMMdd", '/'));
    CHECK(check("dd/MM/y", "dd/MM/yyyy", "ddMMyyyy", '/'));
    CHECK(check("yy. M. d.", "yyyy.MM.dd", "yyyyMMdd", '.'));
    CHECK(check("dd-MM-yy", "dd-MM-yyyy", "ddMMyyyy", '-'));
    CHECK(CalendarModel("en-US").GetDateInputFormat().patternWithDelimiters == "MM/dd/yyyy");
}
