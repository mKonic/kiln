#include "kiln/foundation/AnchoredDraggable.h"

#include "kiln/foundation/Overscroll.h"
#include "kiln/ui/Locals.h"

namespace kiln::foundation {

    using namespace ui;

    namespace {
        struct AnchoredBox {
            AnchoredDragTarget* state = nullptr;
            Orientation orientation = Orientation::Horizontal;
            AnchoredDraggableOptions options;
            DraggableState* drag = nullptr;
            rt::Job fling;

            Offset Single(float v) const { return orientation == Orientation::Vertical ? Offset{ 0, v } : Offset{ v, 0 }; }
            float Axis(Offset o) const { return orientation == Orientation::Vertical ? o.y : o.x; }

            void Delta(float d) {
                if (options.overscroll) {
                    options.overscroll->ApplyToScroll(Single(d), NestedScrollSource::UserInput, [this](Offset o) {
                        return Single(state->UserDragBy(Axis(o)));
                    });
                } else {
                    state->UserDragBy(d);
                }
            }
        };

        rt::Task<> Fling(std::shared_ptr<AnchoredBox> box, float velocity, float threshold) {
            const auto& o = box->options;
            float available = o.transformVelocity ? o.transformVelocity(velocity) : velocity;
            if (o.overscroll) available -= box->Axis(o.overscroll->ConsumePreFling(box->Single(velocity)));
            const float left = co_await box->state->Fling(available, threshold, o.positionalThreshold, o.snapAnimationSpec);
            if (o.overscroll) {
                // At the first or last anchor the velocity goes into the edge.
                const float off = box->state->OffsetValue();
                if (off >= box->state->MaxPosition() || off <= box->state->MinPosition())
                    o.overscroll->AbsorbPostFling(box->Single(available - left));
            }
            if (o.afterFling) co_await o.afterFling();
        }
    }

    Modifier AnchoredDraggable(Modifier m, AnchoredDragTarget& state, Orientation orientation, AnchoredDraggableOptions options) {
        auto& c = *rt::Composition::Current();
        auto box = c.Remember<std::shared_ptr<AnchoredBox>>([] { return std::make_shared<AnchoredBox>(); });
        box->state = &state;
        box->orientation = orientation;
        box->options = options;
        std::weak_ptr<AnchoredBox> weak = box;
        // The node's coroutine scope: a fling still running when this leaves is cancelled (the
        // fling holds the box, so it would otherwise keep it alive until it settled).
        c.DisposableEffect(rt::Keys(), [weak] {
            return [weak] { if (auto b = weak.lock()) b->fling.Cancel(); };
        });
        auto& drag = RememberDraggableState([weak](float d) { if (auto b = weak.lock()) b->Delta(d); });
        box->drag = &drag;
        DraggableOptions d;
        d.enabled = options.enabled;
        d.interactionSource = options.interactionSource;
        // Unset: reversed for horizontal drags in RTL (AnchoredDraggableNode.isReverseDirection).
        d.reverseDirection = options.reverseDirection.value_or(orientation == Orientation::Horizontal &&
                                                               c.Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl);
        d.startDragImmediately = state.AnimationRunning();
        d.onDragStarted = [weak](Offset) {
            if (auto b = weak.lock()) { b->fling.Cancel(); b->state->BeginUserDrag(); }
        };
        d.onDragStopped = [weak](float velocity) {
            auto b = weak.lock();
            if (!b) return;
            b->state->EndUserDrag();
            const float threshold = b->drag->LastDensity().ToPx(Dp(anchored_draggable::MinFlingVelocityDp));
            b->fling = rt::Launch(rt::Dispatcher::Main(), Fling(b, velocity, threshold));
        };
        return Draggable(std::move(m), drag, orientation, std::move(d));
    }

}
