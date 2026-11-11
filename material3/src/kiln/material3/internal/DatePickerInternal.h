#pragma once

// Shared by DatePicker.cpp and DateRangePicker.cpp (DatePicker.kt / DateInput.kt internals).

#include "kiln/foundation/Lazy.h"
#include "kiln/material3/DatePicker.h"

namespace kiln::material3::internal {

    inline constexpr ui::Dp RecommendedSizeForAccessibility{ 48 }, MonthYearHeight{ 56 }, DatePickerHorizontalPadding{ 12 };
    inline constexpr int MaxCalendarRows = 6, YearsInRow = 3;

    // The top area (title, headline and mode toggle over a divider) above `content`. The range
    // picker passes focus targets that sit before and after the content.
    void DateEntryContainer(const ui::Modifier& modifier, std::function<void()> title, std::function<void()> headline,
                            std::function<void()> modeToggleButton, const DatePickerColors& colors, const text::TextStyle& headlineTextStyle,
                            ui::Dp headerMinHeight, std::function<void()> content, ui::FocusRequester* topFocusTarget = nullptr,
                            ui::FocusRequester* bottomFocusTarget = nullptr);
    void DisplayModeToggleButton(const ui::Modifier& modifier, DisplayMode displayMode, std::function<void(DisplayMode)> onDisplayModeChange,
                                 const DatePickerColors& colors);
    void WeekDays(const DatePickerColors& colors, const CalendarModel& calendarModel);

    // A range selection's drawing info: the first and last cells (column, row) of the range
    // within a month, and whether the range starts before / ends after it.
    struct SelectedRangeInfo {
        int startColumn = 0, startRow = 0, endColumn = 0, endRow = 0;
        bool firstIsSelectionStart = false, lastIsSelectionEnd = false;
        bool operator==(const SelectedRangeInfo&) const = default;
    };

    // SelectedRangeInfo.calculateRangeInfo: nullopt when the range misses the month.
    std::optional<SelectedRangeInfo> CalculateRangeInfo(const CalendarMonth& month, const CalendarDate& start, const CalendarDate& end);

    // A month grid. endDateMillis and rangeSelectionInfo are the range picker's; with a
    // focus owner (the single date picker) the arrow keys move between days and months.
    void Month(const CalendarMonth& month, std::function<void(std::int64_t)> onDateSelectionChange, std::int64_t todayMillis,
               std::optional<std::int64_t> startDateMillis, std::optional<std::int64_t> endDateMillis,
               std::optional<SelectedRangeInfo> rangeSelectionInfo, const DatePickerFormatterPtr& dateFormatter,
               const SelectableDatesPtr& selectableDates, const DatePickerColors& colors, const std::string& locale,
               foundation::LazyListState& lazyListState, bool keyboardNavigation, std::function<void()> onReturnFocus);

    // Reports the month at the list's first visible item whenever it changes.
    void UpdateDisplayedMonth(foundation::LazyListState& state, std::function<void(std::int64_t)> onDisplayedMonthChange,
                              const CalendarModel& calendarModel, const IntRange& yearRange);

    enum class InputIdentifier : std::uint8_t { SingleDateInput, StartDateInput, EndDateInput };

    class DateInputValidator {
    public:
        DateInputValidator(IntRange yearRange, SelectableDatesPtr selectableDates, DateInputFormat dateInputFormat,
                           DatePickerFormatterPtr dateFormatter, std::string errorDatePattern, std::string errorDateOutOfYearRange,
                           std::string errorInvalidNotAllowed, std::string errorInvalidRangeInput);
        std::optional<std::int64_t> currentStartDateMillis, currentEndDateMillis;
        // An error text, or empty when the date is fine.
        std::string Validate(const std::optional<CalendarDate>& date, InputIdentifier inputIdentifier, const std::string& locale) const;

    private:
        IntRange m_YearRange;
        SelectableDatesPtr m_SelectableDates;
        DateInputFormat m_Format;
        DatePickerFormatterPtr m_Formatter;
        std::string m_ErrorDatePattern, m_ErrorOutOfYearRange, m_ErrorNotAllowed, m_ErrorInvalidRange;
    };

    // The outlined date field: digits only, delimiters shown as typed, validated once complete.
    void DateInputTextField(const ui::Modifier& modifier, std::optional<std::int64_t> initialDateMillis,
                            std::function<void(std::optional<std::int64_t>)> onDateSelectionChange, const CalendarModel& calendarModel,
                            std::function<void()> label, std::function<void()> placeholder, InputIdentifier inputIdentifier,
                            std::shared_ptr<DateInputValidator> dateInputValidator, const DateInputFormat& dateInputFormat,
                            const std::string& locale, const DatePickerColors& colors, ui::FocusRequester* focusRequester);

    inline constexpr ui::Dp InputTextFieldPaddingStart{ 24 }, InputTextFieldPaddingEnd{ 24 }, InputTextFieldPaddingTop{ 10 };
}
