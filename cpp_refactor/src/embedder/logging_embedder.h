#pragma once

#include <graphiti/embedder.h>
#include <graphiti/logger.h>

#include <chrono>
#include <memory>
#include <vector>

namespace graphiti {

// Decorator that wraps any EmbedderClient and logs calls to registered GraphitiLoggers.
class LoggingEmbedder : public EmbedderClient {
public:
    LoggingEmbedder(std::unique_ptr<EmbedderClient> inner, std::vector<GraphitiLogger*>& loggers,
                    std::string model_name = "")
        : inner_(std::move(inner)), loggers_(loggers), model_name_(std::move(model_name)) {}

    std::vector<float> create(std::string_view input) override {
        auto start = std::chrono::steady_clock::now();
        try {
            auto result = inner_->create(input);
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            GraphitiLogger::EmbeddingCallInfo info;
            info.model       = model_name_;
            info.input_count = 1;
            info.dimensions  = static_cast<int>(result.size());
            info.latency_ms  = elapsed;
            info.success     = true;
            for (auto* l : loggers_) l->on_embedding_call(info);

            return result;
        } catch (const std::exception& e) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            GraphitiLogger::EmbeddingCallInfo info;
            info.model         = model_name_;
            info.input_count   = 1;
            info.latency_ms    = elapsed;
            info.success       = false;
            info.error_message = e.what();
            for (auto* l : loggers_) l->on_embedding_call(info);

            throw;
        }
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override {
        auto start = std::chrono::steady_clock::now();
        try {
            auto result = inner_->create_batch(inputs);
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            GraphitiLogger::EmbeddingCallInfo info;
            info.model       = model_name_;
            info.input_count = static_cast<int>(inputs.size());
            info.dimensions  = result.empty() ? 0 : static_cast<int>(result[0].size());
            info.latency_ms  = elapsed;
            info.success     = true;
            for (auto* l : loggers_) l->on_embedding_call(info);

            return result;
        } catch (const std::exception& e) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

            GraphitiLogger::EmbeddingCallInfo info;
            info.model         = model_name_;
            info.input_count   = static_cast<int>(inputs.size());
            info.latency_ms    = elapsed;
            info.success       = false;
            info.error_message = e.what();
            for (auto* l : loggers_) l->on_embedding_call(info);

            throw;
        }
    }

private:
    std::unique_ptr<EmbedderClient> inner_;
    std::vector<GraphitiLogger*>& loggers_;
    std::string model_name_;
};

} // namespace graphiti
