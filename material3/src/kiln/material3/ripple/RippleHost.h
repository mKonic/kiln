#pragma once

// The platform press ripple as Compose hosts it on Android (material3-ripple androidMain):
// RippleDrawable in its patterned style (frameworks/base RippleDrawable.java,
// RippleAnimationSession.java, android-16.0.0_r2) drawn the way HWUI's AnimatedRippleDrawable
// does, RippleHostView's state timing, and RippleContainer's pool of five hosts per window.

#include "kiln/runtime/Task.h"
#include "kiln/ui/Draw.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::material3::ripple {

    using ui::Color;
    using ui::Offset;
    using ui::Size;

    // RippleDrawable with STYLE_PATTERNED, as UnprojectedRipple uses it: when bounded the mask is
    // an opaque white ColorDrawable (a clip to the bounds, no mask shader); no content layer;
    // only the pressed and enabled states are ever set, so the focus / hover background is unused.
    class RippleDrawable {
    public:
        // One RippleAnimationSession. Times are AnimationUtils.currentAnimationTimeMillis.
        struct Session {
            float touchX = 0, touchY = 0;
            float originX = 0, originY = 0;   // the hotspot bounds' (integer) centre
            float radius = 0;                 // getComputedRadius()
            float width = 0, height = 0;      // shader resolution
            Color color;                      // fixed when the session starts
            std::int64_t start = 0;
            std::optional<std::int64_t> exitStart;   // the exit animator's start, after its delay
            float exitFrom = 0.5f;                   // the progress it starts from
            std::optional<std::int64_t> end;         // the first exit animator's end: onSessionEnd
            float noiseFrom = 0, noiseTo = 0;        // the noise phase animator's ends
        };

        static constexpr int kMaxRipples = 10;
        static constexpr int kEnterDuration = 450, kExitDuration = 375, kNoiseDuration = 7000;

        explicit RippleDrawable(bool bounded) : m_Bounded(bounded) {}

        bool Bounded() const { return m_Bounded; }
        // onStateChange with {state_pressed, state_enabled} (true) or {} (false).
        void SetPressedState(bool pressed);
        void SetHotspot(float x, float y);
        // setBounds: the hotspot bounds follow; running sessions take the new radius,
        // resolution and origin (onHotspotBoundsChanged).
        void SetBounds(int width, int height);
        int Width() const { return m_Width; }
        int Height() const { return m_Height; }
        void SetRadius(int radius) { m_MaxRadius = radius; Invalidate(); }
        int Radius() const { return m_MaxRadius; }
        void SetColor(Color color) { m_Color = color; Invalidate(); }
        // setVisible(false): clearHotspots ends every session at once.
        void Hide() { m_Sessions.clear(); Invalidate(); }

        // drawPatterned at frame time `now` (ms).
        void Draw(ui::DrawScope& s, std::int64_t now);
        // Sessions still running or about to start: the host keeps drawing frames.
        bool Animating() const { return m_AddRipple || m_ExitingAnimation || !m_Sessions.empty(); }
        const std::vector<Session>& Sessions() const { return m_Sessions; }

        // The session's animated in_progress and noise phase at `now`.
        static float Progress(const Session& s, std::int64_t now);
        static float NoisePhase(const Session& s, std::int64_t now);

        std::function<void()> invalidate;   // Drawable.Callback.invalidateDrawable

    private:
        void Invalidate() { if (invalidate) invalidate(); }
        int ComputedRadius() const;
        bool m_Bounded;
        int m_Width = 0, m_Height = 0, m_MaxRadius = -1;   // -1: RADIUS_AUTO
        Color m_Color{ 0xffff00ffu };   // RippleState.mColor's default, magenta
        bool m_RippleActive = false, m_AddRipple = false, m_ExitingAnimation = false;
        float m_PendingX = 0, m_PendingY = 0;
        bool m_HasPending = false;
        std::vector<Session> m_Sessions;
    };

    // RippleHostView: the View whose background is the ripple.
    class RippleHostView {
    public:
        // Bounded ripples start at the press; unbounded ones at the centre of the bounds.
        void AddRipple(Offset pressPosition, bool bounded, Size size, int radius, Color color, float alpha,
                       std::function<void()> onInvalidateRipple);
        void RemoveRipple() { SetRippleState(false); }
        void SetRippleProperties(Size size, int radius, Color color, float alpha);
        // Drops callbacks and ends any ripple at once (the host goes to another node).
        void DisposeRipple();
        void Draw(ui::DrawScope& s);
        const RippleDrawable* Ripple() const { return m_Ripple.get(); }

        // Minimum time between pressed and resting before the resting state is posted, and the
        // delay it is posted with.
        static constexpr std::int64_t kMinimumRippleStateChangeTime = 5, kResetRippleDelayDuration = 50;

    private:
        void SetRippleState(bool pressed);
        void RunReset();
        std::unique_ptr<RippleDrawable> m_Ripple;
        std::optional<bool> m_Bounded;
        std::optional<std::int64_t> m_LastStateChange;
        Color m_RippleColor = Color::Unspecified();   // UnprojectedRipple's last colour
        std::function<void()> m_OnInvalidateRipple;
        rt::Job m_Reset;   // the posted resting state
        bool m_ResetPending = false;
        rt::Job m_Frames;  // the render thread's animation frames
    };

    // RippleHostKey
    class RippleHostKey {
    public:
        virtual ~RippleHostKey() = default;
        virtual void OnResetRippleHostView() = 0;
    };

    // RippleContainer: up to five hosts per window, assigned to nodes as they ripple. With all
    // five in use the next node takes one round-robin and the ripple it showed ends.
    class RippleContainer {
    public:
        static constexpr int kMaxRippleHosts = 5;
        RippleContainer();
        RippleHostView& GetRippleHostView(RippleHostKey& key);
        void DisposeRippleIfNeeded(RippleHostKey& key);
        int HostCount() const { return int(m_Hosts.size()); }

        // The container of a window (createAndAttachRippleContainerIfNeeded).
        static std::shared_ptr<RippleContainer> For(const void* window);

    private:
        RippleHostKey* KeyOf(const RippleHostView* host) const;
        std::vector<std::unique_ptr<RippleHostView>> m_Hosts;
        std::vector<RippleHostView*> m_Unused;
        std::vector<std::pair<RippleHostKey*, RippleHostView*>> m_Map;
        int m_NextHostIndex = 0;
    };

    // AnimationUtils.currentAnimationTimeMillis outside a frame (input events): uptime.
    std::int64_t UptimeMillis();

}
