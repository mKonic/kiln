// RadioButtonSamples.kt
#include "RadioButtonSamples.h"
#include "Common.h"

#include <kiln/foundation/Gestures.h>
#include <kiln/material3/RadioButton.h>

namespace catalog::samples {
    void RadioButtonSample() {
        auto& state = RememberSaveableState(true);
        Row(SelectableGroup({}), Arrangement::Start(), AlignmentV::Top(), [&state] {
            m3::RadioButton(state.Get(), [&state] { state.Set(true); }, ContentDescription({}, "Localized Description"));
            m3::RadioButton(!state.Get(), [&state] { state.Set(false); }, ContentDescription({}, "Localized Description"));
        });
    }

    void RadioGroupSample() {
        static const char* const radioOptions[] = { "Calls", "Missed", "Friends" };
        auto& selectedOption = RememberSaveableState(std::string(radioOptions[0]));
        Column(SelectableGroup({}), Arrangement::Top(), AlignmentH::Start(), [&selectedOption] {
            for (const char* text : radioOptions) {
                const bool selected = text == selectedOption.Get();
                ClickableOptions click;
                click.role = Role::RadioButton;
                Row(Padding(Selectable(Height(FillMax({}, Axis::Horizontal), 56_dp), selected, [&selectedOption, text] { selectedOption.Set(text); }, click),
                            16_dp, 0_dp),
                    Arrangement::Start(), AlignmentV::CenterVertically(), [selected, text] {
                        m3::RadioButton(selected, nullptr);
                        TextStyled(text, m3::MaterialTheme::typography().bodyLarge, Padding({}, { 16_dp, 0_dp, 0_dp, 0_dp }));
                    }, kiln::rt::Key(text));
            }
        });
    }
}
