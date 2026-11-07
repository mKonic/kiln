#pragma once

// Shapes (material3 Shapes.kt): the theme's corner shapes, ShapeDefaults, the half-rounded
// helpers and the token lookup.

#include "kiln/material3/tokens/TokenTypes.h"

#include <memory>

namespace kiln::material3 {

    using CornerBasedShapePtr = std::shared_ptr<const foundation::CornerBasedShape>;

    namespace ShapeDefaults {
        CornerBasedShapePtr ExtraSmall();
        CornerBasedShapePtr Small();
        CornerBasedShapePtr Medium();
        CornerBasedShapePtr Large();
        CornerBasedShapePtr LargeIncreased();
        CornerBasedShapePtr ExtraLarge();
        CornerBasedShapePtr ExtraLargeIncreased();
        CornerBasedShapePtr ExtraExtraLarge();
        // The corner sizes (internal in material3).
        foundation::CornerSize CornerNone();
        foundation::CornerSize CornerExtraSmall();
        foundation::CornerSize CornerSmall();
        foundation::CornerSize CornerMedium();
        foundation::CornerSize CornerLarge();
        foundation::CornerSize CornerLargeIncreased();
        foundation::CornerSize CornerExtraLarge();
        foundation::CornerSize CornerExtraLargeIncreased();
        foundation::CornerSize CornerExtraExtraLarge();
        foundation::CornerSize CornerFull();   // CornerSize(100) percent
    }

    struct Shapes {
        CornerBasedShapePtr extraSmall = ShapeDefaults::ExtraSmall();
        CornerBasedShapePtr small = ShapeDefaults::Small();
        CornerBasedShapePtr medium = ShapeDefaults::Medium();
        CornerBasedShapePtr large = ShapeDefaults::Large();
        CornerBasedShapePtr extraLarge = ShapeDefaults::ExtraLarge();
        CornerBasedShapePtr largeIncreased = ShapeDefaults::LargeIncreased();
        CornerBasedShapePtr extraLargeIncreased = ShapeDefaults::ExtraLargeIncreased();
        CornerBasedShapePtr extraExtraLarge = ShapeDefaults::ExtraExtraLarge();

        // Shapes.equals: shape by shape (Shape equality, not identity).
        bool operator==(const Shapes& o) const;
    };

    // CornerBasedShape.top / bottom / start / end: the given side keeps its corners, the
    // opposite side gets `size` (none by default).
    CornerBasedShapePtr Top(const CornerBasedShapePtr& shape, foundation::CornerSize bottomSize = ShapeDefaults::CornerNone());
    CornerBasedShapePtr Bottom(const CornerBasedShapePtr& shape, foundation::CornerSize topSize = ShapeDefaults::CornerNone());
    CornerBasedShapePtr Start(const CornerBasedShapePtr& shape, foundation::CornerSize endSize = ShapeDefaults::CornerNone());
    CornerBasedShapePtr End(const CornerBasedShapePtr& shape, foundation::CornerSize startSize = ShapeDefaults::CornerNone());

    // Shapes.fromToken (ShapeKeyTokens id to shape; RectangleShape for an unknown id).
    foundation::ShapePtr FromToken(const Shapes& shapes, tokens::ShapeToken token);

}
