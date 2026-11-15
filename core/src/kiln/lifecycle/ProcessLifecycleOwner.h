#pragma once

// ProcessLifecycleOwner (lifecycle-process): the lifecycle of the whole app process, for code
// that cares whether the app is in the foreground at all rather than about one screen.
//   - ON_CREATE once, when the process starts; never ON_DESTROY.
//   - ON_START and ON_RESUME as the first activity (window) starts and resumes.
//   - ON_PAUSE and ON_STOP after the last one pauses and stops, delayed by kTimeoutMs, so an
//     activity recreated for a configuration change does not send them.
//
//   lifecycle::AddObserver(lifecycle::ProcessLifecycleOwner::Get().GetLifecycle(),
//       [](lifecycle::LifecycleOwner&, lifecycle::Event e) { if (e == lifecycle::Event::OnStop) Flush(); });
//
// On Android every activity of the process counts (Application.ActivityLifecycleCallbacks), as
// upstream; on the desktop the app's windows do. Main thread only.

#include "kiln/lifecycle/Lifecycle.h"
#include "kiln/runtime/Task.h"

#include <memory>

namespace kiln::lifecycle {

    class ProcessLifecycleOwner final : public LifecycleOwner {
    public:
        static constexpr int kTimeoutMs = 700;

        static ProcessLifecycleOwner& Get();
        Lifecycle& GetLifecycle() override;

        // From the platform host: activities (windows) starting, resuming, pausing, stopping.
        void ActivityStarted();
        void ActivityResumed();
        void ActivityPaused();
        void ActivityStopped();
        // The platform reports activities itself (Android); windows then do not (App).
        void SetPlatformDriven(bool driven) { m_PlatformDriven = driven; }
        bool PlatformDriven() const { return m_PlatformDriven; }

        ProcessLifecycleOwner();
        ~ProcessLifecycleOwner() override;

    private:
        void DispatchPauseIfNeeded();
        void DispatchStopIfNeeded();
        int m_Started = 0, m_Resumed = 0;
        bool m_PauseSent = true, m_StopSent = true;
        bool m_PlatformDriven = false;
        std::unique_ptr<LifecycleRegistry> m_Registry;
        rt::Job m_DelayedPause;
    };

}
