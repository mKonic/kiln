#pragma once

// Layout rect callbacks (spatial/RectManager, ThrottledCallbacks, RelativeLayoutBounds): a
// node's bounds in the window, told after layout passes that changed them, throttled and
// debounced; and the same on any layout change in the window (global layout listeners).

#include "kiln/runtime/Task.h"
#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kiln::ui {

    class LayoutNode;
    class ModifierNode;

    // RelativeLayoutBounds: a layout's bounds (root pixels) with the window it is in. kiln's
    // root sits at the window's origin, and the window at the screen offset the host reports.
    class RelativeLayoutBounds {
    public:
        RelativeLayoutBounds(IntRect boundsInRoot, IntSize windowSize, IntOffset screenOffset, const LayoutNode* node)
            : m_Bounds(boundsInRoot), m_Window(windowSize), m_Screen(screenOffset), m_Node(node) {}
        IntOffset PositionInRoot() const { return { m_Bounds.left, m_Bounds.top }; }
        IntOffset PositionInWindow() const { return PositionInRoot(); }
        IntOffset PositionInScreen() const { return { m_Bounds.left + m_Screen.x, m_Bounds.top + m_Screen.y }; }
        int Width() const { return m_Bounds.Width(); }
        int Height() const { return m_Bounds.Height(); }
        IntRect BoundsInRoot() const { return m_Bounds; }
        IntRect BoundsInWindow() const { return m_Bounds; }
        IntRect BoundsInScreen() const { return { m_Bounds.left + m_Screen.x, m_Bounds.top + m_Screen.y, m_Bounds.right + m_Screen.x, m_Bounds.bottom + m_Screen.y }; }
        // The parts of it that layouts drawn after it (not its ancestors or descendants) cover.
        std::vector<IntRect> CalculateOcclusions() const;
        // The fraction of it inside the other rect (1 when either contains the other).
        float FractionVisibleIn(const RelativeLayoutBounds& viewport) const;
        float FractionVisibleInRect(int left, int top, int right, int bottom) const;
        float FractionVisibleInWindow() const { return FractionVisibleInRect(0, 0, m_Window.width, m_Window.height); }
        float FractionVisibleInWindowWithInsets(IntOffset topLeftInset, IntOffset bottomRightInset) const {
            return FractionVisibleInRect(topLeftInset.x, topLeftInset.y, m_Window.width - bottomRightInset.x, m_Window.height - bottomRightInset.y);
        }
        bool operator==(const RelativeLayoutBounds& o) const { return m_Bounds == o.m_Bounds && m_Window == o.m_Window && m_Screen == o.m_Screen; }

    private:
        IntRect m_Bounds;
        IntSize m_Window;
        IntOffset m_Screen;
        const LayoutNode* m_Node;
    };

    // DelegatableNode.RegistrationHandle.
    class RegistrationHandle {
    public:
        RegistrationHandle() = default;
        explicit RegistrationHandle(std::function<void()> unregister) : m_Unregister(std::move(unregister)) {}
        void Unregister() { if (auto f = std::move(m_Unregister)) { m_Unregister = {}; f(); } }
        explicit operator bool() const { return bool(m_Unregister); }
    private:
        std::function<void()> m_Unregister;
    };

    class Owner;
    // The window's registrations (RectManager + ThrottledCallbacks); the owner runs Dispatch
    // after each layout.
    class RectRegistry {
    public:
        struct Entry;
        RegistrationHandle Register(const ModifierNode& node, bool global, std::int64_t throttle, std::int64_t debounce,
                                    std::function<void(const RelativeLayoutBounds&)> callback);
        void Dispatch(Owner& owner, bool laidOut);
        void SetScreenOffset(IntOffset o) { m_Screen = o; }
    private:
        void Fire(Owner& owner, Entry& e, std::int64_t now);
        void Schedule(Owner& owner, std::int64_t deadline);
        void TriggerDebounced(Owner& owner);
        std::vector<std::shared_ptr<Entry>> m_Entries;
        IntOffset m_Screen;
        std::int64_t m_Deadline = -1;
        rt::Job m_Timer;
        std::shared_ptr<char> m_Alive = std::make_shared<char>(0);
    };

    // registerOnLayoutRectChanged / registerOnGlobalLayoutListener (on an attached node). The
    // callback runs at most once per throttle period and, while changes keep coming, once
    // they have stopped for the debounce period (0: the throttle period).
    RegistrationHandle RegisterOnLayoutRectChanged(const ModifierNode& node, std::int64_t throttleMillis, std::int64_t debounceMillis,
                                                   std::function<void(const RelativeLayoutBounds&)> callback);
    RegistrationHandle RegisterOnGlobalLayoutListener(const ModifierNode& node, std::int64_t throttleMillis, std::int64_t debounceMillis,
                                                      std::function<void(const RelativeLayoutBounds&)> callback);

}
