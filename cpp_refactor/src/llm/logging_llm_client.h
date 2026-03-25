#pragma once

#include <graphiti/llm_client.h>
#include <graphiti/logger.h>

#include <chrono>
#include <memory>
#include <vector>

namespace graphiti {

// Decorator that wraps any LLMClient and logs calls to registered GraphitiLoggers.
class LoggingLLMClient : public LLMClient {
public:
    LoggingLLMClient(std::unique_ptr<LLMClient> inner, std::vector<GraphitiLogger*>& loggers,
                     std::string model_name = "", std::string small_model_name = "")
        : inner_(std::move(inner)), loggers_(loggers)
        , model_name_(std::move(model_name)), small_model_name_(std::move(small_model_name)) {}

    Result<nlohmann::json> generate_response(
        const std::vector<Message>& messages,
        std::optional<std::string_view> json_schema = std::nullopt,
        ModelSize model_size = ModelSize::medium
    ) override {
        // Extract prompt name from schema title BEFORE calling
        std::string prompt_name = "unknown";
        if (json_schema.has_value()) {
            try {
                auto j = nlohmann::json::parse(*json_schema);
                if (j.contains("title")) prompt_name = j["title"].get<std::string>();
            } catch (...) {}
        }

        // Log request BEFORE sending so we have it even if the call crashes
        {
            GraphitiLogger::LLMCallInfo pre_info;
            std::string model = model_size == ModelSize::small
                ? (small_model_name_.empty() ? "small" : small_model_name_)
                : (model_name_.empty() ? "medium" : model_name_);
            pre_info.model = model;
            pre_info.prompt_name = prompt_name;
            pre_info.success = true;  // will be updated after call
            pre_info.attempt = 0;     // 0 = pre-call log
            for (auto& msg : messages)
                pre_info.request_messages.push_back({msg.role, msg.content});
            for (auto* l : loggers_) l->on_llm_call(pre_info);
        }

        auto start = std::chrono::steady_clock::now();
        auto result = inner_->generate_response(messages, json_schema, model_size);
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        // Resolve actual model name
        std::string model = model_size == ModelSize::small
            ? (small_model_name_.empty() ? "small" : small_model_name_)
            : (model_name_.empty() ? "medium" : model_name_);

        GraphitiLogger::LLMCallInfo info;
        info.prompt_name = prompt_name;
        info.model       = model;
        info.latency_ms  = elapsed;

        // Full request messages
        for (auto& msg : messages) {
            info.request_messages.push_back({msg.role, msg.content});
        }

        if (result.has_value()) {
            info.success = true;
            // Full response body
            info.response_body = result->dump();
            // Token usage
            if (result->contains("__token_usage__")) {
                auto& tu = (*result)["__token_usage__"];
                if (tu.contains("input_tokens")) info.input_tokens = tu["input_tokens"].get<int64_t>();
                if (tu.contains("output_tokens")) info.output_tokens = tu["output_tokens"].get<int64_t>();
            }
        } else {
            info.success       = false;
            info.error_message = result.error().message;
            switch (result.error().code) {
                case ErrorCode::llm_rate_limit: info.error_code = "llm_rate_limit"; break;
                case ErrorCode::llm_parse_error: info.error_code = "llm_parse_error"; break;
                case ErrorCode::llm_error: info.error_code = "llm_error"; break;
                case ErrorCode::http_error: info.error_code = "http_error"; break;
                default: info.error_code = "unknown"; break;
            }
        }

        for (auto* l : loggers_) l->on_llm_call(info);

        // Forward token tracking — merge inner's tracker into ours
        token_tracker.merge(inner_->token_tracker);
        inner_->token_tracker.reset();

        return result;
    }

private:
    std::unique_ptr<LLMClient> inner_;
    std::vector<GraphitiLogger*>& loggers_;
    std::string model_name_;        // actual model name e.g. "gpt-4.1-mini"
    std::string small_model_name_;  // actual small model name e.g. "gpt-4.1-nano"
};

} // namespace graphiti
