#pragma once

#include <graphiti/embedder.h>

#include <functional>
#include <string>
#include <vector>

namespace graphiti {

// Embedder that delegates to a user-provided callback function.
// Allows users to bring their own embedding logic (ONNX, custom models, etc.)
// without implementing a full EmbedderClient subclass.
//
// Usage:
//   CallbackEmbedder embedder([](std::string_view input) -> std::vector<float> {
//       // Run your embedding model here
//       return {0.1f, 0.2f, ...};
//   });
class CallbackEmbedder : public EmbedderClient {
public:
    using Callback = std::function<std::vector<float>(std::string_view)>;

    explicit CallbackEmbedder(Callback callback)
        : callback_(std::move(callback)) {}

    std::vector<float> create(std::string_view input) override {
        return callback_(input);
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override {
        std::vector<std::vector<float>> results;
        results.reserve(inputs.size());
        for (auto& input : inputs) {
            results.push_back(callback_(input));
        }
        return results;
    }

private:
    Callback callback_;
};

} // namespace graphiti
