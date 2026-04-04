#include "search_utils.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace graphiti {

std::pair<std::vector<std::string>, std::vector<float>> rrf(
    const std::vector<std::vector<std::string>>& results,
    int rank_const,
    float min_score
) {
    std::unordered_map<std::string, float> scores;

    for (auto& result_list : results) {
        for (size_t i = 0; i < result_list.size(); ++i) {
            scores[result_list[i]] += 1.0f / static_cast<float>(i + rank_const);
        }
    }

    // Collect and sort by score descending
    std::vector<std::pair<std::string, float>> scored;
    scored.reserve(scores.size());
    for (auto& [uuid, score] : scores) {
        if (score >= min_score) {
            scored.emplace_back(uuid, score);
        }
    }

    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) {
        return a.second > b.second;
    });

    std::vector<std::string> uuids;
    std::vector<float> out_scores;
    uuids.reserve(scored.size());
    out_scores.reserve(scored.size());

    for (auto& [uuid, score] : scored) {
        uuids.push_back(std::move(uuid));
        out_scores.push_back(score);
    }

    return {std::move(uuids), std::move(out_scores)};
}

float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    return denom == 0.0f ? 0.0f : dot / denom;
}

} // namespace graphiti
