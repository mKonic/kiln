// SwitchSamples.kt
#include "SwitchSamples.h"
#include "Common.h"

#include <kiln/material3/Switch.h>

namespace catalog::samples {
    void SwitchSample() {
        auto& checked = RememberSaveableState(true);
        m3::Switch(checked.Get(), [&checked](bool v) { checked.Set(v); }, ContentDescription({}, "Demo"));
    }

    void SwitchWithThumbIconSample() {
        auto& checked = RememberSaveableState(true);
        m3::SwitchOptions o;
        o.thumbContent = [&checked] {
            // Icon isn't focusable, no need for content description
            if (checked.Get()) m3::Icon(Icons::Filled::Check(), std::nullopt, Size({}, m3::SwitchDefaults::IconSize));
        };
        m3::Switch(checked.Get(), [&checked](bool v) { checked.Set(v); }, ContentDescription({}, "Demo with icon"), o);
    }
}
