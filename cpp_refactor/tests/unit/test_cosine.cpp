#include <catch2/catch_all.hpp>

#include <cmath>
#include <vector>

#include "../../src/utils/cosine.h"

using namespace graphiti;

TEST_CASE("Cosine similarity of identical vectors is 1.0", "[cosine]") {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    REQUIRE(cosine_similarity(a, a) == Catch::Approx(1.0f));
}

TEST_CASE("Cosine similarity of orthogonal vectors is 0.0", "[cosine]") {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(0.0f));
}

TEST_CASE("Cosine similarity of opposite vectors is -1.0", "[cosine]") {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {-1.0f, 0.0f};
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(-1.0f));
}

TEST_CASE("Cosine similarity of zero vector is 0.0", "[cosine]") {
    std::vector<float> a = {0.0f, 0.0f, 0.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(0.0f));
}

TEST_CASE("Cosine similarity of empty vectors is 0.0", "[cosine]") {
    std::vector<float> a;
    std::vector<float> b;
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(0.0f));
}

TEST_CASE("Cosine similarity known angle (45 degrees)", "[cosine]") {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {1.0f, 1.0f};
    // cos(45) = 1/sqrt(2) ≈ 0.7071
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(1.0f / std::sqrt(2.0f)).margin(0.0001f));
}

TEST_CASE("Cosine similarity is scale-invariant", "[cosine]") {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f};
    std::vector<float> b_scaled = {40.0f, 50.0f, 60.0f};
    REQUIRE(cosine_similarity(a, b) == Catch::Approx(cosine_similarity(a, b_scaled)));
}
