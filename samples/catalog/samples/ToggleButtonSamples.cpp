// ToggleButtonSamples.kt
#include "ToggleButtonSamples.h"
#include "Common.h"

#include <kiln/material3/ToggleButton.h>

namespace catalog::samples {
    namespace {
        using ToggleFn = void (*)(bool, std::function<void(bool)>, const Modifier&, const m3::ToggleButtonOptions&, std::function<void()>);

        void Plain(ToggleFn toggle, const char* label) {
            auto& checked = RememberSaveableState(false);
            toggle(checked.Get(), [&checked](bool v) { checked.Set(v); }, {}, {}, [label] { m3::Text(label); });
        }

        // An Edit icon (filled while checked) at `size`.
        void WithIcon(m3::ToggleButtonSize size, const char* label) {
            auto& checked = RememberSaveableState(false);
            m3::ToggleButtonOptions o;
            o.buttonSize = size;
            o.icon = [&checked] { m3::Icon(checked.Get() ? Icons::Filled::Edit() : Icons::Outlined::Edit(), std::string("Localized description")); };
            m3::ToggleButton(checked.Get(), [&checked](bool v) { checked.Set(v); }, {}, o, [label] { m3::Text(label); });
        }
    }

    void ToggleButtonSample() { Plain(m3::ToggleButton, "Button"); }
    void ElevatedToggleButtonSample() { Plain(m3::ElevatedToggleButton, "Elevated Button"); }
    void FilledTonalToggleButtonSample() { Plain(m3::FilledTonalToggleButton, "Tonal Button"); }
    void OutlinedToggleButtonSample() { Plain(m3::OutlinedToggleButton, "Outlined Button"); }
    void ToggleButtonWithIconSample() { WithIcon(m3::ToggleButtonSize::Small, "Edit"); }
    void XSmallToggleButtonWithIconSample() { WithIcon(m3::ToggleButtonSize::ExtraSmall, "Label"); }
    void MediumToggleButtonWithIconSample() { WithIcon(m3::ToggleButtonSize::Medium, "Label"); }
    void LargeToggleButtonWithIconSample() { WithIcon(m3::ToggleButtonSize::Large, "Label"); }
    void XLargeToggleButtonWithIconSample() { WithIcon(m3::ToggleButtonSize::ExtraLarge, "Label"); }
    void ToggleButtonWithButtonSizeSample() { WithIcon(m3::ToggleButtonSize::Small, "Label"); }
}
