#pragma once

// Indirect pointer input (ui input/indirect): touches on a surface that does not map to the
// screen, such as a touch-navigation pad (InputDevice.SOURCE_TOUCH_NAVIGATION). Events go to the
// focused path, not to a hit test: Initial root to focused, Main focused to root, Final root to
// focused again (FocusOwnerImpl.dispatchIndirectPointerEvent).

#include "kiln/ui/Geometry.h"
#include "kiln/ui/Pointer.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kiln::ui {

    enum class IndirectPointerEventType : std::uint8_t { Unknown, Press, Release, Move };
    // The axis a one-dimensional pad moves along (from its motion ranges), if it has one.
    enum class IndirectPointerEventPrimaryDirectionalMotionAxis : std::uint8_t { None, X, Y };

    struct IndirectPointerInputChange {
        std::int64_t id = 0;
        std::int64_t uptimeMillis = 0;
        Offset position;
        bool pressed = false;
        float pressure = 1;
        std::int64_t previousUptimeMillis = 0;
        Offset previousPosition;
        bool previousPressed = false;
        std::vector<HistoricalChange> historical;
        bool isConsumed = false;

        void Consume() { isConsumed = true; }
        bool ChangedToDownIgnoreConsumed() const { return !previousPressed && pressed; }
        bool ChangedToUp() const { return !isConsumed && previousPressed && !pressed; }
        bool ChangedToUpIgnoreConsumed() const { return previousPressed && !pressed; }
    };

    struct IndirectPointerEvent {
        std::vector<IndirectPointerInputChange> changes;
        IndirectPointerEventType type = IndirectPointerEventType::Unknown;
        IndirectPointerEventPrimaryDirectionalMotionAxis primaryDirectionalMotionAxis =
            IndirectPointerEventPrimaryDirectionalMotionAxis::None;
    };

    class IndirectPointerInputModifier {
    public:
        virtual ~IndirectPointerInputModifier() = default;
        virtual void OnIndirectPointerEvent(IndirectPointerEvent& event, PointerEventPass pass) = 0;
        // No more events until new pointers go down: the stream was cancelled, or focus left.
        virtual void OnCancelIndirectPointerInput() = 0;
    };

    // One MotionEvent from the pad, as the host reads it.
    struct RawIndirectPointerEvent {
        enum class Action : std::uint8_t { Down, Up, Move, Cancel, PointerDown, PointerUp, Other };
        Action action = Action::Other;
        int actionIndex = 0;
        std::int64_t uptimeMillis = 0;
        struct Pointer {
            std::int64_t id = 0;
            Offset position;
            float pressure = 1;
            std::vector<HistoricalChange> historical;
        };
        std::vector<Pointer> pointers;
        IndirectPointerEventPrimaryDirectionalMotionAxis axis = IndirectPointerEventPrimaryDirectionalMotionAxis::None;
    };

    // MotionEventAdapter.convertToIndirectPointerEvent: tracks each pointer's previous sample.
    // Returns false for a cancel (the stream ends, `out` is untouched).
    class IndirectPointerAdapter {
    public:
        bool Convert(const RawIndirectPointerEvent& raw, IndirectPointerEvent& out);

    private:
        struct Previous { std::int64_t uptime; Offset position; bool down; };
        std::unordered_map<std::int64_t, Previous> m_Previous;
    };

}
