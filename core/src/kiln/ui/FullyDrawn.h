#pragma once

// Reporting when the app is fully drawn (activity's FullyDrawnReporter, activity-compose's
// ReportDrawn): Activity.reportFullyDrawn tells the system the first screen shows real content,
// not just its first frame. Startup metrics (Play vitals, Macrobenchmark's
// timeToFullDisplay, baseline profiles) measure up to it.
//
//   void Feed(FeedModel& model) {
//       ui::ReportDrawnWhen([&] { return !model.items.Get().empty(); });   // drawn once loaded
//       ...
//   }
//
// Each ReportDrawn* adds a reporter; once all are done the host reports on the next drawn
// frame, once per activity.

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <map>
#include <mutex>

namespace kiln::ui {

    class FullyDrawnReporter {
    public:
        // executor: runs its argument after the next drawn frame; report: Activity.reportFullyDrawn.
        FullyDrawnReporter(kiln::Function<void(kiln::Function<void()>)> executor, kiln::Function<void()> reportFullyDrawn);

        bool IsFullyDrawnReported() const;
        // Something is still loading: the report waits for it.
        void AddReporter();
        // It finished; with none left the report goes out after the next frame.
        void RemoveReporter();
        // Called once reported (at once if it was already). Returns an id for removal.
        int AddOnReportDrawnListener(kiln::Function<void()> callback);
        void RemoveOnReportDrawnListener(int id);
        // The host: the report went out (also from a reportFullyDrawn the app made itself).
        void FullyDrawnReported();

    private:
        void PostWhenReportersAreDone();
        mutable std::mutex m_Mutex;
        int m_Reporters = 0;
        bool m_ReportPosted = false;
        bool m_Reported = false;
        std::map<int, kiln::Function<void()>> m_Listeners;
        int m_NextListener = 1;
        kiln::Function<void(kiln::Function<void()>)> m_Executor;
        kiln::Function<void()> m_Report;
    };

    // The platform's Activity.reportFullyDrawn (Android installs it); App's reporter calls it.
    void SetReportFullyDrawnHost(kiln::Function<void()> report);
    void ReportFullyDrawnToHost();

    // reportWhenComplete: a reporter for the duration of block.
    rt::Task<> ReportWhenComplete(FullyDrawnReporter& reporter, kiln::Function<rt::Task<>()> block);

    // LocalFullyDrawnReporterOwner: the host's reporter (null where nothing reports).
    inline rt::Local<FullyDrawnReporter*> LocalFullyDrawnReporter{ nullptr };

    // Reported once predicate is true; it is read again whenever state it read changes.
    void ReportDrawnWhen(kiln::Function<bool()> predicate);
    // Reported once this composes (nothing else holds the report back).
    void ReportDrawn();
    // Reported once block finishes; leaving the composition before then gives up its hold.
    void ReportDrawnAfter(kiln::Function<rt::Task<>()> block);

}
