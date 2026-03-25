#pragma once

#include <graphiti/logger.h>

#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace graphiti {

// GraphitiLogger implementation that writes all calls to a SQLite database.
// Thread-safe. Creates the DB and tables lazily on first log call.
//
// Usage:
//   auto logger = std::make_unique<SqliteGraphitiLogger>("/path/to/graphiti_log.db");
//   graphiti.add_logger(std::move(logger));
class SqliteGraphitiLogger : public GraphitiLogger {
public:
    explicit SqliteGraphitiLogger(const std::string& db_path);
    ~SqliteGraphitiLogger();

    SqliteGraphitiLogger(const SqliteGraphitiLogger&) = delete;
    SqliteGraphitiLogger& operator=(const SqliteGraphitiLogger&) = delete;

    void on_llm_call(const LLMCallInfo& info) override;
    void on_embedding_call(const EmbeddingCallInfo& info) override;
    void on_pipeline_step(const PipelineStepInfo& info) override;

private:
    void ensure_open();

    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mu_;
};

} // namespace graphiti
