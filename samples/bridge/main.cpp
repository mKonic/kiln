// kiln bridge: C++ and Kotlin calling each other through the JVM bridge. src/main/kotlin/.../Bridge.kt
// declares what crosses; tools/gen-jvm writes kiln_bridge.h from it. C++ implements NoteStore
// (which Kotlin calls, App.kt and SelfTest.kt) and calls Platform (which Kotlin implements).

#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Text.h>
#include <kiln/runtime/Composition.h>
#include <kiln/runtime/TaskDispatchers.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "kiln_bridge.h"

using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::ui;
namespace bridge = dev::kiln::bridgesample;
namespace rt = kiln::rt;

namespace {

    // The notes, shared by NoteStore (called from Kotlin on any thread) and the UI.
    struct Notes {
        std::mutex mutex;
        std::vector<bridge::Note> notes;
        std::int64_t nextId = 1;
        std::atomic<int> cancelledSummaries{ 0 };
    };
    Notes& Shared() {
        static Notes n;
        return n;
    }

    class Store : public bridge::NoteStore {
    public:
        std::int64_t Add(std::string title, std::vector<std::string> tags, std::optional<std::string> body) override {
            auto& s = Shared();
            std::lock_guard lock(s.mutex);
            bridge::Note n{ .id = s.nextId++, .title = std::move(title), .tags = std::move(tags), .pinned = false, .body = std::move(body) };
            s.notes.push_back(n);
            return n.id;
        }
        std::optional<bridge::Note> Find(std::int64_t id) override {
            auto& s = Shared();
            std::lock_guard lock(s.mutex);
            for (const auto& n : s.notes) if (n.id == id) return n;
            return std::nullopt;
        }
        std::vector<bridge::Note> All() override {
            auto& s = Shared();
            std::lock_guard lock(s.mutex);
            return s.notes;
        }
        void Remove(std::int64_t id) override {
            auto& s = Shared();
            std::lock_guard lock(s.mutex);
            auto it = std::find_if(s.notes.begin(), s.notes.end(), [&](const bridge::Note& n) { return n.id == id; });
            if (it == s.notes.end()) throw std::out_of_range("no note " + std::to_string(id));
            s.notes.erase(it);
        }
        void Clear() override {
            auto& s = Shared();
            std::lock_guard lock(s.mutex);
            s.notes.clear();
        }
        rt::Task<bridge::Summary> Summarize(std::int64_t delayMs) override {
            // Counts a summary destroyed while it waits (the Kotlin caller cancelled).
            struct Guard {
                bool done = false;
                ~Guard() { if (!done) ++Shared().cancelledSummaries; }
            } guard;
            co_await rt::Delay(delayMs);
            guard.done = true;
            bridge::Summary out;
            std::map<std::string, int> tags;
            for (const auto& n : All()) {
                ++out.count;
                if (!out.longest || n.title.size() > out.longest->title.size()) out.longest = n;
                for (const auto& t : n.tags) ++tags[t];
            }
            out.mood = out.count < 5 ? bridge::Mood::Calm : bridge::Mood::Busy;
            for (const auto& [tag, count] : tags) out.tags.push_back({ tag, count });
            co_return out;
        }
        std::int32_t CancelledSummaries() override { return Shared().cancelledSummaries; }
        void RelayFailure(std::string message) override {
            bridge::Platform::Get().value().Fail(message);   // throws the Kotlin exception on
        }
        rt::Task<std::vector<std::string>> CheckPlatform() override;
    };

    rt::Task<> FetchForever(bridge::Platform p) { co_await p.Fetch("never", 60000); }

