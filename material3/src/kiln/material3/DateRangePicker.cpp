#include "kiln/material3/DateRangePicker.h"

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/DatePickerInternal.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/DatePickerModalTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cctype>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    namespace mi = material3::internal;

    // --- state ----------------------------------------------------------------------------------

    DateRangePickerState::DateRangePickerState(std::string locale, std::optional<std::int64_t> start, std::optional<std::int64_t> end,
                                               std::optional<std::int64_t> displayed, IntRange yearRange, DisplayMode mode,
                                               SelectableDatesPtr selectable)
        : m_Calendar(std::move(locale)), m_YearRange(yearRange),
          m_SelectableDates(selectable ? std::move(selectable) : DatePickerDefaults::AllDates()),
          m_DisplayedMonth([&] {
              if (displayed) {
                  const auto month = m_Calendar.GetMonth(*displayed);
                  if (yearRange.Contains(month.year)) return month;
              }
              return m_Calendar.GetMonth(m_Calendar.Today());
          }()),
          m_Start(std::nullopt), m_End(std::nullopt), m_DisplayMode(mode) {
        SetSelection(start, end);
    }

    std::optional<mi::CalendarDate> DateRangePickerState::DateIn(std::optional<std::int64_t> millis) const {
        if (!millis) return std::nullopt;
        const auto date = m_Calendar.GetCanonicalDate(*millis);
        if (!m_YearRange.Contains(date.year)) return std::nullopt;
        return date;
    }

    std::optional<std::int64_t> DateRangePickerState::SelectedStartDateMillis() const {
        const auto& d = m_Start.Get();
        return d ? std::optional<std::int64_t>(d->utcTimeMillis) : std::nullopt;
    }
    std::optional<std::int64_t> DateRangePickerState::SelectedEndDateMillis() const {
        const auto& d = m_End.Get();
        return d ? std::optional<std::int64_t>(d->utcTimeMillis) : std::nullopt;
    }

    void DateRangePickerState::SetSelection(std::optional<std::int64_t> startMillis, std::optional<std::int64_t> endMillis) {
        auto start = DateIn(startMillis), end = DateIn(endMillis);
        // No end without a start, and no end before the start.
        if (!(start && (!end || start->utcTimeMillis <= end->utcTimeMillis))) { start.reset(); end.reset(); }
        if (m_Start.Peek() != start) m_Start.Set(start);
        if (m_End.Peek() != end) m_End.Set(end);
    }

    void DateRangePickerState::SetDisplayedMonthMillis(std::int64_t millis) {
        const auto month = m_Calendar.GetMonth(millis);
        if (m_YearRange.Contains(month.year) && !(m_DisplayedMonth.Peek() == month)) m_DisplayedMonth.Set(month);
    }

    void DateRangePickerState::SetDisplayMode(DisplayMode mode) {
        if (const auto start = m_Start.Peek()) SetDisplayedMonthMillis(m_Calendar.GetMonth(start->utcTimeMillis).startUtcTimeMillis);
        if (m_DisplayMode.Peek() != mode) m_DisplayMode.Set(mode);
    }

    std::shared_ptr<DateRangePickerState> RememberDateRangePickerState(std::optional<std::int64_t> start, std::optional<std::int64_t> end,
                                                                       std::optional<std::int64_t> displayed, IntRange yearRange, DisplayMode mode,
                                                                       SelectableDatesPtr selectable, rt::Key key) {
        if (!displayed) displayed = start;
        const std::string locale = ui::PlatformLocale();
        if (!selectable) selectable = DatePickerDefaults::AllDates();
        // [start or null, end or null, displayedMonthMillis, yearRange first, last, displayMode].
        auto opt = [](const rt::SavedValue& v) -> std::optional<std::int64_t> {
            if (auto n = rt::SavedNumber(v)) return std::int64_t(*n);
            return std::nullopt;
        };
        auto saver = rt::ListSaver<std::shared_ptr<DateRangePickerState>>(
            [](const std::shared_ptr<DateRangePickerState>& s) {
                const auto a = s->SelectedStartDateMillis(), b = s->SelectedEndDateMillis();
                return rt::SavedValue::List{ a ? rt::SavedValue(double(*a)) : rt::SavedValue(), b ? rt::SavedValue(double(*b)) : rt::SavedValue(),
                                             double(s->DisplayedMonthMillis()), s->YearRange().first, s->YearRange().last, int(s->GetDisplayMode()) };
            },
            [locale, selectable, opt](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<DateRangePickerState>> {
                if (l.size() < 6) return std::nullopt;
                const IntRange range{ int(rt::SavedNumber(l[3]).value_or(1900)), int(rt::SavedNumber(l[4]).value_or(2100)) };
                return std::make_shared<DateRangePickerState>(locale, opt(l[0]), opt(l[1]), opt(l[2]), range,
                                                              DisplayMode(int(rt::SavedNumber(l[5]).value_or(0))), selectable);
            });
        auto& s = rt::RememberSaveable<std::shared_ptr<DateRangePickerState>>(
            [&] { return std::make_shared<DateRangePickerState>(locale, start, end, displayed, yearRange, mode, selectable); }, saver, {}, key);
        s->SetSelectableDates(selectable);
        return s;
    }

    // --- defaults -------------------------------------------------------------------------------

    namespace DateRangePickerDefaults {
        void DateRangePickerTitle(DisplayMode mode, const ui::Modifier& modifier, std::optional<Color> contentColor) {
            TextOptions o;
            o.color = contentColor.value_or(DatePickerDefaults::colors().titleContentColor);
            Text(GetString(mode == DisplayMode::Picker ? Strings::DateRangePickerTitle : Strings::DateRangeInputTitle), modifier, o);
        }

        void DateRangePickerHeadline(std::optional<std::int64_t> start, std::optional<std::int64_t> end, DisplayMode mode,
                                     const DatePickerFormatterPtr& formatter, const ui::Modifier& modifier, std::optional<Color> contentColor) {
            const Color color = contentColor.value_or(DatePickerDefaults::colors().headlineContentColor);
            const std::string startText = GetString(Strings::DateRangePickerStartHeadline);
            const std::string endText = GetString(Strings::DateRangePickerEndHeadline);
            const std::string locale = ui::PlatformLocale();
            const auto startFormatted = formatter->FormatDate(start, locale);
            const auto endFormatted = formatter->FormatDate(end, locale);
            const std::string none = GetString(mode == DisplayMode::Picker ? Strings::DatePickerNoSelectionDescription : Strings::DateInputNoInputDescription);
            const std::string startVerbose = formatter->FormatDate(start, locale, true).value_or(none);
            const std::string endVerbose = formatter->FormatDate(end, locale, true).value_or(none);
            const std::string description = startText + ": " + startVerbose + ", " + endText + ": " + endVerbose;
            ui::Modifier m = ClearAndSetSemantics(modifier, [description](ui::SemanticsConfiguration& c) {
                c.liveRegion = ui::LiveRegionMode::Polite;
                c.contentDescription = { description };
            });
            Row(m, Arrangement::SpacedBy(Dp(4)), ui::AlignmentV::CenterVertically(), [=] {
                TextOptions o;
                o.color = color;
                // The dates, or "Start date" / "End date" until picked.
                Text(startFormatted.value_or(startText), {}, o);
                Text("-", {}, o);
                Text(endFormatted.value_or(endText), {}, o);
            });
        }
    }

    // --- internals ------------------------------------------------------------------------------

    std::optional<mi::SelectedRangeInfo> mi::CalculateRangeInfo(const CalendarMonth& month, const CalendarDate& start, const CalendarDate& end) {
        if (start.utcTimeMillis > month.EndUtcTimeMillis() || end.utcTimeMillis < month.startUtcTimeMillis) return std::nullopt;
        const bool firstIsStart = start.utcTimeMillis >= month.startUtcTimeMillis;
        const bool lastIsEnd = end.utcTimeMillis <= month.EndUtcTimeMillis();
        const int startCell = firstIsStart ? month.daysFromStartOfWeekToFirstOfMonth + start.dayOfMonth - 1 : month.daysFromStartOfWeekToFirstOfMonth;
        const int endCell = lastIsEnd ? month.daysFromStartOfWeekToFirstOfMonth + end.dayOfMonth - 1
                                      : month.daysFromStartOfWeekToFirstOfMonth + month.numberOfDays - 1;
        return SelectedRangeInfo{ startCell % DaysInWeek, startCell / DaysInWeek, endCell % DaysInWeek, endCell / DaysInWeek, firstIsStart, lastIsEnd };
    }

    namespace {
        using SelectionChange = std::function<void(std::optional<std::int64_t>, std::optional<std::int64_t>)>;

        // updateDateSelection: a first tap (or one after a complete range) starts over; a later
        // date ends the range; an earlier one starts it again.
        void UpdateDateSelection(std::int64_t date, std::optional<std::int64_t> start, std::optional<std::int64_t> end, const SelectionChange& change) {
            if ((!start && !end) || (start && end)) change(date, std::nullopt);
            else if (start && date >= *start) change(start, date);
            else change(date, std::nullopt);
        }

        rt::Task<> JumpTo(LazyListState* state, int index) {
            if (index >= 0) state->ScrollToItem(index);
            co_return;
        }

        void VerticalMonthsList(LazyListState& listState, std::optional<std::int64_t> start, std::optional<std::int64_t> end, SelectionChange onChange,
                                std::function<void(std::int64_t)> onDisplayedMonthChange, const mi::CalendarModel& model, const IntRange& yearRange,
                                const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable, const DatePickerColors& colors) {
            const mi::CalendarDate today = model.Today();
            const mi::CalendarMonth firstMonth = model.GetMonth(yearRange.first, 1);
            const mi::CalendarModel* m = &model;
            LazyListState* state = &listState;
            ProvideTextStyle(Value(DatePickerModalTokens::DateLabelTextFont), [=] {
                auto scope = mi::RememberCoroutineScope();
                // Scroll actions for screen readers (the list does not scroll for them itself).
                const std::vector<ui::CustomAccessibilityAction> actions{
                    { GetString(Strings::DateRangePickerScrollToShowPreviousMonth),
                      [state, scope] {
                          if (!state->CanScrollBackward()) return false;
                          scope->Launch(JumpTo(state, state->FirstVisibleItemIndex() - 1));
                          return true;
                      } },
                    { GetString(Strings::DateRangePickerScrollToShowNextMonth),
                      [state, scope] {
                          if (!state->CanScrollForward()) return false;
                          scope->Launch(JumpTo(state, state->FirstVisibleItemIndex() + 1));
                          return true;
                      } },
                };
                auto select = [start, end, onChange](std::int64_t date) { UpdateDateSelection(date, start, end, onChange); };
                ui::Modifier list = Semantics({}, [](ui::SemanticsConfiguration& c) {
                    c.verticalScrollAxisRange = ui::ScrollAxisRange{ [] { return 0.0f; }, [] { return 0.0f; }, false };
                });
                LazyColumn(list, *state, [=](LazyListScope& s) {
                    s.Items(mi::NumberOfMonthsInRange(yearRange), [=](int i) {
                        const mi::CalendarMonth month = m->PlusMonths(firstMonth, i);
                        Column(FillParentMaxWidth({}), [=] {
                            ProvideTextStyle(Value(DatePickerModalTokens::RangeSelectionMonthSubheadFont), [=] {
                                TextOptions o;
                                o.color = colors.subheadContentColor;
                                Text(formatter->FormatMonthYear(month.startUtcTimeMillis, m->Locale()).value_or("-"),
                                     Semantics(Padding({}, { Dp(24), Dp(20), Dp(0), Dp(8) }),
                                               [actions](ui::SemanticsConfiguration& c) { c.customActions = actions; }),
                                     o);
                            });
                            std::optional<mi::SelectedRangeInfo> info;
                            if (start && end) info = mi::CalculateRangeInfo(month, m->GetCanonicalDate(*start), m->GetCanonicalDate(*end));
                            mi::Month(month, select, today.utcTimeMillis, start, end, info, formatter, selectable, colors, m->Locale(), *state, false, {});
                        });
                    });
                });
            });
            mi::UpdateDisplayedMonth(listState, std::move(onDisplayedMonthChange), model, yearRange);
        }

        void DateRangePickerContent(std::optional<std::int64_t> start, std::optional<std::int64_t> end, std::int64_t displayedMonthMillis,
                                    SelectionChange onChange, std::function<void(std::int64_t)> onDisplayedMonthChange, const mi::CalendarModel& model,
                                    const IntRange& yearRange, const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable,
                                    const DatePickerColors& colors, ui::FocusRequester* top, ui::FocusRequester* bottom) {
            auto& c = *rt::Composition::Current();
            const int monthIndex = std::max(0, model.GetMonth(displayedMonthMillis).IndexIn(yearRange.first));
            auto& listState = RememberLazyListState(monthIndex);
            LazyListState* state = &listState;
            // Unlike the single picker, scrolls even while a scroll runs.
            c.LaunchedEffect(rt::Keys(monthIndex), [state, monthIndex]() -> rt::Task<> {
                if (state->FirstVisibleItemIndex() != monthIndex) state->ScrollToItem(monthIndex);
                co_return;
            });
            auto* owner = c.Read(ui::LocalOwner);
            const mi::CalendarModel* m = &model;
            // Tab leaves the days for whatever follows the picker; Shift+Tab for what precedes it.
            ui::Modifier column = OnKeyEvent(Padding({}, mi::DatePickerHorizontalPadding, Dp(0)), {}, [top, bottom, owner](const ui::KeyEvent& e) {
                if (e.type != ui::KeyEventType::KeyDown || e.keyCode != ui::keys::Tab || !owner) return false;
                if (e.Shift()) { top->RequestFocus(); owner->MoveFocus(ui::FocusDirection::Previous); }
                else { bottom->RequestFocus(); owner->MoveFocus(ui::FocusDirection::Next); }
                return true;
            });
            Column(column, [=] {
                mi::WeekDays(colors, *m);
                VerticalMonthsList(*state, start, end, onChange, onDisplayedMonthChange, *m, yearRange, formatter, selectable, colors);
            });
        }

        void DateRangeInputContent(std::optional<std::int64_t> start, std::optional<std::int64_t> end, SelectionChange onChange,
                                   const mi::CalendarModel& model, const IntRange& yearRange, const DatePickerFormatterPtr& formatter,
                                   const SelectableDatesPtr& selectable, const DatePickerColors& colors, ui::FocusRequester* focusRequester) {
            auto& c = *rt::Composition::Current();
            const mi::DateInputFormat format = model.GetDateInputFormat();
            auto validator = c.Remember<std::shared_ptr<mi::DateInputValidator>>(
                [&] {
                    return std::make_shared<mi::DateInputValidator>(yearRange, selectable, format, formatter, GetString(Strings::DateInputInvalidForPattern),
                                                                    GetString(Strings::DateInputInvalidYearRange),
                                                                    GetString(Strings::DateInputInvalidNotAllowed),
                                                                    GetString(Strings::DateRangeInputInvalidRangeInput));
                },
                rt::Keys(format.patternWithDelimiters, reinterpret_cast<std::uintptr_t>(formatter.get())));
            // Both ends, for validating the order.
            validator->currentStartDateMillis = start;
            validator->currentEndDateMillis = end;
            std::string pattern = format.patternWithDelimiters;
            for (char& ch : pattern) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            const mi::CalendarModel* m = &model;
            const ui::Modifier row = Padding({}, { mi::InputTextFieldPaddingStart, mi::InputTextFieldPaddingTop, mi::InputTextFieldPaddingEnd, Dp(0) });
            Row(row, Arrangement::SpacedBy(Dp(8)), ui::AlignmentV::Top(), [=] {
                const auto field = [&](Strings labelId, std::optional<std::int64_t> initial, std::function<void(std::optional<std::int64_t>)> change,
                                       mi::InputIdentifier id, ui::FocusRequester* fr) {
                    const std::string labelText = GetString(labelId);
                    mi::DateInputTextField(
                        Weight({}, 0.5f), initial, std::move(change), *m,
                        [labelText, pattern] {
                            Text(labelText, Semantics({}, [labelText, pattern](ui::SemanticsConfiguration& sc) { sc.contentDescription = { labelText + ", " + pattern }; }));
                        },
                        [pattern] { Text(pattern, ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {})); }, id, validator, format, m->Locale(), colors, fr);
                };
                field(Strings::DateRangePickerStartHeadline, start, [onChange, end](std::optional<std::int64_t> d) { onChange(d, end); },
                      mi::InputIdentifier::StartDateInput, focusRequester);
                // Only the first field takes the initial focus.
                field(Strings::DateRangePickerEndHeadline, end, [onChange, start](std::optional<std::int64_t> d) { onChange(start, d); },
                      mi::InputIdentifier::EndDateInput, nullptr);
            });
        }

        constexpr Dp TitleStart{ 64 }, TitleEnd{ 12 }, HeadlineBottom{ 12 }, HeaderHeightOffset{ 60 };
    }

    void DateRangePicker(DateRangePickerState& state, const ui::Modifier& modifier, const DatePickerOptions& options) {
        auto& c = *rt::Composition::Current();
        const DatePickerColors colors = options.colors.value_or(DatePickerDefaults::colors());
        auto& defaultFormatter = c.Remember<DatePickerFormatterPtr>([] { return DatePickerDefaults::dateFormatter(); });
        const DatePickerFormatterPtr formatter = options.dateFormatter ? options.dateFormatter : defaultFormatter;
        auto& focus = c.Remember<std::shared_ptr<std::array<ui::FocusRequester, 3>>>([] { return std::make_shared<std::array<ui::FocusRequester, 3>>(); });
        ui::FocusRequester* focusRequester = options.focusRequester ? *options.focusRequester : &(*focus)[0];
        ui::FocusRequester* top = &(*focus)[1];
        ui::FocusRequester* bottom = &(*focus)[2];
        DateRangePickerState* st = &state;

        std::function<void()> title = options.title ? *options.title : std::function<void()>([st, colors] {
            DateRangePickerDefaults::DateRangePickerTitle(st->GetDisplayMode(), Padding({}, { TitleStart, Dp(0), TitleEnd, Dp(0) }), colors.titleContentColor);
        });
        std::function<void()> headline = options.headline ? *options.headline : std::function<void()>([st, formatter, colors] {
            DateRangePickerDefaults::DateRangePickerHeadline(st->SelectedStartDateMillis(), st->SelectedEndDateMillis(), st->GetDisplayMode(), formatter,
                                                             Padding({}, { TitleStart, Dp(0), TitleEnd, HeadlineBottom }), colors.headlineContentColor);
        });
        std::function<void()> toggle;
        if (options.showModeToggle)
            toggle = [st, colors] {
                mi::DisplayModeToggleButton(Padding({}, { Dp(0), Dp(0), Dp(12), Dp(12) }), st->GetDisplayMode(),
                                            [st](DisplayMode mode) { st->SetDisplayMode(mode); }, colors);
            };
        const auto selectable = state.GetSelectableDates();
        // setSelection's rejections are ignored here.
        const SelectionChange onChange = [st](std::optional<std::int64_t> a, std::optional<std::int64_t> b) { st->SetSelection(a, b); };
        mi::DateEntryContainer(
            modifier, title, headline, toggle, colors, Value(DatePickerModalTokens::RangeSelectionHeaderHeadlineFont),
            DatePickerModalTokens::RangeSelectionHeaderContainerHeight - HeaderHeightOffset,
            [st, formatter, selectable, colors, focusRequester, top, bottom, onChange] {
                const mi::CalendarModel* m = &st->Calendar();
                const auto start = st->SelectedStartDateMillis(), end = st->SelectedEndDateMillis();
                const std::int64_t displayed = st->DisplayedMonthMillis();
                const IntRange range = st->YearRange();
                // Crossfade between the modes (Compose has no motion spec for this yet).
                Crossfade(rt::Key(int(st->GetDisplayMode())), Semantics({}, [](ui::SemanticsConfiguration& sc) { sc.isTraversalGroup = true; }),
                          [=](rt::Key key) {
                              if (key == rt::Key(int(DisplayMode::Picker)))
                                  DateRangePickerContent(start, end, displayed, onChange, [st](std::int64_t month) { st->SetDisplayedMonthMillis(month); }, *m,
                                                         range, formatter, selectable, colors, top, bottom);
                              else
                                  DateRangeInputContent(start, end, onChange, *m, range, formatter, selectable, colors, focusRequester);
                          },
                          Value(MotionSchemeKeyTokens::FastEffects));
            },
            top, bottom);
    }

}
