#pragma once

// WindowInsets for layout (foundation-layout WindowInsets*.kt): a set of types unioned, padding
// modifiers that subtract what ancestors already consumed, and consumption without padding.

#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Insets.h"

#include <initializer_list>

namespace kiln::foundation {

    struct WindowInsets {
        std::uint32_t types = 0;      // bitmask over ui::InsetsType
        bool left = true, top = true, right = true, bottom = true;   // .only(sides)
        WindowInsets() = default;
        WindowInsets(std::initializer_list<ui::InsetsType> t) { for (auto x : t) types |= 1u << std::uint32_t(x); }
        WindowInsets Union(const WindowInsets& o) const { WindowInsets r = *this; r.types |= o.types; return r; }
        WindowInsets Only(bool l, bool t, bool r, bool b) const { WindowInsets x = *this; x.left = l; x.top = t; x.right = r; x.bottom = b; return x; }
        ui::InsetsValue Resolve(ui::WindowInsetsHolder& h) const;
        bool operator==(const WindowInsets&) const = default;

        static WindowInsets StatusBars() { return { ui::InsetsType::StatusBars }; }
        static WindowInsets NavigationBars() { return { ui::InsetsType::NavigationBars }; }
        static WindowInsets Ime() { return { ui::InsetsType::Ime }; }
        static WindowInsets DisplayCutout() { return { ui::InsetsType::DisplayCutout }; }
        static WindowInsets SystemBars() { return { ui::InsetsType::StatusBars, ui::InsetsType::NavigationBars, ui::InsetsType::CaptionBar }; }
        // safeDrawing = systemBars + displayCutout + ime (+ tappable element)
        static WindowInsets SafeDrawing() {
            return { ui::InsetsType::StatusBars, ui::InsetsType::NavigationBars, ui::InsetsType::CaptionBar,
                     ui::InsetsType::DisplayCutout, ui::InsetsType::Ime, ui::InsetsType::TappableElement };
        }
    };

    Modifier WindowInsetsPadding(Modifier m, WindowInsets insets);
    Modifier ConsumeWindowInsets(Modifier m, WindowInsets insets);
    inline Modifier SafeDrawingPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SafeDrawing()); }
    inline Modifier SystemBarsPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SystemBars()); }
    inline Modifier ImePadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::Ime()); }

}
