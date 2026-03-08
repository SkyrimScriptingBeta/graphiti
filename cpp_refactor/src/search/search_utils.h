#pragma once

#include <string>
#include <utility>
#include <vector>

namespace graphiti {

// Reciprocal Rank Fusion: merges multiple ranked result lists.
// Each inner vector is a ranked list of UUIDs (best first).
// Returns (merged_uuids, scores) sorted by score descending.
std::pair<std::vector<std::string>, std::vector<float>> rrf(
    const std::vector<std::vector<std::string>>& results,
    int rank_const = 1,
    float min_score = 0.0f
);

// Cosine similarity between two vectors (header-only in cosine.h, but also exposed here)
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

} // namespace graphiti
