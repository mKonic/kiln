#pragma once

// material3-ripple (androidx.compose.material3.ripple): the ripple modifier node that material3's
// ripple() resolves its theme into. Presses start the platform ripple (RippleHost.h); hover,
// focus and drag show one state layer, the latest interaction's; focus can instead show an
// inset ring around the shape.

#include "kiln/animation/Spec.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"

#include <functional>
#include <memory>

namespace kiln::material3::ripple {

    using ColorProducer = std::function<ui::Color()>;

    // RippleNodeConfiguration
    struct RippleNodeConfiguration {
        // Press, hover and drag: an opacity, or nothing.
        struct Opacity {
            bool enabled = false;
            float alpha = 0;
            static Opacity None() { return {}; }
            static Opacity Of(float alpha) { return { true, alpha }; }
        };
        // FocusConfiguration.InsetRing: two strokes inside the shape.
        struct InsetRing {
            foundation::ShapePtr shape;
            ui::Dp outerStrokeInset, outerStrokeWidth;
            ColorProducer outerStrokeColor;
            ui::Dp innerStrokeInset, innerStrokeWidth;
            ColorProducer innerStrokeColor;
            anim::SpecPtr focusingAnimationSpec, unfocusingAnimationSpec;
        };
        struct Focus {
            enum class Kind : std::uint8_t { None, Opacity, InsetRing };
            Kind kind = Kind::None;
            float alpha = 0;
            InsetRing ring;
            static Focus None() { return {}; }
            static Focus Of(float alpha) { Focus f; f.kind = Kind::Opacity; f.alpha = alpha; return f; }
            static Focus Ring(InsetRing ring) { Focus f; f.kind = Kind::InsetRing; f.ring = std::move(ring); return f; }
        };

        bool isBounded = true;
        ui::Dp radius = ui::Dp::Unspecified();
        ColorProducer color;
        Opacity press, hover, drag;
        Focus focus;
    };

    // createRippleModifierNode: the ripple drawn over the content after it in the chain.
    ui::Modifier RippleModifier(ui::Modifier m, std::shared_ptr<foundation::MutableInteractionSource> source,
                                RippleNodeConfiguration configuration);

    // getRippleEndRadius: half the diagonal, plus 10 dp when bounded.
    float RippleEndRadius(const ui::Density& density, bool bounded, ui::Size size);

}
