#pragma once

// A database for an app (Room's runtime without its compiler: RoomDatabase, RoomConnectionManager,
// the connection pool and InvalidationTracker). It opens the file on first use, creates or
// migrates the schema by version, and runs work on the IO pool: reads on a pool of read-only
// connections, writes one at a time in a transaction on the write connection (WAL, so readers
// do not wait for the writer). Queries can be observed: a flow that runs the query again when
// a write changes one of its tables (temporary triggers mark changed tables, as Room does).
//
//   auto db = sqlite::Database::Open({
//       .name = "notes.db",
//       .version = 2,
//       .onCreate = [](sqlite::Connection& c) { c.ExecuteScript("CREATE TABLE note (id INTEGER PRIMARY KEY, title TEXT NOT NULL, done INTEGER NOT NULL DEFAULT 0)"); },
//       .migrations = { { 1, 2, [](sqlite::Connection& c) { c.Execute("ALTER TABLE note ADD COLUMN done INTEGER NOT NULL DEFAULT 0"); } } },
//   });
//   co_await db->Write([](sqlite::Connection& c) { c.Execute("INSERT INTO note (title) VALUES (?)", "Milk"); });
//   rt::Flow<std::vector<std::string>> titles = db->Query({ "note" }, [](sqlite::Connection& c) {
//       std::vector<std::string> out;
//       for (auto q = c.Query("SELECT title FROM note ORDER BY id"); q.Step();) out.push_back(q.GetText(0));
//       return out;
//   });
//
// Unlike Room there is no schema identity check: nothing generates the expected schema, so a
// changed schema without a version bump is not detected.

#include "kiln/sqlite/SQLite.h"

#include "kiln/runtime/Flow.h"
#include "kiln/runtime/TaskDispatchers.h"

