#pragma once

// material3's colour utilities (internal/colorUtil: CamUtils, Frame, Cam, HCTSolver): the CAM16
// maths dynamic colour uses to synthesise palette tones (Color.setLuminance). Ported with
// Kotlin's float semantics: Float math functions run in double and round to float.

#include <cstdint>

namespace kiln::material3::colorutil {

    namespace CamUtils {
        std::int32_t ArgbFromLstar(double lstar);
        std::int32_t ArgbFromLinrgbComponents(double r, double g, double b);
        std::int32_t IntFromLstar(float lstar);
        float LstarFromInt(std::int32_t argb);
        double YFromLstar(double lstar);
        int Signum(double num);
    }

    // Frame: CAM16 viewing conditions (Frame.Default: D65, 50 L* background, average surround).
    struct Frame {
        float n, aw, nbb, ncb, c, nc;
        float rgbD[3];
        float fl, flRoot, z;
        static const Frame& Default();
        static Frame Make(const float whitepoint[3], float adaptingLuminance, float backgroundLstar, float surround,
                          bool discountingIlluminant);
    };

    struct Cam {
        float hue, chroma, j, m, s, jstar, astar, bstar;
        float Distance(const Cam& other) const;
        static Cam FromInt(std::int32_t argb);
        // Cam.getInt(hue, chroma, lstar) in the default frame (HCTSolver.solveToInt).
        static std::int32_t GetInt(float hue, float chroma, float lstar);
    };

    namespace HctSolver {
        std::int32_t SolveToInt(double hueDegrees, double chroma, double lstar);
    }

    // Color.setLuminance (DynamicTonalPalette.android.kt): the colour's CAM hue and chroma at
    // the given L*.
    std::uint32_t SetLuminance(std::uint32_t argb, float newLuminance);

}
