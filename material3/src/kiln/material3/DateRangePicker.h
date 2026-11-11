#pragma once

// DateRangePicker.kt / DateRangeInput.kt: a start and end date picked from a vertical list of
// months (the days between highlighted), or typed into two fields.

#include "kiln/material3/DatePicker.h"

namespace kiln::material3 {

    class DateRangePickerState {
    public:
        DateRangePickerState(std::string locale, std::optional<std::int64_t> initialSelectedStartDateMillis = std::nullopt,
                             std::optional<std::int64_t> initialSelectedEndDateMillis = std::nullopt,
                             std::optional<std::int64_t> initialDisplayedMonthMillis = std::nullopt, IntRange yearRange = {},
                             DisplayMode initialDisplayMode = DisplayMode::Picker, SelectableDatesPtr selectableDates = nullptr);
        DateRangePickerState(const DateRangePickerState&) = delete;
        DateRangePickerState& operator=(const DateRangePickerState&) = delete;

        std::optional<std::int64_t> SelectedStartDateMillis() const;
        std::optional<std::int64_t> SelectedEndDateMillis() const;
        // A start without an end is allowed; an end needs a start on or before it, else both
        // clear. Dates outside the year range count as none.
        void SetSelection(std::optional<std::int64_t> startDateMillis, std::optional<std::int64_t> endDateMillis);
        std::int64_t DisplayedMonthMillis() const { return m_DisplayedMonth.Get().startUtcTimeMillis; }
        void SetDisplayedMonthMillis(std::int64_t monthMillis);
        DisplayMode GetDisplayMode() const { return m_DisplayMode.Get(); }
        void SetDisplayMode(DisplayMode mode);   // shows the start date's month
        const IntRange& YearRange() const { return m_YearRange; }
        SelectableDatesPtr GetSelectableDates() const { return m_SelectableDates.Get(); }
        void SetSelectableDates(SelectableDatesPtr s) { if (m_SelectableDates.Peek() != s) m_SelectableDates.Set(std::move(s)); }
        const std::string& Locale() const { return m_Calendar.Locale(); }
        const internal::CalendarModel& Calendar() const { return m_Calendar; }

    private:
        std::optional<internal::CalendarDate> DateIn(std::optional<std::int64_t> millis) const;
        internal::CalendarModel m_Calendar;
        IntRange m_YearRange;
        rt::State<SelectableDatesPtr> m_SelectableDates;
        rt::State<internal::CalendarMonth> m_DisplayedMonth;
        rt::State<std::optional<internal::CalendarDate>> m_Start, m_End;
        rt::State<DisplayMode> m_DisplayMode;
    };

    // rememberDateRangePickerState: the displayed month defaults to the start date's.
    std::shared_ptr<DateRangePickerState> RememberDateRangePickerState(std::optional<std::int64_t> initialSelectedStartDateMillis = std::nullopt,
                                                                       std::optional<std::int64_t> initialSelectedEndDateMillis = std::nullopt,
                                                                       std::optional<std::int64_t> initialDisplayedMonthMillis = std::nullopt,
                                                                       IntRange yearRange = {}, DisplayMode initialDisplayMode = DisplayMode::Picker,
                                                                       SelectableDatesPtr selectableDates = nullptr, rt::Key key = {});

    namespace DateRangePickerDefaults {
        // "Select dates" / "Enter dates".
        void DateRangePickerTitle(DisplayMode displayMode, const ui::Modifier& modifier = {}, std::optional<ui::Color> contentColor = std::nullopt);
        // "Start date - End date", the dates as they are picked.
        void DateRangePickerHeadline(std::optional<std::int64_t> selectedStartDateMillis, std::optional<std::int64_t> selectedEndDateMillis,
                                     DisplayMode displayMode, const DatePickerFormatterPtr& dateFormatter, const ui::Modifier& modifier = {},
                                     std::optional<ui::Color> contentColor = std::nullopt);
    }

    void DateRangePicker(DateRangePickerState& state, const ui::Modifier& modifier = {}, const DatePickerOptions& options = {});

}
