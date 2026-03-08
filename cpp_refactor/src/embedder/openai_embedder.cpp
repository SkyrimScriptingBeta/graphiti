#include "openai_embedder.h"

#include "http/http_client.h"

#include <nlohmann/json.hpp>

#include <format>
#include <stdexcept>

namespace graphiti {

struct OpenAIEmbedder::Impl {
    EmbedderConfig config;
    HttpClient* shared_http = nullptr;
    HttpClient owned_http;

    HttpClient& http() { return shared_http ? *shared_http : owned_http; }

    std::map<std::string, std::string> headers() const {
        return {{"Authorization", std::format("Bearer {}", config.api_key)}};
    }

    Result<nlohmann::json> call_embeddings(const nlohmann::json& input) {
        nlohmann::json body = {
            {"model", config.model},
            {"input", input},
        };

        auto result = http().post_json(config.base_url, "/v1/embeddings", headers(), body.dump());

        if (!result.has_value()) return std::unexpected(result.error());

        auto& resp = result.value();

        if (resp.status != 200) {
            return std::unexpected(GraphitiError{
                ErrorCode::embedding_error,
                std::format("OpenAI embeddings API error (HTTP {}): {}", resp.status, resp.body)
            });
        }

        try {
            return nlohmann::json::parse(resp.body);
        } catch (const nlohmann::json::exception& e) {
            return std::unexpected(GraphitiError{
                ErrorCode::embedding_error,
                std::format("Failed to parse embeddings response: {}", e.what())
            });
        }
    }

    std::vector<float> extract_embedding(const nlohmann::json& data_item) const {
        auto full = data_item.at("embedding").get<std::vector<float>>();
        if (static_cast<int>(full.size()) > config.embedding_dim) {
            full.resize(config.embedding_dim);
        }
        return full;
    }
};

OpenAIEmbedder::OpenAIEmbedder(const EmbedderConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

OpenAIEmbedder::OpenAIEmbedder(const EmbedderConfig& config, HttpClient& shared_http)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    impl_->shared_http = &shared_http;
}

OpenAIEmbedder::~OpenAIEmbedder() = default;

std::vector<float> OpenAIEmbedder::create(std::string_view input) {
    auto result = impl_->call_embeddings(std::string(input));
    if (!result.has_value()) {
        throw std::runtime_error(result.error().message);
    }
    return impl_->extract_embedding(result.value().at("data").at(0));
}

std::vector<std::vector<float>> OpenAIEmbedder::create_batch(const std::vector<std::string>& inputs) {
    auto result = impl_->call_embeddings(inputs);
    if (!result.has_value()) {
        throw std::runtime_error(result.error().message);
    }

    std::vector<std::vector<float>> embeddings;
    for (auto& item : result.value().at("data")) {
        embeddings.push_back(impl_->extract_embedding(item));
    }
    return embeddings;
}

} // namespace graphiti
