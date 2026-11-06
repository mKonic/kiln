#pragma once

// Conic (rational quadratic) segments: Skia's SkConic as androidx graphics-path ports it
// (Conic.cpp), and the unit-circle arcs Skia builds paths from (SkConic::BuildUnitArc).

#include "kiln/ui/Geometry.h"

#include <array>
#include <vector>

namespace kiln::gfx {

    using ui::Offset;

    struct Conic {
        static constexpr int kMaxQuadraticPow2 = 5;   // at most 32 quadratics

        std::array<Offset, 3> points{};
        float weight = 1;

        // The number of halvings that keeps quadratics within `tolerance` of the conic.
        int QuadraticPow2(float tolerance) const;
        // Splits at t = 0.5 into two conics.
        void Split(Conic out[2]) const;
        // 2^pow2 quadratics as 2 * 2^pow2 + 1 points (shared ends); returns the count.
        int ToQuadratics(std::vector<Offset>& out, int pow2) const;
        Offset Eval(float t) const;
    };

    // conicToQuadratics: the quadratics approximating a conic within `tolerance` (points as above).
    int ConicToQuadratics(const Offset points[3], float weight, float tolerance, std::vector<Offset>& out);

    // The conics of a unit-circle arc from uStart to uStop (unit vectors), clockwise (y-down) or
    // not, mapped by `scale` and `translate` (an oval). Empty for coincident vectors.
    std::vector<Conic> BuildUnitArc(Offset uStart, Offset uStop, bool clockwise, Offset scale, Offset translate);

}
