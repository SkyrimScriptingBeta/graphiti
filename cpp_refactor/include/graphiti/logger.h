#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace graphiti {

// Pure abstract interface for observability logging.
// Graphiti calls these methods at key points in the pipeline.
// Implement this interface to log to SQLite, a file, stdout, etc.
//
// Usage:
//   graphiti.add_logger(std::make_unique<MyLogger>());
//   graphiti.add_logger(std::make_unique<AnotherLogger>());  // multiple ok
class GraphitiLogger {
public:
    virtual ~GraphitiLogger() = default;

    // A single message in an LLM conversation (role + content).
    struct LogMessage {
        std::string role;     // "system", "user", "assistant"
        std::string content;
    };

    // Called after every LLM call (success or failure).
    struct LLMCallInfo {
        std::string_view model;          // e.g. "gpt-4.1-nano"
        std::string_view prompt_name;    // e.g. "extract_entities", "dedupe_nodes"
        int64_t input_tokens  = 0;
        int64_t output_tokens = 0;
        double  latency_ms    = 0.0;
        bool    success       = true;
        std::string_view error_code;     // e.g. "llm_rate_limit", "llm_parse_error"
        std::string_view error_message;
        int     attempt       = 1;       // which retry attempt (1 = first try)
        std::string started_at;          // ISO8601 wall-clock time when call started
        // Full request and response content
        std::vector<LogMessage> request_messages;  // the full prompt as sent to the LLM
        std::string response_body;                 // raw JSON response from the LLM
    };
    // Called BEFORE an LLM call starts (request only, no response yet).
    // Returns a row ID that on_llm_call_end uses to UPDATE the same row.
    virtual int64_t on_llm_call_start(const LLMCallInfo& info) { (void)info; return 0; }

    // Called AFTER an LLM call completes (fills in response, success, tokens, latency).
    // row_id comes from on_llm_call_start. If 0, inserts a new row instead.
    virtual void on_llm_call_end(int64_t row_id, const LLMCallInfo& info) = 0;

    // Legacy: called after every LLM call. Default impl delegates to on_llm_call_end with row_id=0.
    virtual void on_llm_call(const LLMCallInfo& info) { on_llm_call_end(0, info); }

    // Called after every embedding call (success or failure).
    struct EmbeddingCallInfo {
        std::string_view model;          // e.g. "text-embedding-3-small"
        int         input_count  = 1;    // batch size (1 for single, N for batch)
        int         dimensions   = 0;    // embedding dimensions
        double      latency_ms   = 0.0;
        bool        success      = true;
        std::string_view error_message;
    };
    virtual void on_embedding_call(const EmbeddingCallInfo& info) = 0;

    // Called when a pipeline step completes.
    struct PipelineStepInfo {
        std::string_view step_name;      // e.g. "extract_nodes", "dedupe_edges", "enrich_summaries"
        double      latency_ms = 0.0;
        bool        success    = true;
        std::string_view error_message;
        int         items_in   = 0;      // items entering the step
        int         items_out  = 0;      // items produced by the step
    };
    virtual void on_pipeline_step(const PipelineStepInfo& info) = 0;
};

} // namespace graphiti
