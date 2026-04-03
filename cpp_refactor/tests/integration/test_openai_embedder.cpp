#include <catch2/catch_all.hpp>

#include <graphiti/config.h>

#include "embedder/openai_embedder.h"

#include <cmath>
#include <cstdlib>
#include <string>

using namespace graphiti;

static EmbedderConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    auto full = GraphitiConfig::from_env();
    return full.embedder;
}

TEST_CASE("OpenAI Embedder: single embedding", "[integration][embedder]") {
    auto config = make_config();
    OpenAIEmbedder embedder(config);

    auto vec = embedder.create("hello world");
    REQUIRE(vec.size() == 1024);

    // Check values are reasonable floats
    float sum_sq = 0.0f;
    for (auto v : vec) {
        CHECK(!std::isnan(v));
        CHECK(!std::isinf(v));
        sum_sq += v * v;
    }

    // L2 norm should be reasonable (truncated dimensions may not be exactly 1.0)
    float norm = std::sqrt(sum_sq);
    CHECK(norm > 0.5f);
    CHECK(norm < 2.0f);
}

TEST_CASE("OpenAI Embedder: batch embedding", "[integration][embedder]") {
    auto config = make_config();
    OpenAIEmbedder embedder(config);

    auto vecs = embedder.create_batch({"hello", "world", "test"});
    REQUIRE(vecs.size() == 3);

    for (auto& vec : vecs) {
        CHECK(vec.size() == 1024);
    }

    // Different inputs should produce different embeddings
    bool all_same = true;
    for (size_t i = 0; i < 10; ++i) {
        if (std::abs(vecs[0][i] - vecs[1][i]) > 1e-6f) {
            all_same = false;
            break;
        }
    }
    CHECK(!all_same);
}

TEST_CASE("OpenAI Embedder: dimension truncation", "[integration][embedder]") {
    auto config = make_config();
    config.embedding_dim = 512; // truncate to smaller dim
    OpenAIEmbedder embedder(config);

    auto vec = embedder.create("test truncation");
    CHECK(vec.size() == 512);
}
