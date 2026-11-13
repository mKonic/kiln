#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Back.h"
#include "kiln/ui/LayoutNode.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        BackDispatcher& back = BackDispatcher::Main();
        std::vector<bool> reported;
        Harness() { back.onHasEnabledChanged = [this](bool b) { reported.push_back(b); }; }
        ~Harness() { composition.SetContent([] {}); composition.Recompose(); back.onHasEnabledChanged = nullptr; }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); composition.Recompose(); }
        void Recompose() { composition.Recompose(); rt::Dispatcher::Main().RunPending(); }
    };
}

TEST(LastComposedEnabledBackHandlerWins) {
    Harness h;
    rt::State<bool> innerEnabled(true);
    std::string got;
    h.Run([&] {
        BackHandler(true, [&] { got += "outer "; });
        BackHandler(innerEnabled.Get(), [&] { got += "inner "; });
    });
    CHECK_EQ(int(h.reported.size()), 1);
    CHECK(h.reported.back());
    CHECK(h.back.Completed());
    innerEnabled.Set(false);
    h.Recompose();
    CHECK(h.back.Completed());
    CHECK_EQ(got, std::string("inner outer "));
}

TEST(PlatformCallbackOnlyWhileEnabled) {
    Harness h;
    rt::State<bool> enabled(false);
    h.Run([&] { BackHandler(enabled.Get(), [] {}); });
    CHECK(h.reported.empty());              // nothing enabled: the system handles back
    CHECK(!h.back.Completed());
    enabled.Set(true);
    h.Recompose();
    CHECK_EQ(int(h.reported.size()), 1);
    enabled.Set(false);
    h.Recompose();
    CHECK_EQ(int(h.reported.size()), 2);
    CHECK(!h.reported.back());
}

TEST(PredictiveBackStreamsProgressAndCompletes) {
    Harness h;
    std::vector<float> seen;
    int committed = 0, sessions = 0;
    h.Run([&] {
        PredictiveBackHandler(true, [&](BackProgress& p) -> rt::Task<> {
            ++sessions;
            while (auto e = co_await p.Next()) seen.push_back(e->progress);
            ++committed;
        });
    });
    BackEvent e;
    h.back.Started(e);
    e.progress = 0.25f; h.back.Progressed(e);
    e.progress = 0.5f; h.back.Progressed(e);
    CHECK_EQ(committed, 0);
    h.back.Completed();
    CHECK_EQ(committed, 1);
    CHECK_EQ(int(seen.size()), 2);
    CHECK_EQ(seen[1], 0.5f);
    // Key/button back: a session with no events that completes at once.
    h.back.Completed();
    CHECK_EQ(committed, 2);
    CHECK_EQ(sessions, 2);
}

TEST(PredictiveBackCancelStopsTheSession) {
    Harness h;
    int committed = 0, progressed = 0;
    h.Run([&] {
        PredictiveBackHandler(true, [&](BackProgress& p) -> rt::Task<> {
            while (co_await p.Next()) ++progressed;
            ++committed;
        });
    });
    BackEvent e;
    h.back.Started(e);
    e.progress = 0.3f; h.back.Progressed(e);
    h.back.Cancelled();
    e.progress = 0.4f; h.back.Progressed(e);   // after cancel: nobody listens
    CHECK_EQ(progressed, 1);
    CHECK_EQ(committed, 0);
    h.back.Started(e);                          // a new gesture gets a fresh session
    h.back.Completed();
    CHECK_EQ(committed, 1);
}
