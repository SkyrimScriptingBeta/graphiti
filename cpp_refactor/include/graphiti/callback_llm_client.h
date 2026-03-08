#pragma once

#include <graphiti/llm_client.h>

#include <functional>

namespace graphiti {

// LLM client that delegates to a user-provided callback function.
// Allows users to bring their own LLM (local llama.cpp, custom API, etc.)
// without implementing a full LLMClient subclass.
//
// Usage:
//   CallbackLLMClient llm([](const std::vector<Message>& msgs, std::optional<std::string_view> schema) {
//       // Call your LLM here, return JSON response
//       return nlohmann::json{{"content", "response"}};
//   });
class CallbackLLMClient : public LLMClient {
public:
    using Callback = std::function<Result<nlohmann::json>(
        const std::vector<Message>&,
        std::optional<std::string_view>
    )>;

    explicit CallbackLLMClient(Callback callback)
        : callback_(std::move(callback)) {}

    Result<nlohmann::json> generate_response(
        const std::vector<Message>& messages,
        std::optional<std::string_view> json_schema = std::nullopt,
        ModelSize /*model_size*/ = ModelSize::medium
    ) override {
        return callback_(messages, json_schema);
    }

private:
    Callback callback_;
};

} // namespace graphiti
