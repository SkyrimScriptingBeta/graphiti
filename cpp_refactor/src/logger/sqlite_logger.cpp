#include <graphiti/sqlite_logger.h>

#define SQLITE_CORE
#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace graphiti {

static const char* SCHEMA = R"(
CREATE TABLE IF NOT EXISTS llm_calls (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now', 'localtime')),
    model           TEXT,
    prompt_name     TEXT,
    input_tokens    INTEGER DEFAULT 0,
    output_tokens   INTEGER DEFAULT 0,
    latency_ms      REAL DEFAULT 0,
    success         INTEGER DEFAULT 1,
    attempt         INTEGER DEFAULT 1,
    max_attempts    INTEGER DEFAULT 1,
    error_code      TEXT,
    error_message   TEXT,
    request_messages TEXT,
    response_body   TEXT
);

CREATE TABLE IF NOT EXISTS embedding_calls (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now', 'localtime')),
    model           TEXT,
    input_count     INTEGER DEFAULT 1,
    dimensions      INTEGER DEFAULT 0,
    latency_ms      REAL DEFAULT 0,
    success         INTEGER DEFAULT 1,
    error_message   TEXT
);

CREATE TABLE IF NOT EXISTS pipeline_steps (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now', 'localtime')),
    step_name       TEXT,
    latency_ms      REAL DEFAULT 0,
    success         INTEGER DEFAULT 1,
    error_message   TEXT,
    items_in        INTEGER DEFAULT 0,
    items_out       INTEGER DEFAULT 0
);
)";

SqliteGraphitiLogger::SqliteGraphitiLogger(const std::string& db_path)
    : db_path_(db_path) {}

SqliteGraphitiLogger::~SqliteGraphitiLogger() {
    if (db_) sqlite3_close(db_);
}

void SqliteGraphitiLogger::ensure_open() {
    if (db_) return;
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        auto msg = std::string("Failed to open graphiti log DB: ") + sqlite3_errmsg(db_);
        db_ = nullptr;
        throw std::runtime_error(msg);
    }
    // WAL mode for concurrent reads
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    // Create tables
    char* err = nullptr;
    rc = sqlite3_exec(db_, SCHEMA, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        auto msg = std::string("Failed to create graphiti log tables: ") + (err ? err : "unknown");
        if (err) sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void SqliteGraphitiLogger::on_llm_call(const LLMCallInfo& info) {
    std::lock_guard lock(mu_);
    try {
        ensure_open();
    } catch (...) {
        return;  // Can't log, but don't crash Graphiti
    }

    static const char* SQL =
        "INSERT INTO llm_calls (model, prompt_name, input_tokens, output_tokens, "
        "latency_ms, success, attempt, max_attempts, error_code, error_message, "
        "request_messages, response_body) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, info.model.data(), (int)info.model.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.prompt_name.data(), (int)info.prompt_name.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, info.input_tokens);
    sqlite3_bind_int64(stmt, 4, info.output_tokens);
    sqlite3_bind_double(stmt, 5, info.latency_ms);
    sqlite3_bind_int(stmt, 6, info.success ? 1 : 0);
    sqlite3_bind_int(stmt, 7, info.attempt);
    sqlite3_bind_int(stmt, 8, info.max_attempts);
    if (!info.error_code.empty())
        sqlite3_bind_text(stmt, 9, info.error_code.data(), (int)info.error_code.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 9);
    if (!info.error_message.empty())
        sqlite3_bind_text(stmt, 10, info.error_message.data(), (int)info.error_message.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 10);

    // Serialize request messages as JSON array
    if (!info.request_messages.empty()) {
        nlohmann::json msgs = nlohmann::json::array();
        for (auto& m : info.request_messages)
            msgs.push_back({{"role", m.role}, {"content", m.content}});
        auto s = msgs.dump();
        sqlite3_bind_text(stmt, 11, s.data(), (int)s.size(), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 11);
    }

    // Response body
    if (!info.response_body.empty())
        sqlite3_bind_text(stmt, 12, info.response_body.data(), (int)info.response_body.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 12);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SqliteGraphitiLogger::on_embedding_call(const EmbeddingCallInfo& info) {
    std::lock_guard lock(mu_);
    try {
        ensure_open();
    } catch (...) {
        return;
    }

    static const char* SQL =
        "INSERT INTO embedding_calls (model, input_count, dimensions, latency_ms, success, error_message) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, info.model.data(), (int)info.model.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, info.input_count);
    sqlite3_bind_int(stmt, 3, info.dimensions);
    sqlite3_bind_double(stmt, 4, info.latency_ms);
    sqlite3_bind_int(stmt, 5, info.success ? 1 : 0);
    if (!info.error_message.empty())
        sqlite3_bind_text(stmt, 6, info.error_message.data(), (int)info.error_message.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 6);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SqliteGraphitiLogger::on_pipeline_step(const PipelineStepInfo& info) {
    std::lock_guard lock(mu_);
    try {
        ensure_open();
    } catch (...) {
        return;
    }

    static const char* SQL =
        "INSERT INTO pipeline_steps (step_name, latency_ms, success, error_message, items_in, items_out) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, info.step_name.data(), (int)info.step_name.size(), SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, info.latency_ms);
    sqlite3_bind_int(stmt, 3, info.success ? 1 : 0);
    if (!info.error_message.empty())
        sqlite3_bind_text(stmt, 4, info.error_message.data(), (int)info.error_message.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, info.items_in);
    sqlite3_bind_int(stmt, 6, info.items_out);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace graphiti
