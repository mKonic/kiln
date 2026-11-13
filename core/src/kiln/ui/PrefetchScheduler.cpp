#include "kiln/ui/PrefetchScheduler.h"

namespace kiln::ui {

    PrefetchScheduler& PrefetchScheduler::Main() {
        thread_local PrefetchScheduler scheduler;
        return scheduler;
    }

    bool PrefetchScheduler::Run(Clock::time_point deadline) {
        PrefetchRequestScope scope(deadline);
        while (!m_Requests.empty()) {
            if (scope.AvailableTimeNanos() <= 0) return true;
            // Leaves the queue once done; one with work left stays first.
            std::shared_ptr<PrefetchRequest> request = m_Requests.front();
            if (request->Execute(scope)) return true;
            if (!m_Requests.empty() && m_Requests.front() == request) m_Requests.pop_front();
        }
        return false;
    }

}
