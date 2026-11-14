// SQLite (androidx.sqlite's BaseConformanceTest cases; Room's migration, transaction and
// invalidation-tracker tests): statements, transactions, opening by version, observed queries.
#include "Check.h"

#include "kiln/sqlite/Database.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

using namespace kiln;
using namespace kiln::sqlite;

namespace {
    template<typename P> void PumpUntil(P done) {
        for (int i = 0; i < 3000 && !done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        rt::Dispatcher::Main().RunPending();
    }
    std::string TempFile(const char* name) {
        char tmpl[] = "/tmp/kiln-sqlite-XXXXXX";
        return std::string(mkdtemp(tmpl)) + "/" + name;
    }
    std::vector<std::string> Titles(Connection& c) {
        std::vector<std::string> out;
        for (Statement q = c.Query("SELECT title FROM note ORDER BY id"); q.Step();) out.push_back(q.GetText(0));
        return out;
    }
    const char* kSchema = "CREATE TABLE note (id INTEGER PRIMARY KEY, title TEXT NOT NULL); CREATE TABLE tag (id INTEGER PRIMARY KEY, name TEXT)";
}

TEST(SQLiteStatementsBindAndReadEveryType) {
    Connection c = Connection::Open(":memory:");
    c.Execute("CREATE TABLE t (i INTEGER, d REAL, s TEXT, b BLOB, n TEXT)");
    const std::vector<std::uint8_t> blob{ 1, 2, 0, 3 };
    c.Execute("INSERT INTO t VALUES (?, ?, ?, ?, ?)", std::int64_t(1) << 40, 1.5, "héllo", blob, nullptr);
    CHECK(c.LastInsertRowId() == 1 && c.Changes() == 1);
    Statement q = c.Prepare("SELECT * FROM t");
    CHECK(q.ColumnCount() == 5 && q.ColumnName(2) == "s" && q.ColumnNames().size() == 5);
    bool noRow = false;
    try { q.GetLong(0); } catch (const Error& e) { noRow = std::string(e.what()) == "Error code: 21, message: no row"; }
    CHECK(noRow);
    CHECK(q.Step());
    CHECK(q.GetLong(0) == std::int64_t(1) << 40 && q.GetDouble(1) == 1.5 && q.GetText(2) == "héllo" && q.GetBlob(3) == blob && q.IsNull(4));
    CHECK(q.ColumnType(0) == DataType::Integer && q.ColumnType(1) == DataType::Float && q.ColumnType(2) == DataType::Text &&
          q.ColumnType(3) == DataType::Blob && q.ColumnType(4) == DataType::Null);
    bool range = false;
    try { q.GetText(5); } catch (const Error& e) { range = e.code == 25; }
    CHECK(range);
    CHECK(!q.Step());
    q.Reset();
    CHECK(q.Step() && q.GetText(2) == "héllo");
    // Bindings survive Reset and go with ClearBindings.
    Statement p = c.Query("SELECT ?", 7);
    CHECK(p.Step() && p.GetInt(0) == 7);
    p.Reset();
    CHECK(p.Step() && p.GetInt(0) == 7);
    p.Reset();
    p.ClearBindings();
    CHECK(p.Step() && p.IsNull(0));
    bool syntax = false;
    try { c.Prepare("SELEC 1"); } catch (const Error& e) { syntax = e.code == 1 && std::string(e.what()).find("syntax error") != std::string::npos; }
    CHECK(syntax);
    bool constraint = false;
    c.Execute("CREATE TABLE u (id INTEGER PRIMARY KEY)");
    c.Execute("INSERT INTO u VALUES (1)");
    try { c.Execute("INSERT INTO u VALUES (1)"); } catch (const Error& e) { constraint = e.code == 1555; }   // SQLITE_CONSTRAINT_PRIMARYKEY
    CHECK(constraint);
}

TEST(SQLiteTransactionsCommitRollBackAndNest) {
    Connection c = Connection::Open(":memory:");
    c.ExecuteScript(kSchema);
    c.WithTransaction([&] { c.Execute("INSERT INTO note (title) VALUES ('a')"); CHECK(c.InTransaction()); });
    CHECK(!c.InTransaction() && Titles(c).size() == 1);
    try {
        c.WithTransaction(TransactionType::Immediate, [&] {
            c.Execute("INSERT INTO note (title) VALUES ('b')");
            throw std::runtime_error("no");
        });
    } catch (const std::runtime_error&) {}
    CHECK(!c.InTransaction() && Titles(c) == std::vector<std::string>{ "a" });
    // A nested level that throws rolls back alone.
    const int got = c.WithTransaction([&] {
        c.Execute("INSERT INTO note (title) VALUES ('c')");
        try {
            c.WithTransaction([&] {
                c.Execute("INSERT INTO note (title) VALUES ('d')");
                throw std::runtime_error("inner");
            });
        } catch (const std::runtime_error&) {}
        c.WithTransaction([&] { c.Execute("INSERT INTO note (title) VALUES ('e')"); });
        return 5;
    });
    CHECK(got == 5 && (Titles(c) == std::vector<std::string>{ "a", "c", "e" }));
}

TEST(SQLiteMigrationPathsTakeTheLargestStep) {
    auto m = [](int a, int b) { return Migration{ a, b, [](Connection&) {} }; };
    const std::vector<Migration> ms{ m(1, 2), m(2, 3), m(1, 3), m(3, 4), m(4, 2) };
    auto path = FindMigrationPath(ms, 1, 4);
    CHECK(path && path->size() == 2 && (*path)[0]->endVersion == 3 && (*path)[1]->endVersion == 4);
    CHECK(FindMigrationPath(ms, 4, 2) && FindMigrationPath(ms, 4, 2)->size() == 1);
    CHECK(!FindMigrationPath(ms, 1, 5) && !FindMigrationPath(ms, 3, 1));
    CHECK(FindMigrationPath(ms, 2, 2) && FindMigrationPath(ms, 2, 2)->empty());
}

TEST(SQLiteDatabaseCreatesMigratesAndFallsBack) {
    const std::string path = TempFile("app.db");
    int created = 0, opened = 0;
    {
        auto db = Database::Open({ .path = path, .version = 1,
            .onCreate = [&](Connection& c) { ++created; c.ExecuteScript(kSchema); },
            .onOpen = [&](Connection&) { ++opened; } });
        db->WriteBlocking([](Connection& c) { c.Execute("INSERT INTO note (title) VALUES ('kept')"); });
        CHECK(db->ReadBlocking([](Connection& c) { return Titles(c); }) == std::vector<std::string>{ "kept" });
        // Readers are read-only.
        bool readOnly = false;
        try { db->ReadBlocking([](Connection& c) { c.Execute("INSERT INTO note (title) VALUES ('x')"); }); } catch (const Error& e) { readOnly = (e.code & 0xff) == 8; }
        CHECK(readOnly);
        // A failed write changes nothing.
        try { db->WriteBlocking([](Connection& c) { c.Execute("INSERT INTO note (title) VALUES ('gone')"); throw std::runtime_error("no"); }); } catch (const std::runtime_error&) {}
        CHECK(db->ReadBlocking([](Connection& c) { return Titles(c); }).size() == 1);
    }
    CHECK(created == 1 && opened == 1);
    {
        // Version 3 through 1->2->3: the data stays.
        auto db = Database::Open({ .path = path, .version = 3,
            .onCreate = [&](Connection&) { ++created; },
            .migrations = {
                { 1, 2, [](Connection& c) { c.Execute("ALTER TABLE note ADD COLUMN done INTEGER NOT NULL DEFAULT 0"); } },
                { 2, 3, [](Connection& c) { c.Execute("ALTER TABLE note ADD COLUMN color TEXT"); } } } });
        CHECK(db->ReadBlocking([](Connection& c) { Statement q = c.Prepare("SELECT title, done, color FROM note"); return q.Step() ? q.GetText(0) : std::string(); }) == "kept");
        CHECK(db->ReadBlocking([](Connection& c) { Statement q = c.Prepare("PRAGMA user_version"); q.Step(); return q.GetInt(0); }) == 3);
        CHECK(db->ReadBlocking([](Connection& c) { Statement q = c.Prepare("PRAGMA journal_mode"); q.Step(); return q.GetText(0); }) == "wal");
    }
    CHECK(created == 1);
    {
        // No path to 5: an error, and the file is left at version 3.
        auto db = Database::Open({ .path = path, .version = 5, .onCreate = [&](Connection&) { ++created; } });
        bool failed = false;
        try { db->ReadBlocking([](Connection&) {}); } catch (const std::runtime_error& e) { failed = std::string(e.what()).find("a migration from 3 to 5 was required") != std::string::npos; }
        CHECK(failed);
    }
    {
        int destroyed = 0;
        auto db = Database::Open({ .path = path, .version = 5,
            .onCreate = [&](Connection& c) { ++created; c.ExecuteScript(kSchema); },
            .fallbackToDestructiveMigration = true,
            .onDestructiveMigration = [&](Connection&) { ++destroyed; } });
        CHECK(db->ReadBlocking([](Connection& c) { return Titles(c); }).empty());
        CHECK(destroyed == 1 && created == 2);
    }
    {
        // A failed migration rolls back: still version 5 with its tables.
        auto db = Database::Open({ .path = path, .version = 6,
            .migrations = { { 5, 6, [](Connection& c) { c.Execute("DROP TABLE note"); throw std::runtime_error("broken"); } } } });
        bool failed = false;
        try { db->ReadBlocking([](Connection&) {}); } catch (const std::runtime_error&) { failed = true; }
        CHECK(failed);
        Connection raw = Connection::Open(path);
        Statement q = raw.Prepare("PRAGMA user_version");
        CHECK(q.Step() && q.GetInt(0) == 5 && Titles(raw).empty());
    }
    std::remove(path.c_str());
}

TEST(SQLiteObservedQueriesRunAgainWhenTheirTablesChange) {
    for (const bool inMemory : { false, true }) {
        const std::string path = inMemory ? std::string() : TempFile("flow.db");
        auto db = Database::Open({ .path = path, .version = 1, .onCreate = [](Connection& c) { c.ExecuteScript(kSchema); } });
        std::vector<std::vector<std::string>> seen;
        std::vector<std::set<std::string>> invalidated;
        rt::Job query = rt::Launch(rt::Dispatcher::Main(), db->Query({ "note" }, [](Connection& c) { return Titles(c); })
            .Collect([&](const std::vector<std::string>& t) { seen.push_back(t); }));
        rt::Job marks = rt::Launch(rt::Dispatcher::Main(), db->Invalidations({ "NOTE", "tag" }, false)
            .Collect([&](const std::set<std::string>& t) { invalidated.push_back(t); }));
        PumpUntil([&] { return seen.size() == 1; });
        CHECK(seen.size() == 1 && seen[0].empty() && invalidated.empty());

        rt::Job w1 = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Database> d) -> rt::Task<> {
            co_await d->Write([](Connection& c) { c.Execute("INSERT INTO note (title) VALUES (?)", "Milk"); });
        }(db));
        PumpUntil([&] { return seen.size() == 2; });
        CHECK(seen.size() == 2 && seen[1] == std::vector<std::string>{ "Milk" });
        CHECK(invalidated.size() == 1 && invalidated[0] == std::set<std::string>{ "NOTE" });

