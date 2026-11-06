#pragma once

// draggable (Draggable.kt): one-axis drags after touch slop (or immediately), deltas into a
// DraggableState, the release velocity to onDragStopped, drag interactions on the source.

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Interaction.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    class DraggableState {
    public:
        explicit DraggableState(std::function<void(float)> onDelta) : m_OnDelta(std::move(onDelta)) {}
        // dispatchRawDelta: applies a delta outside of a drag.
        void DispatchRawDelta(float delta) { if (m_OnDelta) m_OnDelta(delta); }
        bool IsDragging() const { return m_Dragging.Get(); }
        // internal
        void SetOnDelta(std::function<void(float)> f) { m_OnDelta = std::move(f); }
        void SetDragging(bool d) { if (m_Dragging.Peek() != d) m_Dragging.Set(d); }
        // Density of the node the last drag ran on (for dp thresholds in onDragStopped).
        ui::Density LastDensity() const { return m_Density; }
        void SetDensity(ui::Density d) { m_Density = d; }
    private:
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

}
