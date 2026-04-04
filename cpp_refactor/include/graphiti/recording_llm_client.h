#pragma once

#include <graphiti/llm_client.h>

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace graphiti {

// Records every LLM call (request + response) to a JSON file.
// Wraps a real LLMClient and forwards all calls, tee-ing to disk.
//
// Usage (record mode):
//   auto real = std::make_unique<OpenAIClient>(config.llm);
//   auto recorder = std::make_unique<RecordingLLMClient>(std::move(real), "/tmp/recordings/test1_llm.json");
//   Graphiti g(config, std::move(recorder), ...);
//
class RecordingLLMClient : public LLMClient {
public:
    RecordingLLMClient(std::unique_ptr<LLMClient> inner, const std::filesystem::path& output_path)
        : inner_(std::move(inner)), output_path_(output_path) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    ~RecordingLLMClient() override { flush(); }

    Result<nlohmann::json> generate_response(
        const std::vector<Message>& messages,
        std::optional<std::string_view> json_schema = std::nullopt,
        ModelSize model_size = ModelSize::medium
    ) override {
        // Forward prompt_name and max_tokens_override to inner
        inner_->prompt_name = prompt_name;
        inner_->max_tokens_override = max_tokens_override;
        inner_->on_attempt = on_attempt;

        auto result = inner_->generate_response(messages, json_schema, model_size);

        // Merge token tracking
        token_tracker.merge(inner_->token_tracker);

        // Record the call
        nlohmann::json entry;
        entry["prompt_name"] = prompt_name;
        entry["model_size"] = (model_size == ModelSize::small) ? "small" : "medium";

        nlohmann::json msgs = nlohmann::json::array();
        for (auto& m : messages) {
            msgs.push_back({{"role", m.role}, {"content", m.content}});
        }
        entry["messages"] = std::move(msgs);

        if (json_schema.has_value()) {
            entry["json_schema"] = std::string(json_schema.value());
        } else {
            entry["json_schema"] = nullptr;
        }

        if (result.has_value()) {
            entry["response"] = result.value();
            entry["error"] = nullptr;
        } else {
            entry["response"] = nullptr;
            entry["error"] = {
                {"code", static_cast<int>(result.error().code)},
                {"message", result.error().message}
            };
        }

        std::lock_guard lock(mu_);
        recordings_.push_back(std::move(entry));

        return result;
    }

    void flush() {
        std::lock_guard lock(mu_);
        if (recordings_.empty()) return;
        std::ofstream f(output_path_);
        if (f.is_open()) {
            f << nlohmann::json(recordings_).dump(2);
        }
    }

private:
    std::unique_ptr<LLMClient> inner_;
    std::filesystem::path output_path_;
    std::mutex mu_;
    nlohmann::json recordings_ = nlohmann::json::array();
};

// Replays recorded LLM responses in sequence. No network calls.
//
// Usage (replay mode):
//   auto replayer = std::make_unique<ReplayLLMClient>("/tmp/recordings/test1_llm.json");
//   Graphiti g(config, std::move(replayer), ...);
//
class ReplayLLMClient : public LLMClient {
public:
    explicit ReplayLLMClient(const std::filesystem::path& recording_path) {
        std::ifstream f(recording_path);
        if (!f.is_open()) {
            throw std::runtime_error("Cannot open LLM recording: " + recording_path.string());
        }
        recordings_ = nlohmann::json::parse(f);
    }

    Result<nlohmann::json> generate_response(
        const std::vector<Message>&,
        std::optional<std::string_view> = std::nullopt,
        ModelSize = ModelSize::medium
    ) override {
        if (index_ >= recordings_.size()) {
            return std::unexpected(GraphitiError{
                ErrorCode::llm_error,
                "ReplayLLMClient: no more recorded responses (exhausted "
                    + std::to_string(recordings_.size()) + " entries)"
            });
        }

        auto& entry = recordings_[index_++];

        if (!entry["error"].is_null()) {
            return std::unexpected(GraphitiError{
                static_cast<ErrorCode>(entry["error"]["code"].get<int>()),
                entry["error"]["message"].get<std::string>()
            });
        }

        return entry["response"];
    }

    size_t calls_replayed() const { return index_; }
    size_t total_recordings() const { return recordings_.size(); }

private:
    nlohmann::json recordings_;
    size_t index_ = 0;
};

} // namespace graphiti
