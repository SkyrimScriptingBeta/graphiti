#include "openai_client.h"

#include "http/http_client.h"

#include <format>

namespace graphiti {

static constexpr int MAX_RETRIES = 2;

struct OpenAIClient::Impl {
    LLMConfig config;
    HttpClient* shared_http = nullptr;
    HttpClient owned_http;

    HttpClient& http() { return shared_http ? *shared_http : owned_http; }

    std::string model_for_size(ModelSize size) const {
        return size == ModelSize::small ? config.small_model : config.model;
    }

    Result<nlohmann::json> call_completions(
        std::vector<Message> messages,
        std::optional<std::string_view> json_schema,
        ModelSize model_size
    ) {
        auto model = model_for_size(model_size);

        // If a schema is provided, append it to the last user message
        if (json_schema.has_value()) {
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if (it->role == "user") {
                    it->content += std::format(
                        "\n\nRespond with a JSON object matching this schema:\n{}",
                        *json_schema
                    );
                    break;
                }
            }
        }

        // Build request JSON
        nlohmann::json msgs_json = nlohmann::json::array();
        for (auto& m : messages) {
            msgs_json.push_back({{"role", m.role}, {"content", m.content}});
        }

        nlohmann::json request_body = {
            {"model", model},
            {"messages", msgs_json},
            {"max_tokens", config.max_tokens},
            {"response_format", {{"type", "json_object"}}},
        };

        if (config.temperature >= 0.0f) {
            request_body["temperature"] = config.temperature;
        }

        auto headers = std::map<std::string, std::string>{
            {"Authorization", std::format("Bearer {}", config.api_key)},
        };

        auto result = http().post_json(
            config.base_url, "/v1/chat/completions", headers, request_body.dump()
        );

        if (!result.has_value()) return std::unexpected(result.error());

        auto& resp = result.value();

        if (resp.status == 429) {
            return std::unexpected(GraphitiError{
                ErrorCode::llm_rate_limit,
                "OpenAI rate limit exceeded"
            });
        }

        if (resp.status != 200) {
            return std::unexpected(GraphitiError{
                ErrorCode::llm_error,
                std::format("OpenAI API error (HTTP {}): {}", resp.status, resp.body)
            });
        }

        // Parse the response envelope
        nlohmann::json resp_json;
        try {
            resp_json = nlohmann::json::parse(resp.body);
        } catch (const nlohmann::json::exception& e) {
            return std::unexpected(GraphitiError{
                ErrorCode::llm_parse_error,
                std::format("Failed to parse OpenAI response: {}", e.what())
            });
        }

        // Extract token usage from response
        int64_t input_tokens = 0;
        int64_t output_tokens = 0;
        if (resp_json.contains("usage") && resp_json["usage"].is_object()) {
            auto& usage = resp_json["usage"];
            if (usage.contains("prompt_tokens")) input_tokens = usage["prompt_tokens"].get<int64_t>();
            if (usage.contains("completion_tokens")) output_tokens = usage["completion_tokens"].get<int64_t>();
        }

        // Extract choices[0].message.content
        auto content_str = resp_json.at("choices").at(0).at("message").at("content").get<std::string>();

        // Parse the content as JSON
        try {
            auto parsed = nlohmann::json::parse(content_str);
            // Attach usage metadata so generate_response can record it
            parsed["__token_usage__"] = {{"input", input_tokens}, {"output", output_tokens}};
            return parsed;
        } catch (const nlohmann::json::exception& e) {
            return std::unexpected(GraphitiError{
                ErrorCode::llm_parse_error,
                std::format("Failed to parse LLM JSON output: {} — raw: {}", e.what(), content_str)
            });
        }
    }
};

OpenAIClient::OpenAIClient(const LLMConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

OpenAIClient::OpenAIClient(const LLMConfig& config, HttpClient& shared_http)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    impl_->shared_http = &shared_http;
}

OpenAIClient::~OpenAIClient() = default;

Result<nlohmann::json> OpenAIClient::generate_response(
    const std::vector<Message>& messages,
    std::optional<std::string_view> json_schema,
    ModelSize model_size
) {
    auto msgs = messages; // mutable copy for retry

    int64_t total_input = 0;
    int64_t total_output = 0;

    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        auto result = impl_->call_completions(msgs, json_schema, model_size);

        if (result.has_value()) {
            // Extract and strip usage metadata
            auto& val = result.value();
            if (val.contains("__token_usage__")) {
                total_input += val["__token_usage__"]["input"].get<int64_t>();
                total_output += val["__token_usage__"]["output"].get<int64_t>();
                val.erase("__token_usage__");
            }
            // Record accumulated usage (prompt name derived from schema title if available)
            std::string prompt_name = "unknown";
            if (json_schema.has_value()) {
                try {
                    auto schema_json = nlohmann::json::parse(*json_schema);
                    if (schema_json.contains("title")) {
                        prompt_name = schema_json["title"].get<std::string>();
                    }
                } catch (...) {}
            }
            token_tracker.record(prompt_name, total_input, total_output);
            return result;
        }

        auto& err = result.error();

        // Don't retry rate limits or HTTP errors
        if (err.code == ErrorCode::llm_rate_limit || err.code == ErrorCode::http_error) {
            return result;
        }

        // Retry on parse errors by appending error context
        if (attempt < MAX_RETRIES && err.code == ErrorCode::llm_parse_error) {
            msgs.push_back({
                "user",
                std::format(
                    "The previous response was invalid. Error: {}. "
                    "Please try again with a valid JSON response matching the expected format.",
                    err.message
                )
            });
            continue;
        }

        return result;
    }

    return std::unexpected(GraphitiError{
        ErrorCode::llm_error,
        std::format("Max retries ({}) exceeded", MAX_RETRIES)
    });
}

} // namespace graphiti
