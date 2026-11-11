// CheckboxSamples.kt
#include "CheckboxSamples.h"
#include "Common.h"

#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Interaction.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/ui/Locals.h>

#include <cmath>

namespace catalog::samples {
    namespace {
        // The rounded checkmark and the plain outline, a whole number of pixels wide.
        m3::CheckboxOptions RoundedStrokes() {
            const float strokeWidthPx = std::floor(Here().Read(LocalDensity).ToPx(m3::CheckboxDefaults::StrokeWidth));
            m3::CheckboxOptions o;
            o.checkmarkStroke = kiln::gfx::Stroke{ strokeWidthPx, 4, kiln::gfx::StrokeCap::Round, kiln::gfx::StrokeJoin::Round };
            o.outlineStroke = kiln::gfx::Stroke{ strokeWidthPx };
            return o;
        }

        // A row toggling one state; the checkbox inside has no click of its own.
        void CheckboxRow(kiln::rt::State<bool>& state, const char* text, const m3::CheckboxOptions& options) {
            ClickableOptions click;
            click.role = Role::Checkbox;
            Row(Toggleable({}, state.Get(), [&state](bool v) { state.Set(v); }, click), Arrangement::Start(), AlignmentV::CenterVertically(),
                [&state, text, options] {
                    m3::Checkbox(state.Get(), nullptr, {}, options);
                    m3::Text(text);
                });
        }

        void TriState(const m3::CheckboxOptions& options) {
            Column({}, Arrangement::Top(), AlignmentH::Start(), [options] {
                auto& state = RememberSaveableState(true);
                auto& state2 = RememberSaveableState(true);
                const auto parentState = state.Get() && state2.Get()     ? m3::ToggleableState::On
                                         : !state.Get() && !state2.Get() ? m3::ToggleableState::Off
                                                                         : m3::ToggleableState::Indeterminate;
                const auto onParentClick = [&state, &state2, parentState] {
                    const bool s = parentState != m3::ToggleableState::On;
                    state.Set(s);
                    state2.Set(s);
                };
                ClickableOptions click;
                click.role = Role::Checkbox;
                Row(TriStateToggleable({}, int(parentState), onParentClick, click), Arrangement::Start(), AlignmentV::CenterVertically(),
                    [parentState, options] {
                        m3::TriStateCheckbox(parentState, nullptr, {}, options);
                        m3::Text("Receive Emails");
                    });
                Spacer(Size({}, 25_dp));
                Column(Padding({}, { 24_dp, 0_dp, 0_dp, 0_dp }), Arrangement::Top(), AlignmentH::Start(), [&state, &state2, options] {
                    CheckboxRow(state, "Daily", options);
                    Spacer(Size({}, 25_dp));
                    CheckboxRow(state2, "Weekly", options);
                });
            });
        }
    }

    void CheckboxSample() {
        auto& checkedState = RememberSaveableState(true);
        m3::Checkbox(checkedState.Get(), [&checkedState](bool v) { checkedState.Set(v); });
    }

    void CheckboxWithTextSample() {
        auto& checkedState = RememberSaveableState(true);
        auto interactionSource = Here().Remember<std::shared_ptr<MutableInteractionSource>>([] { return std::make_shared<MutableInteractionSource>(); });
        ClickableOptions click;
        click.role = Role::Checkbox;
        click.indication = nullptr;
        click.useLocalIndication = false;
        click.interactionSource = interactionSource;
        Row(Padding(Toggleable(Height(FillMax({}, Axis::Horizontal), 56_dp), checkedState.Get(), [&checkedState](bool) { checkedState.Set(!checkedState.Peek()); }, click),
                    16_dp, 0_dp),
            Arrangement::Start(), AlignmentV::CenterVertically(), [&checkedState, interactionSource] {
                m3::CheckboxOptions o;
                o.interactionSource = interactionSource;
                m3::Checkbox(checkedState.Get(), [&checkedState](bool v) { checkedState.Set(v); }, {}, o);
                TextStyled("Option selection", m3::MaterialTheme::typography().bodyLarge, Padding({}, { 16_dp, 0_dp, 0_dp, 0_dp }));
            });
    }

    void CheckboxRoundedStrokesSample() {
        const m3::CheckboxOptions o = RoundedStrokes();
        auto& checkedState = RememberSaveableState(true);
        m3::Checkbox(checkedState.Get(), [&checkedState](bool v) { checkedState.Set(v); }, {}, o);
    }

    void TriStateCheckboxSample() { TriState({}); }
    void TriStateCheckboxRoundedStrokesSample() { TriState(RoundedStrokes()); }
}
