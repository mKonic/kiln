#pragma once

// IndirectPointerInputDragCycleDetector: drags from a touch-navigation pad for draggable and
// scrollable. Pad positions map onto the node's axis through the pad's primary axis (an X-axis
// pad drags a vertical list along its length). Waits for the touch slop unless the drag starts
// immediately; gives the gesture up to whoever consumes it first, and can pick it up again
// while it stays unconsumed. Deltas of 2 px or less are dropped.

#include "kiln/foundation/Gestures.h"
#include "kiln/ui/IndirectPointer.h"
#include "kiln/ui/VelocityTracker.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::foundation {

    class IndirectDragDetector {
    public:
        struct Callbacks {
            std::function<bool()> startDragImmediately;
            std::function<void(Offset startedAt)> onStart;
            std::function<void(Offset delta)> onDelta;
            std::function<void(Offset velocity)> onStop;
            std::function<void()> onCancel;
        };

        Orientation orientation = Orientation::Vertical;
        float touchSlop = 8;
        float maximumFlingVelocity = 8000;
        Callbacks callbacks;

        void Process(ui::IndirectPointerEvent& event, ui::PointerEventPass pass);
        // The stream ended (cancel, or focus left): back to waiting, cancelling a drag.
        void Reset();
        bool Dragging() const { return m_State == State::Dragging; }

    private:
        enum class State : std::uint8_t { AwaitDown, AwaitTouchSlop, AwaitGesturePickup, Dragging };
        enum class AwaitSlop : std::uint8_t { NotInitialized, Yes, No };

        Offset AxisPosition(Offset p, ui::IndirectPointerEventPrimaryDirectionalMotionAxis axis) const;
        Offset PositionChange(const ui::IndirectPointerInputChange& c, ui::IndirectPointerEventPrimaryDirectionalMotionAxis axis,
                              bool ignoreConsumed) const;
        float Main(Offset o) const { return orientation == Orientation::Horizontal ? o.x : o.y; }
        // TouchSlopDetector.getPostSlopOffset; nullopt below the slop.
        std::optional<Offset> PostSlop(Offset change);

        void ToAwaitDown();
        void ToAwaitTouchSlop(const ui::IndirectPointerInputChange& down, std::int64_t pointer, Offset initialChange = {});
        void SendStart(const ui::IndirectPointerInputChange& down, const ui::IndirectPointerInputChange& trigger,
                       ui::IndirectPointerEventPrimaryDirectionalMotionAxis axis, Offset overSlop);
        void SendDelta(const ui::IndirectPointerInputChange& c, ui::IndirectPointerEventPrimaryDirectionalMotionAxis axis, Offset amount);
        void SendStop(const ui::IndirectPointerInputChange& c, ui::IndirectPointerEventPrimaryDirectionalMotionAxis axis);
        void SendCancel();

        State m_State = State::AwaitDown;
        AwaitSlop m_AwaitSlop = AwaitSlop::NotInitialized;
        bool m_ConsumedOnInitial = false, m_VerifyInFinal = false, m_Listening = false;
        std::optional<ui::IndirectPointerInputChange> m_InitialDown;
        std::int64_t m_Pointer = 0;
        Offset m_SlopTotal{};
        ui::VelocityTracker m_Velocity;
    };

    // Feeds a detector from the focused path's indirect events, with the view configuration's
    // touch slop and fling limit.
    Modifier IndirectDrag(Modifier m, std::shared_ptr<IndirectDragDetector> detector);

}