    // C++ calling Kotlin: 8 checks (SelfTest counts them), the failed ones returned.
    rt::Task<std::vector<std::string>> Store::CheckPlatform() {
        std::vector<std::string> failed;
        auto check = [&](const char* name, bool ok) { if (!ok) failed.push_back(name); };
        auto platform = bridge::Platform::Get();
        check("Platform is registered", platform.has_value());
        if (!platform) co_return failed;
        const bridge::Platform p = *platform;
        const std::string device = p.DeviceName();
        check("DeviceName", !device.empty());
        check("Scale", p.Scale({ 1.5, -2 }, 2) == std::vector<double>{ 3, -4 });
        bool threw = false;
        try {
            p.Fail("boom");
        } catch (const kiln::jvm::JavaException& e) {
            threw = e.className == "java.lang.IllegalStateException" && std::string(e.what()).find("boom") != std::string::npos;
        }
        check("Fail throws JavaException", threw);
        std::vector<std::uint8_t> bytes = co_await p.Fetch("héllo", 10);
        check("Fetch", std::string(bytes.begin(), bytes.end()) == "héllo");
        // Cancelling the waiting task cancels the Kotlin coroutine.
        int before = p.CancelledFetches();
        {
            rt::Job job = rt::Launch(rt::Dispatcher::Main(), FetchForever(p));
            co_await rt::Delay(50);
        }
        co_await rt::Delay(200);
        check("cancelled Fetch cancels the Kotlin coroutine", p.CancelledFetches() == before + 1);
        bool pinged = true;
        try {
            co_await p.Ping();
        } catch (...) {
            pinged = false;
        }
        check("Ping", pinged);
        // From a thread C++ started (attached for the call).
        std::string fromIo = co_await rt::WithContext(rt::TaskDispatchers::Platform().IO(), [p] { return p.DeviceName(); });
        check("DeviceName from an IO thread", fromIo == device);
        co_return failed;
    }

    void Screen() {
        auto& c = *rt::Composition::Current();
        auto& fetched = RememberState(std::string());
        auto& fetches = RememberState(0);
        // The Kotlin side's answer to a button press, awaited as a task.
        c.LaunchedEffect(rt::Keys{ fetches.Get() }, [&fetched, &fetches]() -> rt::Task<> {
            if (fetches.Peek() == 0) co_return;
            auto p = bridge::Platform::Get();
            if (!p) co_return;
            fetched.Set("Asking Kotlin...");
            std::vector<std::uint8_t> b = co_await p->Fetch("Kotlin answered request " + std::to_string(fetches.Peek()), 400);
            fetched.Set(std::string(b.begin(), b.end()));
        });
        auto platform = bridge::Platform::Get();
        const std::string device = platform ? platform->DeviceName() : std::string("no Platform registered");
        std::vector<bridge::Note> notes = Store().All();
        ScaffoldOptions options;
        options.topBar = [] { TopAppBar([] { Text("kiln bridge"); }, {}); };
        // Scaffold composes its content later (in layout): it holds copies, and the states.
        Scaffold({}, options, [device, notes, &fetched, &fetches](const PaddingValues& padding) {
            Column(Padding(Padding(FillMax(Modifier{}, Axis::Both), padding), 16_dp), Arrangement::SpacedBy(12_dp), AlignmentH::Start(), [&] {
                Text("Device (from Kotlin): " + device);
                Button([&] { fetches.Set(fetches.Peek() + 1); }, {}, {}, [] { Text("Ask Kotlin"); });
                if (!fetched.Get().empty()) Text(fetched.Get());
                Text("Notes in C++, added from Kotlin:", {}, { .style = MaterialTheme::typography().titleMedium });
                auto& list = RememberLazyListState();
                LazyColumn(Weight(Modifier{}, 1), list, [notes](LazyListScope& s) {
                    s.Items(int(notes.size()), [notes](int i) {
                        const auto& n = notes[std::size_t(i)];
                        std::string tags;
                        for (const auto& t : n.tags) tags += (tags.empty() ? "" : ", ") + t;
                        Text(n.title + (tags.empty() ? "" : "  [" + tags + "]"));
                    });
                });
            });
        });
    }

}

std::unique_ptr<bridge::NoteStore> bridge::MakeNoteStore() { return std::make_unique<Store>(); }

extern "C" void kiln_main(kiln_app* app) {
    kiln::SetContent(app, [] { MaterialTheme([] { Screen(); }); });
}
