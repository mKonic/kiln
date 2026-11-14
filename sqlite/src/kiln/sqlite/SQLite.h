#pragma once

// SQLite connections and statements (androidx.sqlite: SQLiteConnection, SQLiteStatement, and
// sqlite-bundled's driver over the SQLite kiln carries in vendor/sqlite). These calls block:
// use them on a background thread, or through sqlite::Database (Database.h), which runs them
// on the IO pool. A connection and its statements are used by one thread at a time.
//
//   auto c = sqlite::Connection::Open(ui::DatabasePath("notes.db"));
//   c.Execute("CREATE TABLE IF NOT EXISTS note (id INTEGER PRIMARY KEY, title TEXT NOT NULL)");
//   c.Execute("INSERT INTO note (title) VALUES (?)", "Milk");
//   auto q = c.Query("SELECT id, title FROM note WHERE id > ?", 0);
//   while (q.Step()) Use(q.GetLong(0), q.GetText(1));

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace kiln::sqlite {

    // SQLiteException: what() is "Error code: N, message: ..." as androidx formats it; code is
    // SQLite's extended result code.
    class Error : public std::runtime_error {
    public:
        Error(int code, const char* message);
        int code;
    };

    // SQLITE_DATA_INTEGER ... SQLITE_DATA_NULL
    enum class DataType { Integer = 1, Float = 2, Text = 3, Blob = 4, Null = 5 };

    class Statement {
    public:
        Statement() = default;
        Statement(Statement&& o) noexcept : m_Stmt(std::exchange(o.m_Stmt, nullptr)) {}
        Statement& operator=(Statement&& o) noexcept { if (this != &o) { Close(); m_Stmt = std::exchange(o.m_Stmt, nullptr); } return *this; }
        ~Statement() { Close(); }

        // Parameters, by 1-based index.
        void BindBlob(int index, std::span<const std::uint8_t> value);
        void BindDouble(int index, double value);
        void BindFloat(int index, float value) { BindDouble(index, double(value)); }
        void BindLong(int index, std::int64_t value);
        void BindInt(int index, int value) { BindLong(index, value); }
        void BindBool(int index, bool value) { BindLong(index, value ? 1 : 0); }
        void BindText(int index, std::string_view value);
        void BindNull(int index);
        // The Bind* for the value's type (nullptr binds NULL).
        void Bind(int index, std::nullptr_t) { BindNull(index); }
        void Bind(int index, bool v) { BindBool(index, v); }
        void Bind(int index, double v) { BindDouble(index, v); }
        void Bind(int index, float v) { BindFloat(index, v); }
        void Bind(int index, std::string_view v) { BindText(index, v); }
        void Bind(int index, const char* v) { v ? BindText(index, v) : BindNull(index); }
        void Bind(int index, const std::string& v) { BindText(index, v); }
        void Bind(int index, std::span<const std::uint8_t> v) { BindBlob(index, v); }
        void Bind(int index, const std::vector<std::uint8_t>& v) { BindBlob(index, v); }
        template<typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
        void Bind(int index, T v) { BindLong(index, std::int64_t(v)); }
        // Binds args to parameters 1, 2, ...
        template<typename... A> Statement& BindAll(const A&... args) {
            int i = 0;
            (Bind(++i, args), ...);
            return *this;
        }

        // Runs to the next row: true when there is one, false when the statement is done.
        bool Step();

        // The current row's columns, by 0-based index.
        std::vector<std::uint8_t> GetBlob(int index) const;
        double GetDouble(int index) const;
        float GetFloat(int index) const { return float(GetDouble(index)); }
        std::int64_t GetLong(int index) const;
        int GetInt(int index) const { return int(GetLong(index)); }
        bool GetBool(int index) const { return GetLong(index) != 0; }
        std::string GetText(int index) const;
        bool IsNull(int index) const { return ColumnType(index) == DataType::Null; }
        int ColumnCount() const;
        std::string ColumnName(int index) const;
        std::vector<std::string> ColumnNames() const;
        DataType ColumnType(int index) const;

        // Back to the start to run again; bindings are kept.
        void Reset();
        void ClearBindings();
        void Close();
        sqlite3_stmt* Handle() const { return m_Stmt; }

    private:
        friend class Connection;
        explicit Statement(sqlite3_stmt* s) : m_Stmt(s) {}
        void CheckRow(int index) const;
        sqlite3_stmt* m_Stmt = nullptr;
    };

    // SQLiteTransactionType
    enum class TransactionType { Deferred, Immediate, Exclusive };

    class Connection {
    public:
        // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE by default; ":memory:" for a database in
        // memory.
        static Connection Open(const std::string& path);
        static Connection Open(const std::string& path, int openFlags);
        Connection() = default;
        Connection(Connection&& o) noexcept : m_Db(std::exchange(o.m_Db, nullptr)), m_Levels(std::move(o.m_Levels)) {}
        Connection& operator=(Connection&& o) noexcept { if (this != &o) { Close(); m_Db = std::exchange(o.m_Db, nullptr); m_Levels = std::move(o.m_Levels); } return *this; }
        ~Connection() { Close(); }

        Statement Prepare(std::string_view sql);
        // Prepare with args bound.
        template<typename... A> Statement Query(std::string_view sql, const A&... args) {
            Statement s = Prepare(sql);
            s.BindAll(args...);
            return s;
        }
        // Runs one statement to completion (executeSQL), with args bound.
        template<typename... A> void Execute(std::string_view sql, const A&... args) {
            Statement s = Prepare(sql);
            s.BindAll(args...);
            s.Step();
        }
        // Runs every statement of a script (a schema).
        void ExecuteScript(const std::string& sql);

        bool InTransaction() const;
        std::int64_t LastInsertRowId() const;
        // Rows changed by the last INSERT, UPDATE or DELETE.
        int Changes() const;

        // Runs fn in a transaction: committed when it returns, rolled back when it throws. Inside
        // another transaction it is a savepoint, so it rolls back alone.
        template<typename F> auto WithTransaction(TransactionType type, F&& fn) -> std::invoke_result_t<F> {
            Begin(type);
            try {
                if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
                    fn();
                    End(true);
                } else {
                    auto r = fn();
                    End(true);
                    return r;
                }
            } catch (...) {
                try { End(false); } catch (...) {}
                throw;
            }
        }
        template<typename F> auto WithTransaction(F&& fn) { return WithTransaction(TransactionType::Deferred, std::forward<F>(fn)); }

        void Close();
        bool IsOpen() const { return m_Db != nullptr; }
        // The sqlite3 handle, for what this class does not cover (sqlite3.h is on the include path).
        sqlite3* Handle() const { return m_Db; }

    private:
        void Begin(TransactionType type);
        void End(bool success);
        sqlite3* m_Db = nullptr;
        std::vector<bool> m_Levels;   // open WithTransaction levels: whether each is a savepoint
    };

}