        // Another table: the note query does not run; the tag observer hears it.
        rt::Job w2 = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Database> d) -> rt::Task<> {
            const std::int64_t id = co_await d->Write([](Connection& c) { c.Execute("INSERT INTO tag (name) VALUES ('x')"); return c.LastInsertRowId(); });
            CHECK(id == 1);
        }(db));
        PumpUntil([&] { return invalidated.size() == 2; });
        CHECK(seen.size() == 2 && invalidated.size() == 2 && invalidated[1] == std::set<std::string>{ "tag" });

        // A write that fails is rolled back and notifies nobody.
        bool threw = false;
        rt::Job w3 = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Database> d, bool* threw) -> rt::Task<> {
            try {
                co_await d->Write([](Connection& c) { c.Execute("DELETE FROM note"); throw std::runtime_error("no"); });
            } catch (const std::runtime_error&) { *threw = true; }
        }(db, &threw));
        PumpUntil([&] { return threw; });
        CHECK(threw && seen.size() == 2);

        // After the collector stops, writes no longer reach it.
        query.Cancel();
        rt::Job w4 = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Database> d) -> rt::Task<> {
            co_await d->Write([](Connection& c) { c.Execute("UPDATE note SET title = 'Eggs'"); });
        }(db));
        PumpUntil([&] { return invalidated.size() == 3; });
        CHECK(seen.size() == 2 && invalidated.size() == 3);

        // A name that is not a table.
        bool unknown = false;
        rt::Job bad = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Database> d, bool* unknown) -> rt::Task<> {
            try {
                co_await rt::First(d->Invalidations({ "nope" }));
            } catch (const std::invalid_argument& e) { *unknown = std::string(e.what()) == "There is no table with name nope"; }
        }(db, &unknown));
        PumpUntil([&] { return unknown; });
        CHECK(unknown);
        marks.Cancel();
        db->Close();
        if (!inMemory) std::remove(path.c_str());
    }
}

TEST(SQLiteReadersRunWhileAWriteIsOpen) {
    const std::string path = TempFile("wal.db");
    auto db = Database::Open({ .path = path, .version = 1, .onCreate = [](Connection& c) { c.ExecuteScript(kSchema); } });
    db->WriteBlocking([](Connection& c) { c.Execute("INSERT INTO note (title) VALUES ('a')"); });
    std::size_t during = 99;
    db->WriteBlocking([&](Connection& c) {
        c.Execute("INSERT INTO note (title) VALUES ('b')");
        // Another thread reads the committed state without waiting for this transaction.
        std::thread reader([&] { during = db->ReadBlocking([](Connection& r) { return Titles(r); }).size(); });
        reader.join();
    });
    CHECK(during == 1);
    CHECK(db->ReadBlocking([](Connection& c) { return Titles(c); }).size() == 2);
    db->Close();
    bool closed = false;
    try { db->ReadBlocking([](Connection&) {}); } catch (const Error&) { closed = true; }
    CHECK(closed);
    std::remove(path.c_str());
}
