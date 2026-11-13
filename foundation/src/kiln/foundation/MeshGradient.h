#pragma once

// MeshGradientPainter (ui-graphics MeshGradient.kt, MeshGradientConfig.kt,
// BaseMeshGradientRenderer.kt): a grid of rows x columns Bezier patches whose vertices carry
// colours, tessellated into triangles. Positions are fractions of the drawn size; control points
// left unspecified are inferred from the neighbours.

#include "kiln/foundation/Image.h"
#include <kiln/Function.h>

#include <functional>
#include <optional>
#include <vector>

namespace kiln::foundation {

    using ui::Color;
    using ui::Offset;

    class MeshGradientScope {
    public:
        MeshGradientScope(int rows, int columns);
        int Rows() const { return m_Rows; }
        int Columns() const { return m_Columns; }
        // The vertex at (row 0..rows, column 0..columns); control points are offsets from
        // `position` (fractions too) along the edges to each side.
        void SetVertex(int row, int column, Offset position, Color color,
                       std::optional<Offset> leftControlPoint = std::nullopt, std::optional<Offset> topControlPoint = std::nullopt,
                       std::optional<Offset> rightControlPoint = std::nullopt, std::optional<Offset> bottomControlPoint = std::nullopt);

    private:
        friend class MeshGradientPainter;
        void Reset();
        void InferControlPoints();
        int m_Rows, m_Columns;
        std::vector<Offset> m_Positions;
        std::vector<Color> m_Colors;
        std::vector<std::optional<Offset>> m_Left, m_Right, m_Top, m_Bottom;
    };

    class MeshGradientPainter final : public Painter {
    public:
        // bicubicColor: Catmull-Rom colour interpolation in Oklab instead of bilinear.
        MeshGradientPainter(int rows, int columns, bool bicubicColor, kiln::Function<void(MeshGradientScope&)> block);
        std::optional<ui::Size> IntrinsicSize() const override { return std::nullopt; }
        void Draw(DrawScope& scope, ui::Size size, float alpha, const gfx::ColorFilter* colorFilter) override;

    private:
        int m_Rows, m_Columns;
        bool m_Bicubic;
        kiln::Function<void(MeshGradientScope&)> m_Block;
        MeshGradientScope m_Scope;
    };

}
