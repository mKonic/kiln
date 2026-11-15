// DataStore Preferences (datastore-preferences-core's PreferencesTest, SingleProcessDataStoreTest
// cases): typed keys, edits written atomically and read back, the data flow, corruption.
#include "Check.h"

#include "kiln/datastore/Preferences.h"
#include "kiln/ui/AppDirectories.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

using namespace kiln;
using namespace kiln::datastore;

namespace {
    // Runs the main dispatcher until `done` (IO results come back through it).
    void RunUntil(const rt::Job& job) {
        for (int i = 0; i < 2000 && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::string TempDir() {
        return test::MakeTempDir("kiln-prefs-");
    }
    const auto kName = StringKey("name");
    const auto kCount = IntKey("count");
    const auto kDark = BoolKey("dark");
    const auto kTags = StringSetKey("tags");
    const auto kRatio = DoubleKey("ratio");
}

TEST(PreferencesSerializeEveryType) {
    MutablePreferences p;
    p.Set(kName, std::string("Ada"));
    p.Set(kCount, 3);
    p.Set(kDark, true);
    p.Set(kTags, std::set<std::string>{ "a", "b" });
    p.Set(kRatio, 0.5);
    p.Set(LongKey("big"), std::int64_t(1) << 40);
    p.Set(FloatKey("f"), 1.5f);
    p.Set(ByteArrayKey("bytes"), std::vector<std::uint8_t>{ 0, 255, 7 });
    const auto back = ParsePreferences(SerializePreferences(p));
    CHECK(back && *back == p);
    CHECK(back && back->Get(kName) == std::optional<std::string>("Ada") && back->Get(kTags)->size() == 2);
    CHECK(back && back->Get(ByteArrayKey("bytes")) == std::optional<std::vector<std::uint8_t>>(std::vector<std::uint8_t>{ 0, 255, 7 }));
    CHECK(!p.Get(IntKey("name")).has_value());   // another type under the name
    CHECK(!ParsePreferences("garbage").has_value());
    // putAll: the other's entries replace those of the same name, the rest stay.
    MutablePreferences q;
    q.Set(kName, std::string("Grace"));
    q.Set(IntKey("other"), 7);
    q.PutAll(p);
    CHECK(q.Get(kName) == std::optional<std::string>("Ada") && q.Get(IntKey("other")) == std::optional<int>(7) && q.Size() == p.Size() + 1);
}

TEST(PreferencesDataStoreEditsPersistAndFlow) {
    const std::string dir = TempDir();
    const std::string path = dir + "/datastore/settings.preferences";
    std::vector<std::optional<int>> counts;
    {
        auto store = PreferencesDataStore::OpenFile(path);
        CHECK(PreferencesDataStore::OpenFile(path) == store);   // one store per file
        rt::Job watch = rt::Launch(rt::Dispatcher::Main(), store->Data().Collect([&](const Preferences& p) { counts.push_back(p.Get(kCount)); }));
        rt::Job edits = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<PreferencesDataStore> s) -> rt::Task<> {
            co_await s->Edit([](MutablePreferences& p) { p.Set(kCount, 1); p.Set(kName, std::string("Ada")); });
            co_await s->Edit([](MutablePreferences& p) { p.Set(kCount, p.Get(kCount).value_or(0) + 1); });
            co_await s->Edit([](MutablePreferences&) {});   // unchanged: no write, no emission
        }(store));
        RunUntil(edits);
        rt::Dispatcher::Main().RunPending();
        CHECK(edits.Done());
        CHECK(store->Current().Get(kCount) == std::optional<int>(2));
        CHECK(!counts.empty() && counts.front() == std::nullopt && counts.back() == std::optional<int>(2));
        CHECK(::access(path.c_str(), F_OK) == 0 && ::access((path + ".tmp").c_str(), F_OK) != 0);
    }
    // A new store reads the file.
    auto reopened = PreferencesDataStore::OpenFile(path);
    std::optional<Preferences> read;
    rt::Job first = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<PreferencesDataStore> s, std::optional<Preferences>* out) -> rt::Task<> {
        *out = co_await rt::First(s->Data());
    }(reopened, &read));
    RunUntil(first);
    CHECK(read && read->Get(kCount) == std::optional<int>(2) && read->Get(kName) == std::optional<std::string>("Ada"));
    std::remove(path.c_str());
}

