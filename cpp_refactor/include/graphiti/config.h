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
    float temperature = 1.0f;
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

        return config;
    }
};

} // namespace graphiti
