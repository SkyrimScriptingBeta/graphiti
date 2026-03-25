#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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

    // Called after every LLM call (success or failure).
    struct LLMCallInfo {
        std::string_view model;          // e.g. "gpt-4.1-nano"
        std::string_view prompt_name;    // e.g. "EXTRACTED_ENTITIES", "SUMMARIZED_ENTITIES"
        int64_t input_tokens  = 0;
        int64_t output_tokens = 0;
        double  latency_ms    = 0.0;
        bool    success       = true;
        std::string_view error_code;     // e.g. "llm_rate_limit", "llm_parse_error"
        std::string_view error_message;
        int     attempt       = 1;       // which retry attempt (1 = first try)
        int     max_attempts  = 1;
    };
    virtual void on_llm_call(const LLMCallInfo& info) = 0;

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
