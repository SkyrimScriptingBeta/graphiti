#include "openai_client.h"

#include "http/http_client.h"

#include <graphiti/log.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <thread>
#include <unordered_map>

namespace graphiti {

static int get_max_retries() {
    auto* env = std::getenv("GRAPHITI_RETRY_COUNT");
    return (env && env[0]) ? std::max(0, std::atoi(env)) : 5;
}

static bool get_accept_last_retry() {
    auto* env = std::getenv("GRAPHITI_ACCEPT_LAST_FAILED_RETRY");
    return (env && env[0] == '1');
}

// Attempt to repair truncated JSON by finding the last complete object in an array
// and closing all open brackets/braces. Returns empty string if repair fails.
static std::string repair_truncated_json(const std::string& json) {
    // Find the last complete "}" that's part of an array element
    // Strategy: walk backwards from the end, find the last "},", or the last "}" before truncation
    // Then close all remaining open brackets/braces

    // Find the outermost opening brace
    auto first_brace = json.find('{');
    if (first_brace == std::string::npos) return "";

    // Find the last complete object boundary: "}, " or "}," or just "}"
    // We want the last "}" that ends a complete array element
    size_t last_complete = std::string::npos;
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (escape) { escape = false; continue; }
        if (c == '\\' && in_string) { escape = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (in_string) continue;

        if (c == '{' || c == '[') depth++;
        if (c == '}' || c == ']') {
            depth--;
            if (depth == 1 && c == '}') {
                // Closing a depth-2 object (array element inside the outer object)
                last_complete = i;
            }
        }
    }

    if (last_complete == std::string::npos) return "";

    // Take everything up to and including the last complete object
    auto repaired = json.substr(0, last_complete + 1);

    // Close remaining open structures
    // We're at depth 1 (inside the outer object, inside the array)
    // Need to close: "]}" to complete the array and outer object
    repaired += "]}";

    // Verify it parses
    try {
        (void)nlohmann::json::parse(repaired);
        return repaired;
    } catch (...) {
        return "";
    }
}

struct OpenAIClient::Impl {
    LLMConfig config;
    HttpClient* shared_http = nullptr;
    HttpClient owned_http;
    int max_tokens_for_call = 0;  // set per-call by generate_response; 0 = use config.max_tokens

    HttpClient& http() { return shared_http ? *shared_http : owned_http; }

    std::string model_for_size(ModelSize size) const {
        return size == ModelSize::small ? config.small_model : config.model;
    }

    int effective_max_tokens() const {
        int base = max_tokens_for_call > 0 ? max_tokens_for_call : config.max_tokens;
        return std::min(base, config.max_output_tokens);
    }

