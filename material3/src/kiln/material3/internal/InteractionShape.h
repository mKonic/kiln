#pragma once

#include "kiln/material3/internal/Helpers.h"

namespace kiln::material3::internal {

    // Shared by IconButton.kt and ToggleButton.kt. A press takes precedence over selection;
    // arbitrary shapes switch immediately, while a corner-based set morphs between corners.
    inline foundation::ShapePtr ShapeByInteraction(const foundation::ShapePtr& shape, const foundation::ShapePtr& pressedShape,
                                                   const foundation::ShapePtr& checkedShape, bool pressed, bool checked, anim::SpecPtr spec) {
        const auto& target = pressed ? pressedShape : checked ? checkedShape : shape;
        if (shape == pressedShape && shape == checkedShape) return target;
        const auto rest = std::dynamic_pointer_cast<const foundation::CornerBasedShape>(shape);
        const auto press = std::dynamic_pointer_cast<const foundation::CornerBasedShape>(pressedShape);
        const auto check = std::dynamic_pointer_cast<const foundation::CornerBasedShape>(checkedShape);
        if (!rest || !press || !check) return target;
        const auto hash = std::hash<const void*>{};
        const std::size_t key = (hash(rest.get()) * 31 + hash(press.get())) * 31 + hash(check.get());
        return RememberAnimatedShape(pressed ? press : checked ? check : rest, std::move(spec), key);
    }

}
