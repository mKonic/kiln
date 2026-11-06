#pragma once

// Coroutine pointer input (spec 05 req. 1, §Tap): Modifier.pointerInput with a handler that
// awaits pointer events pass by pass, and the tap detector / clickable built on it.

#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"

#include <coroutine>
#include <functional>
#include <optional>

namespace kiln::foundation {

    using namespace kiln::ui;

    class PointerInputScope {
    public:
        virtual ~PointerInputScope() = default;

        struct Await {
            PointerInputScope* scope;
            PointerEventPass pass;
            std::int64_t timeoutMillis;   // < 0: none
            bool await_ready() const noexcept { return false; }
            template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                scope->Suspend(h, pass, timeoutMillis, h.promise().job);
            }
            std::optional<PointerEvent> await_resume() { return scope->TakeEvent(); }
        };
        // co_await AwaitPointerEvent() -> std::optional<PointerEvent> (always set without a
        // timeout). The event's changes are live: Consume() affects later passes and nodes.
        Await AwaitPointerEvent(PointerEventPass pass = PointerEventPass::Main) { return { this, pass, -1 }; }
        Await AwaitPointerEventWithTimeout(std::int64_t millis, PointerEventPass pass = PointerEventPass::Main) {
            return { this, pass, millis };
        }

        IntSize Size() const { return m_Size; }
        virtual const ViewConfiguration& Config() const = 0;
        virtual Density GetDensity() const = 0;

    protected:
        virtual void Suspend(std::coroutine_handle<> h, PointerEventPass pass, std::int64_t timeout,
                             const std::shared_ptr<rt::JobState>& job) = 0;
        virtual std::optional<PointerEvent> TakeEvent() = 0;
        IntSize m_Size;
    };

    using PointerInputHandler = std::function<rt::Task<>(PointerInputScope&)>;

    // The handler starts on the first event and restarts when cancelled or when keys change.
    Modifier PointerInput(Modifier m, rt::Keys keys, PointerInputHandler handler);

    // --- detectors (TapGestureDetector.kt) ---------------------------------------------------

    struct TapCallbacks {
        std::function<void(Offset)> onPress;            // down
        std::function<void(bool released)> onPressEnd;  // up (true) or cancel (false)
        std::function<void(Offset)> onTap;
        std::function<void(Offset)> onDoubleTap;
        std::function<void(Offset)> onLongPress;
    };
    rt::Task<> DetectTapGestures(PointerInputScope& scope, TapCallbacks callbacks);

    // Clickable: tap + pressed state. Semantics, keys, focus and indication come with M3/M10.
    Modifier Clickable(Modifier m, std::function<void()> onClick, std::function<void(bool)> onPressedChange = {},
                       bool enabled = true);

}
