#include "kiln/material3/TimePicker.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/MutatorMutex.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/foundation/text/TextAutoSize.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/DatePicker.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/ScrollField.h"
#include "kiln/material3/Shapes.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/TextFieldDefaults.h"
#include "kiln/material3/ToggleButton.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/TextFieldDecoration.h"
#include "kiln/material3/internal/TimePickerInternal.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/TimeInputTokens.h"
#include "kiln/material3/tokens/TimePickerTokens.h"
#include "kiln/material3/tokens/TypographyKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/text/LocaleFormat.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Locals.h"

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    namespace mi = material3::internal;
    using Sel = TimePickerSelectionMode;

    namespace {
        constexpr int MaxHourValue = 23, MaxMinuteValue = 59, MaxValueForTextField = 99;
        constexpr Dp VibrantTimeFieldWidth{ 100 }, VibrantTimeFieldWidthPortrait{ 132 }, VibrantTimeFieldHeight{ 120 }, VibrantSeparatorWidth{ 16 },
            VibrantPeriodToggleWidth{ 56 }, VibrantPeriodToggleHeight{ 120 }, UncontainedTimeFieldHeight{ 136 }, UncontainedScrollFieldItemHeight{ 60 },
            UncontainedToggleHeight{ 140 }, VibrantPeriodToggleHorizontalHeight{ 40 }, VibrantPeriodTogglePadding{ 8 },
            VibrantPeriodToggleLargePadding{ 16 }, VibrantHorizontalTimePickerGap{ 52 }, VibrantVerticalTimePickerGap{ 12 };

        constexpr float FullCircle = float(std::numbers::pi * 2), HalfCircle = FullCircle / 2, QuarterCircle = float(std::numbers::pi / 2);
        constexpr float RadiansPerMinute = FullCircle / 60, RadiansPerHour = FullCircle / 12;
        constexpr float SeparatorZIndex = 2;
        constexpr float OuterCircleToSizeRatio = 101.0f / TimePickerTokens::ClockDialContainerSize.value;
        constexpr float InnerCircleToSizeRatio = 69.0f / TimePickerTokens::ClockDialContainerSize.value;
        constexpr Dp ClockDisplayBottomMargin{ 36 }, ClockFaceBottomMargin{ 24 }, DisplaySeparatorWidth{ 24 }, SupportLabelTop{ 7 }, MaxDistance{ 74 },
            MinimumInteractiveSize{ 48 };
        constexpr std::array<int, 12> Minutes{ 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55 };
        constexpr std::array<int, 12> Hours{ 12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        constexpr std::array<int, 12> ExtraHours{ 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };   // Hours % 12 + 12
        constexpr Dp PeriodTogglePaddingOld{ 12 }, PeriodTogglePaddingSmall{ 4 };
        constexpr Dp TimePickerMaxHeight{ 384 }, TimePickerMidHeight{ 330 }, ClockDialMidContainerSize{ 238 }, ClockDialMinContainerSize{ 200 };
        constexpr int FxKeypressInvalid = 9;   // AudioManager.FX_KEYPRESS_INVALID

        template <class T> T OrVibrant(const std::optional<TimePickerShapes>& shapes, T standard, T vibrant) { return shapes ? vibrant : standard; }

        bool UseUpdatedToggle(const std::optional<TimePickerShapes>& shapes) { return ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled || shapes; }

        bool IsValidHour(const TimePickerState& s, int hour) {
            if (s.Is24Hour()) return hour >= 0 && hour <= MaxHourValue;
            if (IsPm(s)) return hour >= 12 && hour <= MaxHourValue;
            return hour >= 0 && hour <= 11;
        }

        int HourForDisplay(const TimePickerState& s) {
            if (s.Is24Hour()) return s.Hour() % 24;
            if (s.Hour() % 12 == 0) return 12;
            return IsPm(s) ? s.Hour() - 12 : s.Hour();
        }

        std::string Locale() { return ui::PlatformLocale(); }
        std::string LocalNumber(int value, int minDigits = 1) { return mi::ToLocalString(value, Locale(), minDigits); }

        std::string NumberContentDescription(Sel selection, bool is24Hour, int number) {
            const Strings id = selection == Sel::Minute ? Strings::TimePickerMinuteSuffix : is24Hour ? Strings::TimePicker24HourSuffix : Strings::TimePickerHourSuffix;
            return GetString(id, { std::int64_t(number) });
        }

        // atan2 turned a quarter clockwise, in [0, 2pi).
        float Atan(float y, float x) {
            const float ret = std::atan2(y, x) - QuarterCircle;
            return ret < 0 ? ret + FullCircle : ret;
        }
        float Dist(float x1, float y1, int x2, int y2) { return std::hypot(float(x2) - x1, float(y2) - y1); }
    }

    // --- state ----------------------------------------------------------------------------------

    void TimePickerState::SetHourInput(int hour) {
        if (IsValidHour(*this, hour)) SetHour(hour);
    }
    void TimePickerState::SetMinuteInput(int minute) {
        if (minute >= 0 && minute <= MaxMinuteValue) SetMinute(minute);
    }
    bool IsPm(const TimePickerState& s) { return s.Hour() >= 12; }
    bool IsHourInputValid(const TimePickerState& s) { return IsValidHour(s, s.HourInput()); }
    bool IsMinuteInputValid(const TimePickerState& s) { return s.MinuteInput() >= 0 && s.MinuteInput() <= MaxMinuteValue; }

    namespace {
        class TimePickerStateImpl final : public TimePickerState {
        public:
            TimePickerStateImpl(int hour, int minute, bool is24Hour, Sel selection)
                : m_Is24Hour(is24Hour), m_Selection(selection), m_Hour(hour), m_Minute(minute), m_HourInput(hour), m_MinuteInput(minute) {
                if (hour < 0 || hour > MaxHourValue) throw std::invalid_argument("initialHour should in [0..23] range");
                if (minute < 0 || minute > MaxMinuteValue) throw std::invalid_argument("initialMinute should be in [0..59] range");
            }
            int Minute() const override { return m_Minute.Get(); }
            void SetMinute(int v) override { m_Minute.Set(v); m_MinuteInput.Set(v); }
            int Hour() const override { return m_Hour.Get(); }
            void SetHour(int v) override { m_Hour.Set(v); m_HourInput.Set(v); }
            int HourInput() const override { return m_HourInput.Get(); }
            void SetHourInput(int v) override { TimePickerState::SetHourInput(v); m_HourInput.Set(v); }
            int MinuteInput() const override { return m_MinuteInput.Get(); }
            void SetMinuteInput(int v) override { TimePickerState::SetMinuteInput(v); m_MinuteInput.Set(v); }
            // A plain field, as Compose's is24hour.
            bool Is24Hour() const override { return m_Is24Hour; }
            void SetIs24Hour(bool v) override { m_Is24Hour = v; }
            Sel Selection() const override { return m_Selection.Get(); }
            void SetSelection(Sel s) override { m_Selection.Set(s); }

        private:
            bool m_Is24Hour;
            rt::State<Sel> m_Selection;
            rt::State<int> m_Hour, m_Minute, m_HourInput, m_MinuteInput;
        };
    }

    std::shared_ptr<TimePickerState> MakeTimePickerState(int initialHour, int initialMinute, bool is24Hour, Sel initialSelection) {
        return std::make_shared<TimePickerStateImpl>(initialHour, initialMinute, is24Hour, initialSelection);
    }

    std::shared_ptr<TimePickerState> RememberTimePickerState(int initialHour, int initialMinute, std::optional<bool> is24Hour, rt::Key key) {
        auto& c = *rt::Composition::Current();
        const bool is24 = is24Hour.value_or(c.Read(ui::LocalIs24HourFormat));
        // [hour, minute, is24hour, selection].
        auto saver = rt::ListSaver<std::shared_ptr<TimePickerState>>(
            [](const std::shared_ptr<TimePickerState>& s) {
                return rt::SavedValue::List{ s->Hour(), s->Minute(), s->Is24Hour() ? 1 : 0, int(s->Selection()) };
            },
            [](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<TimePickerState>> {
                if (l.size() < 3) return std::nullopt;
                const Sel selection = l.size() > 3 && rt::SavedNumber(l[3]).value_or(0) == double(int(Sel::Minute)) ? Sel::Minute : Sel::Hour;
                return MakeTimePickerState(int(rt::SavedNumber(l[0]).value_or(0)), int(rt::SavedNumber(l[1]).value_or(0)),
                                           rt::SavedNumber(l[2]).value_or(0) != 0, selection);
            });
        return rt::RememberSaveable<std::shared_ptr<TimePickerState>>([&] { return MakeTimePickerState(initialHour, initialMinute, is24); }, saver, {}, key);
    }

    namespace internal {
        AnalogTimePickerState::AnalogTimePickerState(TimePickerState& s, std::shared_ptr<std::optional<bool>> override)
            : state(&s), userOverride(override ? std::move(override) : std::make_shared<std::optional<bool>>()) {
            // onRemembered.
            hourAngle = RadiansPerHour * float(s.Hour() % 12) - FullCircle / 4;
            minuteAngle = RadiansPerMinute * float(s.Minute()) - FullCircle / 4;
            m_Anim.Set(std::make_shared<anim::Animatable>(s.Selection() == Sel::Hour ? hourAngle : minuteAngle));
        }
        const std::array<int, 12>& AnalogTimePickerState::ClockFaceValues() const { return Selection() == Sel::Minute ? Minutes : Hours; }
        void AnalogTimePickerState::SetMinute(int value) {
            minuteAngle = RadiansPerMinute * float(value) - FullCircle / 4;
            state->SetMinute(value);
            if (Selection() == Sel::Minute) m_Anim.Set(std::make_shared<anim::Animatable>(minuteAngle));
        }
        void AnalogTimePickerState::SetHour(int value) {
            hourAngle = RadiansPerHour * float(value % 12) - FullCircle / 4;
            state->SetHour(value);
            if (Selection() == Sel::Hour) m_Anim.Set(std::make_shared<anim::Animatable>(hourAngle));
        }
        float AnalogTimePickerState::EndValueForAnimation(float angle) const {
            const float value = m_Anim.Peek()->Float();
            float diff = value - angle;
            while (diff > HalfCircle) diff -= FullCircle;
            while (diff <= -HalfCircle) diff += FullCircle;
            return value - diff;
        }
        bool AnalogTimePickerState::IsUpdated() const {
            const float target = Normalize(m_Anim.Peek()->Target()[0]);
            if (Selection() == Sel::Hour && target == Normalize(hourAngle)) return false;
            if (Selection() == Sel::Minute && target == Normalize(minuteAngle)) return false;
            return true;
        }
        float AnalogTimePickerState::Normalize(float angle) {
            double a = std::fmod(double(angle), 2 * std::numbers::pi);
            if (a < 0) a += 2 * std::numbers::pi;
            return float(a);
        }
        int AnalogTimePickerState::ToHour(float angle) { return int((angle + (RadiansPerHour / 2 + QuarterCircle)) / RadiansPerHour) % 12; }
        int AnalogTimePickerState::ToMinute(float angle) { return int((angle + (RadiansPerMinute / 2 + QuarterCircle)) / RadiansPerMinute) % 60; }
        float AnalogTimePickerState::OffsetAngle(float angle) {
            const float ret = angle + QuarterCircle;
            return ret < 0 ? ret + FullCircle : ret;
        }
    }

    namespace {
        using mi::AnalogTimePickerState;
        using AnalogPtr = std::shared_ptr<AnalogTimePickerState>;

        // Holds the mutex for a coroutine; released when it ends, however it ends.
        struct Mutation {
            MutatorMutex* mutex;
            std::uint64_t token;
            ~Mutation() { mutex->Release(token); }
        };

        rt::Task<> AnimateToCurrent(AnalogPtr s, anim::SpecPtr spec) {
            if (!s->IsUpdated()) co_return;
            const float end = s->EndValueForAnimation(s->Selection() == Sel::Hour ? s->hourAngle : s->minuteAngle);
            auto a = s->Anim();
            Mutation m{ &s->mutex, s->mutex.Acquire(MutatePriority::PreventUserInput, [a] { a->Stop(); }) };
            if (m.token == 0) co_return;
            co_await a->AnimateTo(end, spec);
        }

        rt::Task<> OnGestureEnd(AnalogPtr s, anim::SpecPtr spec) {
            const float end = s->EndValueForAnimation(s->Selection() == Sel::Hour ? s->hourAngle : s->minuteAngle);
            auto a = s->Anim();
            Mutation m{ &s->mutex, s->mutex.Acquire(MutatePriority::PreventUserInput, [a] { a->Stop(); }) };
            if (m.token == 0) co_return;
            co_await a->AnimateTo(end, spec);
        }

        // rotateTo: false when it did not run, or another mutation interrupted it.
        rt::Task<bool> RotateTo(AnalogPtr s, float angle, anim::SpecPtr spec, bool animate) {
            *s->userOverride = false;
            auto a = s->Anim();
            Mutation m{ &s->mutex, s->mutex.Acquire(MutatePriority::UserInput, [a] { a->Stop(); }) };
            if (m.token == 0) co_return false;
            if (s->Selection() == Sel::Hour) {
                s->hourAngle = float(AnalogTimePickerState::ToHour(angle) % 12) * RadiansPerHour;
                s->state->SetHourInput(AnalogTimePickerState::ToHour(s->hourAngle) % 12 + (IsPm(*s) ? 12 : 0));
            } else {
                s->minuteAngle = float(AnalogTimePickerState::ToMinute(angle)) * RadiansPerMinute;
                s->state->SetMinuteInput(AnalogTimePickerState::ToMinute(s->minuteAngle));
            }
            if (!animate) {
                a->SnapTo(AnalogTimePickerState::OffsetAngle(angle));
            } else {
                const float end = s->EndValueForAnimation(AnalogTimePickerState::OffsetAngle(angle));
                co_await a->AnimateTo(end, spec);
            }
            co_return s->mutex.IsHeld(m.token);
        }

        // moveSelector: a 24-hour dial's hour moves between the rings with the distance from the
        // centre.
        void MoveSelector(TimePickerState& s, float x, float y, float maxDist, ui::IntOffset center) {
            if (s.Selection() == Sel::Hour && s.Is24Hour()) {
                const float dist = Dist(x, y, center.x, center.y);
                if (IsPm(s)) s.SetHour(s.Hour() - (dist >= maxDist ? 12 : 0));
                else s.SetHour(s.Hour() + (dist < maxDist ? 12 : 0));
            }
        }

        rt::Task<> OnTap(AnalogPtr s, float x, float y, float maxDist, bool autoSwitchToMinute, ui::IntOffset center, anim::SpecPtr spec) {
            float angle = Atan(y - float(center.y), x - float(center.x));
            // kotlin.math.round: halves to even.
            if (s->Selection() == Sel::Minute) angle = std::nearbyint(angle / RadiansPerMinute / 5.0f) * 5.0f * RadiansPerMinute;
            else angle = std::nearbyint(angle / RadiansPerHour) * RadiansPerHour;
            MoveSelector(*s, x, y, maxDist, center);
            if (!co_await RotateTo(s, angle, spec, true)) co_return;
            if (s->Selection() == Sel::Hour && autoSwitchToMinute) co_await rt::Delay(100);
            if (autoSwitchToMinute) s->SetSelection(Sel::Minute);
        }

        ui::DpOffset SelectorPos(const AnalogTimePickerState& s) {
            const Dp diameter = s.currentDiameter.Get();
            const float scale = diameter.value / TimePickerTokens::ClockDialContainerSize.value;
            const Dp handleRadius = TimePickerTokens::ClockDialSelectorHandleContainerSize / 2 * scale;
            const Dp ring = s.Is24Hour() && IsPm(s) && s.Selection() == Sel::Hour ? diameter * InnerCircleToSizeRatio : diameter * OuterCircleToSizeRatio;
            const Dp selectorLength(std::max(0.0f, (ring - handleRadius).value));
            const Dp length = selectorLength + handleRadius;
            const float angle = s.CurrentAngle();
            return { length * std::cos(angle) + diameter / 2, length * std::sin(angle) + diameter / 2 };
        }
    }

    // --- defaults -------------------------------------------------------------------------------

    namespace TimePickerDefaults {
        TimePickerColors colors() {
            const bool updated = ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled;
            TimePickerColors c;
            c.clockDialColor = Value(TimePickerTokens::ClockDialColor);
            c.clockDialSelectedContentColor = Value(TimePickerTokens::ClockDialSelectedLabelTextColor);
            c.clockDialContentColor = Value(TimePickerTokens::ClockDialUnselectedLabelTextColor);
            c.selectorColor = Value(TimePickerTokens::ClockDialSelectorHandleContainerColor);
            c.containerColor = Value(TimePickerTokens::ContainerColor);
            c.periodSelectorBorderColor = Value(TimePickerTokens::PeriodSelectorOutlineColor);
            c.periodSelectorSelectedContainerColor =
                updated ? Value(ColorSchemeKeyTokens::PrimaryContainer) : Value(TimePickerTokens::PeriodSelectorSelectedContainerColor);
            c.periodSelectorContainerColor = updated ? Value(ColorSchemeKeyTokens::SurfaceContainerLowest) : Color::Transparent();
            c.periodSelectorSelectedContentColor =
                updated ? Value(ColorSchemeKeyTokens::OnPrimaryContainer) : Value(TimePickerTokens::PeriodSelectorSelectedLabelTextColor);
            c.periodSelectorContentColor = Value(TimePickerTokens::PeriodSelectorUnselectedLabelTextColor);
            c.timeSelectorSelectedContainerColor = Value(TimePickerTokens::TimeSelectorSelectedContainerColor);
            c.timeSelectorContainerColor = Value(TimePickerTokens::TimeSelectorUnselectedContainerColor);
            c.timeSelectorSelectedContentColor = Value(TimePickerTokens::TimeSelectorSelectedLabelTextColor);
            c.timeSelectorContentColor = Value(TimePickerTokens::TimeSelectorUnselectedLabelTextColor);
            return c;
        }

        TimePickerColors vibrantColors() {
            TimePickerColors c;
            c.clockDialColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            c.clockDialSelectedContentColor = Value(TimePickerTokens::ClockDialSelectedLabelTextColor);
            c.clockDialContentColor = Value(TimePickerTokens::ClockDialUnselectedLabelTextColor);
            c.selectorColor = Value(TimePickerTokens::ClockDialSelectorHandleContainerColor);
            c.containerColor = Value(ColorSchemeKeyTokens::SurfaceContainer);
            c.periodSelectorBorderColor = Value(TimePickerTokens::PeriodSelectorOutlineColor);
            c.periodSelectorSelectedContainerColor = Value(ColorSchemeKeyTokens::PrimaryContainer);
            c.periodSelectorContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            c.periodSelectorSelectedContentColor = Value(ColorSchemeKeyTokens::OnPrimaryContainer);
            c.periodSelectorContentColor = Value(TimePickerTokens::PeriodSelectorUnselectedLabelTextColor);
            c.timeSelectorSelectedContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            c.timeSelectorContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            c.timeSelectorSelectedContentColor = Value(ColorSchemeKeyTokens::Primary);
            c.timeSelectorContentColor = Value(ColorSchemeKeyTokens::OnSurface);
            return c;
        }

        TimePickerLayoutType layoutType() {
            const auto& config = rt::Composition::Current()->Read(ui::LocalConfiguration);
            return config.screenHeightDp < config.screenWidthDp ? TimePickerLayoutType::Horizontal : TimePickerLayoutType::Vertical;
        }

        TimePickerShapes shapes() { return { Value(ShapeKeyTokens::CornerLarge), Value(ShapeKeyTokens::CornerFull) }; }
    }

    namespace TimeInputDefaults {
        TimeInputColors colors() {
            const bool updated = ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled;
            TimeInputColors c;
            c.containerColor = Value(TimePickerTokens::ContainerColor);
            c.periodSelectorBorderColor = Value(TimePickerTokens::PeriodSelectorOutlineColor);
            c.periodSelectorSelectedContainerColor =
                updated ? Value(ColorSchemeKeyTokens::PrimaryContainer) : Value(TimePickerTokens::PeriodSelectorSelectedContainerColor);
            c.periodSelectorContainerColor = updated ? Value(ColorSchemeKeyTokens::SurfaceContainerLowest) : Color::Transparent();
            c.periodSelectorSelectedContentColor =
                updated ? Value(ColorSchemeKeyTokens::OnPrimaryContainer) : Value(TimePickerTokens::PeriodSelectorSelectedLabelTextColor);
            c.periodSelectorContentColor = Value(TimePickerTokens::PeriodSelectorUnselectedLabelTextColor);
            TextFieldColors t = OutlinedTextFieldDefaults::colors();
            t.focusedContainerColor = Value(TimePickerTokens::TimeSelectorSelectedContainerColor);
            t.unfocusedContainerColor = Value(TimePickerTokens::TimeSelectorUnselectedContainerColor);
            t.focusedTextColor = Value(TimePickerTokens::TimeSelectorSelectedLabelTextColor);
            t.unfocusedTextColor = Value(TimePickerTokens::TimeSelectorUnselectedLabelTextColor);
            t.focusedIndicatorColor = Value(ColorSchemeKeyTokens::Outline);
            t.unfocusedIndicatorColor = Value(ColorSchemeKeyTokens::Outline);
            t.errorContainerColor = Value(ColorSchemeKeyTokens::ErrorContainer);
            t.errorIndicatorColor = Value(ColorSchemeKeyTokens::Error);
            c.timeTextFieldColors = t;
            return c;
        }

        TimeInputColors vibrantColors() {
            TimeInputColors c;
            c.containerColor = Value(ColorSchemeKeyTokens::SurfaceContainer);
            c.periodSelectorBorderColor = Value(TimePickerTokens::PeriodSelectorOutlineColor);
            c.periodSelectorSelectedContainerColor = Value(ColorSchemeKeyTokens::PrimaryContainer);
            c.periodSelectorContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            c.periodSelectorSelectedContentColor = Value(ColorSchemeKeyTokens::OnPrimaryContainer);
            c.periodSelectorContentColor = Value(TimePickerTokens::PeriodSelectorUnselectedLabelTextColor);
            TextFieldColors t = OutlinedTextFieldDefaults::colors();
            t.focusedContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            t.unfocusedContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerLowest);
            t.focusedTextColor = Value(ColorSchemeKeyTokens::Primary);
            t.unfocusedTextColor = Value(ColorSchemeKeyTokens::OnSurface);
            t.focusedIndicatorColor = Value(ColorSchemeKeyTokens::Primary);
            t.unfocusedIndicatorColor = Color::Transparent();
            t.errorContainerColor = Value(ColorSchemeKeyTokens::ErrorContainer);
            t.errorIndicatorColor = Value(ColorSchemeKeyTokens::Error);
            c.timeTextFieldColors = t;
            return c;
        }
    }

    namespace {
        TimeInputColors ToTimeInputColors(const TimePickerColors& p) {
            TextFieldColors t = OutlinedTextFieldDefaults::colors();
            t.focusedContainerColor = p.timeSelectorSelectedContainerColor;
            t.unfocusedContainerColor = p.timeSelectorContainerColor;
            t.focusedTextColor = p.timeSelectorSelectedContentColor;
            t.unfocusedTextColor = p.timeSelectorContentColor;
            return { p.containerColor, p.periodSelectorBorderColor, p.periodSelectorSelectedContainerColor, p.periodSelectorContainerColor,
                     p.periodSelectorSelectedContentColor, p.periodSelectorContentColor, t };
        }

        // --- the clock display (hour : minute, AM / PM) -----------------------------------------

        ui::Modifier ClearedSemantics(ui::Modifier m) { return ClearAndSetSemantics(std::move(m), [](ui::SemanticsConfiguration&) {}); }

        void DisplaySeparator(const ui::Modifier& modifier) {
            text::TextStyle style = rt::Composition::Current()->Read(LocalTextStyle());
            style.WithAlign(text::TextAlign::Center);
            style.paragraph.lineHeightStyle = text::LineHeightStyle{ text::LineHeightStyle::Center, text::LineHeightStyle::Trim::Both };
            Box(ClearedSemantics(modifier), ui::Alignment::Center(), false, [style] {
                TextOptions o;
                o.color = Value(TimeInputTokens::TimeFieldSeparatorColor);
                o.style = style;
                Text(":", OffsetMod({}, Dp(0), Dp(-4)), o);
            });
        }

        void TimeSelector(const ui::Modifier& modifier, int value, TimePickerState* st, Sel selection, const TimeInputColors& colors, bool isValid,
                          const std::optional<TimePickerShapes>& shapes, std::function<void()> onSelectorActivated = {}) {
            const bool selected = st->Selection() == selection;
            const std::string description = GetString(selection == Sel::Hour ? Strings::TimePickerHourSelection : Strings::TimePickerMinuteSelection);
            const auto& scheme = MaterialTheme::colorScheme();
            const Color container = isValid ? colors.TimeSelectorContainerColor(selected) : scheme.errorContainer;
            const Color content = isValid ? colors.TimeSelectorContentColor(selected) : scheme.onErrorContainer;
            SurfaceOptions o;
            o.shape = shapes ? shapes->timeFieldShape : Value(TimePickerTokens::TimeSelectorContainerShape);
            o.color = container;
            if (shapes && selected) o.border = BorderStroke(Dp(2), isValid ? scheme.primary : scheme.error);
            ui::Modifier m = Semantics(modifier, [description](ui::SemanticsConfiguration& c) {
                c.role = ui::Role::RadioButton;
                c.contentDescription = { description };
            }, true);
            const std::string valueDescription = NumberContentDescription(selection, st->Is24Hour(), value);
            const std::string text = LocalNumber(value, 2);
            Surface(selected, [st, selection, onSelectorActivated] {
                if (selection != st->Selection()) st->SetSelection(selection);
                if (onSelectorActivated) onSelectorActivated();
            }, m, o, [valueDescription, text, content] {
                Box({}, ui::Alignment::Center(), false, [valueDescription, text, content] {
                    TextOptions t;
                    t.color = content;
                    Text(text, Semantics({}, [valueDescription](ui::SemanticsConfiguration& c) { c.contentDescription = { valueDescription }; }), t);
                });
            });
        }

        void ToggleText(const std::string& text, bool bold, bool autoSized) {
            text::TextStyle style = Value(TimePickerTokens::PeriodSelectorLabelTextFont);
            if (bold) style.WithWeight(text::FontWeight::Bold());
            TextOptions o;
            o.style = style;
            o.maxLines = 1;
            if (autoSized) {
                const text::TextStyle base = Value(TimePickerTokens::PeriodSelectorLabelTextFont);
                o.lineHeight = text::Em(base.paragraph.lineHeight.value / base.span.fontSize.value);
                o.autoSize = TextAutoSize::StepBased(text::Sp(6), base.span.fontSize);
            }
            Text(text, {}, o);
        }

        ToggleButtonOptions PeriodToggleButtonOptions(const TimeInputColors& colors, PaddingValues padding) {
            ToggleButtonOptions o;
            ToggleButtonColors tc = ToggleButtonDefaults::colors();
            tc.containerColor = colors.periodSelectorContainerColor;
            tc.contentColor = colors.periodSelectorContentColor;
            tc.checkedContainerColor = colors.periodSelectorSelectedContainerColor;
            tc.checkedContentColor = colors.periodSelectorSelectedContentColor;
            o.colors = tc;
            o.shapes = ToggleButtonShapes{ CircleShape(), RoundedCorner(Dp(12)), RoundedCorner(Dp(12)) };
            o.contentPadding = padding;
            return o;
        }

        ui::Modifier SelectedSemantics(ui::Modifier m, bool checked) {
            return Semantics(std::move(m), [checked](ui::SemanticsConfiguration& c) { c.selected = checked; });
        }

        // An AM or PM button in the side column (40 dp inside a 48 dp target).
        void SideControlItem(bool checked, std::function<void()> onClick, const TimeInputColors& colors, std::function<void()> content) {
            rt::Composition::Current()->Provide(LocalMinimumInteractiveComponentSize, MinimumInteractiveSize, [=] {
                Box(DefaultMinSize({}, MinimumInteractiveSize, MinimumInteractiveSize), ui::Alignment::Center(), false, [=] {
                    ui::Modifier m = ZIndex({}, checked ? 0.0f : 1.0f);
                    m = Height(std::move(m), Dp(40));
                    m = DefaultMinSize(std::move(m), Dp(40), Dp::Unspecified());
                    ToggleButton(checked, [onClick](bool) { onClick(); }, SelectedSemantics(std::move(m), checked),
                                 PeriodToggleButtonOptions(colors, PaddingValues{ Dp(4), Dp(0), Dp(4), Dp(0) }), content);
                });
            });
        }

        void ToggleItem(bool checked, std::function<void()> onClick, const TimeInputColors& colors, const ShapePtr& shape,
                        const std::optional<TimePickerShapes>& shapes, std::function<void()> content) {
            ui::Modifier m = FillMax(ZIndex({}, checked ? 0.0f : 1.0f), Axis::Both);
            m = SelectedSemantics(std::move(m), checked);
            const PaddingValues padding{ PeriodTogglePaddingSmall, Dp(0), PeriodTogglePaddingSmall, Dp(0) };
            if (UseUpdatedToggle(shapes)) {
                ToggleButton(checked, [onClick](bool) { onClick(); }, m, PeriodToggleButtonOptions(colors, padding), content);
            } else {
                ButtonOptions o;
                o.contentPadding = padding;
                o.shape = shape;
                o.colors = ButtonDefaults::textButtonColors().Copy(colors.PeriodSelectorContainerColor(checked), colors.PeriodSelectorContentColor(checked));
                TextButton(std::move(onClick), m, o, content);
            }
        }

        void PeriodToggleImpl(const ui::Modifier& modifier, TimePickerState* st, const TimeInputColors& colors, std::shared_ptr<ui::MeasurePolicy> policy,
                              ShapePtr startShape, ShapePtr endShape, const std::optional<TimePickerShapes>& shapes, std::function<void()> toggle = {}) {
            const std::string description = GetString(Strings::TimePickerPeriodToggle);
            const bool updated = UseUpdatedToggle(shapes);
            const bool side = bool(toggle);
            auto* analog = dynamic_cast<AnalogTimePickerState*>(st);
            ui::Modifier m = OnFocusChanged(modifier, [analog](ui::FocusState f) {
                if (ui::IsFocused(f) && analog) analog->isDialFocusable.Set(false);
            });
            m = Semantics(std::move(m), [description](ui::SemanticsConfiguration& c) {
                c.isTraversalGroup = true;
                c.contentDescription = { description };
            });
            m = Semantics(std::move(m), [](ui::SemanticsConfiguration& c) { c.selectableGroup = true; });
            if (!updated)
                m = Border(std::move(m), BorderStroke(TimePickerTokens::PeriodSelectorOutlineWidth, colors.periodSelectorBorderColor),
                           Value(TimePickerTokens::PeriodSelectorContainerShape));
            if (analog) m = FocusRequesterModifier(std::move(m), analog->amPmNodeFocusRequester);
            const std::string am = GetString(Strings::TimePickerAM), pm = GetString(Strings::TimePickerPM);
            auto toAm = [st] { if (IsPm(*st) && IsHourInputValid(*st)) st->SetHour(st->Hour() - 12); };
            auto toPm = [st] { if (!IsPm(*st) && IsHourInputValid(*st)) st->SetHour(st->Hour() + 12); };
            Layout(m, std::move(policy), [=] {
                const bool pmNow = IsPm(*st);
                if (updated) {
                    if (!st->Is24Hour()) {
                        if (side) {
                            SideControlItem(!pmNow, toAm, colors, [am, pmNow] { ToggleText(am, !pmNow, false); });
                            SideControlItem(pmNow, toPm, colors, [pm, pmNow] { ToggleText(pm, pmNow, false); });
                        } else {
                            ToggleItem(!pmNow, toAm, colors, CircleShape(), shapes, [am, pmNow] { ToggleText(am, !pmNow, true); });
                            ToggleItem(pmNow, toPm, colors, CircleShape(), shapes, [pm, pmNow] { ToggleText(pm, pmNow, true); });
                        }
                    }
                    if (toggle) toggle();
                } else {
                    ToggleItem(!pmNow, toAm, colors, startShape, shapes, [am] { ToggleText(am, false, true); });
                    Spacer(Background(FillMax(ZIndex(LayoutId({}, "Spacer"), SeparatorZIndex), Axis::Both), colors.periodSelectorBorderColor));
                    ToggleItem(pmNow, toPm, colors, endShape, shapes, [pm] { ToggleText(pm, false, true); });
                }
            });
        }

        bool IsSpacer(const ui::Measurable& m) {
            const std::string* id = LayoutIdOf(m);
            return id && *id == "Spacer";
        }

        void HorizontalPeriodToggle(const ui::Modifier& modifier, TimePickerState* st, const TimeInputColors& colors, const std::optional<TimePickerShapes>& shapes) {
            std::shared_ptr<ui::MeasurePolicy> policy;
            if (UseUpdatedToggle(shapes)) {
                const Dp gapDp = OrVibrant(shapes, PeriodTogglePaddingSmall, VibrantSeparatorWidth);
                policy = std::make_shared<ui::LambdaMeasurePolicy>([gapDp](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                    const int gap = s.RoundToPx(gapDp);
                    std::vector<ui::Placeable*> items;
                    for (auto* child : children) items.push_back(&child->Measure({ 0, std::max(0, (c.maxWidth - gap) / 2), 0, c.maxHeight }));
                    return s.Layout(c.maxWidth, c.maxHeight, [items, gap](ui::PlacementScope& p) {
                        if (items.size() < 2) return;
                        p.Place(*items[0], 0, 0);
                        p.Place(*items[1], items[0]->Width() + gap, 0);
                    });
                });
            } else {
                policy = std::make_shared<ui::LambdaMeasurePolicy>([](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                    ui::Placeable* spacer = nullptr;
                    std::vector<ui::Placeable*> items;
                    for (auto* child : children) {
                        if (IsSpacer(*child)) spacer = &child->Measure({ 0, s.RoundToPx(TimePickerTokens::PeriodSelectorOutlineWidth), c.minHeight, c.maxHeight });
                        else items.push_back(&child->Measure({ 0, c.maxWidth / 2, c.minHeight, c.maxHeight }));
                    }
                    return s.Layout(c.maxWidth, c.maxHeight, [items, spacer](ui::PlacementScope& p) {
                        if (items.size() < 2) return;
                        p.Place(*items[0], 0, 0);
                        p.Place(*items[1], items[0]->Width(), 0);
                        if (spacer) p.Place(*spacer, items[0]->Width() - spacer->Width() / 2, 0);
                    });
                });
            }
            const auto shape = std::dynamic_pointer_cast<const CornerBasedShape>(
                OrVibrant<ShapePtr>(shapes, Value(TimePickerTokens::PeriodSelectorContainerShape), Value(ShapeKeyTokens::CornerFull)));
            PeriodToggleImpl(modifier, st, colors, policy, Start(shape), End(shape), shapes);
        }

        void VerticalPeriodToggle(const ui::Modifier& modifier, TimePickerState* st, const TimeInputColors& colors, const std::optional<TimePickerShapes>& shapes) {
            std::shared_ptr<ui::MeasurePolicy> policy;
            if (UseUpdatedToggle(shapes)) {
                const Dp gapDp = OrVibrant(shapes, PeriodTogglePaddingSmall, VibrantPeriodTogglePadding);
                policy = std::make_shared<ui::LambdaMeasurePolicy>([gapDp](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                    const int gap = s.RoundToPx(gapDp);
                    std::vector<ui::Placeable*> items;
                    for (auto* child : children) items.push_back(&child->Measure({ 0, c.maxWidth, 0, std::max(0, (c.maxHeight - gap) / 2) }));
                    return s.Layout(c.maxWidth, c.maxHeight, [items, gap](ui::PlacementScope& p) {
                        if (items.size() < 2) return;
                        p.Place(*items[0], 0, 0);
                        p.Place(*items[1], 0, items[0]->Height() + gap);
                    });
                });
            } else {
                policy = std::make_shared<ui::LambdaMeasurePolicy>([](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                    ui::Placeable* spacer = nullptr;
                    std::vector<ui::Placeable*> items;
                    for (auto* child : children) {
                        if (IsSpacer(*child)) spacer = &child->Measure({ c.minWidth, c.maxWidth, 0, s.RoundToPx(TimePickerTokens::PeriodSelectorOutlineWidth) });
                        else items.push_back(&child->Measure({ c.minWidth, c.maxWidth, 0, c.maxHeight / 2 }));
                    }
                    return s.Layout(c.maxWidth, c.maxHeight, [items, spacer](ui::PlacementScope& p) {
                        if (items.size() < 2) return;
                        p.Place(*items[0], 0, 0);
                        p.Place(*items[1], 0, items[0]->Height());
                        if (spacer) p.Place(*spacer, 0, items[0]->Height() - spacer->Height() / 2);
                    });
                });
            }
            const auto shape = std::dynamic_pointer_cast<const CornerBasedShape>(
                OrVibrant<ShapePtr>(shapes, Value(TimePickerTokens::PeriodSelectorContainerShape), Value(ShapeKeyTokens::CornerFull)));
            PeriodToggleImpl(modifier, st, colors, policy, Top(shape), Bottom(shape), shapes);
        }

        // AM, PM and the mode toggle in a column beside the fields (uncontained pickers).
        void SideControlColumn(const ui::Modifier& modifier, TimePickerState* st, const TimeInputColors& colors, const std::optional<TimePickerShapes>& shapes,
                               std::function<void()> toggle) {
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                const int tap = s.RoundToPx(MinimumInteractiveSize);
                std::vector<ui::Placeable*> items;
                int maxItemWidth = 0;
                for (auto* child : children) {
                    items.push_back(&child->Measure({ 0, c.maxWidth, tap, tap }));
                    maxItemWidth = std::max(maxItemWidth, items.back()->Width());
                }
                const int width = std::clamp(std::max(maxItemWidth, tap), c.minWidth, c.maxWidth);
                const int amY = s.RoundToPx(Dp(-4)), pmY = s.RoundToPx(Dp(48 - 4)), toggle3Y = s.RoundToPx(Dp(104 - 12)), toggle1Y = s.RoundToPx(Dp(84));
                const int height = c.maxHeight;
                return s.Layout(width, height, [items, width, height, tap, amY, pmY, toggle3Y, toggle1Y](ui::PlacementScope& p) {
                    auto centred = [width](ui::Placeable* item) { return (width - item->Width()) / 2; };
                    if (items.size() == 3) {
                        // AM and PM (40 dp, 8 dp apart) from the top of the fields, then the
                        // switch 16 dp below; each in a 48 dp target.
                        p.Place(*items[0], centred(items[0]), amY);
                        p.Place(*items[1], centred(items[1]), pmY);
                        p.Place(*items[2], centred(items[2]), toggle3Y);
                    } else if (items.size() == 1) {
                        // The switch alone (24 hours), level with the bottom of the fields.
                        p.Place(*items[0], centred(items[0]), toggle1Y);
                    } else {
                        int y = (height - int(items.size()) * tap) / 2;
                        for (auto* item : items) {
                            p.Place(*item, centred(item), y);
                            y += tap;
                        }
                    }
                });
            });
            PeriodToggleImpl(modifier, st, colors, policy, CircleShape(), CircleShape(), shapes, std::move(toggle));
        }

        void ClockDisplayNumbers(TimePickerState* st, const TimePickerColors& colors, const std::optional<TimePickerShapes>& shapes) {
            const bool portrait = TimePickerDefaults::layoutType() == TimePickerLayoutType::Vertical;
            const Dp vibrantWidth = portrait && st->Is24Hour() ? VibrantTimeFieldWidthPortrait : VibrantTimeFieldWidth;
            auto* analog = dynamic_cast<AnalogTimePickerState*>(st);
            auto onActivate = [analog] { if (analog) analog->isDialFocusable.Set(true); };
            const TimeInputColors inputColors = ToTimeInputColors(colors);
            auto& c = *rt::Composition::Current();
            c.Provide(LocalTextStyle(), Value(TimePickerTokens::TimeSelectorLabelTextFont), [=] {
                // The selectors are always left to right.
                rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Ltr, [=] {
                    Row({}, [=] {
                        auto size = [&] {
                            return SizeMod({}, OrVibrant(shapes, TimePickerTokens::TimeSelectorContainerWidth, vibrantWidth),
                                           OrVibrant(shapes, TimePickerTokens::TimeSelectorContainerHeight, VibrantTimeFieldHeight));
                        };
                        auto notDial = [analog](ui::FocusState f) { if (ui::IsFocused(f) && analog) analog->isDialFocusable.Set(false); };
                        ui::Modifier hour = OnFocusChanged(size(), notDial);
                        if (analog) hour = FocusRequesterModifier(std::move(hour), analog->hourNodeFocusRequester);
                        TimeSelector(hour, HourForDisplay(*st), st, Sel::Hour, inputColors, true, shapes, onActivate);
                        DisplaySeparator(SizeMod({}, OrVibrant(shapes, DisplaySeparatorWidth, VibrantSeparatorWidth),
                                                 OrVibrant(shapes, TimePickerTokens::PeriodSelectorVerticalContainerHeight, VibrantTimeFieldHeight)));
                        ui::Modifier minute = OnFocusChanged(size(), notDial);
                        minute = FocusPropertiesMod(std::move(minute), [analog](ui::FocusProperties& p) {
                            if (analog && analog->isDialFocusable.Peek()) p.down = &analog->dialFocusRequester;
                        });
                        if (analog) minute = FocusRequesterModifier(std::move(minute), analog->minuteNodeFocusRequester);
                        TimeSelector(minute, st->Minute(), st, Sel::Minute, inputColors, true, shapes, onActivate);
                    });
                });
            });
        }

        void HorizontalClockDisplay(TimePickerState* st, const TimePickerColors& colors, const std::optional<TimePickerShapes>& shapes) {
            Column({}, Arrangement::Center(), ui::AlignmentH::Start(), [=] {
                ClockDisplayNumbers(st, colors, shapes);
                if (!st->Is24Hour()) {
                    Box(Padding({}, { Dp(0), VibrantPeriodToggleLargePadding, Dp(0), Dp(0) }), ui::Alignment::TopStart(), false, [=] {
                        HorizontalPeriodToggle(SizeMod({}, TimePickerTokens::PeriodSelectorHorizontalContainerWidth,
                                                       OrVibrant(shapes, TimePickerTokens::PeriodSelectorHorizontalContainerHeight, VibrantPeriodToggleHorizontalHeight)),
                                               st, ToTimeInputColors(colors), shapes);
                    });
                }
            });
        }

        void VerticalClockDisplay(TimePickerState* st, const TimePickerColors& colors, const std::optional<TimePickerShapes>& shapes) {
            const Dp startPadding = ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled ? PeriodTogglePaddingSmall : PeriodTogglePaddingOld;
            Row({}, Arrangement::Center(), ui::AlignmentV::Top(), [=] {
                ClockDisplayNumbers(st, colors, shapes);
                if (!st->Is24Hour()) {
                    Box(Padding({}, { OrVibrant(shapes, startPadding, VibrantPeriodToggleLargePadding), Dp(0), Dp(0), Dp(0) }), ui::Alignment::TopStart(), false,
                        [=] {
                            VerticalPeriodToggle(SizeMod({}, OrVibrant(shapes, TimePickerTokens::PeriodSelectorVerticalContainerWidth, VibrantPeriodToggleWidth),
                                                         OrVibrant(shapes, TimePickerTokens::PeriodSelectorVerticalContainerHeight, VibrantPeriodToggleHeight)),
                                                 st, ToTimeInputColors(colors), shapes);
                        });
                }
            });
        }

        // --- the clock face ---------------------------------------------------------------------

        // Spreads the children evenly on a circle of radius height * ratio (an "InnerCircle"
        // child sits in the middle).
        void CircularLayout(const ui::Modifier& modifier, float radiusToSizeRatio, std::function<void()> content) {
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([radiusToSizeRatio](ui::MeasureScope& s, std::span<ui::Measurable* const> children,
                                                                                         const ui::Constraints& c) {
                const float radius = float(c.maxHeight) * radiusToSizeRatio;
                const ui::Constraints item{ 0, c.maxWidth, 0, c.maxHeight };
                std::vector<ui::Placeable*> items;
                ui::Placeable* inner = nullptr;
                for (auto* child : children) {
                    const std::string* id = LayoutIdOf(*child);
                    if (id && *id == "InnerCircle") inner = &child->Measure(item);
                    else items.push_back(&child->Measure(item));
                }
                const float theta = FullCircle / float(items.size());
                return s.Layout(c.minWidth, c.minHeight, [items, inner, c, radius, theta](ui::PlacementScope& p) {
                    for (std::size_t i = 0; i < items.size(); ++i) {
                        const int cx = c.maxWidth / 2 - items[i]->Width() / 2, cy = c.maxHeight / 2 - items[i]->Height() / 2;
                        const float x = radius * std::cos(theta * float(i) - QuarterCircle) + float(cx);
                        const float y = radius * std::sin(theta * float(i) - QuarterCircle) + float(cy);
                        p.Place(*items[i], int(std::lround(x)), int(std::lround(y)));
                    }
                    if (inner) p.Place(*inner, (c.minWidth - inner->Width()) / 2, (c.minHeight - inner->Height()) / 2);
                });
            });
            Layout(modifier, policy, std::move(content));
        }

        bool IsClick(const ui::KeyEvent& e) {
            return e.type == ui::KeyEventType::KeyUp && (e.keyCode == ui::keys::DpadCenter || e.keyCode == ui::keys::Enter ||
                                                         e.keyCode == ui::keys::NumpadEnter || e.keyCode == ui::keys::Space);
        }

        // Where a number sits in the dial, for taps through accessibility and its selected state.
        struct ClockTextGeometry {
            ui::Offset center;
            ui::IntOffset parentCenter;
            ui::Rect bounds;
        };

        rt::Task<> RequestDialFocus(AnalogPtr s) {
            if (s->isDialFocusable.Peek()) s->dialFocusRequester.RequestFocus();
            co_return;
        }

        void ClockText(const ui::Modifier& modifier, AnalogPtr s, int value, bool autoSwitchToMinute, bool isActiveScreen) {
            auto& c = *rt::Composition::Current();
            auto* owner = c.Read(ui::LocalOwner);
            const float maxDist = c.Read(ui::LocalDensity).ToPx(MaxDistance);
            const ui::Density density = c.Read(ui::LocalDensity);
            auto geometry = c.Remember<std::shared_ptr<ClockTextGeometry>>([] { return std::make_shared<ClockTextGeometry>(); });
            auto source = RememberInteractionSource();
            auto scope = mi::RememberCoroutineScope();
            const Sel selection = s->Selection();
            const std::string description = NumberContentDescription(selection, s->Is24Hour(), value);
            const std::string text = LocalNumber(value);
            bool isTheSelectedValue;
            if (selection == Sel::Hour) isTheSelectedValue = s->Is24Hour() ? value == s->Hour() : value == HourForDisplay(*s);
            else isTheSelectedValue = value == int(std::nearbyint(float(s->Minute()) / 5.0f) * 5) % 60;

            auto onClick = [s, geometry, maxDist, scope](bool autoSwitch) {
                scope->Launch(OnTap(s, geometry->center.x, geometry->center.y, maxDist, autoSwitch, geometry->parentCenter, anim::Snap()));
            };

            if (isTheSelectedValue && isActiveScreen) {
                const bool focusable = s->isDialFocusable.Get();
                c.LaunchedEffect(rt::Keys(focusable), [s] { return RequestDialFocus(s); });
            }

            ui::Modifier m = modifier;
            // Only the selected value takes the dial's focus requester (keyboard navigation).
            if (isTheSelectedValue) m = FocusRequesterModifier(std::move(m), s->dialFocusRequester);
            m = OnGloballyPositioned(std::move(m), [geometry](const LayoutCoordinates& lc) {
                const ui::IntSize parent = lc.ParentSize(), size = lc.GetSize();
                const ui::Offset pos = lc.PositionInParent();
                geometry->parentCenter = { parent.width / 2, parent.height / 2 };
                geometry->bounds = { pos.x, pos.y, pos.x + float(size.width), pos.y + float(size.height) };
                geometry->center = { pos.x + float(size.width) / 2, pos.y + float(size.height) / 2 };
            });
            m = MinimumInteractiveComponentSize(std::move(m));
            m = SizeMod(std::move(m), MinimumInteractiveSize, MinimumInteractiveSize);
            m = OnKeyEvent(std::move(m), [s, owner, onClick](const ui::KeyEvent& e) {
                if (e.type == ui::KeyEventType::KeyDown) {
                    // Arrows loop focus inside the dial.
                    if (e.keyCode == ui::keys::DpadDown || e.keyCode == ui::keys::DpadRight) {
                        if (owner) owner->MoveFocus(ui::FocusDirection::Next);
                        return true;
                    }
                    if (e.keyCode == ui::keys::DpadUp || e.keyCode == ui::keys::DpadLeft) {
                        if (owner) owner->MoveFocus(ui::FocusDirection::Previous);
                        return true;
                    }
                    // Tab and Shift+Tab leave the dial for the selectors.
                    if (e.keyCode == ui::keys::Tab) {
                        if (e.Shift()) {
                            s->isDialFocusable.Set(false);
                            if (s->Selection() == Sel::Hour) s->hourNodeFocusRequester.RequestFocus();
                            else s->minuteNodeFocusRequester.RequestFocus();
                        } else if (s->Selection() == Sel::Hour) {
                            s->minuteNodeFocusRequester.RequestFocus();
                        } else if (s->Is24Hour()) {
                            // No AM / PM: past the minute selector.
                            s->minuteNodeFocusRequester.RequestFocus();
                            if (owner) owner->MoveFocus(ui::FocusDirection::Next);
                        } else {
                            s->amPmNodeFocusRequester.RequestFocus();
                        }
                        return true;
                    }
                }
                if (IsClick(e)) {
                    onClick(false);
                    return true;
                }
                return false;
            });
            m = FocusPropertiesMod(std::move(m), [s, owner, isActiveScreen](ui::FocusProperties& p) {
                p.canFocus = isActiveScreen && s->isDialFocusable.Peek() && owner && owner->GetInputMode() != ui::InputMode::Touch;
            });
            RippleOptions ripple;
            ripple.radius = MinimumInteractiveSize / 2;
            m = Ripple(ripple)->Create(std::move(m), source);
            m = Focusable(std::move(m), ui::Focusability::Always, {}, source);
            // Selected while the selector covers the number (read when asked).
            m = Semantics(std::move(m), [s, geometry, density, onClick, autoSwitchToMinute](ui::SemanticsConfiguration& cfg) {
                cfg.onClick = [onClick, autoSwitchToMinute] { onClick(autoSwitchToMinute); return true; };
                const ui::DpOffset pos = SelectorPos(*s);
                cfg.selected = geometry->bounds.Contains({ density.ToPx(pos.x), density.ToPx(pos.y) });
            }, true);
            const text::TextStyle style = Value(TimePickerTokens::ClockDialLabelTextFont);
            Box(m, ui::Alignment::Center(), false, [text, description, style] {
                TextOptions o;
                o.style = style;
                Text(text, ClearAndSetSemantics({}, [description](ui::SemanticsConfiguration& cfg) { cfg.contentDescription = { description }; }), o);
            });
        }

        // The pointer state of the dial (Compose's ClockDialNode).
        struct DialPointer {
            float offsetX = 0, offsetY = 0;
            ui::IntOffset center;
        };

        float DialMaxDist(const AnalogTimePickerState& s, const ui::Density& d) {
            return d.ToPx(MaxDistance) * float(d.RoundToPx(s.currentDiameter.Peek())) / float(d.RoundToPx(TimePickerTokens::ClockDialContainerSize));
        }

        rt::Task<> DialDragStep(AnalogPtr s, std::shared_ptr<DialPointer> p, ui::Offset drag, anim::SpecPtr spec, ui::Density d) {
            p->offsetX += drag.x;
            p->offsetY += drag.y;
            if (!co_await RotateTo(s, Atan(p->offsetY - float(p->center.y), p->offsetX - float(p->center.x)), spec, false)) co_return;
            MoveSelector(*s, p->offsetX, p->offsetY, DialMaxDist(*s, d), p->center);
        }

        rt::Task<> DialDragEnd(AnalogPtr s, bool autoSwitchToMinute, anim::SpecPtr spec) {
            if (autoSwitchToMinute) s->SetSelection(Sel::Minute);
            co_await OnGestureEnd(s, spec);
        }

        rt::Task<> DialTaps(PointerInputScope& scope, AnalogPtr s, std::shared_ptr<DialPointer> p, bool autoSwitchToMinute, anim::SpecPtr spec,
                            std::shared_ptr<mi::CoroutineScope> coroutines, ui::Owner* owner) {
            const ui::Density d = scope.GetDensity();
            TapCallbacks cb;
            cb.onPress = [owner, p](ui::Offset at) {
                if (owner) owner->ClearFocus();
                p->offsetX = at.x;
                p->offsetY = at.y;
            };
            cb.onTap = [owner, s, p, autoSwitchToMinute, spec, coroutines, d](ui::Offset at) {
                if (owner) owner->ClearFocus();
                coroutines->Launch(OnTap(s, at.x, at.y, DialMaxDist(*s, d), autoSwitchToMinute, p->center, spec));
            };
            co_await DetectTapGestures(scope, std::move(cb));
        }

        rt::Task<> DialDrags(PointerInputScope& scope, AnalogPtr s, std::shared_ptr<DialPointer> p, bool autoSwitchToMinute, anim::SpecPtr spec,
                             std::shared_ptr<mi::CoroutineScope> coroutines) {
            const ui::Density d = scope.GetDensity();
            DragCallbacks cb;
            cb.onDragEnd = [s, autoSwitchToMinute, spec, coroutines](const PointerInputChange&) {
                coroutines->Launch(DialDragEnd(s, autoSwitchToMinute, spec));
            };
            cb.onDrag = [s, p, spec, coroutines, d](const PointerInputChange&, ui::Offset drag) { coroutines->Launch(DialDragStep(s, p, drag, spec, d)); };
            co_await DetectDragGestures(scope, std::nullopt, std::move(cb));
        }

        // drawSelector: the handle, its line and centre dot over the numbers; numbers under the
        // handle show in the selected colour.
        ui::Modifier DrawSelector(ui::Modifier m, AnalogPtr s, const TimePickerColors& colors) {
            const Color selectorColor = colors.selectorColor, selectedContent = colors.ClockDialContentColor(true);
            return DrawWithContent(std::move(m), [s, selectorColor, selectedContent](ui::ContentDrawScope& d) {
                const ui::DpOffset pos = SelectorPos(*s);
                const ui::Offset selector{ d.ToPx(pos.x), d.ToPx(pos.y) };
                const float radius = d.ToPx(TimePickerTokens::ClockDialSelectorHandleContainerSize) / 2.0f * float(d.RoundToPx(s->currentDiameter.Get())) /
                                     float(d.RoundToPx(TimePickerTokens::ClockDialContainerSize));
                auto circle = [](ui::Offset c, float r) {
                    gfx::Path path;
                    path.AddOval({ c.x - r, c.y - r, c.x + r, c.y + r });
                    return path;
                };
                const gfx::Path handle = circle(selector, radius);
                auto drawWith = [&d](gfx::BlendMode mode, const gfx::Path& path, Color color) {
                    d.Save();
                    d.SetBlendMode(mode);
                    d.DrawPath(path, color);
                    d.Restore();
                };
                // Clear the handle's area, draw the numbers, then the handle (clearing the
                // numbers it covers).
                drawWith(gfx::BlendMode::Clear, handle, Color(0xFF000000u));
                d.DrawContent();
                drawWith(gfx::BlendMode::Xor, handle, selectorColor);
                const ui::Size size = d.GetSize();
                const ui::Offset center{ size.width / 2, size.height / 2 };
                const float angle = s->CurrentAngle();
                const ui::Offset end{ selector.x - radius * std::cos(angle), selector.y - radius * std::sin(angle) };
                gfx::Path line;
                line.MoveTo(center.x, center.y);
                line.LineTo(end.x, end.y);
                gfx::Stroke stroke;
                stroke.width = d.ToPx(TimePickerTokens::ClockDialSelectorTrackContainerWidth);
                d.DrawPath(line, selectorColor, &stroke);
                d.DrawPath(circle(center, d.ToPx(TimePickerTokens::ClockDialSelectorCenterContainerSize) / 2), selectorColor);
                // The covered parts of the numbers, in the selected colour.
                drawWith(gfx::BlendMode::DstOver, handle, selectedContent);
            });
        }

    }

    void internal::ClockFace(const ui::Modifier& modifier, std::shared_ptr<AnalogTimePickerState> s, const TimePickerColors& colors, bool autoSwitchToMinute) {
        using namespace kiln::material3;
        {
            auto& c = *rt::Composition::Current();
            auto* owner = c.Read(ui::LocalOwner);
            const ui::Density density = c.Read(ui::LocalDensity);
            // The first and last numbers of each ring, so focus loops.
            auto& requesters = c.Remember<std::shared_ptr<std::array<ui::FocusRequester, 4>>>([] { return std::make_shared<std::array<ui::FocusRequester, 4>>(); });
            auto reqs = requesters;
            ui::FocusRequester* firstOuter = &(*reqs)[0];
            ui::FocusRequester* lastOuter = &(*reqs)[1];
            ui::FocusRequester* firstInner = &(*reqs)[2];
            ui::FocusRequester* lastInner = &(*reqs)[3];
            auto coroutines = mi::RememberCoroutineScope();
            auto& pointer = c.Remember<std::shared_ptr<DialPointer>>([] { return std::make_shared<DialPointer>(); });
            auto p = pointer;
            const anim::SpecPtr spatial = Value(MotionSchemeKeyTokens::DefaultSpatial);
            const Sel selection = s->Selection();

            // A new selection turns the hand to its value.
            auto& lastSelection = c.Remember<std::shared_ptr<Sel>>([selection] { return std::make_shared<Sel>(selection); });
            auto last = lastSelection;
            c.SideEffect([last, selection, s, spatial, coroutines] {
                if (*last == selection) return;
                *last = selection;
                coroutines->Launch(AnimateToCurrent(s, spatial));
            });

            ui::Modifier m = Background(modifier, colors.clockDialColor, CircleShape());
            m = OnSizeChanged(std::move(m), [s, p, density](ui::IntSize size) {
                p->center = { size.width / 2, size.height / 2 };
                s->currentDiameter.Set(density.ToDp(float(size.width)));
            });
            m = PointerInput(std::move(m), rt::Keys(std::uintptr_t(s.get()), autoSwitchToMinute), [=](PointerInputScope& scope) {
                return DialTaps(scope, s, p, autoSwitchToMinute, spatial, coroutines, owner);
            });
            m = PointerInput(std::move(m), rt::Keys(std::uintptr_t(s.get()), autoSwitchToMinute), [=](PointerInputScope& scope) {
                return DialDrags(scope, s, p, autoSwitchToMinute, spatial, coroutines);
            });
            m = DrawSelector(std::move(m), s, colors);
            m = FocusGroup(std::move(m));
            const Color dialContent = colors.ClockDialContentColor(false);
            Crossfade(rt::Key(int(selection)), m, [=](rt::Key screenKey) {
                const bool isActiveScreen = screenKey == rt::Key(int(s->Selection()));
                const auto& screen = screenKey == rt::Key(int(Sel::Minute)) ? Minutes : Hours;
                const bool isMinute = s->Selection() == Sel::Minute;
                const bool is24HourRing = s->Is24Hour() && s->Selection() == Sel::Hour;
                const bool loopsInner = s->Is24Hour() && !isMinute;
                ui::Modifier layout = Semantics(SizeMod({}, TimePickerTokens::ClockDialContainerSize, TimePickerTokens::ClockDialContainerSize),
                                                [](ui::SemanticsConfiguration& cfg) { cfg.selectableGroup = true; });
                CircularLayout(layout, OuterCircleToSizeRatio, [=] {
                    rt::Composition::Current()->Provide(LocalContentColor, dialContent, [=] {
                        const int count = int(screen.size());
                        for (int index = 0; index < count; ++index) {
                            const int outerValue = !s->Is24Hour() || isMinute ? screen[std::size_t(index)] : screen[std::size_t(index)] % 12;
                            ui::Modifier item = Semantics({}, [index](ui::SemanticsConfiguration& cfg) { cfg.traversalIndex = float(index) + 1; });
                            if (index == 0) item = FocusRequesterModifier(std::move(item), *firstOuter);
                            if (index == count - 1) item = FocusRequesterModifier(std::move(item), *lastOuter);
                            // Focus loops: back from the first number to the last (or the inner
                            // ring's), on from the last to the first (or the inner ring).
                            item = FocusPropertiesMod(std::move(item), [=](ui::FocusProperties& fp) {
                                if (index == 0) fp.previous = loopsInner ? lastInner : lastOuter;
                                if (index == count - 1) fp.next = loopsInner ? firstInner : firstOuter;
                            });
                            ClockText(item, s, outerValue, autoSwitchToMinute, isActiveScreen);
                        }
                        if (is24HourRing) {
                            ui::Modifier inner = SizeMod(LayoutId({}, "InnerCircle"), TimePickerTokens::ClockDialContainerSize, TimePickerTokens::ClockDialContainerSize);
                            inner = Background(std::move(inner), Color::Transparent(), CircleShape());
                            CircularLayout(inner, InnerCircleToSizeRatio, [=] {
                                const int extra = int(ExtraHours.size());
                                for (int index = 0; index < extra; ++index) {
                                    const int flatIndex = index + 12;
                                    const bool first = index == 0, lastItem = index == extra - 1;
                                    ui::Modifier item = Semantics({}, [flatIndex](ui::SemanticsConfiguration& cfg) { cfg.traversalIndex = float(flatIndex) + 1; });
                                    if (first) item = FocusRequesterModifier(std::move(item), *firstInner);
                                    if (lastItem) item = FocusRequesterModifier(std::move(item), *lastInner);
                                    // Back out to the outer ring at either end.
                                    item = FocusPropertiesMod(std::move(item), [=](ui::FocusProperties& fp) {
                                        if (first) fp.previous = lastOuter;
                                        if (lastItem) fp.next = firstOuter;
                                    });
                                    ClockText(item, s, ExtraHours[std::size_t(index)], autoSwitchToMinute, isActiveScreen);
                                }
                            });
                        }
                    });
                });
            }, Value(MotionSchemeKeyTokens::DefaultEffects));
        }
    }

    ui::Modifier internal::ClockFaceSizeModifier(ui::Modifier m) {
        {
            return LayoutMod(std::move(m), [](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                const Dp max = c.HasBoundedHeight() ? s.ToDp(float(c.maxHeight)) : Dp(INFINITY);
                const Dp size = max.value >= TimePickerMaxHeight.value   ? TimePickerTokens::ClockDialContainerSize
                                : max.value >= TimePickerMidHeight.value ? ClockDialMidContainerSize
                                                                          : ClockDialMinContainerSize;
                const int px = s.RoundToPx(size);
                auto& placeable = measurable.Measure(ui::Constraints::Fixed(px, px));
                return s.Layout(placeable.Width(), placeable.Height(), [&placeable](ui::PlacementScope& p) { p.Place(placeable, 0, 0); });
            });
        }
    }

    namespace {
        void VerticalTimePicker(AnalogPtr s, const ui::Modifier& modifier, const TimePickerColors& colors, bool autoSwitchToMinute,
                                const std::optional<TimePickerShapes>& shapes) {
            ui::Modifier m = Semantics(modifier, [](ui::SemanticsConfiguration& c) { c.isTraversalGroup = true; });
            Column(m, Arrangement::Top(), ui::AlignmentH::CenterHorizontally(), [=] {
                VerticalClockDisplay(s.get(), colors, shapes);
                Spacer(Height({}, OrVibrant(shapes, ClockDisplayBottomMargin, VibrantVerticalTimePickerGap)));
                mi::ClockFace(SizeMod({}, TimePickerTokens::ClockDialContainerSize, TimePickerTokens::ClockDialContainerSize), s, colors, autoSwitchToMinute);
                Spacer(Height({}, OrVibrant(shapes, ClockFaceBottomMargin, Dp(0))));
            });
        }

        void HorizontalTimePicker(AnalogPtr s, const ui::Modifier& modifier, const TimePickerColors& colors, bool autoSwitchToMinute,
                                  const std::optional<TimePickerShapes>& shapes) {
            ui::Modifier m = Semantics(modifier, [](ui::SemanticsConfiguration& c) { c.isTraversalGroup = true; });
            Row(m, Arrangement::Start(), ui::AlignmentV::CenterVertically(), [=] {
                HorizontalClockDisplay(s.get(), colors, shapes);
                Spacer(Width({}, OrVibrant(shapes, ClockDisplayBottomMargin, VibrantHorizontalTimePickerGap)));
                mi::ClockFace(shapes ? SizeMod({}, TimePickerTokens::ClockDialContainerSize, TimePickerTokens::ClockDialContainerSize) : mi::ClockFaceSizeModifier({}), s, colors,
                          autoSwitchToMinute);
            });
        }

        bool AccessibilityServicesEnabled() {
            const auto manager = rt::Composition::Current()->Read(ui::LocalAccessibilityManager);
            return manager && ui::AccessibilityServiceOn(manager->ServiceState());
        }
    }

    void TimePicker(TimePickerState& state, const ui::Modifier& modifier, const TimePickerOptions& options) {
        auto& c = *rt::Composition::Current();
        const TimePickerColors colors = options.colors.value_or(options.shapes ? TimePickerDefaults::vibrantColors() : TimePickerDefaults::colors());
        const TimePickerLayoutType layoutType = options.layoutType.value_or(TimePickerDefaults::layoutType());
        auto* owner = c.Read(ui::LocalOwner);
        const bool keyboardMode = owner && owner->GetInputMode() == ui::InputMode::Keyboard;
        const bool autoSwitch = !AccessibilityServicesEnabled() && !keyboardMode;
        auto& userOverride = c.Remember<std::shared_ptr<std::optional<bool>>>([] { return std::make_shared<std::optional<bool>>(); });
        auto uo = userOverride;
        auto& analogState = c.Remember<AnalogPtr>([&] { return std::make_shared<AnalogTimePickerState>(state, uo); },
                                                  rt::Keys(reinterpret_cast<std::uintptr_t>(&state)));
        AnalogPtr analog = analogState;
        TimePickerState* st = &state;
        // An hour or minute set from outside the dial moves the hand there.
        c.LaunchedEffect(rt::Keys(state.Hour()), [analog, uo, st]() -> rt::Task<> {
            if (*uo == true) analog->SetHour(st->Hour());
            *uo = true;
            co_return;
        });
        c.LaunchedEffect(rt::Keys(state.Minute()), [analog, uo, st]() -> rt::Task<> {
            if (*uo == true) analog->SetMinute(st->Minute());
            *uo = true;
            co_return;
        });
        if (layoutType == TimePickerLayoutType::Vertical) VerticalTimePicker(analog, modifier, colors, autoSwitch, options.shapes);
        else HorizontalTimePicker(analog, modifier, colors, autoSwitch, options.shapes);
    }

    // --- TimeInput ------------------------------------------------------------------------------

    namespace {
        // Digits in any script (as Kotlin's isDigit / toIntOrNull read them): the value, or -1.
        int DigitValue(char16_t ch) {
            static constexpr char16_t zeros[] = { u'0', 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66, 0x0BE6, 0x0C66,
                                                  0x0CE6, 0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040, 0x1090, 0x17E0, 0x1810, 0xFF10 };
            for (char16_t zero : zeros)
                if (ch >= zero && ch <= zero + 9) return ch - zero;
            return -1;
        }

        // TimeInputErrorHandler: a reject haptic and the invalid key sound (not under touch
        // exploration, where the sound would talk over the screen reader).
        std::function<void()> RememberTimeInputErrorHandler(bool isTouchExplorationEnabled) {
            auto& c = *rt::Composition::Current();
            auto haptics = c.Read(ui::LocalHapticFeedback);
            auto* owner = c.Read(ui::LocalOwner);
            return [haptics, owner, isTouchExplorationEnabled] {
                if (haptics) haptics->PerformHapticFeedback(ui::HapticFeedbackType::Reject);
                if (!isTouchExplorationEnabled && owner) owner->PlayAudioEffect(FxKeypressInvalid, 0.5f);
            };
        }

        InputTransformationPtr TimeInputTransformation(Sel fieldSelection, TimePickerState* st, std::shared_ptr<std::optional<bool>> userOverride,
                                                       bool a11yServicesEnabled, std::function<void()> onError) {
            return InputTransformationOf([=](TextFieldBuffer& b) {
                if (b.Length() == 0) {
                    if (fieldSelection == Sel::Hour) {
                        const int target = IsPm(*st) && !st->Is24Hour() ? 12 : 0;
                        if (st->Hour() != target) *userOverride = false;
                        st->SetHourInput(target);
                    } else {
                        if (st->Minute() != 0) *userOverride = false;
                        st->SetMinuteInput(0);
                    }
                    return;
                }
                for (char16_t ch : b.AsCharSequence())
                    if (DigitValue(ch) < 0) { b.RevertAllChanges(); return; }

                const TextRange original = b.OriginalSelection();
                const int originalLength = int(b.OriginalText().size());
                const bool hasInsertedText = (b.Length() - (originalLength - (original.Max() - original.Min()))) > 0;
                // A digit typed into a full field replaces the digits around it.
                if (original.Collapsed() && originalLength == 2 && b.Length() == 3) {
                    const int at = b.Selection().start;
                    if (at == 1) b.Delete(1, 3);
                    else if (at == 3) b.Delete(0, 2);
                    else if (at == 2) { b.Delete(2, 3); b.Delete(0, 1); }
                }
                if (b.Length() > 2) {
                    b.RevertAllChanges();
                    onError();
                    return;
                }
                int newValue = 0;
                for (char16_t ch : b.AsCharSequence()) newValue = newValue * 10 + DigitValue(ch);
                if (newValue > MaxValueForTextField) {
                    b.RevertAllChanges();
                    onError();
                    return;
                }
                if (fieldSelection == Sel::Hour) {
                    const int target = st->Is24Hour() ? newValue : newValue == 12 ? (IsPm(*st) ? 12 : 0) : IsPm(*st) ? newValue + 12 : newValue;
                    if (IsValidHour(*st, target) && st->Hour() != target) *userOverride = false;
                    st->SetHourInput(target);
                    const bool typedTwoDigits = b.Length() == 2;
                    const bool autoAdvanceDigit = !st->Is24Hour() && newValue >= 2 && newValue <= 9;
                    // On to the minutes once the hour is complete.
                    if ((typedTwoDigits || (hasInsertedText && autoAdvanceDigit)) && !a11yServicesEnabled && IsHourInputValid(*st))
                        st->SetSelection(Sel::Minute);
                } else {
                    if (newValue >= 0 && newValue <= MaxMinuteValue && st->Minute() != newValue) *userOverride = false;
                    st->SetMinuteInput(newValue);
                }
            });
        }

        void SupportingText(const ui::Modifier& modifier, Sel selection, TimePickerState* st, bool isValid) {
            Strings id;
            if (isValid) id = selection == Sel::Hour ? Strings::TimePickerHour : Strings::TimePickerMinute;
            else if (selection == Sel::Hour) id = st->Is24Hour() ? Strings::TimePicker24HourError : Strings::TimePickerHourError;
            else id = Strings::TimePickerMinuteError;
            TextOptions o;
            o.color = isValid ? Value(TimeInputTokens::TimeFieldSupportingTextColor) : MaterialTheme::colorScheme().error;
            o.minLines = 2;
            o.style = Value(TimeInputTokens::TimeFieldSupportingTextFont);
            ui::Modifier m = Padding(modifier, { Dp(0), SupportLabelTop, Dp(0), Dp(0) });
            // The labels are silent; an error is announced.
            m = isValid ? ClearedSemantics(std::move(m))
                        : Semantics(std::move(m), [](ui::SemanticsConfiguration& c) { c.liveRegion = ui::LiveRegionMode::Polite; });
            Text(GetString(id), m, o);
        }

        // Measured but sized 0 x 0 while hidden, so it keeps its place for focus.
        ui::Modifier Visible(ui::Modifier m, bool visible) {
            return LayoutMod(std::move(m), [visible](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                auto& placeable = measurable.Measure(c);
                if (!visible) return s.Layout(0, 0);
                return s.Layout(placeable.Width(), placeable.Height(), [&placeable](ui::PlacementScope& p) { p.Place(placeable, 0, 0); });
            });
        }

        rt::Task<> FocusWhenSelected(TimePickerState* st, Sel selection, std::shared_ptr<ui::FocusRequester> focus) {
            if (st->Selection() == selection) focus->RequestFocus();
            co_return;
        }

        void TimePickerTextField(const ui::Modifier& modifier, TextFieldState& textFieldState, TimePickerState* st, Sel selection,
                                 KeyboardOptions keyboardOptions, std::function<void()> onKeyboardAction, InputTransformationPtr inputTransformation,
                                 const TimeInputColors& colors, const std::optional<TimePickerShapes>& shapes, Dp vibrantHeight) {
            auto& c = *rt::Composition::Current();
            auto& focusRequester = c.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
            auto focus = focusRequester;
            const TextFieldColors textFieldColors = colors.timeTextFieldColors;
            const bool selected = selection == st->Selection();
            const bool isValid = selection == Sel::Hour ? IsHourInputValid(*st) : IsMinuteInputValid(*st);
            const auto& scheme = MaterialTheme::colorScheme();
            const Color textColor = isValid ? colors.TimeSelectorContentColor(selected) : scheme.error;
            const Dp width = OrVibrant(shapes, TimeInputTokens::TimeFieldContainerWidth, VibrantTimeFieldWidth);
            const Dp height = OrVibrant(shapes, TimeInputTokens::TimeFieldContainerHeight, vibrantHeight);
            const Color cursor = shapes ? colors.TimeSelectorContentColor(true) : scheme.primary;
            text::TextStyle textStyle = c.Read(LocalTextStyle());
            textStyle.WithColor(textColor);
            const ShapePtr shape = shapes ? shapes->timeFieldShape : Value(TimeInputTokens::TimeFieldContainerShape);
            TextFieldState* tfs = &textFieldState;
            Column(Width(modifier, IntrinsicSize::Min), [=] {
                if (!selected) {
                    const int value = selection == Sel::Hour ? (IsHourInputValid(*st) ? HourForDisplay(*st) : st->HourInput()) : st->MinuteInput();
                    TimeSelector(SizeMod({}, width, height), value, st, selection, colors, isValid, shapes);
                }
                const std::string description =
                    GetString(selection == Sel::Minute ? Strings::TimePickerMinuteTextField : Strings::TimePickerHourTextField);
                auto source = RememberInteractionSource();
                Box(Visible({}, selected), ui::Alignment::TopStart(), false, [=] {
                    BasicTextFieldOptions o;
                    o.inputTransformation = inputTransformation;
                    o.keyboardOptions = keyboardOptions;
                    if (onKeyboardAction) o.onKeyboardAction = [onKeyboardAction](std::function<void()>) { onKeyboardAction(); };
                    o.lineLimits = TextFieldLineLimits::SingleLine();
                    o.textStyle = textStyle;
                    o.interactionSource = source;
                    // A cursor over the middle 80 % of the line.
                    o.cursorBrush = gfx::Brush::LinearGradient({ Color::Transparent(), Color::Transparent(), cursor, cursor, Color::Transparent(), Color::Transparent() },
                                                               { 0, 0 }, { 0, gfx::Brush::kInf }, gfx::TileMode::Clamp, { 0.0f, 0.1f, 0.1f, 0.9f, 0.9f, 1.0f });
                    o.decorator = [tfs, isValid, textFieldColors, source, shape](const std::function<void()>& inner) {
                        mi::CommonDecorationBox(ToUtf8(tfs->Text()), inner, TextFieldLabelPosition::Cutout(), {}, true, true, !isValid, source,
                                                PaddingValues{ Dp(0), Dp(0), Dp(0), Dp(0) }, textFieldColors, [isValid, source, textFieldColors, shape] {
                                                    OutlinedTextFieldDefaults::Container(true, !isValid, source, {}, textFieldColors, shape, Dp(2), Dp(1));
                                                });
                    };
                    ui::Modifier m = FocusRequesterModifier({}, *focus);
                    m = SizeMod(std::move(m), width, height);
                    m = Semantics(std::move(m), [description](ui::SemanticsConfiguration& cfg) {
                        cfg.contentDescription = { description };
                        cfg.maxTextLength = 2;
                    });
                    BasicTextField(*tfs, m, o);
                });
                if (!shapes || !isValid) SupportingText(FillMax({}, Axis::Horizontal), selection, st, isValid);
            });
            c.LaunchedEffect(rt::Keys(int(st->Selection())), [st, selection, focus] { return FocusWhenSelected(st, selection, focus); });
        }

        // shouldSwitchFocusToMinute: a digit typed at the end of a complete, valid hour.
        bool ShouldSwitchFocusToMinute(const ui::KeyEvent& e, const TextFieldState& hour, const TimePickerState& st) {
            const bool isDigit = e.unicode >= '0' && e.unicode <= '9';
            const auto& text = hour.Text();
            const bool atEnd = hour.Selection().start == 2 && text.size() == 2;
            if (!isDigit || !atEnd) return false;
            int value = 0;
            for (char16_t ch : text) {
                const int d = DigitValue(ch);
                if (d < 0) return false;
                value = value * 10 + d;
            }
            return (st.Is24Hour() && value >= 0 && value <= 23) || (!st.Is24Hour() && value >= 1 && value <= 12);
        }

        rt::Task<> ReplaceText(TextFieldState* state, std::string text) {
            if (ToUtf8(state->Text()) != text) {
                state->Edit([&text](TextFieldBuffer& b) {
                    b.Replace(0, b.Length(), std::string_view(text));
                    b.PlaceCursorBeforeCharAt(0);
                });
            }
            co_return;
        }

        void TimeInputImpl(const ui::Modifier& modifier, const TimeInputColors& colors, TimePickerState& timePickerState,
                           const std::optional<TimePickerShapes>& shapes, std::function<void()> toggle) {
            auto& c = *rt::Composition::Current();
            TimePickerState* st = &timePickerState;
            auto hourTextValue = [st] { return LocalNumber(IsHourInputValid(*st) ? HourForDisplay(*st) : st->HourInput(), 2); };
            auto minuteTextValue = [st] { return LocalNumber(IsMinuteInputValid(*st) ? st->Minute() : st->MinuteInput(), 2); };
            TextFieldState* hourState = &RememberTextFieldState(hourTextValue(), TextRange(0));
            TextFieldState* minuteState = &RememberTextFieldState(minuteTextValue(), TextRange(0));
            auto& hourOverrideRef = c.Remember<std::shared_ptr<std::optional<bool>>>([] { return std::make_shared<std::optional<bool>>(); });
            auto& minuteOverrideRef = c.Remember<std::shared_ptr<std::optional<bool>>>([] { return std::make_shared<std::optional<bool>>(); });
            auto hourOverride = hourOverrideRef, minuteOverride = minuteOverrideRef;
            // The fields follow hour and minute changes made elsewhere (not their own typing).
            const std::string hourText = hourTextValue(), minuteText = minuteTextValue();
            c.LaunchedEffect(rt::Keys(st->Hour()), [hourOverride, hourState, hourText] {
                const bool replace = *hourOverride == true;
                *hourOverride = true;
                return replace ? ReplaceText(hourState, hourText) : ReplaceText(hourState, ToUtf8(hourState->Text()));
            });
            c.LaunchedEffect(rt::Keys(st->Minute()), [minuteOverride, minuteState, minuteText] {
                const bool replace = *minuteOverride == true;
                *minuteOverride = true;
                return replace ? ReplaceText(minuteState, minuteText) : ReplaceText(minuteState, ToUtf8(minuteState->Text()));
            });

            const Dp fieldHeight = toggle ? UncontainedTimeFieldHeight : VibrantTimeFieldHeight;
            text::TextStyle textStyle = shapes ? Value(TypographyKeyTokens::DisplayLarge) : Value(TimeInputTokens::TimeFieldLabelTextFont);
            textStyle.WithAlign(text::TextAlign::Center);
            textStyle.WithColor(colors.TimeSelectorContentColor(true));
            const bool a11y = AccessibilityServicesEnabled();
            auto onError = RememberTimeInputErrorHandler(a11y);
            auto& hourTransformationRef = c.Remember<InputTransformationPtr>(
                [&] { return TimeInputTransformation(Sel::Hour, st, hourOverride, a11y, onError); },
                rt::Keys(reinterpret_cast<std::uintptr_t>(st), a11y));
            auto& minuteTransformationRef = c.Remember<InputTransformationPtr>(
                [&] { return TimeInputTransformation(Sel::Minute, st, minuteOverride, a11y, onError); },
                rt::Keys(reinterpret_cast<std::uintptr_t>(st), a11y));
            const InputTransformationPtr hourTransformation = hourTransformationRef, minuteTransformation = minuteTransformationRef;
            const Dp startPadding = ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled ? PeriodTogglePaddingSmall : PeriodTogglePaddingOld;

            ui::Modifier m = Semantics(modifier, [](ui::SemanticsConfiguration& cfg) { cfg.isTraversalGroup = true; });
            Row(m, Arrangement::Start(), ui::AlignmentV::Top(), [=] {
                auto& rc = *rt::Composition::Current();
                rc.Provide(LocalTextStyle(), textStyle, [=] {
                    // The fields are always left to right.
                    rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Ltr, [=] {
                        Row({}, [=] {
                            ui::Modifier hourModifier = OnKeyEvent({}, [hourState, st](const ui::KeyEvent& e) {
                                if (ShouldSwitchFocusToMinute(e, *hourState, *st) && IsHourInputValid(*st)) st->SetSelection(Sel::Minute);
                                return false;
                            });
                            KeyboardOptions hourKeys;
                            hourKeys.imeAction = ui::ImeAction::Next;
                            hourKeys.keyboardType = ui::KeyboardType::Number;
                            TimePickerTextField(hourModifier, *hourState, st, Sel::Hour, hourKeys,
                                                [st] { if (IsHourInputValid(*st)) st->SetSelection(Sel::Minute); }, hourTransformation, colors, shapes,
                                                fieldHeight);
                            DisplaySeparator(SizeMod({}, OrVibrant(shapes, DisplaySeparatorWidth, VibrantSeparatorWidth),
                                                     OrVibrant(shapes, TimeInputTokens::PeriodSelectorContainerHeight, fieldHeight)));
                            KeyboardOptions minuteKeys;
                            minuteKeys.imeAction = ui::ImeAction::Done;
                            minuteKeys.keyboardType = ui::KeyboardType::Number;
                            TimePickerTextField({}, *minuteState, st, Sel::Minute, minuteKeys, {}, minuteTransformation, colors, shapes, fieldHeight);
                        });
                    });
                });
                if (toggle) {
                    SideControlColumn(Height(Padding({}, { OrVibrant(shapes, startPadding, PeriodTogglePaddingSmall), Dp(0), Dp(0), Dp(0) }), UncontainedToggleHeight),
                                      st, colors, shapes, toggle);
                } else if (!st->Is24Hour()) {
                    Box(Padding({}, { OrVibrant(shapes, startPadding, VibrantPeriodTogglePadding), Dp(0), Dp(0), Dp(0) }), ui::Alignment::TopStart(), false, [=] {
                        VerticalPeriodToggle(SizeMod({}, OrVibrant(shapes, TimeInputTokens::PeriodSelectorContainerWidth, VibrantPeriodToggleWidth),
                                                     OrVibrant(shapes, TimeInputTokens::PeriodSelectorContainerHeight, VibrantPeriodToggleHeight)),
                                             st, colors, shapes);
                    });
                }
            });
        }
    }

    void TimeInput(TimePickerState& state, const ui::Modifier& modifier, const TimeInputOptions& options) {
        const TimeInputColors colors = options.colors.value_or(options.shapes ? TimeInputDefaults::vibrantColors() : TimeInputDefaults::colors());
        TimeInputImpl(modifier, colors, state, options.shapes, options.toggle);
    }

    // --- TimeScroll -----------------------------------------------------------------------------

    namespace {
        int HourScrollIndex(const TimePickerState& s) {
            if (s.Is24Hour()) return s.Hour();
            const int h = s.Hour() % 12;
            return h == 0 ? 11 : h - 1;
        }

        rt::Task<> SetHourFromScroll(TimePickerState* st, int option) {
            st->SetHour(st->Is24Hour() ? option : (option + 1) % 12 + (IsPm(*st) ? 12 : 0));
            co_return;
        }
        rt::Task<> SetMinuteFromScroll(TimePickerState* st, int option) {
            st->SetMinute(option);
            co_return;
        }
        rt::Task<> ScrollMinuteTo(std::shared_ptr<ScrollFieldState> field, int minute) {
            if (field->SelectedOption() != minute && !field->IsScrollInProgress()) co_await field->ScrollToOption(minute);
        }
    }

    void TimeScroll(TimePickerState& state, const TimePickerShapes& shapes, const ui::Modifier& modifier, const TimeScrollOptions& options) {
        auto& c = *rt::Composition::Current();
        const TimePickerColors colors = options.colors.value_or(TimePickerDefaults::vibrantColors());
        const std::optional<TimePickerShapes> vibrant = shapes;
        const std::function<void()> toggle = options.toggle;
        TimePickerState* st = &state;
        auto hourField = RememberScrollFieldState(state.Is24Hour() ? 24 : 12, HourScrollIndex(state));
        auto minuteField = RememberScrollFieldState(60, state.Minute());

        // The wheels set the time; the time (set elsewhere) turns the wheels.
        c.LaunchedEffect(rt::Keys(hourField->SelectedOption()), [st, hourField] { return SetHourFromScroll(st, hourField->SelectedOption()); });
        c.LaunchedEffect(rt::Keys(minuteField->SelectedOption()), [st, minuteField] { return SetMinuteFromScroll(st, minuteField->SelectedOption()); });
        c.LaunchedEffect(rt::Keys(state.Hour()), [st, hourField] { return hourField->ScrollToOption(HourScrollIndex(*st)); });
        c.LaunchedEffect(rt::Keys(state.Minute()), [st, minuteField] { return ScrollMinuteTo(minuteField, st->Minute()); });

        const Dp fieldHeight = toggle ? UncontainedTimeFieldHeight : VibrantTimeFieldHeight;
        // The uncontained field is taller, so its items are further apart (the neighbours stay
        // cut off the same).
        const Dp itemHeight = toggle ? UncontainedScrollFieldItemHeight : ScrollFieldDefaults::ItemHeight;
        text::TextStyle textStyle = Value(TypographyKeyTokens::DisplayLarge);
        textStyle.WithAlign(text::TextAlign::Center);
        textStyle.WithColor(colors.TimeSelectorContentColor(true));
        const std::string hourDescription = GetString(Strings::TimePickerHourSelection);
        const std::string minuteDescription = GetString(Strings::TimePickerMinuteSelection);
        const std::string hourSuffix = GetString(state.Is24Hour() ? Strings::TimePicker24HourSuffix : Strings::TimePickerHourSuffix);
        const std::string minuteSuffix = GetString(Strings::TimePickerMinuteSuffix);
        const std::string locale = Locale();
        const Dp startPadding = ComposeMaterial3Flags::isUpdatedTimepickerToggleEnabled ? PeriodTogglePaddingSmall : PeriodTogglePaddingOld;
        auto format = [locale](const std::string& pattern, int n) {
            const text::FormatArg args[] = { std::int64_t(n) };
            return text::FormatJava(pattern, args, locale);
        };

        ui::Modifier m = Semantics(modifier, [](ui::SemanticsConfiguration& cfg) { cfg.isTraversalGroup = true; });
        Row(m, Arrangement::Start(), ui::AlignmentV::Top(), [=] {
            rt::Composition::Current()->Provide(LocalTextStyle(), textStyle, [=] {
                rt::Composition::Current()->Provide(ui::LocalLayoutDirection, ui::LayoutDirection::Ltr, [=] {
                    const bool is24 = st->Is24Hour();
                    ScrollFieldOptions hourOptions;
                    hourOptions.fieldAccessibilityDescription = [format, hourSuffix, is24](int index) { return format(hourSuffix, is24 ? index : index + 1); };
                    hourOptions.itemHeight = itemHeight;
                    hourOptions.field = [is24](int index, bool selected, bool enabled) { ScrollFieldDefaults::Item(is24 ? index : index + 1, selected, enabled); };
                    ScrollField(*hourField, hourDescription, SizeMod({}, Dp(100), fieldHeight), hourOptions);
                    DisplaySeparator(SizeMod({}, OrVibrant(vibrant, DisplaySeparatorWidth, VibrantSeparatorWidth),
                                             OrVibrant(vibrant, TimeInputTokens::PeriodSelectorContainerHeight, fieldHeight)));
                    ScrollFieldOptions minuteOptions;
                    minuteOptions.fieldAccessibilityDescription = [format, minuteSuffix](int index) { return format(minuteSuffix, index); };
                    minuteOptions.itemHeight = itemHeight;
                    ScrollField(*minuteField, minuteDescription, SizeMod({}, Dp(100), fieldHeight), minuteOptions);
                });
            });
            if (toggle) {
                SideControlColumn(Height(Padding({}, { OrVibrant(vibrant, startPadding, PeriodTogglePaddingSmall), Dp(0), Dp(0), Dp(0) }), UncontainedToggleHeight),
                                  st, ToTimeInputColors(colors), vibrant, toggle);
            } else if (!st->Is24Hour()) {
                Box(Padding({}, { OrVibrant(vibrant, startPadding, VibrantPeriodTogglePadding), Dp(0), Dp(0), Dp(0) }), ui::Alignment::TopStart(), false, [=] {
                    VerticalPeriodToggle(SizeMod({}, OrVibrant(vibrant, TimeInputTokens::PeriodSelectorContainerWidth, VibrantPeriodToggleWidth),
                                                 OrVibrant(vibrant, TimeInputTokens::PeriodSelectorContainerHeight, VibrantPeriodToggleHeight)),
                                         st, ToTimeInputColors(colors), vibrant);
                });
            }
        });
    }

}