    Result<nlohmann::json> call_completions(
        std::vector<Message> messages,
        std::optional<std::string_view> json_schema,
        ModelSize model_size,
        bool json_mode = true
    ) {
        auto model = model_for_size(model_size);

        // If a schema is provided, append it to the last user message
        if (json_mode && json_schema.has_value()) {
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if (it->role == "user") {
                    it->content += std::format(
                        "\n\nRespond with ONLY a valid JSON object. No explanation, no markdown, no extra text.\n\n{}",
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
            {"max_tokens", effective_max_tokens()},
        };

        // Only request JSON response format when in json_mode
        if (json_mode) {
            request_body["response_format"] = {{"type", "json_object"}};
        }

        if (config.temperature >= 0.0f) {
            request_body["temperature"] = config.temperature;
        }
        if (config.repetition_penalty >= 0.0f) {
            request_body["repeat_penalty"] = config.repetition_penalty;  // Ollama
        }
        if (config.frequency_penalty >= 0.0f) {
            request_body["frequency_penalty"] = config.frequency_penalty;  // OpenAI-compatible
        }

        auto headers = std::map<std::string, std::string>{
            {"Authorization", std::format("Bearer {}", config.api_key)},
        };

        log_trace("[graphiti-llm] → POST %s /chat/completions (model=%s, msgs=%zu, max_tokens=%d%s)\n",
                  config.base_url.c_str(), model.c_str(), messages.size(), effective_max_tokens(),
                  config.stream ? ", stream" : "");

        // GRAPHITI_LOG_REQUESTS=1 — dump full request JSON for debugging
        {
            static int log_requests = -1;
            if (log_requests < 0) {
                auto* env = std::getenv("GRAPHITI_LOG_REQUESTS");
                log_requests = (env && env[0] == '1') ? 1 : 0;
            }
            if (log_requests) {
                log_trace("[graphiti-llm] 📤 REQUEST:\n%s\n", request_body.dump(2).c_str());
            }
        }

        auto t0 = std::chrono::steady_clock::now();

        Result<HttpResponse> result;
        bool json_complete_flag = false;
        std::string stream_content;

        if (config.stream) {
            auto stream_body = request_body;
            stream_body["stream"] = true;

            // Loop detection: track "name": "X" patterns in streaming JSON output.
            // If the same value appears K+ times, the model is looping — abort the stream
            // and use the truncated JSON repair to salvage the good data.
            int loop_threshold = 0;
            {
                auto* env = std::getenv("GRAPHITI_STREAM_LOOP_THRESHOLD");
                loop_threshold = env ? std::atoi(env) : 5;
            }
            bool stream_aborted = false;
            auto& stream_accumulator = stream_content;  // alias for outer scope access
            std::unordered_map<std::string, int> name_value_counts;
            size_t last_scan_pos = 0;

            // JSON completeness tracking — stop stream when the top-level object closes
            int json_depth = 0;
            bool json_started = false;
            bool in_json_string = false;
            bool json_escape = false;
            bool json_complete = false;

            result = http().post_json_streaming(
                config.base_url, "/v1/chat/completions", headers, stream_body.dump(),
                [&](const std::string& token) -> bool {
                    log_trace("%s", token.c_str());

                    stream_accumulator += token;

                    // Track JSON depth char-by-char — when depth returns to 0, JSON is complete
                    for (char c : token) {
                        if (json_escape) { json_escape = false; continue; }
                        if (c == '\\' && in_json_string) { json_escape = true; continue; }
                        if (c == '"') { in_json_string = !in_json_string; continue; }
                        if (in_json_string) continue;

                        if (c == '{' || c == '[') { json_depth++; json_started = true; }
                        if (c == '}' || c == ']') { json_depth--; }

                        if (json_started && json_depth == 0) {
                            log_trace("\n[graphiti-llm] ✅ JSON complete — stopping stream\n");
                            json_complete = true;
                            json_complete_flag = true;
                            return false;  // JSON fully closed, take it and bail
                        }
                    }

                    if (loop_threshold <= 0) return true;  // loop detection disabled

                    // Periodically scan for "name": "VALUE" patterns
                    // Only scan new content since last check
                    const std::string needle = "\"name\":";
                    while (true) {
                        auto pos = stream_accumulator.find(needle, last_scan_pos);
                        if (pos == std::string::npos) break;

                        // Find the opening quote of the value
                        auto val_start = stream_accumulator.find('"', pos + needle.size());
                        if (val_start == std::string::npos) break;  // incomplete — wait for more tokens

                        // Find the closing quote
                        auto val_end = stream_accumulator.find('"', val_start + 1);
                        if (val_end == std::string::npos) break;  // incomplete — wait for more tokens

                        auto name_value = stream_accumulator.substr(val_start + 1, val_end - val_start - 1);
                        name_value_counts[name_value]++;

                        if (name_value_counts[name_value] >= loop_threshold) {
                            log_trace("\n[graphiti-llm] ⚡ Loop detected: \"%s\" appeared %d times — aborting stream\n",
                                      name_value.c_str(), name_value_counts[name_value]);
                            stream_aborted = true;
                            return false;
                        }

                        last_scan_pos = val_end + 1;
                    }

                    return true;
                }
            );
            // Newline after streaming tokens
            log_trace("\n");
        } else {
            result = http().post_json(
                config.base_url, "/v1/chat/completions", headers, request_body.dump()
            );
        }

        auto llm_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        // If JSON completeness killed the stream, the HTTP client reports an error.
        // But we have valid JSON in stream_accumulator — synthesize a success.
        if (!result.has_value() && json_complete_flag && !stream_content.empty()) {
            log_trace("[graphiti-llm] ✓ JSON complete (stream killed intentionally) after %.0fms (%zu chars)\n",
                      llm_ms, stream_content.size());
            nlohmann::json fake_resp = {
                {"choices", {{{"message", {{"content", stream_content}}}}}},
                {"usage", {{"prompt_tokens", 0}, {"completion_tokens", 0}}}
            };
            result = HttpResponse{200, fake_resp.dump()};
        }

        if (!result.has_value()) {
            log_trace("[graphiti-llm] ✗ HTTP failed after %.0fms: %s\n",
                      llm_ms, result.error().message.c_str());
            return std::unexpected(result.error());
        }
        log_trace("[graphiti-llm] ✓ HTTP %d after %.0fms (%zu bytes)\n",
                  result->status, llm_ms, result->body.size());

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
        auto& content_val = resp_json.at("choices").at(0).at("message").at("content");
        if (content_val.is_null()) {
            log_trace("[graphiti-llm] ⚠️ LLM returned null content, raw response: %s\n",
                      resp.body.substr(0, 500).c_str());
            return std::unexpected(GraphitiError{
                ErrorCode::llm_parse_error,
                "LLM returned null content in response"
            });
        }
        auto content_str = content_val.get<std::string>();

        // In text mode, return raw content without JSON parsing
        if (!json_mode) {
            nlohmann::json text_result;
            text_result["content"] = content_str;
            text_result["__token_usage__"] = {{"input", input_tokens}, {"output", output_tokens}};
            return text_result;
        }

        // Strip markdown JSON fences (```json ... ```) that some models wrap around their output
        if (content_str.size() >= 7 && content_str[0] == '`') {
            auto first_nl = content_str.find('\n');
            if (first_nl != std::string::npos) {
                auto last_fence = content_str.rfind("```");
                if (last_fence != std::string::npos && last_fence > first_nl) {
                    content_str = content_str.substr(first_nl + 1, last_fence - first_nl - 1);
                    // Trim whitespace
                    while (!content_str.empty() && (content_str.front() == '\n' || content_str.front() == ' '))
                        content_str.erase(content_str.begin());
                    while (!content_str.empty() && (content_str.back() == '\n' || content_str.back() == ' '))
                        content_str.pop_back();
                }
            }
        }

        // Parse the content as JSON
        try {
            auto parsed = nlohmann::json::parse(content_str);
            parsed["__token_usage__"] = {{"input", input_tokens}, {"output", output_tokens}};
            return parsed;
        } catch (const nlohmann::json::exception& e) {
            // Try to repair truncated JSON before giving up
            auto repaired = repair_truncated_json(content_str);
            if (!repaired.empty()) {
                try {
                    auto parsed = nlohmann::json::parse(repaired);
                    parsed["__token_usage__"] = {{"input", input_tokens}, {"output", output_tokens}};
                    log_debug("[graphiti-llm] 🔧 Repaired truncated JSON (kept %zu of %zu chars)\n",
                              repaired.size(), content_str.size());
                    return parsed;
                } catch (...) {}
            }
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

// Detect if a JSON parse error looks like output truncation (ran out of tokens).
static bool is_truncation_error(const std::string& msg) {
    // nlohmann::json errors for truncated JSON:
    //   "missing closing quote"
    //   "unexpected end of input"
    //   "missing '}'" / "missing ']'"
    for (auto* pattern : {"missing closing", "unexpected end", "missing '}'", "missing ']'"}) {
        if (msg.find(pattern) != std::string::npos) return true;
    }
    return false;
}

Result<nlohmann::json> OpenAIClient::generate_response(
    const std::vector<Message>& messages,
    std::optional<std::string_view> json_schema,
    ModelSize model_size
) {
    auto msgs = messages; // mutable copy for retry

    // Set initial token budget from per-call override or config default
    impl_->max_tokens_for_call = max_tokens_override > 0 ? max_tokens_override : impl_->config.max_tokens;

    int64_t total_input = 0;
    int64_t total_output = 0;
    const int max_retries = get_max_retries();
    const bool accept_last = get_accept_last_retry();
    std::string last_raw_content;  // keep the last failed content for salvage

    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        // Pre-call logging
        if (on_attempt) {
            Result<nlohmann::json> empty = std::unexpected(GraphitiError{ErrorCode::ok, ""});
            on_attempt(msgs, empty, true);
        }

        auto result = impl_->call_completions(msgs, json_schema, model_size);

        // Post-call logging
        if (on_attempt) on_attempt(msgs, result, false);

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

        // Retry HTTP errors (connection failures, timeouts) with exponential backoff
        if (err.code == ErrorCode::http_error || err.code == ErrorCode::llm_rate_limit) {
            if (attempt >= max_retries) return result;
            int delay_ms = 1000 * (1 << attempt);  // 1s, 2s, 4s, 8s, 16s
            fprintf(stderr, "  [graphiti] LLM call failed (%s), retrying in %dms (attempt %d/%d)\n",
                    err.code == ErrorCode::http_error ? "connection error" : "rate limit",
                    delay_ms, attempt + 1, max_retries);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            continue;
        }

        // Retry other LLM errors (500s, etc.) with exponential backoff
        if (err.code == ErrorCode::llm_error) {
            if (attempt >= max_retries) return result;
            int delay_ms = 1000 * (1 << attempt);
            fprintf(stderr, "  [graphiti] LLM error: %s, retrying in %dms (attempt %d/%d)\n",
                    err.message.c_str(), delay_ms, attempt + 1, max_retries);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            continue;
        }

        // Retry on parse errors
        if (attempt < max_retries && err.code == ErrorCode::llm_parse_error) {
            if (is_truncation_error(err.message)) {
                // Output was truncated — bump token budget and retry with fresh messages
                int old_budget = impl_->max_tokens_for_call;
                impl_->max_tokens_for_call = static_cast<int>(old_budget * impl_->config.truncation_multiplier);
                // Cap at hard ceiling
                impl_->max_tokens_for_call = std::min(impl_->max_tokens_for_call, impl_->config.max_output_tokens);
                log_debug("[graphiti] 📏 JSON truncated (tokens: %d → %d, multiplier: %.1fx, ceiling: %d), retrying\n",
                        old_budget, impl_->max_tokens_for_call,
                        impl_->config.truncation_multiplier, impl_->config.max_output_tokens);
                // Don't append error context — just retry with more tokens and original messages
                msgs = messages;
                continue;
            }
            // Non-truncation parse error — append error context and retry
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

        // Stash raw content from parse errors for last-retry salvage
        if (err.code == ErrorCode::llm_parse_error && err.message.find("raw: ") != std::string::npos) {
            auto raw_pos = err.message.find("raw: ");
            last_raw_content = err.message.substr(raw_pos + 5);
        }

        return result;
    }

    // GRAPHITI_ACCEPT_LAST_FAILED_RETRY=1 — try to salvage the last failed response
    if (accept_last && !last_raw_content.empty()) {
        auto repaired = repair_truncated_json(last_raw_content);
        if (!repaired.empty()) {
            try {
                auto parsed = nlohmann::json::parse(repaired);
                log_debug("[graphiti-llm] 🛟 Last retry salvaged via repair (%zu of %zu chars)\n",
                          repaired.size(), last_raw_content.size());
                // Record usage
                std::string prompt_name = "unknown";
                if (json_schema.has_value()) {
                    try {
                        auto schema_json = nlohmann::json::parse(*json_schema);
                        if (schema_json.contains("title"))
                            prompt_name = schema_json["title"].get<std::string>();
                    } catch (...) {}
                }
                token_tracker.record(prompt_name, total_input, total_output);
                return parsed;
            } catch (...) {
                log_debug("[graphiti-llm] 💀 Last retry salvage failed — repair didn't parse\n");
            }
        }
    }

    return std::unexpected(GraphitiError{
        ErrorCode::llm_error,
        std::format("Max retries ({}) exceeded", max_retries)
    });
}

Result<std::string> OpenAIClient::generate_text(
    const std::vector<Message>& messages,
    ModelSize model_size
) {
    impl_->max_tokens_for_call = max_tokens_override > 0 ? max_tokens_override : impl_->config.max_tokens;

    // One call, no retries for parse errors, no JSON mode
    auto result = impl_->call_completions(messages, std::nullopt, model_size, /*json_mode=*/false);

    if (!result)
        return std::unexpected(result.error());

    auto& val = *result;
    if (val.contains("__token_usage__")) {
        auto input = val["__token_usage__"]["input"].get<int64_t>();
        auto output = val["__token_usage__"]["output"].get<int64_t>();
        token_tracker.record("generate_text", input, output);
    }

    return val.value("content", "");
}

} // namespace graphiti
