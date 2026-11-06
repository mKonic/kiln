#pragma once

// Back navigation (spec 15 req. 2; OnBackPressedDispatcher + BackEventCompat). Handlers keep
// their registration position; the most recently added enabled one handles a gesture from
// start to completion or cancellation. The host registers its platform callback only while some
// handler is enabled, so the system back-to-home animation runs otherwise; the enabled state is
// pushed to the host synchronously (avoids the one-frame lag noted in PredictiveBackHandler.kt).

#include <cstdint>
#include <functional>
#include <vector>

namespace kiln::ui {

    enum class SwipeEdge : std::uint8_t { Left = 0, Right = 1, None = 2 };

    struct BackEvent {
        float touchX = 0, touchY = 0;   // in the receiving view's pixels
        float progress = 0;             // 0..1
        SwipeEdge swipeEdge = SwipeEdge::None;
        std::int64_t frameTimeMillis = 0;
    };

    class BackCallback {
    public:
        virtual ~BackCallback() = default;
        virtual void OnBackStarted(const BackEvent&) {}
        virtual void OnBackProgressed(const BackEvent&) {}
        virtual void OnBackCancelled() {}
        virtual void OnBack() = 0;   // completed (gesture released, back key or button)
        bool enabled = true;
    };

    class BackDispatcher {
    public:
        static BackDispatcher& Main();   // the thread's dispatcher (one window)

        void Add(BackCallback* cb);
        void Remove(BackCallback* cb);
        // Call after changing a callback's `enabled`.
        void EnabledChanged();
        bool HasEnabledCallbacks() const;

        // Host events. Without a started gesture, Completed picks the top handler itself
        // (button / key back). Returns whether a handler took it.
        bool Started(const BackEvent& e);
        void Progressed(const BackEvent& e);
        void Cancelled();
        bool Completed();

        // Host hook: register/unregister the platform callback.
        std::function<void(bool hasEnabled)> onHasEnabledChanged;

    private:
        BackCallback* Top() const;
        std::vector<BackCallback*> m_Callbacks;
        BackCallback* m_InProgress = nullptr;
        bool m_LastReported = false;
    };

}
