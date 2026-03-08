#include <catch2/catch_all.hpp>

#include "search/search_utils.h"

using namespace graphiti;

// ============================================================================
// RRF
// ============================================================================

TEST_CASE("RRF: single list", "[search][rrf]") {
    auto [uuids, scores] = rrf({{"a", "b", "c"}});

    REQUIRE(uuids.size() == 3);
    // Rank 0 gets highest score: 1/(0+1) = 1.0
    CHECK(uuids[0] == "a");
    CHECK(scores[0] == Catch::Approx(1.0f));
    CHECK(uuids[1] == "b");
    CHECK(scores[1] == Catch::Approx(0.5f));
    CHECK(uuids[2] == "c");
    CHECK(scores[2] == Catch::Approx(1.0f / 3.0f));
}

TEST_CASE("RRF: two overlapping lists", "[search][rrf]") {
    auto [uuids, scores] = rrf({
        {"a", "b", "c"},
        {"b", "a", "d"},
    });

    REQUIRE(uuids.size() == 4);

    // "a" appears at rank 0 in list 1 and rank 1 in list 2: 1/1 + 1/2 = 1.5
    // "b" appears at rank 1 in list 1 and rank 0 in list 2: 1/2 + 1/1 = 1.5
    // Both should be first (order between ties is implementation-defined)
    float score_a = -1, score_b = -1;
    for (size_t i = 0; i < uuids.size(); ++i) {
        if (uuids[i] == "a") score_a = scores[i];
        if (uuids[i] == "b") score_b = scores[i];
    }
    CHECK(score_a == Catch::Approx(1.5f));
    CHECK(score_b == Catch::Approx(1.5f));
}

TEST_CASE("RRF: empty lists", "[search][rrf]") {
    auto [uuids, scores] = rrf({});
    CHECK(uuids.empty());
    CHECK(scores.empty());
}

TEST_CASE("RRF: all empty inner lists", "[search][rrf]") {
    auto [uuids, scores] = rrf({{}, {}});
    CHECK(uuids.empty());
}

TEST_CASE("RRF: no overlap", "[search][rrf]") {
    auto [uuids, scores] = rrf({{"a", "b"}, {"c", "d"}});
    REQUIRE(uuids.size() == 4);

    // All items appear in exactly one list
    for (auto& s : scores) {
        CHECK(s <= 1.0f);
    }
}

TEST_CASE("RRF: min_score filter", "[search][rrf]") {
    auto [uuids, scores] = rrf({{"a", "b", "c"}}, 1, 0.5f);

    // "c" has score 1/3 which is below 0.5
    CHECK(uuids.size() == 2);
    CHECK(uuids[0] == "a");
    CHECK(uuids[1] == "b");
}

TEST_CASE("RRF: custom rank_const", "[search][rrf]") {
    // With rank_const=60 (standard RRF), scores are smaller
    auto [uuids, scores] = rrf({{"a", "b"}}, 60);

    CHECK(scores[0] == Catch::Approx(1.0f / 60.0f));
    CHECK(scores[1] == Catch::Approx(1.0f / 61.0f));
}

// ============================================================================
// Cosine Similarity
// ============================================================================

TEST_CASE("cosine_similarity: identical vectors", "[search][cosine]") {
    std::vector<float> v = {1.0f, 2.0f, 3.0f};
    CHECK(cosine_similarity(v, v) == Catch::Approx(1.0f));
}

TEST_CASE("cosine_similarity: orthogonal vectors", "[search][cosine]") {
    CHECK(cosine_similarity({1, 0, 0}, {0, 1, 0}) == Catch::Approx(0.0f));
}

TEST_CASE("cosine_similarity: opposite vectors", "[search][cosine]") {
    CHECK(cosine_similarity({1, 0}, {-1, 0}) == Catch::Approx(-1.0f));
}

TEST_CASE("cosine_similarity: zero vector", "[search][cosine]") {
    CHECK(cosine_similarity({0, 0}, {1, 1}) == Catch::Approx(0.0f));
}

TEST_CASE("cosine_similarity: empty vectors", "[search][cosine]") {
    CHECK(cosine_similarity({}, {}) == Catch::Approx(0.0f));
}
