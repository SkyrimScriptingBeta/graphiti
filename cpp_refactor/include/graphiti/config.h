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
    int max_tokens = 16384;
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
