#pragma once

// Path geometry helpers (ui-graphics PathGeometry.kt, PathHitTester.kt, PathSvg.kt, Outline.kt).

#include "kiln/graphics/Path.h"
#include "kiln/graphics/Shape.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiln::gfx {

    // computeDirection: the winding of the first contour, from its signed area (Jackowski).
    Path::Direction ComputeDirection(const Path& path);
    // divide: one path per contour (contours with only a move are dropped).
    std::vector<Path> Divide(const Path& path);
    // reverse: the same contours, each traced backwards.
    Path Reverse(const Path& path);
    // addOutline: the outline's shape added to the path.
    void AddOutline(Path& path, const Outline& outline);
    // addSvg: SVG path data appended; toSvg: the path as SVG path data (or a whole document).
    void AddSvg(Path& path, std::string_view pathData);
    std::string ToSvg(const Path& path, bool asDocument = false);

    // PathHitTester: whether points fall inside a path (by its fill type). Curves are flattened
    // within `tolerance`.
    class PathHitTester {
    public:
        PathHitTester() = default;
        explicit PathHitTester(const Path& path, float tolerance = 0.5f) { UpdatePath(path, tolerance); }
        void UpdatePath(const Path& path, float tolerance = 0.5f);
        bool Contains(Offset position) const;

    private:
        struct Edge { Offset a, b; };
        std::vector<Edge> m_Edges;   // sorted by their top
        ui::Rect m_Bounds{};
        bool m_EvenOdd = false;
        bool m_Empty = true;
    };

}
