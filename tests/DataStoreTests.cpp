// Typed DataStore (datastore-core's SingleProcessDataStoreTest, DataMigrationInitializerTest,
// ReplaceFileCorruptionHandlerTest cases): a value of the app's type in a file.
#include "Check.h"

#include "kiln/datastore/DataStore.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace kiln;
using namespace kiln::datastore;

namespace {
    // A byte as the value; "corrupt" files hold anything else.
    Serializer<int> ByteSerializer() {
        return {
            .defaultValue = 0,
            .readFrom = [](std::string_view b) -> int {
                if (b.size() != 1) throw CorruptionException("not one byte");
                return static_cast<unsigned char>(b[0]);
            },
            .writeTo = [](const int& v) { return std::string(1, char(v)); },
        };
    }
    void RunUntil(const rt::Job& job) {
        for (int i = 0; i < 3000 && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    template<typename F> void Run(F body) {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), body());
        RunUntil(job);
        CHECK(job.Done());
    }
    std::string Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    struct Out { std::optional<int> value; bool threw = false; std::string what; };
}

TEST(DataStoreReadsUpdatesAndPersists) {
    const std::string dir = test::MakeTempDir("kiln-datastore-");
    const std::string path = dir + "/value.pb";
    auto out = std::make_shared<Out>();
    {
        auto store = DataStoreFactory::Create(ByteSerializer(), path);
        // testReadNewMessage: no file reads the default and writes nothing.
        Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await rt::First(s->Data()); }(store, out); });
        CHECK(out->value == std::optional<int>(0) && !std::filesystem::exists(path));
        // testWriteAndRead / testUpdateDataReturnsNewValue.
        Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await s->UpdateData([](int v) { return v + 5; }); }(store, out); });
        CHECK(out->value == std::optional<int>(5) && Read(path) == std::string(1, char(5)));
        // An unchanged value is not written (the file stays as it was).
        std::filesystem::remove(path);
        Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await s->UpdateData([](int v) { return v; }); }(store, out); });
        CHECK(out->value == std::optional<int>(5) && !std::filesystem::exists(path));
        Run([&] { return [](auto s) -> rt::Task<> { co_await s->UpdateData([](int) { return 9; }); }(store); });
        // testTransformThrows: the value stays.
        Run([&] { return [](auto s, auto o) -> rt::Task<> {
            try { co_await s->UpdateData([](int) -> int { throw std::runtime_error("no"); }); } catch (const std::exception&) { o->threw = true; }
        }(store, out); });
        CHECK(out->threw && store->Current() == 9);
        // testCreateDuplicateActiveFiles.
        bool duplicate = false;
        try { DataStoreFactory::Create(ByteSerializer(), path); } catch (const std::logic_error&) { duplicate = true; }
        CHECK(duplicate);
    }
    // A store gone frees the file; a new one reads what was written.
    auto again = DataStoreFactory::Create(ByteSerializer(), path);
    Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await rt::First(s->Data()); }(again, out); });
    CHECK(out->value == std::optional<int>(9));
    std::filesystem::remove_all(dir);
}

TEST(DataStoreHandlesCorruption) {
    const std::string dir = test::MakeTempDir("kiln-datastore-");
    const std::string path = dir + "/corrupt.pb";
    std::ofstream(path, std::ios::binary) << "garbage";
    auto out = std::make_shared<Out>();
    {
        // Without a handler the read throws CorruptionException.
        auto store = DataStoreFactory::Create(ByteSerializer(), path);
        Run([&] { return [](auto s, auto o) -> rt::Task<> {
            try { co_await rt::First(s->Data()); } catch (const CorruptionException& e) { o->threw = true; o->what = e.what(); }
        }(store, out); });
        CHECK(out->threw && out->what == "not one byte");
    }
    // ReplaceFileCorruptionHandler: the replacement is served and written back.
    auto store = DataStoreFactory::Create(ByteSerializer(), path, ReplaceFileCorruptionHandler<int>([](const CorruptionException&) { return 42; }));
    Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await rt::First(s->Data()); }(store, out); });
    CHECK(out->value == std::optional<int>(42) && Read(path) == std::string(1, char(42)));
    std::filesystem::remove_all(dir);
}

TEST(DataStoreRunsMigrationsOnceBeforeReading) {
    const std::string dir = test::MakeTempDir("kiln-datastore-");
    const std::string path = dir + "/migrated.pb";
    int migrated = 0, skipped = 0, cleaned = 0, notCleaned = 0;
    std::vector<DataMigration<int>> migrations{
        { .shouldMigrate = [](const int& v) { return v == 0; }, .migrate = [&](int) { ++migrated; return 7; }, .cleanUp = [&] { ++cleaned; } },
        { .shouldMigrate = [](const int& v) { return v == 100; }, .migrate = [&](int v) { ++skipped; return v; }, .cleanUp = [&] { ++notCleaned; } },
        // Sees the first migration's result (testMigrationsRunInOrder).
        { .shouldMigrate = [](const int& v) { return v == 7; }, .migrate = [](int v) { return v + 1; }, .cleanUp = [&] { ++cleaned; } },
    };
    auto out = std::make_shared<Out>();
    auto store = DataStoreFactory::Create(ByteSerializer(), path, std::nullopt, std::move(migrations));
    Run([&] { return [](auto s, auto o) -> rt::Task<> { o->value = co_await rt::First(s->Data()); }(store, out); });
    CHECK(out->value == std::optional<int>(8) && Read(path) == std::string(1, char(8)));
    CHECK(migrated == 1 && skipped == 0 && cleaned == 2 && notCleaned == 0);
    // Not again on later reads or updates.
    Run([&] { return [](auto s) -> rt::Task<> { co_await s->UpdateData([](int) { return 0; }); co_await rt::First(s->Data()); }(store); });
    CHECK(migrated == 1 && store->Current() == 0);
    std::filesystem::remove_all(dir);
}
