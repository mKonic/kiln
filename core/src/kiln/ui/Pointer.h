#pragma once

// Pointer input (spec 03 req. 6, 02 req. 9): the host converts platform events into
// PointerInputEvents; the Owner hit-tests on press, keeps the hit path for the pointer until it
// lifts, and dispatches each event to the path in three passes: Initial (outer -> inner),
// Main (inner -> outer), Final (outer -> inner). Consumption flags let detectors coordinate.

#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kiln::ui {

    enum class PointerType : std::uint8_t { Unknown, Touch, Mouse, Stylus, Eraser };
    enum class PointerEventPass : std::uint8_t { Initial, Main, Final };
    enum class PointerEventType : std::uint8_t { Unknown, Press, Release, Move, Enter, Exit, Scroll };

    // A sample batched into an event before its current position (MotionEvent history).
    struct HistoricalChange { std::int64_t uptimeMillis = 0; Offset position; };

    struct PointerInputChange {
        std::int64_t id = 0;
        std::vector<HistoricalChange> historical;   // oldest first, local coordinates
        std::int64_t uptimeMillis = 0;
        Offset position;                 // local to the receiving node
        bool pressed = false;
        std::int64_t previousUptimeMillis = 0;
        Offset previousPosition;
        bool previousPressed = false;
        PointerType type = PointerType::Touch;
        Offset scrollDelta;
        // Shared by every copy of this change in one dispatch (all nodes, all passes); owned, so
        // a copy kept past the dispatch (currentEvent) stays valid.
        std::shared_ptr<bool> consumed;

        bool IsConsumed() const { return consumed && *consumed; }
        void Consume() { if (consumed) *consumed = true; }
        bool ChangedToDown() const { return !IsConsumed() && !previousPressed && pressed; }
        bool ChangedToDownIgnoreConsumed() const { return !previousPressed && pressed; }
        bool ChangedToUp() const { return !IsConsumed() && previousPressed && !pressed; }
        bool ChangedToUpIgnoreConsumed() const { return previousPressed && !pressed; }
        Offset PositionChange() const { return IsConsumed() ? Offset{} : position - previousPosition; }
        Offset PositionChangeIgnoreConsumed() const { return position - previousPosition; }
        bool IsOutOfBounds(IntSize size, Size extended = {}) const {
            return position.x < -extended.width || position.x > size.width + extended.width ||
                   position.y < -extended.height || position.y > size.height + extended.height;
        }
    };

    struct PointerEvent {
        std::vector<PointerInputChange> changes;
        PointerEventType type = PointerEventType::Unknown;
        std::uint32_t buttons = 0;       // bit 0 primary
        int classification = 0;          // MotionEvent classification (2 = deep press)
    };

    // What a host feeds the Owner: positions in window pixels.
    struct RawPointer {
        std::int64_t id = 0;
        Offset position;
        bool down = false;
        PointerType type = PointerType::Touch;
        Offset scrollDelta;
        std::vector<HistoricalChange> historical;   // root coordinates
    };
    struct RawPointerEvent {
        std::int64_t uptimeMillis = 0;
        std::vector<RawPointer> pointers;    // all pointers currently known (down or hovering)
        PointerEventType type = PointerEventType::Unknown;
        std::uint32_t buttons = 0;
        int classification = 0;
        bool cancel = false;                 // ACTION_CANCEL: every path is cancelled
    };

    class PointerInputModifier {
    public:
        virtual ~PointerInputModifier() = default;
        virtual void OnPointerEvent(PointerEvent& event, PointerEventPass pass, IntSize bounds) = 0;
        virtual void OnCancelPointerInput() = 0;
        // Children's hit areas grow to this size (minimum touch target) when true.
        virtual bool InterceptOutOfBoundsChildEvents() const { return false; }
    };

    // ViewConfiguration values (spec 02 req. 7), read from the platform by the host.
    struct ViewConfiguration {
        std::int64_t longPressTimeoutMillis = 400;   // ViewConfiguration default 400 ms
        std::int64_t doubleTapTimeoutMillis = 300;
        std::int64_t doubleTapMinTimeMillis = 40;
        std::int64_t tapTimeoutMillis = 100;         // ViewConfiguration.getTapTimeout (TapIndicationDelay)
        float touchSlop = 8;                         // px; the host sets the scaled value
        Dp minimumTouchTargetSize{ 48 };
        float maximumFlingVelocity = 8000;           // px/s
        float minimumFlingVelocity = 50;             // px/s
        float scrollFriction = 0.015f;               // ViewConfiguration.getScrollFriction()
        float verticalScrollFactor = 64;             // px per wheel notch (getScaledVerticalScrollFactor)
        float horizontalScrollFactor = 64;
    };

}
