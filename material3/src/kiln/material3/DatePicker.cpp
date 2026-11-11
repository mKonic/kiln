#include "kiln/material3/DatePicker.h"

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Snapping.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/Divider.h"
#include "kiln/material3/IconButton.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/TextFieldDefaults.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/DatePickerInternal.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/DatePickerModalTokens.h"
#include "kiln/material3/tokens/DividerTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/text/LocaleFormat.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Locals.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    namespace mi = material3::internal;

    // --- state ----------------------------------------------------------------------------------

    namespace {
        class FunctionSelectableDates final : public SelectableDates {
        public:
            FunctionSelectableDates(std::function<bool(std::int64_t)> d, std::function<bool(int)> y) : m_Date(std::move(d)), m_Year(std::move(y)) {}
            bool IsSelectableDate(std::int64_t ms) const override { return !m_Date || m_Date(ms); }
            bool IsSelectableYear(int year) const override { return !m_Year || m_Year(year); }
        private:
            std::function<bool(std::int64_t)> m_Date;
            std::function<bool(int)> m_Year;
        };

        mi::CalendarMonth InitialMonth(const mi::CalendarModel& model, std::optional<std::int64_t> millis, const IntRange& range) {
            if (millis) {
                const mi::CalendarMonth month = model.GetMonth(*millis);
                if (range.Contains(month.year)) return month;
            }
            // Out of range or unset: the current month.
            return model.GetMonth(model.Today());
        }

        std::optional<mi::CalendarDate> InitialSelection(const mi::CalendarModel& model, std::optional<std::int64_t> millis, const IntRange& range) {
            if (!millis) return std::nullopt;
            const mi::CalendarDate date = model.GetCanonicalDate(*millis);
            if (!range.Contains(date.year)) return std::nullopt;
            return date;
        }
    }

    SelectableDatesPtr MakeSelectableDates(std::function<bool(std::int64_t)> isSelectableDate, std::function<bool(int)> isSelectableYear) {
        return std::make_shared<FunctionSelectableDates>(std::move(isSelectableDate), std::move(isSelectableYear));
    }

    DatePickerState::DatePickerState(std::string locale, std::optional<std::int64_t> selected, std::optional<std::int64_t> displayed,
                                     IntRange yearRange, DisplayMode mode, SelectableDatesPtr selectableDates)
        : m_Calendar(std::move(locale)), m_YearRange(yearRange),
          m_SelectableDates(selectableDates ? std::move(selectableDates) : DatePickerDefaults::AllDates()),
          m_DisplayedMonth(InitialMonth(m_Calendar, displayed, yearRange)),
          m_Selected(InitialSelection(m_Calendar, selected, yearRange)), m_DisplayMode(mode) {}

    std::optional<std::int64_t> DatePickerState::SelectedDateMillis() const {
        const auto& d = m_Selected.Get();
        return d ? std::optional<std::int64_t>(d->utcTimeMillis) : std::nullopt;
    }

    void DatePickerState::SetSelectedDateMillis(std::optional<std::int64_t> millis) {
        // Outside the year range: no selection.
        std::optional<mi::CalendarDate> next;
        if (millis) {
            const mi::CalendarDate date = m_Calendar.GetCanonicalDate(*millis);
            if (m_YearRange.Contains(date.year)) next = date;
        }
        if (m_Selected.Peek() != next) m_Selected.Set(next);
    }

    void DatePickerState::SetDisplayedMonthMillis(std::int64_t millis) {
        const mi::CalendarMonth month = m_Calendar.GetMonth(millis);
        if (m_YearRange.Contains(month.year) && !(m_DisplayedMonth.Peek() == month)) m_DisplayedMonth.Set(month);
    }

    void DatePickerState::SetDisplayMode(DisplayMode mode) {
        if (const auto selected = m_Selected.Peek()) SetDisplayedMonthMillis(m_Calendar.GetMonth(selected->utcTimeMillis).startUtcTimeMillis);
        if (m_DisplayMode.Peek() != mode) m_DisplayMode.Set(mode);
    }

    std::shared_ptr<DatePickerState> RememberDatePickerState(std::optional<std::int64_t> initialSelected, std::optional<std::int64_t> initialDisplayed,
                                                             IntRange yearRange, DisplayMode initialMode, SelectableDatesPtr selectableDates, rt::Key key) {
        if (!initialDisplayed) initialDisplayed = initialSelected;
        const std::string locale = ui::PlatformLocale();
        if (!selectableDates) selectableDates = DatePickerDefaults::AllDates();
        // [selectedDateMillis or null, displayedMonthMillis, yearRange first, last, displayMode].
        auto saver = rt::ListSaver<std::shared_ptr<DatePickerState>>(
            [](const std::shared_ptr<DatePickerState>& s) {
                const auto sel = s->SelectedDateMillis();
                return rt::SavedValue::List{ sel ? rt::SavedValue(double(*sel)) : rt::SavedValue(), double(s->DisplayedMonthMillis()),
                                             s->YearRange().first, s->YearRange().last, int(s->GetDisplayMode()) };
            },
            [locale, selectableDates](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<DatePickerState>> {
                if (l.size() < 5) return std::nullopt;
                std::optional<std::int64_t> sel;
                if (auto n = rt::SavedNumber(l[0])) sel = std::int64_t(*n);
                const auto month = std::int64_t(rt::SavedNumber(l[1]).value_or(0));
                const IntRange range{ int(rt::SavedNumber(l[2]).value_or(1900)), int(rt::SavedNumber(l[3]).value_or(2100)) };
                return std::make_shared<DatePickerState>(locale, sel, month, range, DisplayMode(int(rt::SavedNumber(l[4]).value_or(0))), selectableDates);
            });
        auto& s = rt::RememberSaveable<std::shared_ptr<DatePickerState>>(
            [&] { return std::make_shared<DatePickerState>(locale, initialSelected, initialDisplayed, yearRange, initialMode, selectableDates); }, saver, {}, key);
        // Follows the caller's selectable dates.
        s->SetSelectableDates(selectableDates);
        return s;
    }

    // --- defaults -------------------------------------------------------------------------------

    namespace {
        class FormatterImpl final : public DatePickerFormatter {
        public:
            FormatterImpl(std::string year, std::string date, std::string description)
                : m_Year(std::move(year)), m_Date(std::move(date)), m_Description(std::move(description)) {}
            std::optional<std::string> FormatMonthYear(std::optional<std::int64_t> ms, const std::string& locale) const override {
                if (!ms) return std::nullopt;
                return text::DateFormat().FormatSkeleton(*ms, m_Year, locale);
            }
            std::optional<std::string> FormatDate(std::optional<std::int64_t> ms, const std::string& locale, bool forDescription) const override {
                if (!ms) return std::nullopt;
                return text::DateFormat().FormatSkeleton(*ms, forDescription ? m_Description : m_Date, locale);
            }
        private:
            std::string m_Year, m_Date, m_Description;
        };
    }

    namespace DatePickerDefaults {
        DatePickerColors colors() {
            const auto disabled = [](Color c) { return c.WithAlpha(0.38f); };   // DisabledAlpha
            DatePickerColors c;
            c.containerColor = Value(DatePickerModalTokens::ContainerColor);
            c.titleContentColor = Value(DatePickerModalTokens::HeaderSupportingTextColor);
            c.headlineContentColor = Value(DatePickerModalTokens::HeaderHeadlineColor);
            c.weekdayContentColor = Value(DatePickerModalTokens::WeekdaysLabelTextColor);
            c.subheadContentColor = Value(DatePickerModalTokens::RangeSelectionMonthSubheadColor);
            c.navigationContentColor = MaterialTheme::colorScheme().onSurfaceVariant;
            c.yearContentColor = Value(DatePickerModalTokens::SelectionYearUnselectedLabelTextColor);
            c.disabledYearContentColor = disabled(c.yearContentColor);
            c.currentYearContentColor = Value(DatePickerModalTokens::DateTodayLabelTextColor);
            c.selectedYearContentColor = Value(DatePickerModalTokens::SelectionYearSelectedLabelTextColor);
            c.disabledSelectedYearContentColor = disabled(c.selectedYearContentColor);
            c.selectedYearContainerColor = Value(DatePickerModalTokens::SelectionYearSelectedContainerColor);
            c.disabledSelectedYearContainerColor = disabled(c.selectedYearContainerColor);
            c.dayContentColor = Value(DatePickerModalTokens::DateUnselectedLabelTextColor);
            c.disabledDayContentColor = disabled(c.dayContentColor);
            c.selectedDayContentColor = Value(DatePickerModalTokens::DateSelectedLabelTextColor);
            c.disabledSelectedDayContentColor = disabled(c.selectedDayContentColor);
            c.selectedDayContainerColor = Value(DatePickerModalTokens::DateSelectedContainerColor);
            c.disabledSelectedDayContainerColor = disabled(c.selectedDayContainerColor);
            c.todayContentColor = Value(DatePickerModalTokens::DateTodayLabelTextColor);
            c.todayDateBorderColor = Value(DatePickerModalTokens::DateTodayContainerOutlineColor);
            c.dayInSelectionRangeContentColor = Value(DatePickerModalTokens::SelectionDateInRangeLabelTextColor);
            c.dayInSelectionRangeContainerColor = Value(DatePickerModalTokens::RangeSelectionActiveIndicatorContainerColor);
            c.dividerColor = Value(DividerTokens::Color);
            c.dateTextFieldColors = OutlinedTextFieldDefaults::colors();
            return c;
        }

        ShapePtr shape() { return Value(DatePickerModalTokens::ContainerShape); }

        SelectableDatesPtr AllDates() {
            static const SelectableDatesPtr all = std::make_shared<SelectableDates>();
            return all;
        }

        DatePickerFormatterPtr dateFormatter(std::string year, std::string date, std::string description) {
            return std::make_shared<FormatterImpl>(std::move(year), std::move(date), std::move(description));
        }

        void DatePickerTitle(DisplayMode mode, const ui::Modifier& modifier, std::optional<Color> contentColor) {
            TextOptions o;
            o.color = contentColor.value_or(colors().titleContentColor);
            Text(GetString(mode == DisplayMode::Picker ? Strings::DatePickerTitle : Strings::DateInputTitle), modifier, o);
        }

        void DatePickerHeadline(std::optional<std::int64_t> selected, DisplayMode mode, const DatePickerFormatterPtr& formatter,
                                const ui::Modifier& modifier, std::optional<Color> contentColor) {
            const std::string locale = ui::PlatformLocale();
            const auto formatted = formatter->FormatDate(selected, locale);
            const std::string verbose = formatter->FormatDate(selected, locale, true).value_or(
                GetString(mode == DisplayMode::Picker ? Strings::DatePickerNoSelectionDescription : Strings::DateInputNoInputDescription));
            const std::string headline = formatted.value_or(
                GetString(mode == DisplayMode::Picker ? Strings::DatePickerHeadline : Strings::DateInputHeadline));
            const std::string description = GetString(
                mode == DisplayMode::Picker ? Strings::DatePickerHeadlineDescription : Strings::DateInputHeadlineDescription, { verbose });
            TextOptions o;
            o.color = contentColor.value_or(colors().headlineContentColor);
            o.maxLines = 1;
            Text(headline, Semantics(modifier, [description](ui::SemanticsConfiguration& c) {
                c.liveRegion = ui::LiveRegionMode::Polite;
                c.contentDescription = { description };
            }), o);
        }
    }

    // --- shared internals -----------------------------------------------------------------------

    namespace internal {
        int NumberOfMonthsInRange(const IntRange& r) { return (r.last - r.first + 1) * 12; }

        std::string ToLocalString(int value, const std::string& locale, int minDigits) {
            std::string digits = std::to_string(value < 0 ? -value : value);
            while (int(digits.size()) < minDigits) digits.insert(digits.begin(), '0');
            const char32_t zero = text::LocaleZeroDigit(locale);
            std::string out = value < 0 ? "-" : "";
            for (char c : digits) {
                const char32_t cp = zero + char32_t(c - '0');
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            }
            return out;
        }

        namespace {
            ui::Modifier RequiredHeightDp(ui::Modifier m, Dp h) { return SizeIn(std::move(m), Dp::Unspecified(), h, Dp::Unspecified(), h, false); }

            void IconButtonWithTooltip(std::function<void()> onClick, const gfx::vector::ImageVectorPtr& icon, std::string description,
                                       const ui::Modifier& modifier = {}, bool enabled = true) {
                auto provider = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above);
                auto state = RememberTooltipState();
                TooltipBox(provider, [description](const TooltipScope& scope) {
                    PlainTooltip(scope, {}, {}, [description] { Text(description); });
                }, state, {}, {}, [=] {
                    IconButtonOptions o;
                    o.enabled = enabled;
                    IconButton(onClick, modifier, o, [icon, description] { Icon(icon, description); });
                });
            }
        }

        void DateEntryContainer(const ui::Modifier& modifier, std::function<void()> title, std::function<void()> headline,
                                std::function<void()> modeToggleButton, const DatePickerColors& colors, const text::TextStyle& headlineTextStyle,
                                Dp headerMinHeight, std::function<void()> content, ui::FocusRequester* topFocusTarget,
                                ui::FocusRequester* bottomFocusTarget) {
            ui::Modifier m = SizeIn(modifier, DatePickerModalTokens::ContainerWidth, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified());
            m = Semantics(std::move(m), [](ui::SemanticsConfiguration& c) { c.isTraversalGroup = true; });
            m = Background(std::move(m), colors.containerColor);
            Column(m, [=] {
                // DatePickerHeader: a minimum height only with a title.
                ui::Modifier header = FillMax({}, Axis::Horizontal);
                if (title) header = DefaultMinSize(std::move(header), Dp::Unspecified(), headerMinHeight);
                Column(header, Arrangement::SpaceBetween(), ui::AlignmentH::Start(), [=] {
                    if (title) {
                        mi::ProvideContentColorTextStyle(colors.titleContentColor, Value(DatePickerModalTokens::HeaderSupportingTextFont), [=] {
                            Box({}, ui::Alignment::BottomStart(), false, title);
                        });
                    }
                    rt::Composition::Current()->Provide(LocalContentColor, colors.headlineContentColor, [=] {
                        Column(FillMax({}, Axis::Horizontal), [=] {
                            const Arrangement arrangement = headline && modeToggleButton ? Arrangement::SpaceBetween()
                                                          : headline ? Arrangement::Start() : Arrangement::End();
                            Row(FillMax({}, Axis::Horizontal), arrangement, ui::AlignmentV::CenterVertically(), [=] {
                                if (headline) ProvideTextStyle(headlineTextStyle, [=] { Box(Weight({}, 1), ui::Alignment::TopStart(), false, headline); });
                                if (modeToggleButton) modeToggleButton();
                            });
                            // A divider only below a title, headline or toggle.
                            if (title || headline || modeToggleButton) HorizontalDivider({}, DividerDefaults::Thickness, colors.dividerColor);
                        });
                    });
                });
                // The range picker's invisible focus targets around its content (so focus can
                // leave the list).
                auto invisibleDivider = [](ui::FocusRequester* r, bool top) {
                    auto* owner = rt::Composition::Current()->Read(ui::LocalOwner);
                    ui::Modifier d = FocusRequesterModifier(SizeMod({}, Dp(0), Dp(0)), *r);
                    // Focus never stays on it: still focused once the move that brought it ends
                    // (Tab from above, Shift+Tab from below), it goes on into the content.
                    d = OnFocusChanged(std::move(d), [r, top, owner](ui::FocusState f) {
                        if (!ui::IsFocused(f) || !owner) return;
                        std::weak_ptr<const char> alive = r->AliveToken();
                        rt::Dispatcher::Main().Post([r, top, owner, alive] {
                            if (alive.expired()) return;
                            bool still = false;
                            r->FindFocusTarget([&still](ui::FocusTargetNode& t) { still = ui::IsFocused(t.State()); return true; });
                            if (still) owner->MoveFocus(top ? ui::FocusDirection::Next : ui::FocusDirection::Previous);
                        });
                    });
                    d = OnKeyEvent(std::move(d), [owner](const ui::KeyEvent& e) {
                        if (e.type != ui::KeyEventType::KeyDown || !owner) return false;
                        if (e.keyCode == ui::keys::DpadUp || (e.Shift() && e.keyCode == ui::keys::Tab)) { owner->MoveFocus(ui::FocusDirection::Previous); return true; }
                        if (e.keyCode == ui::keys::DpadDown || e.keyCode == ui::keys::Tab) { owner->MoveFocus(ui::FocusDirection::Next); return true; }
                        return false;
                    });
                    HorizontalDivider(Focusable(std::move(d)), DividerDefaults::Thickness, Color::Transparent());
                };
                if (topFocusTarget) invisibleDivider(topFocusTarget, true);
                content();
                if (bottomFocusTarget) invisibleDivider(bottomFocusTarget, false);
            });
        }

        void DisplayModeToggleButton(const ui::Modifier& modifier, DisplayMode mode, std::function<void(DisplayMode)> onChange, const DatePickerColors& colors) {
            rt::Composition::Current()->Provide(LocalContentColor, colors.headlineContentColor, [=] {
                if (mode == DisplayMode::Picker)
                    IconButtonWithTooltip([onChange] { onChange(DisplayMode::Input); }, Icons::Filled::Edit(),
                                          GetString(Strings::DatePickerSwitchToInputMode), modifier);
                else
                    IconButtonWithTooltip([onChange] { onChange(DisplayMode::Picker); }, Icons::Filled::DateRange(),
                                          GetString(Strings::DatePickerSwitchToCalendarMode), modifier);
            });
        }

        void WeekDays(const DatePickerColors& colors, const CalendarModel& model) {
            const int first = model.FirstDayOfWeek();
            const auto& weekdays = model.WeekdayNames();
            std::vector<std::pair<std::string, std::string>> names;
            for (int i = first - 1; i < int(weekdays.size()); ++i) names.push_back(weekdays[std::size_t(i)]);
            for (int i = 0; i < first - 1; ++i) names.push_back(weekdays[std::size_t(i)]);
            const Dp minimum = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize);
            const Color color = colors.weekdayContentColor;
            Row(FillMax(DefaultMinSize({}, Dp::Unspecified(), RecommendedSizeForAccessibility), Axis::Horizontal), Arrangement::SpaceEvenly(),
                ui::AlignmentV::CenterVertically(), [names, minimum, color] {
                    for (const auto& [full, narrow] : names) {
                        // As large as a day, so the letters line up with the days below.
                        ui::Modifier m = ClearAndSetSemantics({}, [full = full](ui::SemanticsConfiguration& c) { c.contentDescription = { full }; });
                        m = SizeIn(std::move(m), DatePickerModalTokens::DateContainerWidth, DatePickerModalTokens::DateContainerHeight, Dp::Unspecified(),
                                   Dp::Unspecified());
                        m = SizeMod(std::move(m), minimum, minimum);
                        Box(m, ui::Alignment::Center(), false, [narrow = narrow, color] {
                            TextOptions o;
                            o.color = color;
                            o.style = Value(DatePickerModalTokens::WeekdaysLabelTextFont);
                            o.textAlign = text::TextAlign::Center;
                            Text(narrow, {}, o);
                        });
                    }
                });
        }

        void UpdateDisplayedMonth(LazyListState& state, std::function<void(std::int64_t)> onChange, const CalendarModel& model, const IntRange& range) {
            // snapshotFlow { firstVisibleItemIndex }: a scope of its own reads the index and reports
            // each new value.
            auto& c = *rt::Composition::Current();
            auto& last = c.Remember<std::shared_ptr<int>>([] { return std::make_shared<int>(-1); });
            auto lastPtr = last;
            const CalendarModel* m = &model;
            c.Child([&state, onChange, m, range, lastPtr] {
                const int index = state.FirstVisibleItemIndex();
                if (index == *lastPtr) return;
                rt::Composition::Current()->SideEffect([=] {
                    if (index == *lastPtr) return;
                    *lastPtr = index;
                    onChange(m->GetMonth(range.first + index / 12, index % 12 + 1).startUtcTimeMillis);
                });
            });
        }

        namespace {
            void DrawRangeBackground(ui::ContentDrawScope& s, const SelectedRangeInfo& info, Color color) {
                const ui::Size size = s.GetSize();
                const float itemW = s.ToPx(RecommendedSizeForAccessibility), itemH = itemW;
                const float layerH = s.ToPx(DatePickerModalTokens::DateStateLayerHeight);
                const float vPad = (itemH - layerH) / 2;
                const float space = (size.width - float(DaysInWeek) * itemW) / float(DaysInWeek);
                float startX = float(info.startColumn) * (itemW + space) + (info.firstIsSelectionStart ? itemW / 2 : 0) + space / 2;
                const float startY = float(info.startRow) * itemH + vPad;
                float endX = float(info.endColumn) * (itemW + space) + (info.lastIsSelectionEnd ? itemW / 2 : itemW) + space / 2;
                const float endY = float(info.endRow) * itemH + vPad;
                const bool rtl = s.Direction() == ui::LayoutDirection::Rtl;
                if (rtl) { startX = size.width - startX; endX = size.width - endX; }
                // Rectangles from x0 to x1 in either order.
                auto rect = [&](float x0, float x1, float y) {
                    s.DrawRect(color, { std::min(x0, x1), y }, { std::abs(x1 - x0), layerH });
                };
                if (info.startRow == info.endRow) { rect(startX, endX, startY); return; }
                rect(startX, rtl ? 0.0f : size.width, startY);
                for (int y = info.endRow - info.startRow - 1; y >= 1; --y) rect(0, size.width, startY + float(y) * itemH);
                rect(rtl ? size.width : 0.0f, endX, endY);
            }

            std::optional<std::string> DayContentDescription(bool range, bool today, bool start, bool end, bool inRange) {
                std::string d;
                if (range) {
                    if (start) d = GetString(Strings::DateRangePickerStartHeadline);
                    else if (end) d = GetString(Strings::DateRangePickerEndHeadline);
                    else if (inRange) d = GetString(Strings::DateRangePickerDayInRange);
                }
                if (today) {
                    if (!d.empty()) d += ", ";
                    d += GetString(Strings::DatePickerTodayDescription);
                }
                if (d.empty()) return std::nullopt;
                return d;
            }

            Color DayContentTarget(const DatePickerColors& c, bool today, bool selected, bool inRange, bool enabled) {
                if (selected) return enabled ? c.selectedDayContentColor : c.disabledSelectedDayContentColor;
                if (inRange) return enabled ? c.dayInSelectionRangeContentColor : c.disabledDayContentColor;
                if (today && enabled) return c.todayContentColor;
                return enabled ? c.dayContentColor : c.disabledDayContentColor;
            }

            void Day(const std::string& label, const ui::Modifier& modifier, bool selected, std::function<void()> onClick, bool animateChecked, bool enabled,
                     bool today, bool inRange, const std::string& description, const DatePickerColors& colors) {
                const Color containerTarget = selected ? (enabled ? colors.selectedDayContainerColor : colors.disabledSelectedDayContainerColor)
                                                       : Color::Transparent();
                const Color container = animateChecked ? AnimateColorAsState(containerTarget, Value(MotionSchemeKeyTokens::DefaultEffects)) : containerTarget;
                const Color contentTarget = DayContentTarget(colors, today, selected, inRange, enabled);
                // Only a day outside a range animates its content colour.
                const Color content = inRange ? contentTarget : AnimateColorAsState(contentTarget, Value(MotionSchemeKeyTokens::DefaultEffects));
                SurfaceOptions o;
                o.shape = Value(DatePickerModalTokens::DateContainerShape);
                o.color = container;
                if (today && !selected) o.border = BorderStroke(DatePickerModalTokens::DateTodayContainerOutlineWidth, colors.todayDateBorderColor);
                // The whole day is one node, holding its date even while partly scrolled out.
                ui::Modifier m = Semantics(modifier, [description](ui::SemanticsConfiguration& c) {
                    c.text = { description };
                    c.role = ui::Role::Button;
                }, true);
                Surface(selected, std::move(onClick), m, o, [label, content] {
                    Box(RequiredSize({}, DatePickerModalTokens::DateContainerWidth, DatePickerModalTokens::DateContainerHeight), ui::Alignment::Center(), false,
                        [label, content] {
                            TextOptions t;
                            t.color = content;
                            t.textAlign = text::TextAlign::Center;
                            Text(label, ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {}), t);
                        });
                }, enabled);
            }

            bool IsDirection(const ui::KeyEvent& e, int key) { return e.type == ui::KeyEventType::KeyDown && e.keyCode == key; }
            bool IsTab(const ui::KeyEvent& e) { return e.type == ui::KeyEventType::KeyDown && e.keyCode == ui::keys::Tab && !e.Shift(); }
            bool IsShiftTab(const ui::KeyEvent& e) { return e.type == ui::KeyEventType::KeyDown && e.keyCode == ui::keys::Tab && e.Shift(); }

            rt::Task<> GoToMonth(LazyListState* state, int delta, ui::Owner* owner, ui::FocusDirection direction) {
                co_await state->AnimateScrollToItem(state->FirstVisibleItemIndex() + delta);
                if (owner) owner->MoveFocus(direction);
            }

            int FirstEnabledCell(const CalendarMonth& month, const SelectableDates& s) {
                int first = month.daysFromStartOfWeekToFirstOfMonth;
                const int last = month.daysFromStartOfWeekToFirstOfMonth + month.numberOfDays - 1;
                if (s.IsSelectableYear(month.year))
                    for (int day = 0; !s.IsSelectableDate(month.startUtcTimeMillis + day * MillisecondsIn24Hours) && first <= last; ++day) ++first;
                return first;
            }
            int LastEnabledCell(const CalendarMonth& month, const SelectableDates& s) {
                const int first = month.daysFromStartOfWeekToFirstOfMonth;
                int last = month.daysFromStartOfWeekToFirstOfMonth + month.numberOfDays - 1;
                if (s.IsSelectableYear(month.year))
                    for (int day = 0; !s.IsSelectableDate(month.EndUtcTimeMillis() - day * MillisecondsIn24Hours) && last >= first; ++day) --last;
                return last;
            }
        }

        void Month(const CalendarMonth& month, std::function<void(std::int64_t)> onSelect, std::int64_t todayMillis,
                   std::optional<std::int64_t> startMillis, std::optional<std::int64_t> endMillis, std::optional<SelectedRangeInfo> rangeInfo,
                   const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable, const DatePickerColors& colors,
                   const std::string& locale, LazyListState& listState, bool keyboardNavigation, std::function<void()> onReturnFocus) {
            auto& c = *rt::Composition::Current();
            auto scope = RememberCoroutineScope();
            auto* owner = c.Read(ui::LocalOwner);
            const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const int firstEnabled = FirstEnabledCell(month, *selectable);
            const int lastEnabled = LastEnabledCell(month, *selectable);
            const Dp minimum = c.Read(LocalMinimumInteractiveComponentSize);
            ui::Modifier m = RequiredHeightDp({}, RecommendedSizeForAccessibility * float(MaxCalendarRows));
            if (rangeInfo) {
                const Color rangeColor = colors.dayInSelectionRangeContainerColor;
                const SelectedRangeInfo info = *rangeInfo;
                m = DrawWithContent(std::move(m), [info, rangeColor](ui::ContentDrawScope& s) {
                    DrawRangeBackground(s, info, rangeColor);
                    s.DrawContent();
                });
            }
            LazyListState* state = &listState;
            Column(m, Arrangement::SpaceEvenly(), ui::AlignmentH::Start(), [=] {
                for (int week = 0; week < MaxCalendarRows; ++week) {
                    Row(FillMax({}, Axis::Horizontal), Arrangement::SpaceEvenly(), ui::AlignmentV::CenterVertically(), [=] {
                        for (int d = 0; d < DaysInWeek; ++d) {
                            const int cell = week * DaysInWeek + d;
                            if (cell < month.daysFromStartOfWeekToFirstOfMonth || cell >= month.daysFromStartOfWeekToFirstOfMonth + month.numberOfDays) {
                                // An empty cell, as large as a day.
                                Spacer(SizeMod(SizeIn({}, DatePickerModalTokens::DateContainerWidth, DatePickerModalTokens::DateContainerHeight,
                                                      Dp::Unspecified(), Dp::Unspecified()), minimum, minimum));
                                continue;
                            }
                            const int dayNumber = cell - month.daysFromStartOfWeekToFirstOfMonth;
                            const std::int64_t date = month.startUtcTimeMillis + dayNumber * MillisecondsIn24Hours;
                            const bool isToday = date == todayMillis;
                            const bool startSelected = startMillis && date == *startMillis;
                            const bool endSelected = endMillis && date == *endMillis;
                            const bool inRange = rangeInfo && startMillis && endMillis && date >= *startMillis && date <= *endMillis;
                            const auto prefix = DayContentDescription(rangeInfo.has_value(), isToday, startSelected, endSelected, inRange);
                            const std::string formatted = formatter->FormatDate(date, locale, true).value_or("");
                            const bool enabled = selectable->IsSelectableYear(month.year) && selectable->IsSelectableDate(date);
                            ui::Modifier dayModifier;
                            if (keyboardNavigation) {
                                const bool isFirst = cell == firstEnabled, isLast = cell == lastEnabled;
                                const int back = rtl ? ui::keys::DpadRight : ui::keys::DpadLeft, forward = rtl ? ui::keys::DpadLeft : ui::keys::DpadRight;
                                dayModifier = OnKeyEvent({}, [=](const ui::KeyEvent& e) {
                                    if (!owner) return false;
                                    if (isFirst) {
                                        // Shift+Tab goes back to the next-month button.
                                        if (IsShiftTab(e)) { if (onReturnFocus) onReturnFocus(); return true; }
                                        if (state->IsScrollInProgress()) return true;
                                        if (IsDirection(e, back)) { scope->Launch(GoToMonth(state, -1, owner, ui::FocusDirection::Previous)); return true; }
                                        if (IsDirection(e, forward)) { owner->MoveFocus(ui::FocusDirection::Next); return true; }
                                        return false;
                                    }
                                    if (isLast) {
                                        // Tab leaves the days downwards.
                                        if (IsTab(e)) { owner->MoveFocus(ui::FocusDirection::Down); return true; }
                                        if (state->IsScrollInProgress()) return true;
                                        if (IsDirection(e, forward)) { scope->Launch(GoToMonth(state, 1, owner, ui::FocusDirection::Next)); return true; }
                                        if (IsDirection(e, back)) { owner->MoveFocus(ui::FocusDirection::Previous); return true; }
                                        return false;
                                    }
                                    // Left and right stay within the month.
                                    if (IsDirection(e, forward)) { owner->MoveFocus(ui::FocusDirection::Next); return true; }
                                    if (IsDirection(e, back)) { owner->MoveFocus(ui::FocusDirection::Previous); return true; }
                                    return false;
                                });
                            }
                            Day(ToLocalString(dayNumber + 1, locale), dayModifier, startSelected || endSelected, [onSelect, date] { onSelect(date); },
                                // Only the first selected day animates (a range's end draws over its marker).
                                startSelected, enabled, isToday, inRange, prefix ? *prefix + ", " + formatted : formatted, colors);
                        }
                    }, rt::Key(week));
                }
            });
        }

        // --- date input -------------------------------------------------------------------------

        DateInputValidator::DateInputValidator(IntRange yearRange, SelectableDatesPtr selectable, DateInputFormat format, DatePickerFormatterPtr formatter,
                                               std::string errorPattern, std::string errorRange, std::string errorNotAllowed, std::string errorInvalidRange)
            : m_YearRange(yearRange), m_SelectableDates(std::move(selectable)), m_Format(std::move(format)), m_Formatter(std::move(formatter)),
              m_ErrorDatePattern(std::move(errorPattern)), m_ErrorOutOfYearRange(std::move(errorRange)), m_ErrorNotAllowed(std::move(errorNotAllowed)),
              m_ErrorInvalidRange(std::move(errorInvalidRange)) {}

        namespace {
            std::string Upper(std::string s) {
                for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
                return s;
            }
            // String.format with %1$s / %2$s.
            std::string FormatString(const std::string& tmpl, std::initializer_list<std::string> args, const std::string& locale) {
                std::vector<text::FormatArg> a;
                for (const auto& s : args) a.emplace_back(s);
                return text::FormatJava(tmpl, std::span<const text::FormatArg>(a), locale);
            }
        }

        std::string DateInputValidator::Validate(const std::optional<CalendarDate>& date, InputIdentifier id, const std::string& locale) const {
            if (!date) return FormatString(m_ErrorDatePattern, { Upper(m_Format.patternWithDelimiters) }, locale);
            if (!m_YearRange.Contains(date->year))
                return FormatString(m_ErrorOutOfYearRange, { ToLocalString(m_YearRange.first, locale), ToLocalString(m_YearRange.last, locale) }, locale);
            if (!m_SelectableDates->IsSelectableYear(date->year) || !m_SelectableDates->IsSelectableDate(date->utcTimeMillis))
                return FormatString(m_ErrorNotAllowed, { m_Formatter->FormatDate(date->utcTimeMillis, locale).value_or("") }, locale);
            // A range's start after its end, or its end before its start.
            if ((id == InputIdentifier::StartDateInput && currentEndDateMillis && date->utcTimeMillis > *currentEndDateMillis) ||
                (id == InputIdentifier::EndDateInput && currentStartDateMillis && date->utcTimeMillis < *currentStartDateMillis))
                return m_ErrorInvalidRange;
            return {};
        }

        namespace {
            std::string Utf8(std::u16string_view s) {
                std::string out;
                for (char16_t c : s) out += c < 0x80 ? char(c) : '?';
                return out;
            }
            std::u16string Trim(std::u16string_view s) {
                std::size_t a = 0, b = s.size();
                while (a < b && (s[a] == u' ' || s[a] == u'\t')) ++a;
                while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t')) --b;
                return std::u16string(s.substr(a, b - a));
            }
            bool AllDigits(std::u16string_view s) {
                for (char16_t c : s) if (c < u'0' || c > u'9') return false;
                return true;
            }
        }

        namespace {
            // A coroutine of its own: a lambda coroutine's captures go with the lambda.
            // The years are composed while the grid measures, after effects start: a frame later
            // (Compose's effect coroutines are dispatched, so they also start after that layout).
            rt::Task<> FocusAfterLayout(ui::FocusRequester* focusRequester) {
                co_await rt::NextFrame{};
                focusRequester->RequestFocus();
            }
            rt::Task<> FocusAfterDelay(ui::FocusRequester* focusRequester) {
                if (!focusRequester) co_return;
                co_await rt::Delay(300);   // MotionTokens.DurationMedium2
                focusRequester->RequestFocus();
            }
        }

        void DateInputTextField(const ui::Modifier& modifier, std::optional<std::int64_t> initialDateMillis,
                                std::function<void(std::optional<std::int64_t>)> onDateSelectionChange, const CalendarModel& calendarModel,
                                std::function<void()> label, std::function<void()> placeholder, InputIdentifier inputIdentifier,
                                std::shared_ptr<DateInputValidator> validator, const DateInputFormat& format, const std::string& locale,
                                const DatePickerColors& colors, ui::FocusRequester* focusRequester) {
            auto& c = *rt::Composition::Current();
            const std::string pattern = format.PatternWithoutDelimiters();
            const std::string initialText = initialDateMillis ? calendarModel.FormatWithPattern(*initialDateMillis, pattern) : std::string();
            auto& state = RememberTextFieldState(initialText);
            const CalendarModel* model = &calendarModel;

            // The error, once the text is as long as the pattern.
            std::string errorText;
            const std::u16string trimmed = Trim(state.Text());
            if (int(trimmed.size()) == int(pattern.size()))
                errorText = validator->Validate(model->Parse(Utf8(trimmed), pattern), inputIdentifier, locale);

            // DateInputTransformation: digits only, no longer than the pattern; a complete valid
            // date is selected, anything else clears the selection.
            auto input = InputTransformationOf([pattern, model, validator, inputIdentifier, locale, onDateSelectionChange](TextFieldBuffer& b) {
                if (b.Length() > int(pattern.size()) || !AllDigits(b.AsCharSequence())) { b.RevertAllChanges(); return; }
                const std::u16string text = Trim(b.AsCharSequence());
                if (int(text.size()) < int(pattern.size())) { onDateSelectionChange(std::nullopt); return; }
                const auto parsed = model->Parse(Utf8(text), pattern);
                const std::string error = validator->Validate(parsed, inputIdentifier, locale);
                onDateSelectionChange(error.empty() && parsed ? std::optional<std::int64_t>(parsed->utcTimeMillis) : std::nullopt);
            });
            // DateOutputTransformation: the delimiters appear as the date is typed.
            const int first = int(format.patternWithDelimiters.find(format.delimiter));
            const int second = int(format.patternWithDelimiters.rfind(format.delimiter));
            const std::u16string delimiter(1, char16_t(format.delimiter));
            auto output = OutputTransformationOf([first, second, delimiter](TextFieldBuffer& b) {
                if (first >= 0 && first <= b.Length()) b.Insert(first, delimiter);
                if (second > first && second <= b.Length()) b.Insert(second, delimiter);
            });

            // With an error, the supporting text's padding comes out of the bottom padding, so the
            // field keeps its height.
            Dp bottom{ 16 };
            if (!errorText.empty()) {
                const PaddingValues p = SupportingTextPadding();
                bottom = Dp(16) - (p.bottom + p.top);
            }
            ui::Modifier m = Padding(modifier, { Dp(0), Dp(0), Dp(0), bottom });
            m = Semantics(std::move(m), [errorText](ui::SemanticsConfiguration& sc) { if (!errorText.empty()) sc.error = errorText; });
            if (focusRequester) m = FocusRequesterModifier(std::move(m), *focusRequester);
            TextFieldOptions o;
            if (label) o.label = [label](const TextFieldLabelScope&) { label(); };
            o.placeholder = placeholder;
            // Always present (empty without an error), as Compose's slot is.
            o.supportingText = [errorText] { if (!errorText.empty()) Text(errorText); };
            o.isError = !errorText.empty();
            o.inputTransformation = input;
            o.outputTransformation = output;
            o.keyboardOptions.autoCorrectEnabled = false;
            o.keyboardOptions.keyboardType = ui::KeyboardType::Number;
            o.keyboardOptions.imeAction = ui::ImeAction::Done;
            o.lineLimits = TextFieldLineLimits::SingleLine();
            o.colors = colors.dateTextFieldColors;
            OutlinedTextField(state, m, o);

            // Focus after a short delay (a dialog may still be animating in).
            TextFieldState* st = &state;
            c.LaunchedEffect({}, [focusRequester] { return FocusAfterDelay(focusRequester); });
            // A new initial date replaces the text.
            c.LaunchedEffect(rt::Keys(initialDateMillis.value_or(INT64_MIN)), [initialDateMillis, model, pattern, st]() -> rt::Task<> {
                if (initialDateMillis) {
                    const std::string text = model->FormatWithPattern(*initialDateMillis, pattern);
                    if (Utf8(st->Text()) != text) st->SetTextAndPlaceCursorAtEnd(text);
                }
                co_return;
            });
        }

        namespace {
            void DateInputContent(std::optional<std::int64_t> selected, std::function<void(std::optional<std::int64_t>)> onSelect, const CalendarModel& model,
                                  const IntRange& yearRange, const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable,
                                  const DatePickerColors& colors, ui::FocusRequester* focusRequester) {
                auto& c = *rt::Composition::Current();
                const DateInputFormat format = model.GetDateInputFormat();
                auto validator = c.Remember<std::shared_ptr<DateInputValidator>>(
                    [&] {
                        return std::make_shared<DateInputValidator>(yearRange, selectable, format, formatter, GetString(Strings::DateInputInvalidForPattern),
                                                                    GetString(Strings::DateInputInvalidYearRange),
                                                                    GetString(Strings::DateInputInvalidNotAllowed), "");
                    },
                    rt::Keys(format.patternWithDelimiters, reinterpret_cast<std::uintptr_t>(formatter.get())));
                // A single date: only the start applies.
                validator->currentStartDateMillis = selected;
                const std::string pattern = Upper(format.patternWithDelimiters);
                const std::string labelText = GetString(Strings::DateInputLabel);
                DateInputTextField(
                    Padding(FillMax({}, Axis::Horizontal), { InputTextFieldPaddingStart, InputTextFieldPaddingTop, InputTextFieldPaddingEnd, Dp(0) }), selected,
                    std::move(onSelect), model,
                    [labelText, pattern] {
                        Text(labelText, Semantics({}, [labelText, pattern](ui::SemanticsConfiguration& sc) { sc.contentDescription = { labelText + ", " + pattern }; }));
                    },
                    [pattern] { Text(pattern, ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {})); }, InputIdentifier::SingleDateInput, validator,
                    format, model.Locale(), colors, focusRequester);
            }

            // --- the calendar ---------------------------------------------------------------------

            void Year(const std::string& label, const ui::Modifier& modifier, bool selected, bool currentYear, std::function<void()> onClick, bool enabled,
                      const std::string& description, const DatePickerColors& colors) {
                const Color containerTarget = selected ? (enabled ? colors.selectedYearContainerColor : colors.disabledSelectedYearContainerColor)
                                                       : Color::Transparent();
                const Color contentTarget = selected ? (enabled ? colors.selectedYearContentColor : colors.disabledSelectedYearContentColor)
                                          : currentYear && enabled ? colors.currentYearContentColor
                                          : enabled ? colors.yearContentColor : colors.disabledYearContentColor;
                const auto spec = Value(MotionSchemeKeyTokens::DefaultEffects);
                const Color container = AnimateColorAsState(containerTarget, spec);
                const Color content = AnimateColorAsState(contentTarget, spec);
                SurfaceOptions o;
                o.shape = Value(DatePickerModalTokens::SelectionYearStateLayerShape);
                o.color = container;
                // The current year is outlined like today.
                if (currentYear && !selected) o.border = BorderStroke(DatePickerModalTokens::DateTodayContainerOutlineWidth, colors.todayDateBorderColor);
                ui::Modifier m = Semantics(modifier, [description](ui::SemanticsConfiguration& c) {
                    c.text = { description };
                    c.role = ui::Role::Button;
                }, true);
                Surface(selected, std::move(onClick), m, o, [label, content] {
                    Box(FillMax({}, Axis::Horizontal), ui::Alignment::Center(), false, [label, content] {
                        TextOptions t;
                        t.color = content;
                        t.textAlign = text::TextAlign::Center;
                        Text(label, ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {}), t);
                    });
                }, enabled);
            }

            void YearPicker(const ui::Modifier& modifier, std::int64_t displayedMonthMillis, std::function<void(int)> onYearSelected,
                            const SelectableDatesPtr& selectable, const CalendarModel& model, const IntRange& yearRange, const DatePickerColors& colors,
                            ui::FocusRequester* currentYearFocusRequester, std::function<void()> onShiftTab, std::function<void()> onTab) {
                auto& c = *rt::Composition::Current();
                const CalendarModel* m = &model;
                ProvideTextStyle(Value(DatePickerModalTokens::SelectionYearLabelTextFont), [=] {
                    const int currentYear = m->Today().year;
                    const int displayedYear = m->GetMonth(displayedMonthMillis).year;
                    // Starts a row above the displayed year, for quicker picks of earlier years.
                    auto& grid = RememberLazyGridState(std::max(0, displayedYear - yearRange.first - YearsInRow));
                    LazyGridOptions go;
                    go.crossAxisArrangement = Arrangement::SpaceEvenly();
                    go.mainAxisArrangement = Arrangement::SpacedBy(Dp(16));
                    const std::string locale = m->Locale();
                    const std::string templ = GetString(Strings::DatePickerNavigateToYearDescription);
                    LazyVerticalGrid(Background(modifier, colors.containerColor), GridCells::Fixed(YearsInRow), grid, [=](LazyGridScope& s) {
                        s.Items(yearRange.Count(), [=](int i) {
                            const int year = i + yearRange.first;
                            const std::string localized = ToLocalString(year, locale);
                            ui::Modifier m = RequiredSize({}, DatePickerModalTokens::SelectionYearContainerWidth, DatePickerModalTokens::SelectionYearContainerHeight);
                            m = OnKeyEvent(std::move(m), [onShiftTab, onTab](const ui::KeyEvent& e) {
                                if (IsShiftTab(e)) { onShiftTab(); return true; }
                                if (IsTab(e)) { onTab(); return true; }
                                return false;
                            });
                            if (year == displayedYear) m = FocusRequesterModifier(std::move(m), *currentYearFocusRequester);
                            std::vector<text::FormatArg> args{ localized };
                            Year(localized, m, year == displayedYear, year == currentYear, [onYearSelected, year] { onYearSelected(year); },
                                 selectable->IsSelectableYear(year), text::FormatJava(templ, std::span<const text::FormatArg>(args), locale), colors);
                        });
                    }, go);
                });
                // Keyboard focus starts on the selected year.
                c.LaunchedEffect({}, [currentYearFocusRequester] { return FocusAfterLayout(currentYearFocusRequester); });
            }

            void YearPickerMenuButton(std::function<void()> onClick, bool expanded, const ui::Modifier& modifier, std::function<void()> content) {
                ButtonOptions o;
                o.shape = CircleShape();
                o.colors = ButtonDefaults::textButtonColors().Copy(Color::Unspecified(), rt::Composition::Current()->Read(LocalContentColor));
                o.elevation = std::optional<ButtonElevation>{};
                o.border = std::optional<BorderStroke>{};
                TextButton(std::move(onClick), modifier, o, [=] {
                    content();
                    Spacer(SizeMod({}, ButtonDefaults::IconSpacing, ButtonDefaults::IconSpacing));
                    Icon(Icons::Filled::ArrowDropDown(),
                         GetString(expanded ? Strings::DatePickerSwitchToDaySelection : Strings::DatePickerSwitchToYearSelection),
                         Rotate({}, expanded ? 180.0f : 0.0f));
                });
            }

            void MonthsNavigation(const ui::Modifier& modifier, bool nextAvailable, bool previousAvailable, bool yearPickerVisible,
                                  const std::string& yearPickerText, const ui::Modifier& nextButtonModifier, std::function<void()> onNext,
                                  std::function<void()> onPrevious, std::function<void()> onYearPickerButton, std::function<void()> onYearPickerTab,
                                  ui::FocusRequester* yearSelectionButtonFocusRequester, const DatePickerColors& colors) {
                Row(RequiredHeightDp(FillMax(modifier, Axis::Horizontal), MonthYearHeight),
                    yearPickerVisible ? Arrangement::Start() : Arrangement::SpaceBetween(), ui::AlignmentV::CenterVertically(), [=] {
                        rt::Composition::Current()->Provide(LocalContentColor, colors.navigationContentColor, [=] {
                            ui::Modifier button = FocusRequesterModifier({}, *yearSelectionButtonFocusRequester);
                            button = OnKeyEvent(std::move(button), [yearPickerVisible, onYearPickerTab](const ui::KeyEvent& e) {
                                if (yearPickerVisible && IsTab(e)) { onYearPickerTab(); return true; }
                                return false;
                            });
                            YearPickerMenuButton(onYearPickerButton, yearPickerVisible, button, [yearPickerText, colors] {
                                // Announced as the month changes.
                                TextOptions o;
                                o.color = colors.navigationContentColor;
                                Text(yearPickerText, Semantics({}, [yearPickerText](ui::SemanticsConfiguration& c) {
                                    c.liveRegion = ui::LiveRegionMode::Polite;
                                    c.contentDescription = { yearPickerText };
                                }), o);
                            });
                            // The month arrows, while the years are hidden.
                            if (!yearPickerVisible) {
                                Row({}, [=] {
                                    IconButtonWithTooltip(onPrevious, Icons::AutoMirrored::Filled::KeyboardArrowLeft(),
                                                          GetString(Strings::DatePickerSwitchToPreviousMonth), {}, previousAvailable);
                                    IconButtonWithTooltip(onNext, Icons::AutoMirrored::Filled::KeyboardArrowRight(),
                                                          GetString(Strings::DatePickerSwitchToNextMonth), nextButtonModifier, nextAvailable);
                                });
                            }
                        });
                    });
            }

            rt::Task<> ScrollMonths(LazyListState* state, int delta) {
                // A quick second tap while the list animates may go past the range: ignored.
                const int target = state->FirstVisibleItemIndex() + delta;
                if (target < 0) co_return;
                co_await state->AnimateScrollToItem(target);
            }
            rt::Task<> JumpToMonth(LazyListState* state, int index) {
                state->ScrollToItem(index);
                co_return;
            }

            // The months list's snap: straight to the closest month (no approach).
            FlingBehavior MonthsFlingBehavior(LazyListState& state) {
                auto& c = *rt::Composition::Current();
                const anim::SpecPtr spec = Value(MotionSchemeKeyTokens::DefaultEffects);
                return c.Remember<FlingBehavior>([&state, spec] {
                    struct NoApproach final : SnapLayoutInfoProvider {
                        std::shared_ptr<SnapLayoutInfoProvider> inner;
                        float CalculateApproachOffset(float, float) override { return 0; }
                        float CalculateSnapOffset(float v) override { return inner->CalculateSnapOffset(v); }
                    };
                    auto p = std::make_shared<NoApproach>();
                    p->inner = MakeSnapLayoutInfoProvider(state, SnapPosition::Center());
                    // exponentialDecay()
                    return SnapFlingBehavior(p, [] { return std::make_shared<anim::ExponentialDecay>(); }, spec);
                }, rt::Keys(reinterpret_cast<std::uintptr_t>(&state)));
            }

            void HorizontalMonthsList(LazyListState& listState, std::optional<std::int64_t> selected, std::function<void(std::int64_t)> onSelect,
                                      std::function<void(std::int64_t)> onDisplayedMonthChange, const CalendarModel& model, const IntRange& yearRange,
                                      const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable, const DatePickerColors& colors,
                                      std::function<void()> onReturnFocus) {
                const CalendarDate today = model.Today();
                const CalendarMonth firstMonth = model.GetMonth(yearRange.first, 1);
                const CalendarModel* m = &model;
                LazyListState* state = &listState;
                ProvideTextStyle(Value(DatePickerModalTokens::DateLabelTextFont), [=] {
                    LazyListOptions o;
                    o.flingBehavior = MonthsFlingBehavior(*state);
                    // The screen reader traverses out of the month instead of scrolling the list.
                    ui::Modifier list = Semantics({}, [](ui::SemanticsConfiguration& c) {
                        c.horizontalScrollAxisRange = ui::ScrollAxisRange{ [] { return 0.0f; }, [] { return 0.0f; }, false };
                    });
                    LazyRow(list, *state, [=](LazyListScope& s) {
                        s.Items(NumberOfMonthsInRange(yearRange), [=](int i) {
                            const CalendarMonth month = m->PlusMonths(firstMonth, i);
                            Box(FillParentMaxWidth({}), ui::Alignment::TopStart(), false, [=] {
                                Month(month, onSelect, today.utcTimeMillis, selected, std::nullopt, std::nullopt, formatter, selectable, colors, m->Locale(),
                                      *state, true, onReturnFocus);
                            });
                        });
                    }, o);
                });
                UpdateDisplayedMonth(listState, std::move(onDisplayedMonthChange), model, yearRange);
            }

            void DatePickerContent(std::optional<std::int64_t> selected, std::int64_t displayedMonthMillis, std::function<void(std::int64_t)> onSelect,
                                   std::function<void(std::int64_t)> onDisplayedMonthChange, const CalendarModel& model, const IntRange& yearRange,
                                   const DatePickerFormatterPtr& formatter, const SelectableDatesPtr& selectable, const DatePickerColors& colors) {
                auto& c = *rt::Composition::Current();
                const CalendarMonth displayedMonth = model.GetMonth(displayedMonthMillis);
                const int monthIndex = std::max(0, displayedMonth.IndexIn(yearRange.first));
                auto& listState = RememberLazyListState(monthIndex);
                LazyListState* state = &listState;
                // Scroll to the displayed month when it changes elsewhere (not while scrolling).
                c.LaunchedEffect(rt::Keys(monthIndex), [state, monthIndex]() -> rt::Task<> {
                    if (!state->IsScrollInProgress() && state->FirstVisibleItemIndex() != monthIndex) state->ScrollToItem(monthIndex);
                    co_return;
                });
                auto scope = RememberCoroutineScope();
                auto& yearPickerVisible = rt::RememberSaveableState<bool>(false, rt::AutoSaver<bool>());
                auto* owner = c.Read(ui::LocalOwner);
                auto& focus = c.Remember<std::shared_ptr<std::array<ui::FocusRequester, 4>>>([] { return std::make_shared<std::array<ui::FocusRequester, 4>>(); });
                ui::FocusRequester* nextButton = &(*focus)[0];
                ui::FocusRequester* yearButton = &(*focus)[1];
                ui::FocusRequester* currentYear = &(*focus)[2];
                ui::FocusRequester* divider = &(*focus)[3];
                const int totalMonths = NumberOfMonthsInRange(yearRange);
                rt::State<bool>* visible = &yearPickerVisible;
                const CalendarModel* m = &model;
                Column({}, [=] {
                    MonthsNavigation(Padding({}, DatePickerHorizontalPadding, Dp(0)), monthIndex < totalMonths - 1, monthIndex > 0, visible->Get(),
                                     formatter->FormatMonthYear(displayedMonthMillis, m->Locale()).value_or("-"), FocusRequesterModifier({}, *nextButton),
                                     [scope, state] { scope->Launch(ScrollMonths(state, 1)); },
                                     [scope, state] { scope->Launch(ScrollMonths(state, -1)); }, [visible] { visible->Set(!visible->Peek()); },
                                     [currentYear, owner] {
                                         // From the open years button, Tab goes to the selected year (or the closest).
                                         if (!currentYear->RequestFocus() && owner) owner->MoveFocus(ui::FocusDirection::Down);
                                     },
                                     yearButton, colors);
                    Box({}, ui::Alignment::TopStart(), false, [=] {
                        Column(Padding({}, DatePickerHorizontalPadding, Dp(0)), [=] {
                            WeekDays(colors, *m);
                            HorizontalMonthsList(*state, selected, onSelect, onDisplayedMonthChange, *m, yearRange, formatter, selectable, colors,
                                                 [nextButton] { nextButton->RequestFocus(); });
                        });
                        const auto enter = ExpandVertically(Value(MotionSchemeKeyTokens::DefaultEffects)) + FadeIn(Value(MotionSchemeKeyTokens::DefaultEffects), 0.6f);
                        const auto exit = ShrinkVertically(Value(MotionSchemeKeyTokens::DefaultEffects)) + FadeOut(Value(MotionSchemeKeyTokens::FastEffects));
                        AnimatedVisibility(visible->Get(), ClipToBounds({}), [=] {
                            // A pane title gives the screen reader somewhere to land when the years go.
                            const std::string pane = GetString(Strings::DatePickerYearPickerPaneTitle);
                            Column(Semantics({}, [pane](ui::SemanticsConfiguration& sc) { sc.paneTitle = pane; }), [=] {
                                // As tall as the weekdays and month, less the divider below.
                                YearPicker(Padding(RequiredHeightDp({}, RecommendedSizeForAccessibility * float(MaxCalendarRows + 1) - DividerDefaults::Thickness),
                                                   DatePickerHorizontalPadding, Dp(0)),
                                           displayedMonthMillis,
                                           [=](int year) {
                                               // Back to the months, at the chosen year (same month).
                                               visible->Set(!visible->Peek());
                                               scope->Launch(JumpToMonth(state, (year - yearRange.first) * 12 + displayedMonth.month - 1));
                                           },
                                           selectable, *m, yearRange, colors, currentYear, [yearButton] { yearButton->RequestFocus(); },
                                           [divider, owner] {
                                               divider->RequestFocus();
                                               if (owner) owner->MoveFocus(ui::FocusDirection::Next);
                                           });
                                // A focus target so keyboard focus can reach buttons below the picker;
                                // it passes focus on and never keeps it.
                                ui::Modifier d = FocusRequesterModifier({}, *divider);
                                d = OnKeyEvent(std::move(d), [owner, currentYear](const ui::KeyEvent& e) {
                                    if (e.type != ui::KeyEventType::KeyDown || !owner) return false;
                                    if (e.keyCode == ui::keys::DpadUp) { owner->MoveFocus(ui::FocusDirection::Previous); return true; }
                                    if (e.Shift() && e.keyCode == ui::keys::Tab) { currentYear->RequestFocus(); return true; }
                                    if (e.keyCode == ui::keys::DpadDown || e.keyCode == ui::keys::Tab) { owner->MoveFocus(ui::FocusDirection::Next); return true; }
                                    return false;
                                });
                                // Focus never stays here: still focused once the move that brought it
                                // ends (a Previous from the buttons below), it goes on to the year.
                                d = OnFocusChanged(std::move(d), [focus, divider, currentYear](ui::FocusState f) {
                                    if (!ui::IsFocused(f)) return;
                                    // Holds the requesters until it runs.
                                    rt::Dispatcher::Main().Post([focus, divider, currentYear] {
                                        bool still = false;
                                        divider->FindFocusTarget([&still](ui::FocusTargetNode& t) { still = ui::IsFocused(t.State()); return true; });
                                        if (still) currentYear->RequestFocus();
                                    });
                                });
                                HorizontalDivider(Focusable(std::move(d)), DividerDefaults::Thickness, colors.dividerColor);
                            });
                        }, enter, exit);
                    });
                });
            }

            void SwitchableDateEntryContent(std::optional<std::int64_t> selected, std::int64_t displayedMonthMillis, DisplayMode mode,
                                            std::function<void(std::optional<std::int64_t>)> onSelect, std::function<void(std::int64_t)> onDisplayedMonthChange,
                                            const CalendarModel& model, const IntRange& yearRange, const DatePickerFormatterPtr& formatter,
                                            const SelectableDatesPtr& selectable, const DatePickerColors& colors, ui::FocusRequester* focusRequester) {
                auto& c = *rt::Composition::Current();
                // The navigation part slides a little as the modes swap.
                const int parallax = -int(std::lround(c.Read(ui::LocalDensity).ToPx(Dp(48))));
                const auto effectsIn = Value(MotionSchemeKeyTokens::DefaultEffects), effectsOut = Value(MotionSchemeKeyTokens::FastEffects);
                const auto spatial = Value(MotionSchemeKeyTokens::DefaultSpatial);
                const CalendarModel* m = &model;
                AnimatedContent(rt::Key(int(mode)), Semantics({}, [](ui::SemanticsConfiguration& sc) { sc.isTraversalGroup = true; }),
                    [=](rt::Key key) {
                        if (key == rt::Key(int(DisplayMode::Picker)))
                            DatePickerContent(selected, displayedMonthMillis, [onSelect](std::int64_t d) { onSelect(d); }, onDisplayedMonthChange, *m, yearRange,
                                              formatter, selectable, colors);
                        else
                            DateInputContent(selected, onSelect, *m, yearRange, formatter, selectable, colors, focusRequester);
                    },
                    [=](AnimatedContentTransitionScope& s) {
                        SizeTransform size{ true, [spatial](ui::IntSize, ui::IntSize) { return spatial; } };
                        // Into input: the calendar fades out and the field slides up from below;
                        // back to the picker: the field slides down and the calendar fades in.
                        if (s.targetState == rt::Key(int(DisplayMode::Input)))
                            return Using(TogetherWith(SlideInVertically([](int h) { return h; }, spatial) + FadeIn(effectsIn),
                                                      FadeOut(effectsOut) + SlideOutVertically([parallax](int) { return parallax; }, spatial)),
                                         size);
                        return Using(TogetherWith(SlideInVertically([parallax](int) { return parallax; }, spatial) + FadeIn(effectsIn),
                                                  SlideOutVertically([](int h) { return h; }, spatial) + FadeOut(effectsOut)),
                                     size);
                    });
            }
        }
    }

    // --- DatePicker -----------------------------------------------------------------------------

    namespace {
        constexpr Dp TitleStart{ 24 }, TitleEnd{ 12 }, TitleTop{ 16 }, HeadlineBottom{ 12 };
    }

    void DatePicker(DatePickerState& state, const ui::Modifier& modifier, const DatePickerOptions& options) {
        auto& c = *rt::Composition::Current();
        const DatePickerColors colors = options.colors.value_or(DatePickerDefaults::colors());
        auto& defaultFormatter = c.Remember<DatePickerFormatterPtr>([] { return DatePickerDefaults::dateFormatter(); });
        const DatePickerFormatterPtr formatter = options.dateFormatter ? options.dateFormatter : defaultFormatter;
        auto& defaultFocus = c.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
        ui::FocusRequester* focusRequester = options.focusRequester ? *options.focusRequester : defaultFocus.get();
        DatePickerState* st = &state;

        std::function<void()> title = options.title ? *options.title : std::function<void()>([st, colors] {
            DatePickerDefaults::DatePickerTitle(st->GetDisplayMode(), Padding({}, { TitleStart, TitleTop, TitleEnd, Dp(0) }), colors.titleContentColor);
        });
        std::function<void()> headline = options.headline ? *options.headline : std::function<void()>([st, formatter, colors] {
            DatePickerDefaults::DatePickerHeadline(st->SelectedDateMillis(), st->GetDisplayMode(), formatter,
                                                   Padding({}, { TitleStart, Dp(0), TitleEnd, HeadlineBottom }), colors.headlineContentColor);
        });
        std::function<void()> toggle;
        if (options.showModeToggle)
            toggle = [st, colors] {
                mi::DisplayModeToggleButton(Padding({}, { Dp(0), Dp(0), Dp(12), Dp(12) }), st->GetDisplayMode(),
                                            [st](DisplayMode m) { st->SetDisplayMode(m); }, colors);
            };
        const auto selectable = state.GetSelectableDates();
        mi::DateEntryContainer(modifier, title, headline, toggle, colors, Value(DatePickerModalTokens::HeaderHeadlineFont),
                               DatePickerModalTokens::HeaderContainerHeight, [st, formatter, selectable, colors, focusRequester] {
                                   mi::SwitchableDateEntryContent(
                                       st->SelectedDateMillis(), st->DisplayedMonthMillis(), st->GetDisplayMode(),
                                       [st](std::optional<std::int64_t> d) { st->SetSelectedDateMillis(d); },
                                       [st](std::int64_t month) { st->SetDisplayedMonthMillis(month); }, st->Calendar(), st->YearRange(), formatter,
                                       selectable, colors, focusRequester);
                               });
    }

}

