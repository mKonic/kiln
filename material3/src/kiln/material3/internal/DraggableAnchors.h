#pragma once

// internal/DraggableAnchors.kt: Modifier.draggableAnchors sets an AnchoredDraggableState's anchors
// from this layout's size (each measure, so they follow a size that animates) and offsets the
// layout by the state's offset. Kiln has no lookahead pass, so only the main pass applies.

#include "kiln/foundation/AnchoredDraggable.h"
#include <kiln/Function.h>
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Modifier.h"

#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace kiln::material3::internal {

    template<typename T>
    using AnchorsCalculation = kiln::Function<std::pair<foundation::DraggableAnchors<T>, T>(ui::IntSize size, const ui::Constraints& constraints)>;

    template<typename T>
    struct DraggableAnchorsParams {
        foundation::AnchoredDraggableState<T>* state = nullptr;
        std::shared_ptr<const AnchorsCalculation<T>> anchors;   // compared by identity
        foundation::Orientation orientation = foundation::Orientation::Horizontal;
        bool operator==(const DraggableAnchorsParams& o) const { return state == o.state && anchors == o.anchors && orientation == o.orientation; }
    };

    template<typename T>
    struct DraggableAnchorsNode final : ui::ModifierNode, ui::LayoutModifier {
        DraggableAnchorsParams<T> params;
        explicit DraggableAnchorsNode(DraggableAnchorsParams<T> p) : params(std::move(p)) {}
        void OnParamsChanged() { InvalidateMeasure(); }
        ui::LayoutModifier* AsLayout() override { return this; }

        ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
            ui::Placeable& placeable = m.Measure(c);
            auto& state = *params.state;
            if (params.anchors && *params.anchors) {
                auto [newAnchors, suggested] = (*params.anchors)(ui::IntSize{ placeable.Width(), placeable.Height() }, c);
                T target = newAnchors.HasPositionFor(suggested) ? suggested : newAnchors.Size() > 0 ? newAnchors.AnchorAt(0) : suggested;
                // Equal anchors but no offset yet: reset them so the update snaps to the target.
                if (state.Anchors() == newAnchors && std::isnan(state.Offset()) && newAnchors.Size() > 0)
                    state.UpdateAnchors(foundation::DraggableAnchors<T>{}, target);
                state.UpdateAnchors(std::move(newAnchors), target);
            }
            const bool reverse = s.Direction() == ui::LayoutDirection::Rtl && params.orientation == foundation::Orientation::Horizontal;
            const auto orientation = params.orientation;
            auto* st = params.state;
            return s.Layout(placeable.Width(), placeable.Height(), [&placeable, st, reverse, orientation](ui::PlacementScope& p) {
                const float offset = st->Offset();
                if (std::isnan(offset)) return;   // not placed until the anchors are set
                const float x = orientation == foundation::Orientation::Horizontal ? offset * (reverse ? -1.0f : 1.0f) : 0.0f;
                const float y = orientation == foundation::Orientation::Vertical ? offset : 0.0f;
                // The offset is motion (a drag), not structure: what reads positions can leave it
                // out, so it follows the drag instead of animating after it.
                p.WithMotionFrameOfReferencePlacement([&] { p.Place(placeable, ui::RoundJava(x), ui::RoundJava(y)); });
            });
        }
    };

    template<typename T>
    ui::Modifier DraggableAnchors(ui::Modifier m, foundation::AnchoredDraggableState<T>& state, foundation::Orientation orientation,
                                  std::shared_ptr<const AnchorsCalculation<T>> anchors) {
        return m.template With<DraggableAnchorsNode<T>>(DraggableAnchorsParams<T>{ &state, std::move(anchors), orientation });
    }

}
