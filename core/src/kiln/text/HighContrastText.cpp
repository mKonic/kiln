#include "kiln/text/HighContrastText.h"

#include "kiln/graphics/ColorSpace.h"

#include <atomic>
#include <cmath>

namespace kiln::text {

    namespace {
        struct Lab { float L, a, b; };
        // uirenderer::sRGBToLab / LabToSRGB: CIE Lab (D50, Bradford adaptation from sRGB).
        Lab ToLab(ui::Color c) {
            const auto v = gfx::colorspace::ConvertColor(c, gfx::colorspace::ColorSpaces::CieLab());
            return { v[0], v[1], v[2] };
        }
        ui::Color FromLab(Lab lab) {
            return gfx::colorspace::ColorFromSpace(std::clamp(lab.L, 0.0f, 100.0f), std::clamp(lab.a, -128.0f, 128.0f), std::clamp(lab.b, -128.0f, 128.0f), 1.0f,
                                       gfx::colorspace::ColorSpaces::CieLab());
        }
    }

    namespace {
        std::atomic<bool> g_Outlines{ false };
    }
    void SetHighContrastOutlines(bool enabled) { g_Outlines = enabled; }
    bool HighContrastOutlines() { return g_Outlines; }

    bool ShouldDarkenForHighContrast(ui::Color color) { return ToLab(color).L <= 50; }

    ui::Color HighContrastInnerColor(ui::Color color) {
        Lab lab = ToLab(color);
        const bool darken = lab.L <= 50;
        const bool grayscale = std::abs(lab.a) < 10 && std::abs(lab.b) < 10;
        if (grayscale) {
            lab.a = lab.b = 0;
            if (lab.L > 40 && lab.L < 60) lab.L = darken ? 20 : 80;
            else lab.L = darken ? 0 : 100;
        } else {
            if (darken && lab.L > 20.f) lab.L = 20.0f;
            else if (!darken && lab.L < 90.f) lab.L = 90.0f;
        }
        return FromLab(lab);
    }

    ui::Color HighContrastBackgroundColor(ui::Color textColor) {
        // Color.argb(0.7f, ...): alpha 0.7 * 255 + 0.5 = 179.
        return ShouldDarkenForHighContrast(textColor) ? ui::Color(0xB3FFFFFFu) : ui::Color(0xB3000000u);
    }

}