#include "kiln/material3/AlertDialog.h"
#include "kiln/material3/internal/AlertDialogInternal.h"
#include "kiln/material3/tokens/DialogTokens.h"

namespace kiln::material3 {

    namespace {
        bool IsTabKey(const ui::KeyEvent& e) { return e.type == ui::KeyEventType::KeyDown && e.keyCode == ui::keys::Tab && !e.Shift(); }
        bool IsShiftTabKey(const ui::KeyEvent& e) { return e.type == ui::KeyEventType::KeyDown && e.keyCode == ui::keys::Tab && e.Shift(); }
    }

    void DatePickerDialog(std::function<void()> onDismissRequest, std::function<void()> confirmButton, const ui::Modifier& modifier,
                          const DatePickerDialogOptions& o, std::function<void()> content) {
        const DatePickerColors colors = o.colors.value_or(DatePickerDefaults::colors());
        const ShapePtr shape = o.shape ? o.shape : DatePickerDefaults::shape();
        const Dp elevation = o.tonalElevation;
        const auto dismissButton = o.dismissButton;
        BasicAlertDialog(std::move(onDismissRequest), WrapContent(modifier, Axis::Vertical), o.properties, [=] {
            SurfaceOptions so;
            so.shape = shape;
            so.color = colors.containerColor;
            so.tonalElevation = elevation;
            ui::Modifier sm = SizeIn({}, DatePickerModalTokens::ContainerWidth, Dp::Unspecified(), DatePickerModalTokens::ContainerWidth,
                                     DatePickerModalTokens::ContainerHeight, false);
            Surface(sm, so, [=] {
                Column({}, Arrangement::SpaceBetween(), ui::AlignmentH::Start(), [=] {
                    auto& c = *rt::Composition::Current();
                    auto* owner = c.Read(ui::LocalOwner);
                    const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
                    // The content gives way first, so the buttons stay in view on small screens
                    // (not filling, so switching to input shrinks the dialog).
                    Box(Weight({}, 1, false), ui::Alignment::TopStart(), false, content);
                    // The buttons, at the end.
                    Box(Padding(AlignInColumn({}, ui::AlignmentH::End()), { Dp(0), Dp(0), Dp(6), Dp(8) }), ui::Alignment::TopStart(), false, [=] {
                        mi::ProvideContentColorTextStyle(Value(DialogTokens::ActionLabelTextColor), Value(DialogTokens::ActionLabelTextFont), [=] {
                            const Dp mics = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize);
                            const Dp fromMics = (mics.Specified() ? mics : Dp(0)) - ButtonDefaults::MinHeight();
                            const Dp cross(std::clamp((Dp(8) - fromMics).value, 0.0f, 8.0f));
                            mi::AlertDialogFlowRow(Dp(8), cross, [=] {
                                // Tab order follows the eye: the dismiss button (shown first) before confirm.
                                Box(OnKeyEvent({}, [owner, dismissButton, rtl](const ui::KeyEvent& e) {
                                        if (!owner) return false;
                                        if (IsTabKey(e)) {
                                            owner->MoveFocus(ui::FocusDirection::Next);
                                            // Past the dismiss button too.
                                            if (dismissButton) owner->MoveFocus(ui::FocusDirection::Next);
                                            return true;
                                        }
                                        if (IsShiftTabKey(e) && dismissButton) {
                                            owner->MoveFocus(rtl ? ui::FocusDirection::Right : ui::FocusDirection::Left);
                                            return true;
                                        }
                                        return false;
                                    }), ui::Alignment::TopStart(), false, confirmButton);
                                Box(OnKeyEvent({}, [owner, rtl](const ui::KeyEvent& e) {
                                        if (!owner) return false;
                                        const auto toConfirm = rtl ? ui::FocusDirection::Left : ui::FocusDirection::Right;
                                        if (IsTabKey(e)) {
                                            // A disabled confirm button: move on.
                                            if (!owner->MoveFocus(toConfirm)) owner->MoveFocus(ui::FocusDirection::Next);
                                            return true;
                                        }
                                        if (IsShiftTabKey(e)) {
                                            // Back to the month's last date, by way of the confirm button.
                                            owner->MoveFocus(toConfirm);
                                            owner->MoveFocus(ui::FocusDirection::Previous);
                                            return true;
                                        }
                                        return false;
                                    }), ui::Alignment::TopStart(), false, dismissButton ? dismissButton : std::function<void()>([] {}));
                            });
                        });
                    });
                });
            });
        });
    }

}
