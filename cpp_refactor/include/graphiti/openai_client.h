#pragma once

#include <graphiti/config.h>
#include <graphiti/llm_client.h>

#include <memory>

namespace graphiti {

class HttpClient;

class OpenAIClient : public LLMClient {
public:
    explicit OpenAIClient(const LLMConfig& config);
    OpenAIClient(const LLMConfig& config, HttpClient& shared_http);
    ~OpenAIClient() override;

    OpenAIClient(const OpenAIClient&) = delete;
    OpenAIClient& operator=(const OpenAIClient&) = delete;

    Result<nlohmann::json> generate_response(
        const std::vector<Message>& messages,
        std::optional<std::string_view> json_schema = std::nullopt,
        ModelSize model_size = ModelSize::medium
    ) override;

    // Raw text completion — no JSON mode, no schema injection, no parse retries.
    // Returns the content string directly. One call, one response.
    Result<std::string> generate_text(
        const std::vector<Message>& messages,
        ModelSize model_size = ModelSize::medium
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
