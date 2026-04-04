#pragma once

#include <cmath>
#include <numeric>
#include <span>

namespace graphiti {

inline float cosine_similarity(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom == 0.0f) return 0.0f;
    return dot / denom;
}

} // namespace graphiti
