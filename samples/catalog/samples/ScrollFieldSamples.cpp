// ScrollFieldSamples.kt
#include "ScrollFieldSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/Semantics.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/ScrollField.h>

#include <charconv>
#include <string>

namespace catalog::samples {
    namespace SD = m3::ScrollFieldDefaults;

    namespace {
        // Double.toString for these tenths: the shortest form, with ".0" when whole.
        std::string DoubleToString(double v) {
            char buffer[32];
            const auto end = std::to_chars(buffer, buffer + sizeof buffer, v).ptr;
            std::string s(buffer, end);
            if (s.find_first_of(".e") == std::string::npos) s += ".0";
            return s;
        }

        // The samples' container: a rounded surfaceContainerHighest row, 12 dp in.
        void Frame(kiln::Function<void()> content) {
            Row(Padding(Background({}, m3::MaterialTheme::colorScheme().surfaceContainerHighest, RoundedCorner(28_dp)), 12_dp), Arrangement::SpacedBy(8_dp),
                AlignmentV::CenterVertically(), std::move(content));
        }

        // A field showing `text`, displayLargeEmphasized when selected.
        void FieldText(const std::string& text, bool selected, bool enabled, const m3::ScrollFieldColors& colors) {
            Box(FillMax({}, Axis::Vertical), Alignment::Center(), false, [text, selected, enabled, colors] {
                m3::TextOptions o;
                o.style = selected ? m3::MaterialTheme::typography().displayLargeEmphasized : m3::MaterialTheme::typography().displayMedium;
                o.color = enabled && selected    ? colors.selectedContentColor
                          : enabled && !selected ? colors.contentColor
                          : !enabled && selected ? colors.disabledSelectedContentColor
                                                 : colors.disabledContentColor;
                m3::Text(text, {}, o);
            });
        }
    }

    void ScrollFieldSample() {
        constexpr int minVal = 1000, maxVal = 2000, itemCount = (maxVal - minVal) + 1;
        const auto state = m3::RememberScrollFieldState(itemCount, 0);
        const m3::ScrollFieldColors colors = SD::colors();
        Frame([state, colors] {
            m3::ScrollFieldOptions o;
            o.fieldAccessibilityDescription = [](int index) { return std::to_string(minVal + index); };
            o.field = [colors](int index, bool selected, bool enabled) { FieldText(std::to_string(minVal + index), selected, enabled, colors); };
            m3::ScrollField(*state, "Select value between 1000 and 2000", SizeMod({}, 192_dp, SD::ScrollFieldHeight), o);
        });
    }

    void TimeScrollFieldSample() {
        const auto hourState = m3::RememberScrollFieldState(24, 12);
        const auto minuteState = m3::RememberScrollFieldState(60, 30);
        Frame([hourState, minuteState] {
            m3::ScrollFieldOptions h;
            h.fieldAccessibilityDescription = [](int index) { return std::to_string(index) + (index == 1 ? " hour" : " hours"); };
            m3::ScrollField(*hourState, "Select hour", SizeMod({}, 100_dp, 120_dp), h);
            m3::TextOptions t;
            t.style = m3::MaterialTheme::typography().displayLarge;
            t.color = m3::MaterialTheme::colorScheme().onSurfaceVariant;
            t.textAlign = kiln::text::TextAlign::Center;
            m3::Text(":", Semantics(OffsetMod({}, 0_dp, -4_dp), [](SemanticsConfiguration& c) { c.hideFromAccessibility = true; }), t);
            m3::ScrollFieldOptions m;
            m.fieldAccessibilityDescription = [](int index) { return std::to_string(index) + (index == 1 ? " minute" : " minutes"); };
            m3::ScrollField(*minuteState, "Select minute", SizeMod({}, 100_dp, 120_dp), m);
        });
    }

    void UnitScrollFieldSample() {
        const auto amountState = m3::RememberScrollFieldState(100, 26);   // 0.1 to 10.0; 2.7
        const auto unitState = m3::RememberScrollFieldState(2, 0);        // L, oz; L
        static const char* const units[2] = { "L", "oz" };
        const m3::ScrollFieldColors colors = SD::colors();
        Frame([amountState, unitState, colors] {
            m3::ScrollFieldOptions a;
            a.fieldAccessibilityDescription = [](int index) { return DoubleToString((index + 1) / 10.0); };
            a.field = [colors](int index, bool selected, bool enabled) {
                const double amount = (index + 1) / 10.0;
                FieldText(DoubleToString(int(amount * 10) / 10.0), selected, enabled, colors);
            };
            m3::ScrollField(*amountState, "Select amount", SizeMod({}, 120_dp, SD::ScrollFieldHeight), a);
            m3::ScrollFieldOptions u;
            u.fieldAccessibilityDescription = [](int index) { return std::string(units[index]); };
            u.field = [colors](int index, bool selected, bool enabled) { FieldText(units[index], selected, enabled, colors); };
            m3::ScrollField(*unitState, "Select unit", SizeMod({}, 120_dp, SD::ScrollFieldHeight), u);
        });
    }
}
