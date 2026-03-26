#pragma once

#include <graphiti/llm_client.h>
#include <graphiti/logger.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <vector>

namespace graphiti {

// Decorator that wraps any LLMClient and logs EVERY call attempt to registered GraphitiLoggers.
// Logs only AFTER each attempt completes (no pre-call noise rows).
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
        // Forward prompt_name from outer to inner so call sites can set it on either
        if (!prompt_name.empty())
            inner_->prompt_name = prompt_name;
        if (max_tokens_override > 0)
            inner_->max_tokens_override = max_tokens_override;

        std::string pname = inner_->prompt_name.empty() ? "unknown" : inner_->prompt_name;

        std::string model = model_size == ModelSize::small
            ? (small_model_name_.empty() ? "small" : small_model_name_)
            : (model_name_.empty() ? "medium" : model_name_);

        // Track attempt number — incremented on each post-call
        int attempt_num = 0;

        inner_->on_attempt = [&](const std::vector<Message>& msgs,
                                  const Result<nlohmann::json>& result,
                                  bool is_pre_call) {
            if (is_pre_call) {
                // Just bump the counter, don't log a row
                attempt_num++;
                return;
            }

            // Post-call: log the actual result
            GraphitiLogger::LLMCallInfo info;
            info.model = model;
            info.prompt_name = pname;
            info.attempt = attempt_num;
            info.started_at = format_now();

            for (auto& msg : msgs)
                info.request_messages.push_back({msg.role, msg.content});

            if (result.has_value()) {
                info.success = true;
                info.response_body = result->dump();
                if (result->contains("__token_usage__")) {
                    auto& tu = (*result)["__token_usage__"];
                    if (tu.contains("input_tokens")) info.input_tokens = tu["input_tokens"].get<int64_t>();
                    if (tu.contains("output_tokens")) info.output_tokens = tu["output_tokens"].get<int64_t>();
                }
            } else {
                info.success = false;
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
        };

        auto start = std::chrono::steady_clock::now();
        auto result = inner_->generate_response(messages, json_schema, model_size);
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        // Clear callback, prompt_name, and token override
        inner_->on_attempt = nullptr;
        inner_->prompt_name.clear();
        inner_->max_tokens_override = 0;
        prompt_name.clear();
        max_tokens_override = 0;

        // Forward token tracking
        token_tracker.merge(inner_->token_tracker);
        inner_->token_tracker.reset();

        return result;
    }

private:
    static std::string format_now() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                 tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                 tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                 static_cast<int>(ms.count()));
        return buf;
    }

    std::unique_ptr<LLMClient> inner_;
    std::vector<GraphitiLogger*>& loggers_;
    std::string model_name_;
    std::string small_model_name_;
};

} // namespace graphiti