#include <kiln/Function.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace kiln::sqlite {

    // Migration(startVersion, endVersion): runs inside the upgrade's transaction.
    struct Migration {
        int startVersion = 0;
        int endVersion = 0;
        kiln::Function<void(Connection&)> migrate;
    };

    enum class JournalMode { WriteAheadLogging, Truncate };

    struct DatabaseOptions {
        // The file: Context.getDatabasePath(name) (ui::DatabasePath). Empty for a database in
        // memory, which lives as long as the Database.
        std::string name;
        // A file elsewhere, instead of name.
        std::string path;
        // The schema version, above 0. Opening a file at another version migrates it.
        int version = 1;
        // Creates the schema of `version` in a new database.
        kiln::Function<void(Connection&)> onCreate;
        std::vector<Migration> migrations;
        // fallbackToDestructiveMigration(dropAllTables = true): without a migration path, drop
        // every table and view and create the schema again, instead of failing.
        bool fallbackToDestructiveMigration = false;
        // The same for downgrades only.
        bool fallbackToDestructiveMigrationOnDowngrade = false;
        // fallbackToDestructiveMigrationFrom(startVersions): destructive only from these versions
        // (no migration may start or end at one of them).
        std::vector<int> fallbackToDestructiveMigrationFrom;
        // Callback.onDestructiveMigration, after the tables were dropped.
        kiln::Function<void(Connection&)> onDestructiveMigration;
        // Callback.onOpen: each time the database is opened, on its write connection.
        kiln::Function<void(Connection&)> onOpen;
        // createFromAsset / createFromFile / createFromInputStream: a prepackaged database copied
        // in when there is no file yet (or the file's version needs a destructive migration and a
        // fresh copy can replace it). One of the three.
        std::string createFromAsset;
        std::string createFromFile;
        kiln::Function<std::vector<std::uint8_t>()> createFromInputStream;
        // PrepackagedDatabaseCallback.onOpenPrepackagedDatabase: the copy, before it is put in
        // place (and before migrations).
        kiln::Function<void(Connection&)> onOpenPrepackagedDatabase;
        JournalMode journalMode = JournalMode::WriteAheadLogging;
        // Read connections kept open (Room's pool has 4 with write-ahead logging):
        // setMultipleConnectionPool(maxReaders, 1).
        int maxReaders = 4;
        // setSingleConnectionPool: reads and writes share the one connection (the default with
        // JournalMode::Truncate, where readers would wait for the writer anyway).
        bool singleConnection = false;
        // setConnectionPoolTimeout: how long a read waits for a free connection before it fails
        // with SQLITE_BUSY.
        int connectionPoolTimeoutMs = 30'000;
        // allowDataLossOnRecovery: a file that cannot be opened even on the retry (corrupt) is
        // deleted with its journals and created again, instead of failing.
        bool allowDataLossOnRecovery = false;
        // setAutoCloseTimeout: after this long unused the connections close, and open again on
        // the next use (0: never). Not for a database in memory.
        int autoCloseTimeoutMs = 0;
        // setQueryCoroutineContext: the pool Read and Write run on (the IO pool when null).
        rt::BackgroundDispatcher* queryDispatcher = nullptr;
    };

    // MigrationContainer.findMigrationPath: the migrations from start to end, taking the
    // largest step first; nullopt when there is no path.
    std::optional<std::vector<const Migration*>> FindMigrationPath(const std::vector<Migration>& migrations, int start, int end);

    class Database : public std::enable_shared_from_this<Database> {
    public:
        // Call on the main thread (the flows deliver there); the file is opened on first use.
        static std::shared_ptr<Database> Open(DatabaseOptions options);
        ~Database();
        Database(const Database&) = delete;

        // fn(Connection&) on the IO pool with a read-only connection; co_await gives its result.
        template<typename F> auto Read(F fn) {
            return rt::WithContext(QueryPool(), [self = shared_from_this(), fn = std::move(fn)]() mutable { return self->ReadBlocking(fn); });
        }
        // fn(Connection&) on the IO pool with the write connection, in an immediate transaction:
        // committed when fn returns, rolled back when it throws (the exception comes out of the
        // co_await). Observed queries over the changed tables then run again.
        template<typename F> auto Write(F fn) {
            return rt::WithContext(QueryPool(), [self = shared_from_this(), fn = std::move(fn)]() mutable { return self->WriteBlocking(fn); });
        }
        // The same on the calling thread, for code already off the main thread.
        template<typename F> auto ReadBlocking(F&& fn) -> std::invoke_result_t<F, Connection&> {
            UseGuard use(*this);
            ReaderLease lease = AcquireReader();
            return fn(*lease.connection);
        }
        template<typename F> auto WriteBlocking(F&& fn) -> std::invoke_result_t<F, Connection&> {
            UseGuard use(*this);
            std::unique_lock lock(m_WriterMutex);
            Connection& c = WriterLocked();
            SyncTriggersLocked(c);
            if constexpr (std::is_void_v<std::invoke_result_t<F, Connection&>>) {
                c.WithTransaction(TransactionType::Immediate, [&] { fn(c); });
                NotifyLocked(c);
            } else {
                auto r = c.WithTransaction(TransactionType::Immediate, [&] { return fn(c); });
                NotifyLocked(c);
                return r;
            }
        }

        // InvalidationTracker.createFlow: the set of `tables` that changed, each time a write
        // changes some; with emitInitialState, all of them at once when collection starts.
        // A name that is not a table throws std::invalid_argument from the collection.
        rt::Flow<std::set<std::string>> Invalidations(std::vector<std::string> tables, bool emitInitialState = true);
        // A query as a flow (Room's Flow-returning queries): fn's result now, and again after
        // each write that changes one of `tables`. Changes that arrive while a query runs are
        // conflated into one more run.
        template<typename F> auto Query(std::vector<std::string> tables, F fn) -> rt::Flow<std::invoke_result_t<F, Connection&>> {
            using R = std::invoke_result_t<F, Connection&>;
            auto self = shared_from_this();
            return rt::Transform<std::set<std::string>, R>(rt::Conflate(Invalidations(std::move(tables))),
                [self, fn = std::move(fn)](std::set<std::string>, rt::FlowCollector<R> emit) -> rt::Task<> {
                    R r = co_await self->Read(fn);
                    co_await emit(std::move(r));
                });
        }
        // InvalidationTracker.refreshAsync: checks for changes made outside Write (through
        // another connection's triggers this cannot see; through this database's own write
        // connection it can).
        void RefreshInvalidations();
        // clearAllTables: deletes every row of every table (the schema stays), then checkpoints
        // and vacuums. Observed queries run again.
        auto ClearAllTables() {
            return rt::WithContext(QueryPool(), [self = shared_from_this()] { self->ClearAllTablesBlocking(); });
        }
        void ClearAllTablesBlocking();

        // Closes the connections once running work is done; later use throws.
        void Close();
        const std::string& Path() const { return m_Path; }
        // isOpen: a connection is open and configured (not before first use, after Close, or
        // while auto-closed).
        bool IsOpen() const { return m_Configured.load(); }

        explicit Database(DatabaseOptions options);

    private:
        rt::BackgroundDispatcher& QueryPool() const { return m_Options.queryDispatcher ? *m_Options.queryDispatcher : rt::TaskDispatchers::Platform().IO(); }
        Connection OpenInitialConnection();
        // AutoCloser's reference count around each use.
        struct UseGuard {
            Database& db;
            explicit UseGuard(Database& d) : db(d) { db.IncrementUse(); }
            ~UseGuard() { db.DecrementUse(); }
        };
        void IncrementUse();
        void DecrementUse();
        void AutoCloseLoop();
        struct ReaderLease {
            Database* db = nullptr;
            Connection* connection = nullptr;
            std::unique_lock<std::mutex> writerLock;   // held when the reader is the write connection
            ReaderLease() = default;
            ReaderLease(ReaderLease&& o) noexcept : db(std::exchange(o.db, nullptr)), connection(o.connection), writerLock(std::move(o.writerLock)) {}
            ~ReaderLease();
        };
        ReaderLease AcquireReader();
        Connection& WriterLocked();
        void VerifyPrepackagedFile();
        void CopyPrepackaged(const std::string& destination);
        void ConfigureDatabase(Connection& c);
        void ConfigureConnection(Connection& c);
        void SyncTriggersLocked(Connection& c);
        void NotifyLocked(Connection& c);
        std::vector<int> ResolveTables(const std::vector<std::string>& tables);
        void ObserversChanged(const std::vector<int>& ids, int delta);
        rt::Task<> CollectInvalidations(std::vector<std::string> tables, bool emitInitialState, rt::FlowCollector<std::set<std::string>> emit);

        DatabaseOptions m_Options;
        std::string m_Path;
        bool m_InMemory = false;
        bool m_PrepackagedVerified = false;
        bool m_AutoClosed = false;   // reopen with the connection's pragmas only
        std::mutex m_UseMutex;
        std::condition_variable m_UseChanged;
        int m_Uses = 0;
        std::chrono::steady_clock::time_point m_LastUse;
        bool m_StopAutoClose = false;
        std::thread m_AutoCloser;
        rt::Dispatcher* m_Home;   // the dispatcher of the thread that opened it: where the flows run

        std::mutex m_WriterMutex;          // the write connection and trigger sync
        Connection m_Writer;
        std::atomic<bool> m_Configured{ false };
        bool m_Closed = false;

        std::mutex m_ReadersMutex;         // the reader pool
        std::condition_variable m_ReaderFree;
        std::vector<std::unique_ptr<Connection>> m_FreeReaders;
        int m_ReadersOpen = 0;
        bool m_ReadersClosed = false;

        // ObservedTableStates: tables by id, who observes them, and which have triggers.
        std::mutex m_TrackMutex;
        std::map<std::string, int> m_TableIds;   // lowercase name -> id
        std::vector<std::string> m_TableNames;
        std::vector<long> m_ObserverCount;
        std::vector<bool> m_Tracked;
        bool m_NeedsSync = false;

        // ObservedTableVersions (main thread).
        rt::MutableStateFlow<std::vector<int>> m_Versions{ std::vector<int>{} };
    };

}
