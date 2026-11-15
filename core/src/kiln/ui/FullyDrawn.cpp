#include "kiln/ui/FullyDrawn.h"

#include "kiln/runtime/State.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kiln::ui {

    FullyDrawnReporter::FullyDrawnReporter(kiln::Function<void(kiln::Function<void()>)> executor, kiln::Function<void()> reportFullyDrawn)
        : m_Executor(std::move(executor)), m_Report(std::move(reportFullyDrawn)) {}

    bool FullyDrawnReporter::IsFullyDrawnReported() const {
        std::lock_guard lock(m_Mutex);
        return m_Reported;
    }

    void FullyDrawnReporter::AddReporter() {
        std::lock_guard lock(m_Mutex);
        if (!m_Reported) ++m_Reporters;
    }

    void FullyDrawnReporter::RemoveReporter() {
        bool post = false;
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Reported && m_Reporters > 0) {
                --m_Reporters;
                if (!m_ReportPosted && m_Reporters == 0) post = m_ReportPosted = true;
            }
        }
        if (post) PostWhenReportersAreDone();
    }

    void FullyDrawnReporter::PostWhenReportersAreDone() {
        // reportRunnable: still nothing holding it back, and not reported meanwhile.
        m_Executor([this] {
            bool report;
            {
                std::lock_guard lock(m_Mutex);
                m_ReportPosted = false;
                report = m_Reporters == 0 && !m_Reported;
            }
            if (!report) return;
            if (m_Report) m_Report();
            FullyDrawnReported();
        });
    }

    int FullyDrawnReporter::AddOnReportDrawnListener(kiln::Function<void()> callback) {
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Reported) {
                const int id = m_NextListener++;
                m_Listeners.emplace(id, std::move(callback));
                return id;
            }
        }
        callback();
        return 0;
    }

    void FullyDrawnReporter::RemoveOnReportDrawnListener(int id) {
        std::lock_guard lock(m_Mutex);
        m_Listeners.erase(id);
    }

    void FullyDrawnReporter::FullyDrawnReported() {
        std::map<int, kiln::Function<void()>> listeners;
        {
            std::lock_guard lock(m_Mutex);
            if (m_Reported) return;
            m_Reported = true;
            listeners.swap(m_Listeners);
        }
        for (auto& [id, l] : listeners) l();
    }

    namespace {
        std::mutex g_HostMutex;
        kiln::Function<void()>& Host() {
            static auto* h = new kiln::Function<void()>;
            return *h;
        }
    }
    void SetReportFullyDrawnHost(kiln::Function<void()> report) {
        std::lock_guard lock(g_HostMutex);
        Host() = std::move(report);
    }
    void ReportFullyDrawnToHost() {
        kiln::Function<void()> report;
        {
            std::lock_guard lock(g_HostMutex);
            report = Host();
        }
        if (report) report();
    }

    rt::Task<> ReportWhenComplete(FullyDrawnReporter& reporter, kiln::Function<rt::Task<>()> block) {
        reporter.AddReporter();
        if (reporter.IsFullyDrawnReported()) co_return;
        // The hold ends however the block does: returning, throwing, or cancelled.
        struct Release {
            FullyDrawnReporter& r;
            ~Release() { r.RemoveReporter(); }
        } release{ reporter };
        co_await block();
    }

    namespace {
        // ReportDrawnComposition: a reporter held while the predicate is false, the predicate
        // read again when state it read changes.
        class ReportDrawnComposition final : public rt::Subscriber, public std::enable_shared_from_this<ReportDrawnComposition> {
        public:
            ReportDrawnComposition(FullyDrawnReporter& reporter, std::shared_ptr<kiln::Function<bool()>> predicate)
                : m_Reporter(reporter), m_Predicate(std::move(predicate)) {}

            void Start() {
                std::weak_ptr<ReportDrawnComposition> weak = weak_from_this();
                m_Listener = m_Reporter.AddOnReportDrawnListener([weak] { if (auto s = weak.lock()) s->Stop(); });
                if (!m_Reporter.IsFullyDrawnReported()) {
                    m_Reporter.AddReporter();
                    m_Holding = true;
                    Observe();
                }
            }
            // onDispose: the hold goes if it is still there.
            void Dispose() {
                m_Reporter.RemoveOnReportDrawnListener(m_Listener);
                RemoveReporter();
            }
            void OnStateChanged() override {
                std::weak_ptr<ReportDrawnComposition> weak = weak_from_this();
                rt::Dispatcher::Main().Post([weak] { if (auto s = weak.lock(); s && s->m_Holding) s->Observe(); });
            }

        private:
            void Observe() {
                bool passed = false;
                rt::Observation::Track(*this, [&] { passed = (*m_Predicate)(); });
                if (passed) RemoveReporter();
            }
            void RemoveReporter() {
                Unsubscribe();
                if (!m_Holding) return;
                m_Holding = false;
                if (!m_Reporter.IsFullyDrawnReported()) m_Reporter.RemoveReporter();
            }
            void Stop() {
                Unsubscribe();
                m_Holding = false;
            }
            FullyDrawnReporter& m_Reporter;
            std::shared_ptr<kiln::Function<bool()>> m_Predicate;
            int m_Listener = 0;
            bool m_Holding = false;
        };
    }

    void ReportDrawnWhen(kiln::Function<bool()> predicate) {
        auto& c = *rt::Composition::Current();
        FullyDrawnReporter* reporter = c.Read(LocalFullyDrawnReporter);
        if (!reporter) return;
        // The latest predicate (a lambda is new each composition; the effect keys on the reporter).
        auto& latest = c.Remember<std::shared_ptr<kiln::Function<bool()>>>([] { return std::make_shared<kiln::Function<bool()>>(); });
        *latest = std::move(predicate);
        auto shared = latest;
        c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(reporter)), [reporter, shared]() -> kiln::Function<void()> {
            if (reporter->IsFullyDrawnReported()) return [] {};
            auto composition = std::make_shared<ReportDrawnComposition>(*reporter, shared);
            composition->Start();
            return [composition] { composition->Dispose(); };
        });
    }

    void ReportDrawn() { ReportDrawnWhen([] { return true; }); }

    void ReportDrawnAfter(kiln::Function<rt::Task<>()> block) {
        auto& c = *rt::Composition::Current();
        FullyDrawnReporter* reporter = c.Read(LocalFullyDrawnReporter);
        if (!reporter) return;
        auto shared = std::make_shared<kiln::Function<rt::Task<>()>>(std::move(block));
        c.LaunchedEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(reporter)), [reporter, shared] { return ReportWhenComplete(*reporter, std::move(*shared)); });
    }

}
