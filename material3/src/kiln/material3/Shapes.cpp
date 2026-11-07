#include "kiln/material3/Shapes.h"

#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/ShapeTokens.h"

namespace kiln::material3 {

    namespace ShapeDefaults {
        CornerBasedShapePtr ExtraSmall() { return tokens::ShapeTokens::CornerExtraSmall.ToCornerBasedShape(); }
        CornerBasedShapePtr Small() { return tokens::ShapeTokens::CornerSmall.ToCornerBasedShape(); }
        CornerBasedShapePtr Medium() { return tokens::ShapeTokens::CornerMedium.ToCornerBasedShape(); }
        CornerBasedShapePtr Large() { return tokens::ShapeTokens::CornerLarge.ToCornerBasedShape(); }
        CornerBasedShapePtr LargeIncreased() { return tokens::ShapeTokens::CornerLargeIncreased.ToCornerBasedShape(); }
        CornerBasedShapePtr ExtraLarge() { return tokens::ShapeTokens::CornerExtraLarge.ToCornerBasedShape(); }
        CornerBasedShapePtr ExtraLargeIncreased() { return tokens::ShapeTokens::CornerExtraLargeIncreased.ToCornerBasedShape(); }
        CornerBasedShapePtr ExtraExtraLarge() { return tokens::ShapeTokens::CornerExtraExtraLarge.ToCornerBasedShape(); }
        foundation::CornerSize CornerNone() { return tokens::ShapeTokens::CornerValueNone; }
        foundation::CornerSize CornerExtraSmall() { return tokens::ShapeTokens::CornerValueExtraSmall; }
        foundation::CornerSize CornerSmall() { return tokens::ShapeTokens::CornerValueSmall; }
        foundation::CornerSize CornerMedium() { return tokens::ShapeTokens::CornerValueMedium; }
        foundation::CornerSize CornerLarge() { return tokens::ShapeTokens::CornerValueLarge; }
        foundation::CornerSize CornerLargeIncreased() { return tokens::ShapeTokens::CornerValueLargeIncreased; }
        foundation::CornerSize CornerExtraLarge() { return tokens::ShapeTokens::CornerValueExtraLarge; }
        foundation::CornerSize CornerExtraLargeIncreased() { return tokens::ShapeTokens::CornerValueExtraLargeIncreased; }
        foundation::CornerSize CornerExtraExtraLarge() { return tokens::ShapeTokens::CornerValueExtraExtraLarge; }
        foundation::CornerSize CornerFull() { return foundation::CornerSize::Percent(100); }
    }

    namespace {
        bool Same(const CornerBasedShapePtr& a, const CornerBasedShapePtr& b) {
            if (a == b) return true;
            return a && b && a->Equals(*b);
        }
    }

    bool Shapes::operator==(const Shapes& o) const {
        return Same(extraSmall, o.extraSmall) && Same(small, o.small) && Same(medium, o.medium) && Same(large, o.large) &&
               Same(extraLarge, o.extraLarge) && Same(largeIncreased, o.largeIncreased) &&
               Same(extraLargeIncreased, o.extraLargeIncreased) && Same(extraExtraLarge, o.extraExtraLarge);
    }

    CornerBasedShapePtr Top(const CornerBasedShapePtr& s, foundation::CornerSize bottomSize) {
        return s->Copy(s->topStart, s->topEnd, bottomSize, bottomSize);
    }
    CornerBasedShapePtr Bottom(const CornerBasedShapePtr& s, foundation::CornerSize topSize) {
        return s->Copy(topSize, topSize, s->bottomEnd, s->bottomStart);
    }
    CornerBasedShapePtr Start(const CornerBasedShapePtr& s, foundation::CornerSize endSize) {
        return s->Copy(s->topStart, endSize, endSize, s->bottomStart);
    }
    CornerBasedShapePtr End(const CornerBasedShapePtr& s, foundation::CornerSize startSize) {
        return s->Copy(startSize, s->topEnd, s->bottomEnd, startSize);
    }

    foundation::ShapePtr FromToken(const Shapes& s, tokens::ShapeToken t) {
        namespace K = tokens::ShapeKeyTokens;
        if (t == K::CornerExtraLarge) return s.extraLarge;
        if (t == K::CornerExtraLargeIncreased) return s.extraLargeIncreased;
        if (t == K::CornerExtraExtraLarge) return s.extraExtraLarge;
        if (t == K::CornerExtraLargeTop) return Top(s.extraLarge);
        if (t == K::CornerExtraSmall) return s.extraSmall;
        if (t == K::CornerExtraSmallTop) return Top(s.extraSmall);
        if (t == K::CornerFull) return foundation::CircleShape();
        if (t == K::CornerLarge) return s.large;
        if (t == K::CornerLargeIncreased) return s.largeIncreased;
        if (t == K::CornerLargeEnd) return End(s.large);
        if (t == K::CornerLargeTop) return Top(s.large);
        if (t == K::CornerMedium) return s.medium;
        if (t == K::CornerNone) return foundation::RectangleShape();
        if (t == K::CornerSmall) return s.small;
        if (t == K::CornerLargeStart) return Start(s.large);
        return foundation::RectangleShape();
    }

}
