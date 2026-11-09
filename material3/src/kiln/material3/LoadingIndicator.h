#pragma once

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/graphics/shapes/RoundedPolygon.h"

#include <optional>

namespace kiln::material3 {
    struct LoadingIndicatorOptions {
        ui::Color color = ui::Color::Unspecified();
        std::optional<std::vector<gfx::shapes::RoundedPolygon>> polygons;
    };
    struct ContainedLoadingIndicatorOptions {
        ui::Color containerColor = ui::Color::Unspecified(), indicatorColor = ui::Color::Unspecified();
        foundation::ShapePtr containerShape;
        std::optional<std::vector<gfx::shapes::RoundedPolygon>> polygons;
    };
    void LoadingIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const LoadingIndicatorOptions& options = {});
    void LoadingIndicator(const ui::Modifier& modifier = {}, const LoadingIndicatorOptions& options = {});
    void ContainedLoadingIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const ContainedLoadingIndicatorOptions& options = {});
    void ContainedLoadingIndicator(const ui::Modifier& modifier = {}, const ContainedLoadingIndicatorOptions& options = {});

    namespace LoadingIndicatorDefaults {
        inline constexpr ui::Dp ContainerWidth{48}, ContainerHeight{48}, IndicatorSize{38};
        foundation::ShapePtr containerShape();
        ui::Color indicatorColor();
        ui::Color containedIndicatorColor();
        ui::Color containedContainerColor();
        const std::vector<gfx::shapes::RoundedPolygon>& indeterminateIndicatorPolygons();
        const std::vector<gfx::shapes::RoundedPolygon>& determinateIndicatorPolygons();
    }
}
