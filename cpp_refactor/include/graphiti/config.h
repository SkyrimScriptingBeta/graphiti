#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace graphiti {

struct LLMConfig {
    std::string api_key;
    std::string model = "gpt-4.1-mini";
    std::string small_model = "gpt-4.1-nano";  // used for dedup + resolution (cheaper/faster)
    std::string base_url = "https://api.openai.com";
    float temperature = 0.0f;  // deterministic — extraction needs precision, not creativity
    float repetition_penalty = -1.0f;  // Ollama: repeat_penalty (GRAPHITI_REPETITION_PENALTY, default off)
    float frequency_penalty = -1.0f;   // OpenAI: frequency_penalty (GRAPHITI_FREQUENCY_PENALTY, default off)
    bool stream = false;               // stream LLM responses to log_trace (GRAPHITI_STREAM=1)
    std::optional<bool> think;          // if set, sends "think": true/false in request body (GRAPHITI_THINK=0 or 1)
    bool no_think = false;              // if true, prepends /no_think to first user message (GRAPHITI_NO_THINK=1)
    int max_tokens = 16384;            // base output token budget (GRAPHITI_MAX_TOKENS)
    int max_output_tokens = 32768;     // hard ceiling — nothing goes above this (GRAPHITI_MAX_OUTPUT_TOKENS)
    float truncation_multiplier = 1.5f; // on JSON truncation, multiply budget by this (GRAPHITI_TRUNCATION_MULTIPLIER)
    int edge_shard_size = 0;            // 0 = no sharding; N = split entities into groups of N for edge extraction (GRAPHITI_EDGE_SHARD_SIZE)
    int max_edges = 0;                  // 0 = auto (1.5x entity count); N = hard cap per extraction call (GRAPHITI_MAX_EDGES)
    int extra_tokens_per_entity = 100;  // for edge extraction: add this many tokens per entity (GRAPHITI_EXTRA_TOKENS_PER_ENTITY)
    int entity_scaling_threshold = 20;  // start scaling after this many entities (GRAPHITI_ENTITY_SCALING_THRESHOLD)
};

struct EmbedderConfig {
    std::string api_key;
    std::string model = "text-embedding-3-small";
    std::string base_url = "https://api.openai.com";
    int embedding_dim = 1024;
};

struct GraphitiConfig {
    std::string db_path = ":memory:";
    LLMConfig llm;
    EmbedderConfig embedder;
    std::optional<std::string> default_group_id;
    bool store_raw_episode_content = true;
    std::string onnx_model_path;  // if set, used to create OnnxEmbedder
    bool read_only = false;       // if true, opens Kuzu in read-only mode (no write lock)
    int max_parallel_extractions = 4;  // max concurrent LLM calls in add_episode_bulk()
    std::string kuzu_writer_uri;  // e.g. "ws://127.0.0.1:9876" — when set, ALL writes go over WebSocket
                                   // to a centralized kuzu-writer-server daemon. Reads stay local.
    std::string kuzu_writer_target_db;  // e.g. "keel" — which DB on the writer server to target