TEST(PreferencesDataStoreStartsOverFromACorruptFile) {
    const std::string dir = TempDir();
    const std::string path = dir + "/broken.preferences";
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fputs("not preferences", f); std::fclose(f); }
    auto store = PreferencesDataStore::OpenFile(path);
    std::optional<Preferences> read;
    rt::Job first = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<PreferencesDataStore> s, std::optional<Preferences>* out) -> rt::Task<> {
        *out = co_await rt::First(s->Data());
    }(store, &read));
    RunUntil(first);
    CHECK(read && read->Size() == 0);
    rt::Job edit = rt::Launch(rt::Dispatcher::Main(), store->Edit([](MutablePreferences& p) { p.Set(kDark, true); }));
    RunUntil(edit);
    CHECK(store->Current().Get(kDark) == std::optional<bool>(true));
    std::remove(path.c_str());
}

// FileStorage's readData: a file that exists but cannot be read fails the read and the edit,
// rather than reading as empty preferences an edit would write over.
TEST(PreferencesDataStoreUnreadableFileFailsWithoutOverwriting) {
    const std::string dir = TempDir();
    const std::string path = dir + "/unreadable.preferences";
    ::mkdir(path.c_str(), 0700);   // there, but not a readable file
    auto store = PreferencesDataStore::OpenFile(path);
    struct Out { bool readFailed = false, editFailed = false; };
    auto out = std::make_shared<Out>();
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<PreferencesDataStore> s, std::shared_ptr<Out> o) -> rt::Task<> {
        try { co_await rt::First(s->Data()); } catch (const std::exception&) { o->readFailed = true; }
        try { co_await s->Edit([](MutablePreferences& p) { p.Set(kDark, true); }); } catch (const std::exception&) { o->editFailed = true; }
    }(store, out));
    RunUntil(job);
    CHECK(job.Done() && out->readFailed && out->editFailed);
    CHECK(!store->Current().Contains(kDark));
    struct stat st{};
    CHECK(::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    ::rmdir(path.c_str());
}

TEST(AppDirectoriesExistAndCanBeSet) {
    const std::string files = ui::FilesDir();
    CHECK(!files.empty() && ::access(files.c_str(), W_OK) == 0);
    CHECK(::access(ui::CacheDir().c_str(), W_OK) == 0);
    const std::string dir = TempDir();
    ui::SetAppDirectories(dir + "/f", dir + "/c", dir + "/n");
    CHECK(ui::FilesDir() == dir + "/f" && ::access((dir + "/f").c_str(), F_OK) == 0);
    ui::SetAppDirectories({}, {}, {});
}

TEST(PreferencesUpdateRunsWithoutAScopeInOrder) {
    const std::string path = TempDir() + "/u.preferences";
    auto store = PreferencesDataStore::OpenFile(path);
    for (int i = 1; i <= 5; ++i) store->Update([i](MutablePreferences& p) { p.Set(kCount, p.Get(kCount).value_or(0) * 10 + i); });
    for (int i = 0; i < 2000 && store->Current().Get(kCount) != std::optional<int>(12345); ++i) {
        rt::Dispatcher::Main().RunPending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(store->Current().Get(kCount) == std::optional<int>(12345));
    const auto onDisk = ParsePreferences([&] { std::string s; if (FILE* f = std::fopen(path.c_str(), "rb")) { char b[4096]; std::size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n); std::fclose(f); } return s; }());
    CHECK(onDisk && onDisk->Get(kCount) == std::optional<int>(12345));
    std::remove(path.c_str());
}
