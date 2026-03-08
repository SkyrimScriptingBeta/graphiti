#pragma once

#include <optional>
#include <string>

namespace graphiti {

struct LLMConfig {
    std::string api_key;
    std::string model = "gpt-4.1-mini";
    std::string small_model = "gpt-4.1-nano";
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
};

} // namespace graphiti
