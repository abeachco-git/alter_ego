#pragma once
// SQLite connection, statement and transaction ownership. No Nexus or UI calls.
// One owning thread per connection; do not share a statement across threads.
// Statements and transactions must be destroyed before their connection.
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace AlterEgo::Database {

class Error : public std::runtime_error {
public:
    int code;
    Error(int result, std::string message)
        : std::runtime_error(std::move(message)), code(result) {}
};

inline void Check(sqlite3* db, int rc, const char* operation) {
    if (rc != SQLITE_OK)
        throw Error(rc, std::string(operation) + ": " +
            (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc)));
}

class Statement {
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            if (stmt_) sqlite3_finalize(stmt_);
            Check(db_, rc, "prepare");
        }
        if (!stmt_) throw Error(SQLITE_MISUSE, "Empty SQL statement");
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    void Bind(int index, std::int64_t value) {
        Check(db_, sqlite3_bind_int64(stmt_, index, value), "bind integer");
    }
    void Bind(int index, const std::string& value) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw Error(SQLITE_TOOBIG, "Text binding exceeds size limit");
        Check(db_, sqlite3_bind_text(stmt_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT), "bind text");
    }
    void BindNull(int index) {
        Check(db_, sqlite3_bind_null(stmt_, index), "bind null");
    }
    bool Step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        Check(db_, rc, "step");
        return false;
    }
    void Execute() {
        if (Step()) throw Error(SQLITE_MISUSE, "Expected no result rows");
    }
    void Reset() {
        Check(db_, sqlite3_reset(stmt_), "reset");
        Check(db_, sqlite3_clear_bindings(stmt_), "clear bindings");
    }
    bool IsNull(int column) const {
        return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
    }
    std::int64_t Integer(int column) const {
        return sqlite3_column_int64(stmt_, column);
    }
    std::string Text(int column) const {
        const auto* value = sqlite3_column_text(stmt_, column);
        const int size = sqlite3_column_bytes(stmt_, column);
        if (!value) {
            if (!IsNull(column)) throw Error(SQLITE_NOMEM, "Read text");
            return {};
        }
        return std::string(reinterpret_cast<const char*>(value),
                           static_cast<std::size_t>(size));
    }
};

class Connection {
    sqlite3* db_ = nullptr;
public:
    enum class OpenMode { Existing, Create };
    explicit Connection(const std::string& utf8Path, OpenMode mode) {
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX |
            (mode == OpenMode::Create ? SQLITE_OPEN_CREATE : 0);
        const int rc = sqlite3_open_v2(utf8Path.c_str(), &db_, flags, nullptr);
        try {
            Check(db_, rc, "open");
            Check(db_, sqlite3_extended_result_codes(db_, 1), "extended errors");
            Check(db_, sqlite3_busy_timeout(db_, 1500), "busy timeout");
            Check(db_, sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr),
                  "defensive mode");
            Exec("PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF;");
            if (Scalar("PRAGMA foreign_keys") != 1)
                throw Error(SQLITE_ERROR, "Foreign key enforcement unavailable");
            // Durable user data: rollback journal avoids a persistent WAL
            // sidecar; FULL synchronization favors recovery over throughput.
            {
                auto journal = Prepare("PRAGMA journal_mode=DELETE");
                if (!journal.Step() || journal.Text(0) != "delete")
                    throw Error(SQLITE_ERROR, "Rollback journal unavailable");
            }
            Exec("PRAGMA synchronous=FULL");
        } catch (...) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
            throw;
        }
    }
    ~Connection() { sqlite3_close_v2(db_); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    Statement Prepare(const char* sql) { return Statement(db_, sql); }
    void Exec(const char* sql) {
        char* error = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            try {
                std::string message = error ? error : sqlite3_errmsg(db_);
                sqlite3_free(error);
                error = nullptr;
                throw Error(rc, "SQL: " + message);
            } catch (...) {
                sqlite3_free(error);
                throw;
            }
        }
        sqlite3_free(error);
    }
    std::int64_t Scalar(const char* sql) {
        auto query = Prepare(sql);
        if (!query.Step() || query.IsNull(0))
            throw Error(SQLITE_ERROR, "Missing scalar result");
        const auto result = query.Integer(0);
        if (query.Step()) throw Error(SQLITE_ERROR, "Multiple scalar results");
        return result;
    }
    void Verify() {
        auto check = Prepare("PRAGMA integrity_check");
        if (!check.Step() || check.Text(0) != "ok" || check.Step())
            throw Error(SQLITE_CORRUPT, "Database integrity check failed");
        auto foreign = Prepare("PRAGMA foreign_key_check");
        if (foreign.Step())
            throw Error(SQLITE_CONSTRAINT_FOREIGNKEY, "Database foreign key check failed");
    }
    // The caller owns an unpublished, fresh destination. Publish the backup
    // only after successful verification; never overwrite the active file.
    void BackupTo(Connection& destination) {
        if (&destination == this ||
            destination.Scalar("SELECT count(*) FROM sqlite_schema") != 0)
            throw Error(SQLITE_MISUSE, "Backup requires a distinct empty destination");
        auto* backup = sqlite3_backup_init(destination.db_, "main", db_, "main");
        if (!backup) {
            const int rc = sqlite3_errcode(destination.db_);
            throw Error(rc == SQLITE_OK ? SQLITE_ERROR : rc, "Cannot initialize backup");
        }
        const int step = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if (step != SQLITE_DONE) Check(destination.db_, step, "backup step");
        Check(destination.db_, finish, "backup finish");
        destination.Verify();
    }
};

class Transaction {
    Connection& db_;
    bool active_ = true;
public:
    explicit Transaction(Connection& db) : db_(db) { db_.Exec("BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (active_) {
            try { db_.Exec("ROLLBACK"); } catch (...) {}
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void Commit() {
        if (!active_) throw Error(SQLITE_MISUSE, "Transaction already committed");
        db_.Exec("COMMIT");
        active_ = false;
    }
};

// Each migration callback owns exactly one schema transition. Application ID
// prevents an unrelated or uninitialized nonempty DB from being adopted.
template<class Upgrade>
void Migrate(Connection& db, int applicationId, int target, Upgrade upgrade) {
    if (applicationId <= 0 || target <= 0)
        throw Error(SQLITE_MISUSE, "Invalid schema identity/version");
    Transaction tx(db);
    const auto id = db.Scalar("PRAGMA application_id");
    auto version = db.Scalar("PRAGMA user_version");
    if (version < 0 || version > target ||
        (id != applicationId && (id != 0 || version != 0 ||
           db.Scalar("SELECT count(*) FROM sqlite_schema") != 0)))
        throw Error(SQLITE_NOTADB, "Unexpected database identity or schema version");
    if (id == 0)
        db.Exec(("PRAGMA application_id=" + std::to_string(applicationId)).c_str());
    while (version < target) {
        upgrade(db, static_cast<int>(version + 1));
        ++version;
        db.Exec(("PRAGMA user_version=" + std::to_string(version)).c_str());
    }
    db.Verify();
    tx.Commit();
}

} // namespace AlterEgo::Database
