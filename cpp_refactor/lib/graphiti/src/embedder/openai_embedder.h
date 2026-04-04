#pragma once

#include <graphiti/config.h>
#include <graphiti/embedder.h>
#include <graphiti/error.h>

#include <memory>

namespace graphiti {

class HttpClient;

class OpenAIEmbedder : public EmbedderClient {
public:
    explicit OpenAIEmbedder(const EmbedderConfig& config);
    OpenAIEmbedder(const EmbedderConfig& config, HttpClient& shared_http);
    ~OpenAIEmbedder() override;

    OpenAIEmbedder(const OpenAIEmbedder&) = delete;
    OpenAIEmbedder& operator=(const OpenAIEmbedder&) = delete;

    std::vector<float> create(std::string_view input) override;
    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
