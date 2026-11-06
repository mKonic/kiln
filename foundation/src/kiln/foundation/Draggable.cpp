#include "kiln/foundation/Draggable.h"

#include "kiln/foundation/IndirectDrag.h"
#include "kiln/ui/VelocityTracker.h"

#include <cmath>

namespace kiln::foundation {

    using namespace ui;

    DraggableState& RememberDraggableState(std::function<void(float)> onDelta, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& s = c.RememberNew<DraggableState>({}, key, onDelta);
        s.SetOnDelta(std::move(onDelta));
        return s;
    }

    namespace {
        struct DragBox {
            DraggableState* state = nullptr;
            Orientation orientation = Orientation::Horizontal;
            DraggableOptions options;
            std::uint64_t interaction = 0;
            std::shared_ptr<IndirectDragDetector> indirect;
        };

        // Pad drags (DragGestureNode with the indirect detector): the same start, deltas and
        // stop as a touch drag, along our axis.
        void WireIndirect(const std::shared_ptr<DragBox>& box) {
            box->indirect = std::make_shared<IndirectDragDetector>();
            std::weak_ptr<DragBox> w = box;
            auto& cb = box->indirect->callbacks;
            cb.startDragImmediately = [w] { auto b = w.lock(); return b && b->options.startDragImmediately; };
            cb.onStart = [w](Offset at) {
                auto b = w.lock();
                if (!b || !b->options.enabled) return;
                b->state->SetDragging(true);
                if (b->options.interactionSource) b->interaction = b->options.interactionSource->Start(Interaction::Kind::Drag, at);
                if (b->options.onDragStarted) b->options.onDragStarted(at);
            };
            cb.onDelta = [w](Offset d) {
                auto b = w.lock();
                if (!b || !b->options.enabled) return;
                const float v = b->orientation == Orientation::Vertical ? d.y : d.x;
                b->state->DispatchRawDelta(b->options.reverseDirection ? -v : v);
            };
            auto end = [w](float velocity, bool cancelled) {
                auto b = w.lock();
                if (!b) return;
                b->state->SetDragging(false);
                if (b->options.interactionSource && b->interaction) {
                    if (cancelled) b->options.interactionSource->Cancel(Interaction::Kind::Drag, b->interaction);
                    else b->options.interactionSource->End(Interaction::Kind::Drag, b->interaction);
                    b->interaction = 0;
                }
                if (b->options.onDragStopped) b->options.onDragStopped(b->options.reverseDirection ? -velocity : velocity);
            };
            cb.onStop = [w, end](Offset v) {
                auto b = w.lock();
                if (b) end(b->orientation == Orientation::Vertical ? v.y : v.x, false);
            };
            cb.onCancel = [end] { end(0, true); };
        }

        rt::Task<> DragLoop(PointerInputScope& s, std::shared_ptr<DragBox> box) {
            VelocityTracker tracker;
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (e->changes.empty() || !e->changes[0].ChangedToDown() ) continue;
                const DraggableOptions& o = box->options;
                if (!o.enabled) continue;
                const bool vertical = box->orientation == Orientation::Vertical;
                auto axis = [vertical](Offset v) { return vertical ? v.y : v.x; };
                const std::int64_t id = e->changes[0].id;
                const Offset downAt = e->changes[0].position;
                tracker.Reset();
                tracker.AddPointerInputChange(e->changes[0]);
                const float slop = PointerSlop(s.Config(), e->changes[0].type);
                float total = 0;
                bool dragging = false;
                auto start = [&](Offset at) {
                    dragging = true;
                    box->state->SetDensity(s.GetDensity());
                    box->state->SetDragging(true);
                    if (o.interactionSource) box->interaction = o.interactionSource->Start(Interaction::Kind::Drag, at);
                    if (o.onDragStarted) o.onDragStarted(at);
                };
                auto apply = [&](float d) { box->state->DispatchRawDelta(o.reverseDirection ? -d : d); };
                if (o.startDragImmediately) { start(downAt); e->changes[0].Consume(); }
                for (;;) {
                    auto m = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    PointerInputChange* c = nullptr;
                    for (auto& ch : m->changes) if (ch.id == id) c = &ch;
                    if (!c) break;
                    tracker.AddPointerInputChange(*c);
                    if (c->ChangedToUpIgnoreConsumed()) {
                        if (dragging) {
                            const auto& cfg = s.Config();
                            const float v = axis(tracker.CalculateVelocity({ cfg.maximumFlingVelocity, cfg.maximumFlingVelocity }));
                            box->state->SetDragging(false);
                            if (o.interactionSource && box->interaction) { o.interactionSource->End(Interaction::Kind::Drag, box->interaction); box->interaction = 0; }
                            if (o.onDragStopped) o.onDragStopped(o.reverseDirection ? -v : v);
                            c->Consume();
                        }
                        break;
                    }
                    if (!dragging) {
                        if (c->IsConsumed()) break;   // someone else took it
                        total += axis(c->PositionChangeIgnoreConsumed());
                        if (std::abs(total) >= slop && std::abs(total) > 0) {
                            start(c->position);
                            apply(total - (total > 0 ? slop : -slop));   // the part past the slop
                            c->Consume();
                        }
                        continue;
                    }
                    if (c->IsConsumed()) {
                        // Taken mid-drag: cancel (DragInteraction.Cancel, stopped with 0 velocity).
                        box->state->SetDragging(false);
                        if (o.interactionSource && box->interaction) { o.interactionSource->Cancel(Interaction::Kind::Drag, box->interaction); box->interaction = 0; }
                        if (o.onDragStopped) o.onDragStopped(0);
                        break;
                    }
                    apply(axis(c->PositionChange()));
                    c->Consume();
                }
            }
        }
    }

    Modifier Draggable(Modifier m, DraggableState& state, Orientation orientation, DraggableOptions options) {
        auto box = std::make_shared<DragBox>();
        if (auto* c = rt::Composition::Current()) box = c->Remember<std::shared_ptr<DragBox>>([&] { return box; });
        box->state = &state;
        box->orientation = orientation;
        box->options = std::move(options);
        if (!box->indirect) WireIndirect(box);
        box->indirect->orientation = orientation;
        if (box->options.enabled) m = IndirectDrag(std::move(m), box->indirect);
        return PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(orientation)),
                            [box](PointerInputScope& s) { return DragLoop(s, box); });
    }

}
