// kiln::trace: sections reach the installed tracer only while it records, and kiln's own
// recomposition is marked with Compose's names.
#include "Check.h"

#include "kiln/Trace.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/LayoutNode.h"

#include <string>
#include <vector>

using namespace kiln;

namespace {
    std::vector<std::string> g_Events;
    bool g_Recording = false;
}

TEST(TraceSectionsReachTheTracerWhileItRecords) {
    trace::SetTracer({
        .enabled = [] { return g_Recording; },
        .begin = [](const char* n) { g_Events.push_back(std::string("B ") + n); },
        .end = [] { g_Events.push_back("E"); },
        .counter = [](const char* n, std::int64_t v) { g_Events.push_back(std::string("C ") + n + "=" + std::to_string(v)); },
    });
    g_Events.clear();
    { trace::Section s("off"); }
    CHECK(g_Events.empty());
    g_Recording = true;
    {
        trace::Section outer("outer");
        { trace::Section inner("inner"); }
        trace::Counter("items", 3);
    }
    trace::AsyncBegin("async", 1);   // no async calls installed: nothing
    CHECK(g_Events == (std::vector<std::string>{ "B outer", "B inner", "E", "C items=3", "E" }));

    // A recomposition is a Compose:recompose section.
    g_Events.clear();
    ui::Owner owner;
    rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
    rt::State<int> n(0);
    composition.SetContent([&] { (void)n.Get(); });
    composition.Recompose();
    g_Events.clear();
    n.Set(1);
    rt::Observation::ApplyChanges();
    rt::Composition::RecomposeAll();
    bool recomposed = false;
    for (const auto& e : g_Events) recomposed = recomposed || e == "B Compose:recompose";
    CHECK(recomposed);
    g_Recording = false;
    trace::SetTracer({});
}
