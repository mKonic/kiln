#pragma once

// SystemBarStyle (activity EdgeToEdge.kt): how enableEdgeToEdge treats a system bar. Auto follows
// the configuration's night mode: its scrim is left to the system (transparent bars, the
// navigation bar's contrast scrim on 3-button navigation) and the icons are dark in light mode.
// Dark and Light fix the icons (light icons on Dark) and draw the given scrim behind the bar.

#include "kiln/ui/Geometry.h"

namespace kiln::ui {

    struct SystemBarStyle {
        enum class Mode : std::uint8_t { Auto, Dark, Light };
        Mode mode = Mode::Auto;
        Color lightScrim = Color::Transparent(), darkScrim = Color::Transparent();
        static SystemBarStyle Auto(Color lightScrim, Color darkScrim) { return { Mode::Auto, lightScrim, darkScrim }; }
        static SystemBarStyle Dark(Color scrim) { return { Mode::Dark, scrim, scrim }; }
        static SystemBarStyle Light(Color scrim, Color darkScrim) { return { Mode::Light, scrim, darkScrim }; }
        // enableEdgeToEdge's defaults: transparent status bar; DefaultLightScrim (0xe6ffffff) /
        // DefaultDarkScrim (0x801b1b1b) for the navigation bar.
        static SystemBarStyle DefaultStatus() { return Auto(Color::Transparent(), Color::Transparent()); }
        static SystemBarStyle DefaultNavigation() { return Auto(Color(0xe6ffffffu), Color(0x801b1b1bu)); }
        bool operator==(const SystemBarStyle&) const = default;
    };

}
