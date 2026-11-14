#include "kiln/sqlite/SQLite.h"

#include "sqlite3.h"

namespace kiln::sqlite {

    namespace {
        // throwSQLiteException
        std::string MessageOf(int code, const char* message) {
            std::string s = "Error code: " + std::to_string(code);
            if (message) s += std::string(", message: ") + message;
            return s;
        }
        [[noreturn]] void Throw(int code, const char* message) { throw Error(code, message); }
        void Check(sqlite3_stmt* stmt, int rc) {
            if (rc != SQLITE_OK) Throw(rc, sqlite3_errmsg(sqlite3_db_handle(stmt)));
        }
        void RequireOpen(sqlite3_stmt* stmt) {
            if (!stmt) Throw(SQLITE_MISUSE, "statement is closed");
        }
    }

    Error::Error(int c, const char* message) : std::runtime_error(MessageOf(c, message)), code(c) {}

    void Statement::BindBlob(int index, std::span<const std::uint8_t> value) {
        RequireOpen(m_Stmt);
        // A zero-length blob, not NULL, for an empty span.
        Check(m_Stmt, sqlite3_bind_blob(m_Stmt, index, value.empty() ? static_cast<const void*>("") : value.data(), int(value.size()), SQLITE_TRANSIENT));
    }
    void Statement::BindDouble(int index, double value) {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_bind_double(m_Stmt, index, value));
    }
    void Statement::BindLong(int index, std::int64_t value) {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_bind_int64(m_Stmt, index, value));
    }
    void Statement::BindText(int index, std::string_view value) {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_bind_text(m_Stmt, index, value.data() ? value.data() : "", int(value.size()), SQLITE_TRANSIENT));
    }
    void Statement::BindNull(int index) {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_bind_null(m_Stmt, index));
    }

    bool Statement::Step() {
        RequireOpen(m_Stmt);
        const int rc = sqlite3_step(m_Stmt);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        Throw(rc, sqlite3_errmsg(sqlite3_db_handle(m_Stmt)));
    }

    // throwIfNoRow, throwIfInvalidColumn
    void Statement::CheckRow(int index) const {
        RequireOpen(m_Stmt);
        if (sqlite3_stmt_busy(m_Stmt) == 0) Throw(SQLITE_MISUSE, "no row");
        if (index < 0 || index >= sqlite3_column_count(m_Stmt)) Throw(SQLITE_RANGE, "column index out of range");
    }
    std::vector<std::uint8_t> Statement::GetBlob(int index) const {
        CheckRow(index);
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(m_Stmt, index));
        const int size = sqlite3_column_bytes(m_Stmt, index);
        if ((!blob || size == 0) && sqlite3_errcode(sqlite3_db_handle(m_Stmt)) == SQLITE_NOMEM) throw std::bad_alloc();
        return blob && size > 0 ? std::vector<std::uint8_t>(blob, blob + size) : std::vector<std::uint8_t>();
    }
    double Statement::GetDouble(int index) const {
        CheckRow(index);
        return sqlite3_column_double(m_Stmt, index);
    }
    std::int64_t Statement::GetLong(int index) const {
        CheckRow(index);
        return sqlite3_column_int64(m_Stmt, index);
    }
    std::string Statement::GetText(int index) const {
        CheckRow(index);
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(m_Stmt, index));
        const int size = sqlite3_column_bytes(m_Stmt, index);
        if ((!text || size == 0) && sqlite3_errcode(sqlite3_db_handle(m_Stmt)) == SQLITE_NOMEM) throw std::bad_alloc();
        return text && size > 0 ? std::string(text, std::size_t(size)) : std::string();
    }
    int Statement::ColumnCount() const {
        RequireOpen(m_Stmt);
        return sqlite3_column_count(m_Stmt);
    }
    std::string Statement::ColumnName(int index) const {
        RequireOpen(m_Stmt);
        if (index < 0 || index >= sqlite3_column_count(m_Stmt)) Throw(SQLITE_RANGE, "column index out of range");
        const char* name = sqlite3_column_name(m_Stmt, index);
        if (!name) throw std::bad_alloc();
        return name;
    }
    std::vector<std::string> Statement::ColumnNames() const {
        std::vector<std::string> out;
        for (int i = 0, n = ColumnCount(); i < n; ++i) out.push_back(ColumnName(i));
        return out;
    }
    DataType Statement::ColumnType(int index) const {
        CheckRow(index);
        return DataType(sqlite3_column_type(m_Stmt, index));
    }
    void Statement::Reset() {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_reset(m_Stmt));
    }
    void Statement::ClearBindings() {
        RequireOpen(m_Stmt);
        Check(m_Stmt, sqlite3_clear_bindings(m_Stmt));
    }
    void Statement::Close() {
        if (m_Stmt) sqlite3_finalize(m_Stmt);
        m_Stmt = nullptr;
    }

    Connection Connection::Open(const std::string& path) { return Open(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE); }

    Connection Connection::Open(const std::string& path, int openFlags) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &db, openFlags, nullptr);
        if (rc != SQLITE_OK) {
            const std::string message = db ? sqlite3_errmsg(db) : "";
            sqlite3_close_v2(db);
            Throw(rc, message.empty() ? nullptr : message.c_str());
        }
        rc = sqlite3_extended_result_codes(db, 1);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            Throw(rc, nullptr);
        }
        Connection c;
        c.m_Db = db;
        return c;
    }

    Statement Connection::Prepare(std::string_view sql) {
        if (!m_Db) Throw(SQLITE_MISUSE, "connection is closed");
        sqlite3_stmt* stmt = nullptr;
        const int rc = sqlite3_prepare_v2(m_Db, sql.data(), int(sql.size()), &stmt, nullptr);
        if (rc != SQLITE_OK) Throw(rc, sqlite3_errmsg(m_Db));
        return Statement(stmt);
    }

    void Connection::ExecuteScript(const std::string& sql) {
        if (!m_Db) Throw(SQLITE_MISUSE, "connection is closed");
        char* error = nullptr;
        const int rc = sqlite3_exec(m_Db, sql.c_str(), nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error ? error : "";
            sqlite3_free(error);
            Throw(rc, message.c_str());
        }
    }

    bool Connection::InTransaction() const { return m_Db && sqlite3_get_autocommit(m_Db) == 0; }
    std::int64_t Connection::LastInsertRowId() const { return m_Db ? sqlite3_last_insert_rowid(m_Db) : 0; }
    int Connection::Changes() const { return m_Db ? sqlite3_changes(m_Db) : 0; }

    // PooledConnectionImpl's transaction: BEGIN for the first level, a savepoint for nested ones.
    void Connection::Begin(TransactionType type) {
        const bool savepoint = InTransaction();
        if (!savepoint) {
            switch (type) {
                case TransactionType::Deferred: Execute("BEGIN DEFERRED TRANSACTION"); break;
                case TransactionType::Immediate: Execute("BEGIN IMMEDIATE TRANSACTION"); break;
                case TransactionType::Exclusive: Execute("BEGIN EXCLUSIVE TRANSACTION"); break;
            }
        } else {
            Execute("SAVEPOINT '" + std::to_string(m_Levels.size()) + "'");
        }
        m_Levels.push_back(savepoint);
    }

    void Connection::End(bool success) {
        const bool savepoint = m_Levels.back();
        m_Levels.pop_back();
        const std::string name = "'" + std::to_string(m_Levels.size()) + "'";
        if (!savepoint) {
            Execute(success ? "END TRANSACTION" : "ROLLBACK TRANSACTION");
        } else if (success) {
            Execute("RELEASE SAVEPOINT " + name);
        } else {
            Execute("ROLLBACK TRANSACTION TO SAVEPOINT " + name);
            Execute("RELEASE SAVEPOINT " + name);
        }
    }

    void Connection::Close() {
        if (m_Db) sqlite3_close_v2(m_Db);
        m_Db = nullptr;
        m_Levels.clear();
    }

}
