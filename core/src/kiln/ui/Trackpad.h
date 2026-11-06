#pragma once

// Trackpad gestures on Android 14+ (MotionEventAdapter): a two-finger swipe
// (CLASSIFICATION_TWO_FINGER_SWIPE) and a pinch (CLASSIFICATION_PINCH) arrive as fake fingers.
// They are reinterpreted as one unpressed mouse pointer at the cursor that carries the swipe's
// scroll distance (Pan events) or the pinch's scale factor (Scale events). A pinch is
// reinterpreted only when the cursor position is known from hovering first.

#include "kiln/ui/Pointer.h"

#include <optional>

namespace kiln::ui {

    class TrackpadGestureTracker {
    public:
        // MotionEvent action and classification codes.
        enum Action { Down = 0, Up = 1, Move = 2, Cancel = 3, PointerDown = 5, PointerUp = 6, HoverMove = 7,
                      Scroll = 8, HoverEnter = 9, HoverExit = 10 };
        enum Classification { TwoFingerSwipe = 3, Pinch = 5 };

        // Sees every pointer event. `ev` holds the event's pointers (window pixels) with their
        // history; `gesture` is (scrollX, scrollY, scale) for pointer 0 now, then per batched
        // sample, or null when the source has no gesture axes. Sets ev.type, and for a
        // reinterpreted gesture replaces the pointers with the cursor pointer.
        void Process(RawPointerEvent& ev, int action, int classification, const float* gesture, int gestureCount);

    private:
        void Reset();
        bool m_PanOngoing = false, m_Reinterpreting = false, m_AnyDown = false;
        std::optional<Offset> m_LastHover, m_Cursor;
    };

}
