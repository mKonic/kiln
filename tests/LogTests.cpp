// kiln::log: formats only kept messages and hands them to the sink.
#include "Check.h"

#include "kiln/Log.h"

#include <string>
#include <vector>

using namespace kiln;

namespace {
    std::vector<std::string> g_Lines;
    int g_Formats = 0;
    struct Counted { int v; };
}
template<> struct std::formatter<Counted> : std::formatter<int> {
    auto format(const Counted& c, std::format_context& ctx) const { ++g_Formats; return std::formatter<int>::format(c.v, ctx); }
};

TEST(LogFormatsOnlyWhatIsKept) {
    log::SetSink([](log::Level l, std::string_view tag, std::string_view m) { g_Lines.push_back(std::to_string(int(l)) + " " + std::string(tag) + ": " + std::string(m)); });
    log::SetLoggableCheck([](log::Level l, std::string_view tag) { return l >= log::Level::Info || tag == "Verbose"; });
    log::Info("Notes", "loaded {} notes", 3);
    log::Debug("Notes", "skipped {}", Counted{ 1 });
    log::Debug("Verbose", "kept {}", Counted{ 2 });
    log::Error("Notes", "failed: {}", std::string("disk full"));
    CHECK(g_Lines == (std::vector<std::string>{ "4 Notes: loaded 3 notes", "3 Verbose: kept 2", "6 Notes: failed: disk full" }));
    CHECK(g_Formats == 1);   // the skipped message was never formatted
    log::SetSink(nullptr);
    log::SetLoggableCheck(nullptr);
}
