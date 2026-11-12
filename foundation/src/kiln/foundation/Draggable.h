#pragma once

// draggable (Draggable.kt): one-axis drags after touch slop (or immediately), deltas into a
// DraggableState, the release velocity to onDragStopped, drag interactions on the source.

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/MutatorMutex.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    // DragScope / Drag2DScope: what a drag { } block moves the state with.
    struct DragScope { std::function<void(float)> dragBy; void DragBy(float pixels) const { dragBy(pixels); } };
    struct Drag2DScope { std::function<void(Offset)> dragBy; void DragBy(Offset pixels) const { dragBy(pixels); } };

    class DraggableState {
    public:
        explicit DraggableState(std::function<void(float)> onDelta) : m_OnDelta(std::move(onDelta)) {}
        // dispatchRawDelta: applies a delta outside of a drag.
        void DispatchRawDelta(float delta) { if (m_OnDelta) m_OnDelta(delta); }
        bool IsDragging() const { return m_Dragging.Get(); }
        // drag(priority) { dragBy(...) }: moves the state from code, one drag at a time; a newer
        // drag of the same or higher priority stops this one's dragBy.
        rt::Task<> Drag(std::function<rt::Task<>(const DragScope&)> block, MutatePriority priority = MutatePriority::Default);
        // internal
        void SetOnDelta(std::function<void(float)> f) { m_OnDelta = std::move(f); }
        void SetDragging(bool d) { if (m_Dragging.Peek() != d) m_Dragging.Set(d); }
        // Density of the node the last drag ran on (for dp thresholds in onDragStopped).
        ui::Density LastDensity() const { return m_Density; }
        void SetDensity(ui::Density d) { m_Density = d; }
    private:
        MutatorMutex m_Mutex;
        ui::Density m_Density{};
        std::function<void(float)> m_OnDelta;
        rt::State<bool> m_Dragging{ false };
    };

    // rememberDraggableState(onDelta): the latest onDelta is used.
    DraggableState& RememberDraggableState(std::function<void(float)> onDelta, rt::Key key = {});

    struct DraggableOptions {
        bool enabled = true;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        bool startDragImmediately = false;
        std::function<void(Offset startedPosition)> onDragStarted;
        std::function<void(float velocity)> onDragStopped;
        bool reverseDirection = false;
    };

    Modifier Draggable(Modifier m, DraggableState& state, Orientation orientation, DraggableOptions options = {});

    // --- draggable2D (Draggable2D.kt): drags in any direction after touch slop ------------------

    class Draggable2DState {
    public:
        explicit Draggable2DState(std::function<void(Offset)> onDelta) : m_OnDelta(std::move(onDelta)) {}
        void DispatchRawDelta(Offset delta) { if (m_OnDelta) m_OnDelta(delta); }
        bool IsDragging() const { return m_Dragging.Get(); }
        rt::Task<> Drag(std::function<rt::Task<>(const Drag2DScope&)> block, MutatePriority priority = MutatePriority::Default);
        // internal
        void SetOnDelta(std::function<void(Offset)> f) { m_OnDelta = std::move(f); }
        void SetDragging(bool d) { if (m_Dragging.Peek() != d) m_Dragging.Set(d); }

    private:
        MutatorMutex m_Mutex;
        std::function<void(Offset)> m_OnDelta;
        rt::State<bool> m_Dragging{ false };
    };
    Draggable2DState& RememberDraggable2DState(std::function<void(Offset)> onDelta, rt::Key key = {});

    struct Draggable2DOptions {
        bool enabled = true;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        bool startDragImmediately = false;
        std::function<void(Offset startedPosition)> onDragStarted;
        std::function<void(Offset velocity)> onDragStopped;
        bool reverseDirection = false;
    };
    Modifier Draggable2D(Modifier m, Draggable2DState& state, Draggable2DOptions options = {});


    // The 2D drag detector draggable2D and scrollable2D share: after the slop (the distance in
    // any direction), deltas to onDelta and the release velocity to onStop.
    struct Drag2DCallbacks {
        std::function<bool()> enabled, startImmediately;
        std::function<void(Offset at)> onStart;
        std::function<void(Offset delta)> onDelta;
        std::function<void(Offset velocity)> onStop;
        std::function<void()> onCancel;
    };
    rt::Task<> DetectDrag2D(PointerInputScope& s, std::shared_ptr<Drag2DCallbacks> callbacks);

}
