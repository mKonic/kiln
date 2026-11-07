#pragma once

// Pointer input (spec 03 req. 6, 02 req. 9): the host converts platform events into
// PointerInputEvents; the Owner hit-tests on press, keeps the hit path for the pointer until it
// lifts, and dispatches each event to the path in three passes: Initial (outer -> inner),
// Main (inner -> outer), Final (outer -> inner). Consumption flags let detectors coordinate.

#include "kiln/ui/Geometry.h"

#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>

namespace kiln::ui {

    enum class PointerType : std::uint8_t { Unknown, Touch, Mouse, Stylus, Eraser };
    enum class PointerEventPass : std::uint8_t { Initial, Main, Final };
    // PanStart / PanMove / PanEnd: a trackpad two-finger swipe (CLASSIFICATION_TWO_FINGER_SWIPE),
    // reported as one unpressed mouse pointer at the cursor carrying panOffset. ScaleStart /
    // ScaleChange / ScaleEnd: a trackpad pinch (CLASSIFICATION_PINCH), the same kind of pointer
    // carrying scaleFactor.
    enum class PointerEventType : std::uint8_t { Unknown, Press, Release, Move, Enter, Exit, Scroll, PanStart, PanMove, PanEnd,
                                                 ScaleStart, ScaleChange, ScaleEnd };

    // A sample batched into an event before its current position (MotionEvent history).
    struct HistoricalChange { std::int64_t uptimeMillis = 0; Offset position; Offset panOffset; float scaleFactor = 1; };

    // PointerKeyboardModifiers: the MotionEvent's meta state (KeyEvent.META_* bits).
    struct PointerKeyboardModifiers {
        std::uint32_t packed = 0;
        bool IsCtrlPressed() const { return (packed & 0x1000u) != 0; }
        bool IsMetaPressed() const { return (packed & 0x10000u) != 0; }
        bool IsAltPressed() const { return (packed & 0x02u) != 0; }
        bool IsAltGraphPressed() const { return (packed & 0x20u) != 0; }   // META_ALT_RIGHT_ON
        bool IsSymPressed() const { return (packed & 0x04u) != 0; }
        bool IsShiftPressed() const { return (packed & 0x01u) != 0; }
        bool IsFunctionPressed() const { return (packed & 0x08u) != 0; }
        bool IsCapsLockOn() const { return (packed & 0x100000u) != 0; }
        bool IsScrollLockOn() const { return (packed & 0x400000u) != 0; }
        bool IsNumLockOn() const { return (packed & 0x200000u) != 0; }
    };

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
        Offset panOffset;                // trackpad pan: AXIS_GESTURE_SCROLL_X/Y_DISTANCE
        float scaleFactor = 1;           // trackpad pinch: AXIS_GESTURE_PINCH_SCALE_FACTOR
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

    // PointerButtons: MotionEvent.getButtonState bits. A stylus side button counts as the
    // primary (BUTTON_STYLUS_PRIMARY) or secondary (BUTTON_STYLUS_SECONDARY) button.
    struct PointerButtons {
        static constexpr std::uint32_t Primary = 1, Secondary = 2, Tertiary = 4, Back = 8, Forward = 16,
                                       StylusPrimary = 32, StylusSecondary = 64;
        std::uint32_t packed = 0;
        PointerButtons() = default;
        PointerButtons(std::uint32_t bits) : packed(bits) {}
        operator std::uint32_t() const { return packed; }
        bool IsPrimaryPressed() const { return (packed & (Primary | StylusPrimary)) != 0; }
        bool IsSecondaryPressed() const { return (packed & (Secondary | StylusSecondary)) != 0; }
        bool IsTertiaryPressed() const { return (packed & Tertiary) != 0; }
        bool IsBackPressed() const { return (packed & Back) != 0; }
        bool IsForwardPressed() const { return (packed & Forward) != 0; }
        bool IsPressed(int index) const {
            switch (index) {
                case 0: return IsPrimaryPressed();
                case 1: return IsSecondaryPressed();
                case 2: case 3: case 4: return (packed & (1u << index)) != 0;
                default: return index >= 0 && index < 30 && (packed & (1u << (index + 2))) != 0;
            }
        }
        bool AreAnyPressed() const { return packed != 0; }
        // Stylus buttons fold into primary / secondary for the index; -1 when none are down.
        int IndexOfFirstPressed() const {
            if (!packed) return -1;
            std::uint32_t s = Folded();
            int i = 0;
            while (!(s & 1)) { ++i; s >>= 1; }
            return i;
        }
        int IndexOfLastPressed() const {
            std::uint32_t s = Folded();
            int i = -1;
            while (s) { ++i; s >>= 1; }
            return i;
        }

    private:
        std::uint32_t Folded() const { return ((packed & 0x60u) >> 5) | (packed & ~0x60u); }
    };

    struct PointerEvent {
        std::vector<PointerInputChange> changes;
        PointerEventType type = PointerEventType::Unknown;
        PointerButtons buttons;
        PointerKeyboardModifiers keyboardModifiers;
        int classification = 0;          // MotionEvent classification (2 = deep press)
    };

    // What a host feeds the Owner: positions in window pixels.
    struct RawPointer {
        std::int64_t id = 0;
        Offset position;
        bool down = false;
        PointerType type = PointerType::Touch;
        Offset scrollDelta;
        Offset panOffset;
        float scaleFactor = 1;
        std::vector<HistoricalChange> historical;   // root coordinates
    };
    struct RawPointerEvent {
        std::int64_t uptimeMillis = 0;
        std::vector<RawPointer> pointers;    // all pointers currently known (down or hovering)
        PointerEventType type = PointerEventType::Unknown;
        std::uint32_t buttons = 0;
        std::uint32_t metaState = 0;
        int classification = 0;
        bool cancel = false;                 // ACTION_CANCEL: every path is cancelled
    };

    class PointerInputModifier {
    public:
        virtual ~PointerInputModifier() = default;
        virtual void OnPointerEvent(PointerEvent& event, PointerEventPass pass, IntSize bounds) = 0;
        virtual void OnCancelPointerInput() = 0;
        // touchBoundsExpansion: how far past its bounds a stylus or eraser still hits it (left,
        // top, right, bottom px, already resolved for the layout direction); a direct hit on
        // anything else wins.
        virtual std::optional<std::array<float, 4>> TouchBoundsExpansion(const Density&, LayoutDirection) const { return std::nullopt; }
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
        float handwritingSlop = 2;                   // px; ViewConfiguration.getScaledHandwritingSlop (API 34)
        float handwritingGestureLineMargin = 16;     // px; getScaledHandwritingGestureLineMargin (API 34)
        Dp minimumTouchTargetSize{ 48 };
        float maximumFlingVelocity = 8000;           // px/s
        float minimumFlingVelocity = 50;             // px/s
        float scrollFriction = 0.015f;               // ViewConfiguration.getScrollFriction()
        float verticalScrollFactor = 64;             // px per wheel notch (getScaledVerticalScrollFactor)
        float horizontalScrollFactor = 64;
    };

}
