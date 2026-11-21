#include "kiln/sqlite/Database.h"

#include "kiln/Assets.h"
#include "kiln/ui/AppDirectories.h"

#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace kiln::sqlite {

    namespace {
        constexpr int kBusyTimeoutMs = 3000;   // BaseRoomConnectionManager.BUSY_TIMEOUT_MS
        constexpr const char* kLogTable = "kiln_table_modification_log";
        constexpr const char* kTriggers[] = { "INSERT", "UPDATE", "DELETE" };

        std::string Lower(std::string s) {
            for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        std::int64_t PragmaLong(Connection& c, const char* sql) {
            Statement s = c.Prepare(sql);
            return s.Step() ? s.GetLong(0) : 0;
        }
        void ConfigureBusyTimeout(Connection& c) {
            if (PragmaLong(c, "PRAGMA busy_timeout") < kBusyTimeoutMs) c.Execute("PRAGMA busy_timeout = " + std::to_string(kBusyTimeoutMs));
        }
        // NORMAL with write-ahead logging, FULL without (sqlite.org/pragma.html#pragma_synchronous).
        void ConfigureSynchronous(Connection& c, JournalMode mode) {
            c.Execute(mode == JournalMode::WriteAheadLogging ? "PRAGMA synchronous = NORMAL" : "PRAGMA synchronous = FULL");
        }
        // openAndVerify: opening and reading the schema version finds a corrupt file.
        Connection OpenAndVerify(const std::string& path) {
            Connection c = Connection::Open(path);
            Statement s = c.Prepare("PRAGMA schema_version");
            s.Step();
            return c;
        }
        std::string TriggerName(const std::string& table, const char* trigger) {
            return "kiln_table_modification_trigger_" + table + "_" + trigger;
        }
        void DropAllTables(Connection& c) {
            std::vector<std::pair<std::string, bool>> names;
            for (Statement s = c.Prepare("SELECT name, type FROM sqlite_master WHERE type = 'table' OR type = 'view'"); s.Step();) {
                std::string name = s.GetText(0);
                if (name.starts_with("sqlite_") || name == "android_metadata") continue;
                names.emplace_back(std::move(name), s.GetText(1) == "view");
            }
            for (const auto& [name, view] : names) c.Execute(std::string(view ? "DROP VIEW IF EXISTS `" : "DROP TABLE IF EXISTS `") + name + "`");
        }
    }

    namespace {
        // isMigrationRequired: false when a destructive migration may stand in.
        bool IsMigrationRequired(const DatabaseOptions& o, int from, int to) {
            if (from > to && o.fallbackToDestructiveMigrationOnDowngrade) return false;
            const auto& notRequiredFrom = o.fallbackToDestructiveMigrationFrom;
            return !o.fallbackToDestructiveMigration && std::find(notRequiredFrom.begin(), notRequiredFrom.end(), from) == notRequiredFrom.end();
        }
        // readVersion: the header's user_version, a big-endian int at offset 60.
        std::optional<int> ReadVersion(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return std::nullopt;
            in.seekg(60);
            unsigned char b[4];
            if (!in.read(reinterpret_cast<char*>(b), 4)) return std::nullopt;
            return int((std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | std::uint32_t(b[3]));
        }
        std::vector<std::string> UserTables(Connection& c) {
            std::vector<std::string> names;
            for (Statement s = c.Prepare("SELECT name FROM sqlite_master WHERE type = 'table'"); s.Step();) {
                std::string name = s.GetText(0);
                if (name.starts_with("sqlite_") || name == "android_metadata" || name == "room_master_table") continue;
                names.push_back(std::move(name));
            }
            return names;
        }
    }

    std::optional<std::vector<const Migration*>> FindMigrationPath(const std::vector<Migration>& migrations, int start, int end) {
        std::vector<const Migration*> result;
        if (start == end) return result;
        const bool upgrade = end > start;
        int at = start;
        while (upgrade ? at < end : at > end) {
            // Of the migrations from `at`, the one that goes furthest without passing `end`.
            const Migration* best = nullptr;
            for (const Migration& m : migrations) {
                if (m.startVersion != at) continue;
                const bool onPath = upgrade ? (m.endVersion > at && m.endVersion <= end) : (m.endVersion >= end && m.endVersion < at);
                if (!onPath) continue;
                if (!best || (upgrade ? m.endVersion > best->endVersion : m.endVersion < best->endVersion)) best = &m;
            }
            if (!best) return std::nullopt;
            result.push_back(best);
            at = best->endVersion;
        }
        return result;
    }

    Database::Database(DatabaseOptions options) : m_Options(std::move(options)), m_Home(&rt::Dispatcher::Main()) {
        if (m_Options.version < 1) throw std::invalid_argument("kiln: a database version must be above 0");
        m_InMemory = m_Options.name.empty() && m_Options.path.empty();
        m_Path = m_InMemory ? ":memory:" : !m_Options.path.empty() ? m_Options.path : ui::DatabasePath(m_Options.name);
        // Builder.build's checks.
        for (const Migration& m : m_Options.migrations)
            for (const int version : { m.startVersion, m.endVersion })
                if (std::find(m_Options.fallbackToDestructiveMigrationFrom.begin(), m_Options.fallbackToDestructiveMigrationFrom.end(), version) != m_Options.fallbackToDestructiveMigrationFrom.end()) {
                    throw std::invalid_argument("kiln: Inconsistency detected. A Migration was supplied that has a start or end version equal to a start version "
                                                "supplied to fallbackToDestructiveMigrationFrom. Start version is: " + std::to_string(version));
                }
        const int sources = int(!m_Options.createFromAsset.empty()) + int(!m_Options.createFromFile.empty()) + int(bool(m_Options.createFromInputStream));
        if (sources > 1) throw std::invalid_argument("kiln: Both createFromAsset() and createFromFile() was called on this Builder, but the database can only be created using one of the two configurations.");
        if (sources && m_InMemory) throw std::invalid_argument("kiln: Cannot create from asset or file for an in-memory database.");
        if (m_Options.autoCloseTimeoutMs < 0) throw std::invalid_argument("kiln: autoCloseTimeout must be >= 0");
        if (m_Options.autoCloseTimeoutMs > 0) {
            if (m_InMemory) throw std::invalid_argument("kiln: Cannot create auto-closing database for an in-memory database.");
            m_AutoCloser = std::thread([this] { AutoCloseLoop(); });
        }
    }

    void Database::IncrementUse() {
        std::lock_guard lock(m_UseMutex);
        ++m_Uses;
    }
    void Database::DecrementUse() {
        {
            std::lock_guard lock(m_UseMutex);
            --m_Uses;
            m_LastUse = std::chrono::steady_clock::now();
        }
        m_UseChanged.notify_all();
    }

    // AutoCloser: once nothing has used the database for the timeout, close its connections
    // (they open again on the next use).
    void Database::AutoCloseLoop() {
        const auto timeout = std::chrono::milliseconds(m_Options.autoCloseTimeoutMs);
        std::unique_lock lock(m_UseMutex);
        while (!m_StopAutoClose) {
            // Wait for an idle open database.
            m_UseChanged.wait(lock, [&] { return m_StopAutoClose || (m_Uses == 0 && m_Configured.load()); });
            if (m_StopAutoClose) break;
            const auto due = m_LastUse + timeout;
            if (m_UseChanged.wait_until(lock, due, [&] { return m_StopAutoClose || m_Uses != 0 || m_LastUse + timeout != due; })) continue;
            // Still unused for the whole timeout: close (no use can start while m_UseMutex is held
            // by this check, and none is running).
            lock.unlock();
            {
                std::scoped_lock both(m_WriterMutex, m_ReadersMutex);
                std::lock_guard uses(m_UseMutex);
                if (m_Uses == 0 && !m_Closed && m_Writer.IsOpen()) {
                    m_Writer.Close();
                    m_FreeReaders.clear();
                    m_ReadersOpen = 0;
                    m_Configured = false;
                    m_AutoClosed = true;
                }
            }
            lock.lock();
        }
    }

    // PrePackagedCopySQLiteDriver.verifyDatabaseFile: copy when there is no file, or when its
    // version would be migrated destructively anyway.
    void Database::VerifyPrepackagedFile() {
        namespace fs = std::filesystem;
        if (!fs::exists(m_Path)) {
            CopyPrepackaged(m_Path);
            return;
        }
        const std::optional<int> current = ReadVersion(m_Path);
        if (!current || *current == m_Options.version) return;
        if (FindMigrationPath(m_Options.migrations, *current, m_Options.version)) return;   // migrations come first
        if (IsMigrationRequired(m_Options, *current, m_Options.version)) return;
        bool deleted = false;
        for (const char* postfix : { "", "-wal", "-shm", "-journal" }) {
            std::error_code ec;
            if (fs::exists(m_Path + postfix)) deleted = fs::remove(m_Path + postfix, ec) || deleted;
        }
        if (!deleted) {
            std::fprintf(stderr, "kiln: failed to delete database file (%s) for a copy destructive migration\n", m_Path.c_str());
            return;
        }
        try {
            CopyPrepackaged(m_Path);
        } catch (const std::exception& e) {
            // There is a database to open (an empty one); upstream forgives this.
            std::fprintf(stderr, "kiln: unable to copy database file: %s\n", e.what());
        }
    }

    // copyDatabase: through an intermediate file, so a half copy never takes the database's place.
    void Database::CopyPrepackaged(const std::string& destination) {
        namespace fs = std::filesystem;
        std::vector<std::uint8_t> bytes;
        if (!m_Options.createFromAsset.empty()) {
            auto asset = ReadAsset(m_Options.createFromAsset);
            if (!asset) throw std::runtime_error("kiln: no asset " + m_Options.createFromAsset + " to create the database from");
            bytes = std::move(*asset);
        } else if (!m_Options.createFromFile.empty()) {
            std::ifstream in(m_Options.createFromFile, std::ios::binary);
            if (!in) throw std::runtime_error("kiln: cannot read " + m_Options.createFromFile + " to create the database from");
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        } else {
            bytes = m_Options.createFromInputStream();
        }
        const fs::path dest(destination);
        if (dest.has_parent_path()) fs::create_directories(dest.parent_path());
        const std::string intermediate = destination + "-kiln-copy.tmp";
        {
            std::ofstream out(intermediate, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!out) throw std::runtime_error("kiln: failed to write " + intermediate);
        }
        if (m_Options.onOpenPrepackagedDatabase) {
            Connection c = Connection::Open(intermediate);
            m_Options.onOpenPrepackagedDatabase(c);
        }
        std::error_code ec;
        fs::rename(intermediate, dest, ec);
        if (ec) {
            fs::remove(intermediate, ec);
            throw std::runtime_error("kiln: Failed to move intermediate file (" + intermediate + ") to destination (" + destination + ").");
        }
    }

    // tryOpenInitialConnection: a retry after a short wait; then, when allowed, the corrupt file
    // goes and a new one is made.
    Connection Database::OpenInitialConnection() {
        try {
            return OpenAndVerify(m_Path);
        } catch (...) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        try {
            return OpenAndVerify(m_Path);
        } catch (const Error&) {
            if (m_InMemory || !m_Options.allowDataLossOnRecovery) throw;
        }
        for (const char* postfix : { "", "-wal", "-shm", "-journal" }) {
            std::error_code ec;
            std::filesystem::remove(m_Path + postfix, ec);
        }
        return OpenAndVerify(m_Path);
    }

    void Database::ClearAllTablesBlocking() {
        UseGuard use(*this);
        std::unique_lock lock(m_WriterMutex);
        Connection& c = WriterLocked();
        SyncTriggersLocked(c);
        const std::vector<std::string> tables = UserTables(c);
        bool hasForeignKeys = false;
        for (const auto& t : tables) {
            Statement fk = c.Prepare("PRAGMA foreign_key_list(`" + t + "`)");
            if (fk.Step()) hasForeignKeys = true;
        }
        c.WithTransaction(TransactionType::Immediate, [&] {
            if (hasForeignKeys) c.Execute("PRAGMA defer_foreign_keys = TRUE");
            for (const auto& t : tables) c.Execute("DELETE FROM `" + t + "`");
        });
        NotifyLocked(c);
        c.Execute("PRAGMA wal_checkpoint(FULL)");
        c.Execute("VACUUM");
    }

    std::shared_ptr<Database> Database::Open(DatabaseOptions options) { return std::make_shared<Database>(std::move(options)); }

    Database::~Database() { Close(); }

    void Database::Close() {
        {
            std::lock_guard lock(m_UseMutex);
            m_StopAutoClose = true;
        }
        m_UseChanged.notify_all();
        if (m_AutoCloser.joinable() && m_AutoCloser.get_id() != std::this_thread::get_id()) m_AutoCloser.join();
        {
            std::lock_guard lock(m_WriterMutex);
            m_Closed = true;
            m_Writer.Close();
        }
        std::unique_lock lock(m_ReadersMutex);
        // Leased readers come back first.
        m_ReaderFree.wait(lock, [&] { return int(m_FreeReaders.size()) == m_ReadersOpen; });
        m_FreeReaders.clear();
        m_ReadersOpen = 0;
        m_ReadersClosed = true;
        m_Configured = false;
    }

    // The write connection, opened and configured on first use (RoomConnectionManager.openLocked).
    Connection& Database::WriterLocked() {
        if (m_Closed) throw Error(SQLITE_MISUSE, "database is closed");
        if (m_Writer.IsOpen()) return m_Writer;
        // tryOpenInitialConnection: a second try after a short wait before giving up.
        if (!m_InMemory && !m_PrepackagedVerified && (!m_Options.createFromAsset.empty() || !m_Options.createFromFile.empty() || m_Options.createFromInputStream)) {
            VerifyPrepackagedFile();
            m_PrepackagedVerified = true;
        }
        Connection c = OpenInitialConnection();
        // After an auto-close only the connection is configured again (the auto-open callback).
        if (std::exchange(m_AutoClosed, false)) ConfigureConnection(c);
        else ConfigureDatabase(c);
        // TriggerBasedInvalidationTracker.configureConnection
        c.Execute("PRAGMA temp_store = MEMORY");
        c.Execute("PRAGMA recursive_triggers = 1");
        c.Execute(std::string("DROP TABLE IF EXISTS ") + kLogTable);
        c.Execute(std::string("CREATE TEMP TABLE IF NOT EXISTS ") + kLogTable + " (table_id INTEGER PRIMARY KEY, invalidated INTEGER NOT NULL DEFAULT 0)");
        {
            std::lock_guard lock(m_TrackMutex);
            std::fill(m_Tracked.begin(), m_Tracked.end(), false);
            m_NeedsSync = true;
        }
        m_Writer = std::move(c);
        m_Configured = true;
        return m_Writer;
    }

    // configureDatabase: pragmas, then create or migrate to the wanted version.
    // configureConnection: the pragmas every opened connection gets.
    void Database::ConfigureConnection(Connection& c) {
        ConfigureBusyTimeout(c);
        if (!m_InMemory) c.Execute(m_Options.journalMode == JournalMode::WriteAheadLogging ? "PRAGMA journal_mode = WAL" : "PRAGMA journal_mode = TRUNCATE");
        ConfigureSynchronous(c, m_Options.journalMode);
    }

    void Database::ConfigureDatabase(Connection& c) {
        ConfigureConnection(c);
        const int version = int(PragmaLong(c, "PRAGMA user_version"));
        const int wanted = m_Options.version;
        if (version != wanted) {
            c.Execute("BEGIN EXCLUSIVE TRANSACTION");
            try {
                if (version == 0) {
                    if (m_Options.onCreate) m_Options.onCreate(c);
                } else if (auto path = FindMigrationPath(m_Options.migrations, version, wanted)) {
                    for (const Migration* m : *path) m->migrate(c);
                } else {
                    // isMigrationRequired: destructive fallbacks stand in for the missing path.
                    if (IsMigrationRequired(m_Options, version, wanted)) {
                        throw std::runtime_error("kiln: a migration from " + std::to_string(version) + " to " + std::to_string(wanted) +
                            " was required but not found. Add it to DatabaseOptions::migrations or allow destructive migrations with fallbackToDestructiveMigration.");
                    }
                    DropAllTables(c);
                    if (m_Options.onDestructiveMigration) m_Options.onDestructiveMigration(c);
                    if (m_Options.onCreate) m_Options.onCreate(c);
                }
                c.Execute("PRAGMA user_version = " + std::to_string(wanted));
            } catch (...) {
                try { c.Execute("ROLLBACK TRANSACTION"); } catch (...) {}
                throw;
            }
            c.Execute("END TRANSACTION");
        }
        if (m_Options.onOpen) m_Options.onOpen(c);
    }

    Database::ReaderLease::~ReaderLease() {
        if (!db || writerLock.owns_lock()) return;
        {
            std::lock_guard lock(db->m_ReadersMutex);
            db->m_FreeReaders.emplace_back(connection);
        }
        db->m_ReaderFree.notify_all();
    }

    Database::ReaderLease Database::AcquireReader() {
        // The schema exists once the write connection was configured; after that a file's
        // readers do not wait for the writer.
        const bool single = m_InMemory || m_Options.singleConnection;
        if (single || !m_Configured.load()) {
            std::unique_lock lock(m_WriterMutex);
            Connection& writer = WriterLocked();
            // A database in memory is one connection (each connection would be its own database);
            // a single-connection pool shares it too.
            if (single) {
                ReaderLease lease;
                lease.db = this;
                lease.connection = &writer;
                lease.writerLock = std::move(lock);
                return lease;
            }
        }
        std::unique_lock lock(m_ReadersMutex);
        if (m_ReadersClosed) throw Error(SQLITE_MISUSE, "database is closed");
        const int max = std::max(1, m_Options.maxReaders);
        // ConnectionPool's acquire timeout.
        if (!m_ReaderFree.wait_for(lock, std::chrono::milliseconds(std::max(1, m_Options.connectionPoolTimeoutMs)), [&] { return !m_FreeReaders.empty() || m_ReadersOpen < max; }))
            throw Error(SQLITE_BUSY, "Timed out attempting to acquire a reader connection.");
        ReaderLease lease;
        lease.db = this;
        if (!m_FreeReaders.empty()) {
            lease.connection = m_FreeReaders.back().release();
            m_FreeReaders.pop_back();
            return lease;
        }
        ++m_ReadersOpen;
        lock.unlock();
        try {
            // configurationConnection, and read-only as Room's reader connections are.
            auto c = std::make_unique<Connection>(Connection::Open(m_Path));
            ConfigureBusyTimeout(*c);
            ConfigureSynchronous(*c, m_Options.journalMode);
            c->Execute("PRAGMA query_only = 1");
            lease.connection = c.release();
        } catch (...) {
            lock.lock();
            --m_ReadersOpen;
            lock.unlock();
            m_ReaderFree.notify_all();
            lease.db = nullptr;
            throw;
        }
        return lease;
    }

    // syncTriggers: adds triggers for tables that got their first observer and drops those of
    // tables that lost their last.
    void Database::SyncTriggersLocked(Connection& c) {
        std::vector<std::pair<int, bool>> ops;   // table id, start tracking
        std::vector<std::string> names;
        {
            std::lock_guard lock(m_TrackMutex);
            if (!m_NeedsSync) return;
            m_NeedsSync = false;
            for (std::size_t i = 0; i < m_ObserverCount.size(); ++i) {
                const bool observed = m_ObserverCount[i] > 0;
                if (observed != m_Tracked[i]) {
                    m_Tracked[i] = observed;
                    ops.emplace_back(int(i), observed);
                }
            }
            names = m_TableNames;
        }
        if (ops.empty() || c.InTransaction()) return;
        c.WithTransaction(TransactionType::Immediate, [&] {
            for (const auto& [id, add] : ops) {
                const std::string& table = names[std::size_t(id)];
                if (add) c.Execute(std::string("INSERT OR IGNORE INTO ") + kLogTable + " VALUES(" + std::to_string(id) + ", 0)");
                for (const char* trigger : kTriggers) {
                    const std::string name = TriggerName(table, trigger);
                    if (add) {
                        c.Execute("CREATE TEMP TRIGGER IF NOT EXISTS `" + name + "` AFTER " + trigger + " ON `" + table + "` BEGIN UPDATE " + kLogTable +
                                  " SET invalidated = 1 WHERE table_id = " + std::to_string(id) + " AND invalidated = 0; END");
                    } else {
                        c.Execute("DROP TRIGGER IF EXISTS `" + name + "`");
                    }
                }
            }
        });
    }

    // notifyInvalidation: reads and clears the changed-table marks, then bumps those tables'
    // versions on the main thread, where the flows are.
    void Database::NotifyLocked(Connection& c) {
        if (c.InTransaction()) return;
        {
            std::lock_guard lock(m_TrackMutex);
            if (std::none_of(m_Tracked.begin(), m_Tracked.end(), [](bool t) { return t; })) return;
        }
        std::vector<int> ids;
        try {
            c.WithTransaction(TransactionType::Immediate, [&] {
                for (Statement s = c.Prepare(std::string("SELECT * FROM ") + kLogTable + " WHERE invalidated = 1"); s.Step();) ids.push_back(s.GetInt(0));
                if (!ids.empty()) c.Execute(std::string("UPDATE ") + kLogTable + " SET invalidated = 0 WHERE invalidated = 1");
            });
        } catch (const Error&) {
            return;
        }
        if (ids.empty()) return;
        m_Home->PostFromAnyThread([weak = weak_from_this(), ids = std::move(ids)] {
            auto self = weak.lock();
            if (!self) return;
            std::vector<int> versions = self->m_Versions.Value();
            for (int id : ids) {
                if (std::size_t(id) >= versions.size()) versions.resize(std::size_t(id) + 1, 0);
                ++versions[std::size_t(id)];
            }
            self->m_Versions.SetValue(std::move(versions));
        });
    }

    void Database::RefreshInvalidations() {
        rt::TaskDispatchers::Platform().IO().Execute([self = shared_from_this()] {
            try {
                std::lock_guard lock(self->m_WriterMutex);
                self->NotifyLocked(self->WriterLocked());
            } catch (const std::exception&) {
            }
        });
    }

    // validateTableNames, with ids given out as tables are first named (Room knows its tables
    // up front; here the schema is asked).
    std::vector<int> Database::ResolveTables(const std::vector<std::string>& tables) {
        std::vector<int> ids;
        ReaderLease lease = AcquireReader();
        for (const std::string& table : tables) {
            const std::string key = Lower(table);
            {
                std::lock_guard lock(m_TrackMutex);
                if (auto it = m_TableIds.find(key); it != m_TableIds.end()) {
                    ids.push_back(it->second);
                    continue;
                }
            }
            Statement s = lease.connection->Query("SELECT name FROM sqlite_master WHERE type = 'table' AND lower(name) = ?", key);
            if (!s.Step()) throw std::invalid_argument("There is no table with name " + table);
            const std::string name = s.GetText(0);
            std::lock_guard lock(m_TrackMutex);
            auto [it, added] = m_TableIds.emplace(key, int(m_TableNames.size()));
            if (added) {
                m_TableNames.push_back(name);
                m_ObserverCount.push_back(0);
                m_Tracked.push_back(false);
            }
            ids.push_back(it->second);
        }
        return ids;
    }

    // onObserverAdded / onObserverRemoved
    void Database::ObserversChanged(const std::vector<int>& ids, int delta) {
        std::lock_guard lock(m_TrackMutex);
        for (int id : ids) {
            long& count = m_ObserverCount[std::size_t(id)];
            const long before = count;
            count += delta;
            if ((before == 0) != (count == 0)) m_NeedsSync = true;
        }
    }

    namespace {
        // The observer count goes down when the collecting frame goes away, however it ends.
        struct ObserverGuard {
            kiln::Function<void()> release;
            ~ObserverGuard() { if (release) release(); }
        };
    }

    rt::Task<> Database::CollectInvalidations(std::vector<std::string> tables, bool emitInitialState, rt::FlowCollector<std::set<std::string>> emit) {
        auto self = shared_from_this();
        auto& io = rt::TaskDispatchers::Platform().IO();
        const std::vector<int> ids = co_await rt::WithContext(io, [self, tables] { return self->ResolveTables(tables); });
        self->ObserversChanged(ids, +1);
        ObserverGuard guard{ [self, ids] { self->ObserversChanged(ids, -1); } };
        // The triggers are in place before the first emission, so no change is missed after it.
        co_await rt::WithContext(io, [self] {
            std::lock_guard lock(self->m_WriterMutex);
            self->SyncTriggersLocked(self->WriterLocked());
        });
        auto versionOf = [](const std::vector<int>& v, int id) { return std::size_t(id) < v.size() ? v[std::size_t(id)] : 0; };
        std::optional<std::vector<int>> current;
        auto step = [&](const std::vector<int>& now) -> std::optional<std::set<std::string>> {
            std::optional<std::set<std::string>> out;
            if (!current) {
                if (emitInitialState) out = std::set<std::string>(tables.begin(), tables.end());
            } else {
                std::set<std::string> changed;
                for (std::size_t i = 0; i < ids.size(); ++i)
                    if (versionOf(*current, ids[i]) != versionOf(now, ids[i])) changed.insert(tables[i]);
                if (!changed.empty()) out = std::move(changed);
            }
            current = now;
            return out;
        };
        co_await self->m_Versions.AsFlow().Collect(rt::FlowCollector<std::vector<int>>([&](std::vector<int> now) -> rt::Task<> {
            if (auto out = step(now)) co_await emit(std::move(*out));
        }));
    }

    rt::Flow<std::set<std::string>> Database::Invalidations(std::vector<std::string> tables, bool emitInitialState) {
        return rt::Flow<std::set<std::string>>::Create([self = shared_from_this(), tables = std::move(tables), emitInitialState](rt::FlowCollector<std::set<std::string>> emit) -> rt::Task<> {
            co_await self->CollectInvalidations(tables, emitInitialState, std::move(emit));
        });
    }

}
