// Reporting fully drawn (activity's FullyDrawnReporterTest, activity-compose's ReportDrawnTest):
// the report waits for every reporter, goes out once, after the next frame.
#include "Check.h"

#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/FullyDrawn.h"

#include <memory>
#include <vector>

using namespace kiln;

namespace {
    struct Root : rt::TreeNode {
        void SetChildren(std::vector<rt::TreeNode*>) override {}
    };
    // An activity: the executor runs at the end of the next "frame" (Idle).
    struct Activity {
        std::vector<kiln::Function<void()>> afterFrame;
        int reported = 0;
        ui::FullyDrawnReporter reporter{ [this](kiln::Function<void()> f) { afterFrame.push_back(std::move(f)); }, [this] { ++reported; } };
        void Idle(rt::Composition& c) {
            for (int i = 0; i < 4; ++i) {
                c.Recompose();
                rt::Observation::ApplyChanges();
                rt::Dispatcher::Main().RunPending();
                auto run = std::move(afterFrame);
                afterFrame.clear();
                for (auto& f : run) f();
            }
        }
    };
    void Content(rt::Composition& c, Activity& a, kiln::Function<void()> body) {
        c.SetContent([&c, &a, body = std::move(body)] { c.Provide(ui::LocalFullyDrawnReporter, &a.reporter, body); });
    }
}

TEST(FullyDrawnReporterWaitsForEveryReporter) {
    Activity a;
    int listened = 0;
    a.reporter.AddReporter();
    a.reporter.AddReporter();
    a.reporter.AddOnReportDrawnListener([&] { ++listened; });
    a.reporter.RemoveReporter();
    CHECK(a.afterFrame.empty());
    a.reporter.RemoveReporter();
    // Posted, not yet run: a reporter added before the frame holds it back again.
    CHECK(a.afterFrame.size() == 1 && !a.reporter.IsFullyDrawnReported());
    a.reporter.AddReporter();
    for (auto& f : a.afterFrame) f();
    a.afterFrame.clear();
    CHECK(a.reported == 0 && listened == 0);
    a.reporter.RemoveReporter();
    for (auto& f : a.afterFrame) f();
    CHECK(a.reported == 1 && listened == 1 && a.reporter.IsFullyDrawnReported());
    // Once reported: reporters are ignored, a late listener is called at once.
    a.reporter.AddReporter();
    a.reporter.RemoveReporter();
    a.reporter.AddOnReportDrawnListener([&] { ++listened; });
    CHECK(a.reported == 1 && listened == 2);
}

TEST(ReportDrawnWhenReportsOnceThePredicateHolds) {
    Root root;
    rt::Composition c(root);
    Activity a;
    rt::State<bool> ready(false);
    int checks = 0;
    Content(c, a, [&] { ui::ReportDrawnWhen([&] { ++checks; return ready.Get(); }); });
    a.Idle(c);
    CHECK(a.reported == 0 && checks == 1);
    ready.Set(true);
    a.Idle(c);
    CHECK(a.reported == 1 && checks == 2);
    // Not repeated, and no longer read.
    ready.Set(false);
    a.Idle(c);
    ready.Set(true);
    a.Idle(c);
    CHECK(a.reported == 1 && checks == 2);
}

TEST(ReportDrawnAfterReportsWhenTheBlockFinishes) {
    Root root;
    rt::Composition c(root);
    Activity a;
    auto gate = std::make_shared<rt::Channel<bool>>(1);
    int runs = 0;
    rt::State<int> recompose(0);
    Content(c, a, [&] {
        recompose.Get();
        ui::ReportDrawnAfter([&runs, gate]() -> rt::Task<> { ++runs; co_await gate->Next(); });
    });
    a.Idle(c);
    CHECK(a.reported == 0 && runs == 1);
    gate->TrySend(true);
    a.Idle(c);
    CHECK(a.reported == 1 && runs == 1);
    recompose.Set(1);
    a.Idle(c);
    CHECK(a.reported == 1 && runs == 1);
}

TEST(ReportDrawnWaitsForEveryConditionAndDroppedOnes) {
    Root root;
    rt::Composition c(root);
    Activity a;
    rt::State<bool> one(false), two(false), useTwo(true);
    auto gate = std::make_shared<rt::Channel<bool>>(1);
    Content(c, a, [&] {
        ui::ReportDrawnWhen([&] { return one.Get(); });
        if (useTwo.Get()) ui::ReportDrawnWhen([&] { return two.Get(); });
        ui::ReportDrawnAfter([gate]() -> rt::Task<> { co_await gate->Next(); });
    });
    a.Idle(c);
    one.Set(true);
    a.Idle(c);
    CHECK(a.reported == 0);
    // removedCondition: a condition leaving the composition stops holding the report.
    useTwo.Set(false);
    a.Idle(c);
    CHECK(a.reported == 0);   // the block still runs (waitUntilTwoAreDrawn)
    gate->TrySend(true);
    a.Idle(c);
    CHECK(a.reported == 1);
}

TEST(ReportDrawnDisposedBeforeReportingAndWithoutAReporter) {
    {
        Root root;
        Activity a;
        {
            rt::Composition c(root);
            Content(c, a, [&] {
                ui::ReportDrawnWhen([] { return false; });
                ui::ReportDrawn();
            });
            a.Idle(c);
        }   // testDisposingBeforeReporting: both reporters let go
        CHECK(a.reported == 0);
    }
    // No reporter provided (a host that does not report): nothing happens.
    Root root;
    rt::Composition c(root);
    c.SetContent([&] { ui::ReportDrawn(); });
    c.Recompose();
}
