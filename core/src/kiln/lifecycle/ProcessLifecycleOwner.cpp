#include "kiln/lifecycle/ProcessLifecycleOwner.h"

namespace kiln::lifecycle {

    ProcessLifecycleOwner& ProcessLifecycleOwner::Get() {
        static auto* owner = new ProcessLifecycleOwner;   // the process's; never destroyed
        return *owner;
    }

    ProcessLifecycleOwner::ProcessLifecycleOwner() : m_Registry(std::make_unique<LifecycleRegistry>(*this)) {
        // attach: created with the process.
        m_Registry->HandleLifecycleEvent(Event::OnCreate);
    }

    ProcessLifecycleOwner::~ProcessLifecycleOwner() = default;

    Lifecycle& ProcessLifecycleOwner::GetLifecycle() { return *m_Registry; }

    void ProcessLifecycleOwner::ActivityStarted() {
        ++m_Started;
        if (m_Started == 1 && m_StopSent) {
            m_Registry->HandleLifecycleEvent(Event::OnStart);
            m_StopSent = false;
        }
    }

    void ProcessLifecycleOwner::ActivityResumed() {
        ++m_Resumed;
        if (m_Resumed == 1) {
            if (m_PauseSent) {
                m_Registry->HandleLifecycleEvent(Event::OnResume);
                m_PauseSent = false;
            } else {
                m_DelayedPause = {};   // removeCallbacks(delayedPauseRunnable)
            }
        }
    }

    void ProcessLifecycleOwner::ActivityPaused() {
        --m_Resumed;
        if (m_Resumed == 0) {
            m_DelayedPause = rt::Launch(rt::Dispatcher::Main(), [](ProcessLifecycleOwner* self) -> rt::Task<> {
                co_await rt::Delay(kTimeoutMs);
                self->DispatchPauseIfNeeded();
                self->DispatchStopIfNeeded();
            }(this));
        }
    }

    void ProcessLifecycleOwner::ActivityStopped() {
        --m_Started;
        DispatchStopIfNeeded();
    }

    void ProcessLifecycleOwner::DispatchPauseIfNeeded() {
        if (m_Resumed == 0) {
            m_PauseSent = true;
            m_Registry->HandleLifecycleEvent(Event::OnPause);
        }
    }

    void ProcessLifecycleOwner::DispatchStopIfNeeded() {
        if (m_Started == 0 && m_PauseSent) {
            m_Registry->HandleLifecycleEvent(Event::OnStop);
            m_StopSent = true;
        }
    }

}
