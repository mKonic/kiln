#include "kiln/ui/IndirectPointer.h"

namespace kiln::ui {

    bool IndirectPointerAdapter::Convert(const RawIndirectPointerEvent& raw, IndirectPointerEvent& out) {
        using A = RawIndirectPointerEvent::Action;
        if (raw.action == A::Cancel) {
            m_Previous.clear();
            return false;
        }
        // A new stream forgets pointers it never saw lift.
        if (raw.action == A::Down) m_Previous.clear();
        const int upIndex = raw.action == A::Up ? 0 : raw.action == A::PointerUp ? raw.actionIndex : -1;
        const bool downOrMove = raw.action == A::Down || raw.action == A::PointerDown || raw.action == A::Move;
        out.changes.clear();
        for (std::size_t i = 0; i < raw.pointers.size(); ++i) {
            const auto& p = raw.pointers[i];
            IndirectPointerInputChange c;
            c.id = p.id;
            c.uptimeMillis = raw.uptimeMillis;
            c.position = p.position;
            c.pressed = int(i) != upIndex;
            c.pressure = p.pressure;
            c.historical = p.historical;
            auto prev = m_Previous.find(p.id);
            if (prev != m_Previous.end()) {
                c.previousUptimeMillis = prev->second.uptime;
                c.previousPosition = prev->second.position;
                c.previousPressed = prev->second.down;
            } else {
                c.previousUptimeMillis = raw.uptimeMillis;
                c.previousPosition = p.position;
                c.previousPressed = false;
            }
            if (int(i) == upIndex) m_Previous.erase(p.id);
            else if (downOrMove) m_Previous[p.id] = { raw.uptimeMillis, p.position, true };
            out.changes.push_back(std::move(c));
        }
        switch (raw.action) {
            case A::Up: case A::PointerUp: out.type = IndirectPointerEventType::Release; break;
            case A::Down: case A::PointerDown: out.type = IndirectPointerEventType::Press; break;
            case A::Move: out.type = IndirectPointerEventType::Move; break;
            default: out.type = IndirectPointerEventType::Unknown; break;
        }
        out.primaryDirectionalMotionAxis = raw.axis;
        return !out.changes.empty();
    }

}
