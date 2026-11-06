#pragma once

// androidx.graphics.shapes port, part 3: the RoundedPolygon factories (Shapes.kt).

#include "kiln/graphics/shapes/RoundedPolygon.h"

#include <optional>

namespace kiln::gfx::shapes {

    RoundedPolygon Circle(int numVertices = 8, float radius = 1, float centerX = 0, float centerY = 0);
    RoundedPolygon Rectangle(float width = 2, float height = 2, CornerRounding rounding = {},
                             const std::vector<CornerRounding>* perVertex = nullptr, float centerX = 0, float centerY = 0);
    RoundedPolygon Star(int numVerticesPerRadius, float radius = 1, float innerRadius = 0.5f, CornerRounding rounding = {},
                        std::optional<CornerRounding> innerRounding = std::nullopt,
                        const std::vector<CornerRounding>* perVertex = nullptr, float centerX = 0, float centerY = 0);
    RoundedPolygon Pill(float width = 2, float height = 1, float smoothing = 0, float centerX = 0, float centerY = 0);
    RoundedPolygon PillStar(float width = 2, float height = 1, int numVerticesPerRadius = 8, float innerRadiusRatio = 0.5f,
                            CornerRounding rounding = {}, std::optional<CornerRounding> innerRounding = std::nullopt,
                            const std::vector<CornerRounding>* perVertex = nullptr, float vertexSpacing = 0.5f,
                            float startLocation = 0, float centerX = 0, float centerY = 0);

}
