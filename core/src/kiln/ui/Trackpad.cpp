#include "kiln/ui/Trackpad.h"

namespace kiln::ui {

    void TrackpadGestureTracker::Reset() {
        m_Reinterpreting = false;
        m_Cursor.reset();
        m_PanOngoing = false;
        m_AnyDown = false;
    }

    void TrackpadGestureTracker::Process(RawPointerEvent& ev, int action, int classification, const float* gesture, int gestureCount) {
        using T = PointerEventType;
        const bool trackpadGesture = classification == TwoFingerSwipe || classification == Pinch;
        if (action == Cancel) {
            Reset();
            m_LastHover.reset();
            return;
        }
        // A new stream that is not a trackpad gesture forgets stale gesture state.
        if (action == Down && !trackpadGesture) Reset();
        if (action == Down || action == PointerDown) m_AnyDown = true;
        const bool isHover = action == HoverEnter || action == HoverMove || action == HoverExit;
        // Interleaved hover moves during a swipe (classification none) must not reset it.
        if ((action == HoverEnter || action == HoverExit || (action == HoverMove && !m_AnyDown)) && !trackpadGesture) Reset();
        if (classification == TwoFingerSwipe) m_PanOngoing = true;
        else if (classification == Pinch) m_PanOngoing = false;
        if (isHover && !m_AnyDown && (action == HoverMove || action == HoverEnter) && !ev.pointers.empty())
            m_LastHover = ev.pointers[0].position;

        const bool isSwipe = classification == TwoFingerSwipe || m_PanOngoing;
        const bool isPinch = classification == Pinch;
        const bool reinterpretPinch = isPinch && ((action == Down && m_LastHover) || (action != Down && m_Reinterpreting));
        if ((isSwipe || reinterpretPinch) && !ev.pointers.empty()) {
            m_Reinterpreting = true;
            if (action == PointerDown && isPinch && ev.pointers.size() == 2) {
                const Offset a = ev.pointers[0].position, b = ev.pointers[1].position;
                m_Cursor = Offset{ (a.x + b.x) / 2, (a.y + b.y) / 2 };
            } else if (action == Down || !m_Cursor) {
                m_Cursor = isPinch && m_LastHover ? *m_LastHover : ev.pointers[0].position;
            }
            RawPointer rp = ev.pointers[0];
            rp.position = *m_Cursor;
            rp.down = false;
            rp.type = PointerType::Mouse;
            rp.panOffset = {};
            rp.scaleFactor = 1;
            const bool pan = isSwipe;
            auto scale = [](float s) { return s > 0 ? s : 1.0f; };
            if (gesture && gestureCount >= 3) {
                if (pan) rp.panOffset = { gesture[0], gesture[1] };
                if (isPinch) rp.scaleFactor = scale(gesture[2]);
                for (std::size_t h = 0; h < rp.historical.size(); ++h) {
                    auto& hc = rp.historical[h];
                    hc.position = *m_Cursor;
                    const int at = 3 + 3 * int(h);
                    if (at + 2 >= gestureCount) continue;
                    if (pan) hc.panOffset = { gesture[at], gesture[at + 1] };
                    hc.scaleFactor = scale(gesture[at + 2]);
                }
            }
            ev.pointers = { rp };
        } else {
            m_Reinterpreting = false;
        }

        // The event type follows the active gesture (PointerEvent.android calculatePointerEventType).
        const bool panGesture = m_PanOngoing || classification == TwoFingerSwipe;
        const bool pinchGesture = !panGesture && isPinch;
        switch (action) {
            case Down: ev.type = panGesture ? T::PanStart : pinchGesture ? T::ScaleStart : T::Press; break;
            case PointerDown: ev.type = panGesture ? T::PanStart : pinchGesture ? T::ScaleChange : T::Press; break;
            case Up: ev.type = panGesture ? T::PanEnd : pinchGesture ? T::ScaleEnd : T::Release; break;
            case PointerUp: ev.type = panGesture ? T::PanEnd : pinchGesture ? T::ScaleChange : T::Release; break;
            case Move: case HoverMove: ev.type = panGesture ? T::PanMove : pinchGesture ? T::ScaleChange : T::Move; break;
            case HoverEnter: ev.type = T::Enter; break;
            case HoverExit: ev.type = T::Exit; break;
            case Scroll: ev.type = T::Scroll; break;
            default: ev.type = T::Unknown; break;
        }

        const bool panExit = action == HoverExit && classification != TwoFingerSwipe;
        if (action == Up || panExit) Reset();
    }

}