    // Build config from environment variables:
    //   OPENAI_API_KEY        -> llm.api_key, embedder.api_key
    //   ANTHROPIC_API_KEY     -> llm.api_key (overrides OPENAI_API_KEY for LLM)
    //   OPENAI_BASE_URL       -> llm.base_url, embedder.base_url
    //   LLM_MODEL             -> llm.model (also sets small_model)
    //   LLM_SMALL_MODEL       -> llm.small_model (overrides LLM_MODEL for small)
    //   EMBEDDING_MODEL       -> embedder.model
    //   EMBEDDING_DIM         -> embedder.embedding_dim
    //   ONNX_MODEL_PATH       -> onnx_model_path
    //   GRAPHITI_DB_PATH      -> db_path
    //   GRAPHITI_GROUP_ID     -> default_group_id
    static GraphitiConfig from_env() {
        GraphitiConfig config;

        auto env = [](const char* name) -> std::string {
            auto* val = std::getenv(name);
            return (val && val[0]) ? std::string(val) : std::string{};
        };

        auto openai_key = env("OPENAI_API_KEY");
        if (!openai_key.empty()) {
            config.llm.api_key = openai_key;
            config.embedder.api_key = openai_key;
        }

        auto anthropic_key = env("ANTHROPIC_API_KEY");
        if (!anthropic_key.empty()) {
            config.llm.api_key = anthropic_key;
        }

        auto base_url = env("OPENAI_BASE_URL");
        if (!base_url.empty()) {
            config.llm.base_url = base_url;
            config.embedder.base_url = base_url;
        }

        auto llm_model = env("LLM_MODEL");
        if (!llm_model.empty()) {
            config.llm.model = llm_model;
            config.llm.small_model = llm_model;
        }

        auto small_model = env("LLM_SMALL_MODEL");
        if (!small_model.empty()) {
            config.llm.small_model = small_model;
        }

        auto embed_model = env("EMBEDDING_MODEL");
        if (!embed_model.empty()) {
            config.embedder.model = embed_model;
        }

        auto embed_dim = env("EMBEDDING_DIM");
        if (!embed_dim.empty()) {
            config.embedder.embedding_dim = std::stoi(embed_dim);
        }

        auto onnx_path = env("ONNX_MODEL_PATH");
        if (!onnx_path.empty()) {
            config.onnx_model_path = onnx_path;
        }

        auto db_path = env("GRAPHITI_DB_PATH");
        if (!db_path.empty()) {
            config.db_path = db_path;
        }

        auto group_id = env("GRAPHITI_GROUP_ID");
        if (!group_id.empty()) {
            config.default_group_id = group_id;
        }

        auto temperature = env("GRAPHITI_TEMPERATURE");
        if (!temperature.empty()) {
            config.llm.temperature = std::stof(temperature);
        }

        auto rep_penalty = env("GRAPHITI_REPETITION_PENALTY");
        if (!rep_penalty.empty()) {
            config.llm.repetition_penalty = std::stof(rep_penalty);
        }

        auto freq_penalty = env("GRAPHITI_FREQUENCY_PENALTY");
        if (!freq_penalty.empty()) {
            config.llm.frequency_penalty = std::stof(freq_penalty);
        }

        auto stream = env("GRAPHITI_STREAM");
        if (stream == "1" || stream == "true") {
            config.llm.stream = true;
        }

        auto no_think = env("GRAPHITI_NO_THINK");
        if (no_think == "1" || no_think == "true") {
            config.llm.no_think = true;
        }

        auto think = env("GRAPHITI_THINK");
        if (think == "1" || think == "true") {
            config.llm.think = true;
        } else if (think == "0" || think == "false") {
            config.llm.think = false;
        }

        auto max_tokens = env("GRAPHITI_MAX_TOKENS");
        if (!max_tokens.empty()) {
            config.llm.max_tokens = std::max(256, std::stoi(max_tokens));
        }

        auto max_output_tokens = env("GRAPHITI_MAX_OUTPUT_TOKENS");
        if (!max_output_tokens.empty()) {
            config.llm.max_output_tokens = std::max(config.llm.max_tokens, std::stoi(max_output_tokens));
        }

        auto trunc_mult = env("GRAPHITI_TRUNCATION_MULTIPLIER");
        if (!trunc_mult.empty()) {
            config.llm.truncation_multiplier = std::max(1.1f, std::stof(trunc_mult));
        }

        auto max_edges = env("GRAPHITI_MAX_EDGES");
        if (!max_edges.empty()) {
            config.llm.max_edges = std::max(0, std::stoi(max_edges));
        }

        auto edge_shard = env("GRAPHITI_EDGE_SHARD_SIZE");
        if (!edge_shard.empty()) {
            config.llm.edge_shard_size = std::max(0, std::stoi(edge_shard));
        }

        auto extra_per_entity = env("GRAPHITI_EXTRA_TOKENS_PER_ENTITY");
        if (!extra_per_entity.empty()) {
            config.llm.extra_tokens_per_entity = std::max(0, std::stoi(extra_per_entity));
        }

        auto entity_threshold = env("GRAPHITI_ENTITY_SCALING_THRESHOLD");
        if (!entity_threshold.empty()) {
            config.llm.entity_scaling_threshold = std::max(1, std::stoi(entity_threshold));
        }

        auto max_parallel = env("GRAPHITI_MAX_PARALLEL");
        if (!max_parallel.empty()) {
            config.max_parallel_extractions = std::max(1, std::stoi(max_parallel));
        }

        auto writer_uri = env("KUZU_WRITER_URI");
        if (!writer_uri.empty()) {
            config.kuzu_writer_uri = writer_uri;
        }

        auto writer_target = env("KUZU_WRITER_TARGET_DB");
        if (!writer_target.empty()) {
            config.kuzu_writer_target_db = writer_target;
        }

        return config;
    }
};

} // namespace graphiti
