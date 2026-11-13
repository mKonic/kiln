#pragma once

// DatePicker.kt / DateInput.kt: a calendar of months to pick a date from (paging sideways, with a
// year grid), or a text field to type one in the locale's short date format; the header shows
// the selection. Dates are the start of their day in UTC milliseconds.

#include "kiln/foundation/Dialog.h"
#include <kiln/Function.h>
#include "kiln/material3/TextField.h"
#include "kiln/material3/internal/CalendarModel.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kiln::material3 {

    enum class DisplayMode : std::uint8_t { Picker, Input };

    struct IntRange {
        int first = 1900, last = 2100;
        bool Contains(int v) const { return v >= first && v <= last; }
        int Count() const { return last - first + 1; }
        bool operator==(const IntRange&) const = default;
    };

    // SelectableDates: which dates and years may be picked (others show disabled).
    class SelectableDates {
    public:
        virtual ~SelectableDates() = default;
        virtual bool IsSelectableDate(std::int64_t utcTimeMillis) const { (void)utcTimeMillis; return true; }
        virtual bool IsSelectableYear(int year) const { (void)year; return true; }
    };
    using SelectableDatesPtr = std::shared_ptr<const SelectableDates>;
    // A SelectableDates from two functions (either may be null: allowed).
    SelectableDatesPtr MakeSelectableDates(kiln::Function<bool(std::int64_t)> isSelectableDate,
                                           kiln::Function<bool(int)> isSelectableYear = {});

    // DatePickerFormatter: the month-and-year and date texts.
    class DatePickerFormatter {
    public:
        virtual ~DatePickerFormatter() = default;
        virtual std::optional<std::string> FormatMonthYear(std::optional<std::int64_t> monthMillis, const std::string& locale) const = 0;
        virtual std::optional<std::string> FormatDate(std::optional<std::int64_t> dateMillis, const std::string& locale,
                                                      bool forContentDescription = false) const = 0;
    };
    using DatePickerFormatterPtr = std::shared_ptr<const DatePickerFormatter>;

    struct DatePickerColors {
        ui::Color containerColor, titleContentColor, headlineContentColor, weekdayContentColor, subheadContentColor,
            navigationContentColor, yearContentColor, disabledYearContentColor, currentYearContentColor, selectedYearContentColor,
            disabledSelectedYearContentColor, selectedYearContainerColor, disabledSelectedYearContainerColor, dayContentColor,
            disabledDayContentColor, selectedDayContentColor, disabledSelectedDayContentColor, selectedDayContainerColor,
            disabledSelectedDayContainerColor, todayContentColor, todayDateBorderColor, dayInSelectionRangeContainerColor,
            dayInSelectionRangeContentColor, dividerColor;
        TextFieldColors dateTextFieldColors;
        bool operator==(const DatePickerColors&) const = default;
    };

    // The state of a date picker: the selection, the month shown, the mode. Dates outside the
    // year range are not selected (and months outside it not shown).
    class DatePickerState {
    public:
        DatePickerState(std::string locale, std::optional<std::int64_t> initialSelectedDateMillis = std::nullopt,
                        std::optional<std::int64_t> initialDisplayedMonthMillis = std::nullopt, IntRange yearRange = {},
                        DisplayMode initialDisplayMode = DisplayMode::Picker, SelectableDatesPtr selectableDates = nullptr);
        DatePickerState(const DatePickerState&) = delete;
        DatePickerState& operator=(const DatePickerState&) = delete;

        std::optional<std::int64_t> SelectedDateMillis() const;
        void SetSelectedDateMillis(std::optional<std::int64_t> dateMillis);
        std::int64_t DisplayedMonthMillis() const { return m_DisplayedMonth.Get().startUtcTimeMillis; }
        void SetDisplayedMonthMillis(std::int64_t monthMillis);
        DisplayMode GetDisplayMode() const { return m_DisplayMode.Get(); }
        // Switching mode shows the selected date's month.
        void SetDisplayMode(DisplayMode mode);
        const IntRange& YearRange() const { return m_YearRange; }
        SelectableDatesPtr GetSelectableDates() const { return m_SelectableDates.Get(); }
        void SetSelectableDates(SelectableDatesPtr s) { if (m_SelectableDates.Peek() != s) m_SelectableDates.Set(std::move(s)); }
        const std::string& Locale() const { return m_Calendar.Locale(); }
        const internal::CalendarModel& Calendar() const { return m_Calendar; }

    private:
        internal::CalendarModel m_Calendar;
        IntRange m_YearRange;
        rt::State<SelectableDatesPtr> m_SelectableDates;
        rt::State<internal::CalendarMonth> m_DisplayedMonth;
        rt::State<std::optional<internal::CalendarDate>> m_Selected;
        rt::State<DisplayMode> m_DisplayMode;
    };

    // rememberDatePickerState: the platform locale; the selection, month, range and mode survive
    // re-creation. The displayed month defaults to the selected date's.
    std::shared_ptr<DatePickerState> RememberDatePickerState(std::optional<std::int64_t> initialSelectedDateMillis = std::nullopt,
                                                             std::optional<std::int64_t> initialDisplayedMonthMillis = std::nullopt,
                                                             IntRange yearRange = {}, DisplayMode initialDisplayMode = DisplayMode::Picker,
                                                             SelectableDatesPtr selectableDates = nullptr, rt::Key key = {});

    namespace DatePickerDefaults {
        DatePickerColors colors();
        inline constexpr IntRange YearRange{ 1900, 2100 };
        inline constexpr ui::Dp TonalElevation{ 0 };
        foundation::ShapePtr shape();
        SelectableDatesPtr AllDates();
        inline constexpr const char* YearMonthSkeleton = "yMMMM";
        inline constexpr const char* YearAbbrMonthDaySkeleton = "yMMMd";
        inline constexpr const char* YearMonthWeekdayDaySkeleton = "yMMMMEEEEd";
        // Formats with the best localized form of each skeleton.
        DatePickerFormatterPtr dateFormatter(std::string yearSelectionSkeleton = YearMonthSkeleton,
                                             std::string selectedDateSkeleton = YearAbbrMonthDaySkeleton,
                                             std::string selectedDateDescriptionSkeleton = YearMonthWeekdayDaySkeleton);
        // "Select date" / "Enter date".
        void DatePickerTitle(DisplayMode displayMode, const ui::Modifier& modifier = {}, std::optional<ui::Color> contentColor = std::nullopt);
        // The selected date, or a prompt when there is none (announced politely as it changes).
        void DatePickerHeadline(std::optional<std::int64_t> selectedDateMillis, DisplayMode displayMode, const DatePickerFormatterPtr& dateFormatter,
                                const ui::Modifier& modifier = {}, std::optional<ui::Color> contentColor = std::nullopt);
    }

    struct DatePickerOptions {
        DatePickerFormatterPtr dateFormatter;        // null: DatePickerDefaults::dateFormatter()
        std::optional<DatePickerColors> colors;      // unset: DatePickerDefaults::colors()
        // Unset: the defaults; an empty function: none.
        std::optional<kiln::Function<void()>> title, headline;
        bool showModeToggle = true;
        // Focused when the input mode shows; unset: remembered; null: none.
        std::optional<ui::FocusRequester*> focusRequester;
    };
    void DatePicker(DatePickerState& state, const ui::Modifier& modifier = {}, const DatePickerOptions& options = {});

    // DatePickerDialog: a dialog holding a picker (content) above its dismiss and confirm
    // buttons; the buttons stay in view on small screens. onDismissRequest: a click outside or
    // Back (not the dismiss button).
    struct DatePickerDialogOptions {
        kiln::Function<void()> dismissButton;
        foundation::ShapePtr shape;                  // null: DatePickerDefaults::shape()
        ui::Dp tonalElevation = DatePickerDefaults::TonalElevation;
        std::optional<DatePickerColors> colors;
        foundation::DialogProperties properties{ .usePlatformDefaultWidth = false };
    };
    void DatePickerDialog(kiln::Function<void()> onDismissRequest, kiln::Function<void()> confirmButton, const ui::Modifier& modifier,
                          const DatePickerDialogOptions& options, kiln::Function<void()> content);

    // Shared with the range picker.
    namespace internal {
        int NumberOfMonthsInRange(const IntRange& yearRange);
        // Int.toLocalString: in the locale's digits, at least minDigits long.
        std::string ToLocalString(int value, const std::string& locale, int minDigits = 1);
    }

}
