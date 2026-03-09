#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace graphiti {

struct TokenUsage {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;

    int64_t total_tokens() const { return input_tokens + output_tokens; }
};

struct PromptTokenUsage {
    std::string prompt_name;
    int64_t call_count = 0;
    int64_t total_input_tokens = 0;
    int64_t total_output_tokens = 0;

    int64_t total_tokens() const { return total_input_tokens + total_output_tokens; }
    double avg_input_tokens() const { return call_count > 0 ? static_cast<double>(total_input_tokens) / call_count : 0; }
    double avg_output_tokens() const { return call_count > 0 ? static_cast<double>(total_output_tokens) / call_count : 0; }
};

// Thread-safe token usage tracker. Accumulates usage by prompt name.
class TokenTracker {
public:
    void record(const std::string& prompt_name, int64_t input_tokens, int64_t output_tokens) {
        std::lock_guard lock(mu_);
        auto& entry = usage_[prompt_name];
        entry.prompt_name = prompt_name;
        entry.call_count++;
        entry.total_input_tokens += input_tokens;
        entry.total_output_tokens += output_tokens;
    }

    // Get usage broken down by prompt name.
    std::map<std::string, PromptTokenUsage> get_usage() const {
        std::lock_guard lock(mu_);
        return usage_;
    }

    // Get total usage across all prompts.
    TokenUsage get_total_usage() const {
        std::lock_guard lock(mu_);
        TokenUsage total;
        for (auto& [_, entry] : usage_) {
            total.input_tokens += entry.total_input_tokens;
            total.output_tokens += entry.total_output_tokens;
        }
        return total;
    }

    // Get total number of LLM calls.
    int64_t get_total_calls() const {
        std::lock_guard lock(mu_);
        int64_t total = 0;
        for (auto& [_, entry] : usage_) {
            total += entry.call_count;
        }
        return total;
    }

    void reset() {
        std::lock_guard lock(mu_);
        usage_.clear();
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, PromptTokenUsage> usage_;
};

} // namespace graphiti
