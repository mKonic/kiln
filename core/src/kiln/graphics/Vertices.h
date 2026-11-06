#pragma once

// Vertices (ui-graphics Vertices.kt): triangles with per-vertex colours for Canvas.drawVertices.

#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <vector>

namespace kiln::gfx {

    enum class VertexMode : std::uint8_t { Triangles, TriangleStrip, TriangleFan };

    struct Vertices {
        VertexMode mode = VertexMode::Triangles;
        std::vector<ui::Offset> positions;
        std::vector<ui::Color> colors;          // one per position; empty: the paint colour
        std::vector<std::uint16_t> indices;     // into positions; empty: positions in order

        // The triangle list this mode describes, as indices into positions.
        std::vector<std::uint32_t> TriangleList() const {
            std::vector<std::uint32_t> order;
            if (indices.empty()) for (std::size_t i = 0; i < positions.size(); ++i) order.push_back(std::uint32_t(i));
            else for (auto i : indices) order.push_back(i);
            std::vector<std::uint32_t> out;
            const std::size_t n = order.size();
            switch (mode) {
                case VertexMode::Triangles:
                    for (std::size_t i = 0; i + 2 < n; i += 3) out.insert(out.end(), { order[i], order[i + 1], order[i + 2] });
                    break;
                case VertexMode::TriangleStrip:
                    for (std::size_t i = 0; i + 2 < n; ++i) out.insert(out.end(), { order[i], order[i + 1], order[i + 2] });
                    break;
                case VertexMode::TriangleFan:
                    for (std::size_t i = 1; i + 1 < n; ++i) out.insert(out.end(), { order[0], order[i], order[i + 1] });
                    break;
            }
            return out;
        }
    };

}
