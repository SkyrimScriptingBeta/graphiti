#pragma once

#include <graphiti/error.h>
#include <graphiti/token_tracker.h>

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace graphiti {

enum class ModelSize { small, medium };

struct Message {
    std::string role;
    std::string content;
};

class LLMClient {
public:
    virtual ~LLMClient() = default;

    virtual Result<nlohmann::json> generate_response(
        const std::vector<Message>& messages,
        std::optional<std::string_view> json_schema = std::nullopt,
        ModelSize model_size = ModelSize::medium
    ) = 0;

    TokenTracker token_tracker;

    // Set this before calling generate_response() to tag log entries.
    // e.g. "extract_message", "dedupe_node", "extract_edges"
    std::string prompt_name;

    // Optional per-attempt callback for logging. Set by LoggingLLMClient.
    // Called before each attempt (is_pre_call=true) and after (is_pre_call=false).
    std::function<void(const std::vector<Message>&, const Result<nlohmann::json>&, bool)> on_attempt;
};

} // namespace graphiti
