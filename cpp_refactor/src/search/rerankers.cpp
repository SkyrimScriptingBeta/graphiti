#include "rerankers.h"

#include "driver/kuzu_driver.h"
#include "search/search_utils.h"

#include <graphiti/llm_client.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <unordered_map>

namespace graphiti {

// ============================================================================
// Episode mentions reranker
// ============================================================================

Result<std::pair<std::vector<std::string>, std::vector<float>>> episode_mentions_reranker(
    KuzuDriver& driver,
    const std::vector<std::vector<std::string>>& node_uuid_lists,
    float min_score
) {
    // Step 1: RRF to get preliminary ordering
    auto [sorted_uuids, _] = rrf(node_uuid_lists);

    // Step 2: Query episode mention counts per node
    std::unordered_map<std::string, float> scores;
    for (auto& uuid : sorted_uuids) {
        auto count_result = driver.count_episode_mentions(uuid);
        scores[uuid] = count_result.has_value() ? static_cast<float>(count_result.value()) : 0.0f;
    }

    // Step 3: Sort by mention count descending
    std::sort(sorted_uuids.begin(), sorted_uuids.end(),
        [&scores](const std::string& a, const std::string& b) {
            return scores[a] > scores[b];
        }
    );

    // Step 4: Filter and build result
    std::vector<std::string> result_uuids;
    std::vector<float> result_scores;
    for (auto& uuid : sorted_uuids) {
        if (scores[uuid] >= min_score) {
            result_uuids.push_back(uuid);
            result_scores.push_back(scores[uuid]);
        }
    }

    return std::make_pair(std::move(result_uuids), std::move(result_scores));
}

// ============================================================================
// Node distance reranker
// ============================================================================

Result<std::pair<std::vector<std::string>, std::vector<float>>> node_distance_reranker(
    KuzuDriver& driver,
    const std::vector<std::string>& node_uuids,
    std::string_view center_node_uuid,
    float min_score
) {
    // Remove center from candidates
    std::vector<std::string> filtered;
    bool center_in_list = false;
    for (auto& uuid : node_uuids) {
        if (uuid == center_node_uuid) {
            center_in_list = true;
        } else {
            filtered.push_back(uuid);
        }
    }

    // Query 1-hop adjacency to center node
    std::unordered_map<std::string, float> raw_scores;
    for (auto& uuid : filtered) {
        auto adj_result = driver.check_node_adjacency(center_node_uuid, uuid);
        if (adj_result.has_value() && adj_result.value()) {
            raw_scores[uuid] = 1.0f;  // 1-hop connected
        } else {
            raw_scores[uuid] = std::numeric_limits<float>::infinity();
        }
    }

    // Sort: connected first (score 1), unconnected last (inf)
    std::sort(filtered.begin(), filtered.end(),
        [&raw_scores](const std::string& a, const std::string& b) {
            return raw_scores[a] < raw_scores[b];
        }
    );

    // Re-insert center at front
    if (center_in_list) {
        raw_scores[std::string(center_node_uuid)] = 0.1f;
        filtered.insert(filtered.begin(), std::string(center_node_uuid));
    }

    // Convert to inverse-distance scores and filter
    std::vector<std::string> result_uuids;
    std::vector<float> result_scores;
    for (auto& uuid : filtered) {
        float score = 1.0f / raw_scores[uuid];  // center=10.0, adjacent=1.0, unconnected=0.0
        if (score >= min_score) {
            result_uuids.push_back(uuid);
            result_scores.push_back(score);
        }
    }

    return std::make_pair(std::move(result_uuids), std::move(result_scores));
}

// ============================================================================
// MMR (Maximal Marginal Relevance)
// ============================================================================

static float dot_product(const std::vector<float>& a, const std::vector<float>& b) {
    float sum = 0.0f;
    auto n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

static std::vector<float> normalize_l2(const std::vector<float>& v) {
    float norm = 0.0f;
    for (auto x : v) norm += x * x;
    norm = std::sqrt(norm);
    if (norm < 1e-10f) return v;
    std::vector<float> result(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        result[i] = v[i] / norm;
    }
    return result;
}

std::pair<std::vector<std::string>, std::vector<float>> maximal_marginal_relevance(
    const std::vector<float>& query_vector,
    const std::unordered_map<std::string, std::vector<float>>& candidates,
    float mmr_lambda,
    float min_score
) {
    if (candidates.empty()) return {{}, {}};

    auto norm_query = normalize_l2(query_vector);

    std::vector<std::string> uuids;
    std::unordered_map<std::string, std::vector<float>> norm_candidates;
    for (auto& [uuid, vec] : candidates) {
        uuids.push_back(uuid);
        norm_candidates[uuid] = normalize_l2(vec);
    }

    // Build pairwise similarity matrix
    auto n = uuids.size();
    std::vector<std::vector<float>> sim_matrix(n, std::vector<float>(n, 0.0f));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < i; ++j) {
            float sim = dot_product(norm_candidates[uuids[i]], norm_candidates[uuids[j]]);
            sim_matrix[i][j] = sim;
            sim_matrix[j][i] = sim;
        }
    }

    // Score each candidate
    std::unordered_map<std::string, float> mmr_scores;
    for (size_t i = 0; i < n; ++i) {
        float max_sim = *std::max_element(sim_matrix[i].begin(), sim_matrix[i].end());
        float query_sim = dot_product(norm_query, norm_candidates[uuids[i]]);
        float mmr = mmr_lambda * query_sim + (mmr_lambda - 1.0f) * max_sim;
        mmr_scores[uuids[i]] = mmr;
    }

    // Sort descending
    std::sort(uuids.begin(), uuids.end(),
        [&mmr_scores](const std::string& a, const std::string& b) {
            return mmr_scores[a] > mmr_scores[b];
        }
    );

    std::vector<std::string> result_uuids;
    std::vector<float> result_scores;
    for (auto& uuid : uuids) {
        if (mmr_scores[uuid] >= min_score) {
            result_uuids.push_back(uuid);
            result_scores.push_back(mmr_scores[uuid]);
        }
    }

    return {std::move(result_uuids), std::move(result_scores)};
}

// ============================================================================
// Cross-encoder reranker (LLM-based)
// ============================================================================

Result<std::vector<std::pair<std::string, float>>> cross_encoder_rerank(
    LLMClient& llm,
    std::string_view query,
    const std::vector<std::string>& passages
) {
    std::vector<std::pair<std::string, float>> results;
    results.reserve(passages.size());

    for (auto& passage : passages) {
        auto prompt = std::format(
            R"(Respond with "True" if PASSAGE is relevant to QUERY and "False" otherwise.

<PASSAGE>{}</PASSAGE>
<QUERY>{}</QUERY>)",
            passage, query
        );

        std::vector<Message> messages = {
            {"system", "You are an expert tasked with determining whether the passage is relevant to the query. Respond with only 'True' or 'False'."},
            {"user", prompt}
        };

        constexpr int MAX_RETRIES = 2;
        bool rerank_done = false;
        llm.prompt_name = "rerank";
        for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
            auto response = llm.generate_response(messages, std::nullopt, ModelSize::small);
            if (!response.has_value()) {
                if (attempt < MAX_RETRIES) {
                    fprintf(stderr, "  [graphiti] rerank failed (attempt %d/%d), retrying\n",
                            attempt + 1, MAX_RETRIES + 1);
                    continue;
                }
                results.emplace_back(passage, 0.5f);
                rerank_done = true;
                break;
            }

            // Parse: look for "true" or "false" in the response
            std::string content;
            if (response.value().is_string()) {
                content = response.value().get<std::string>();
            } else if (response.value().contains("content")) {
                content = response.value()["content"].get<std::string>();
            } else {
                content = response.value().dump();
            }

            std::string lower;
            for (char c : content) lower += static_cast<char>(std::tolower(c));

            if (lower.find("true") != std::string::npos) {
                results.emplace_back(passage, 0.9f);
            } else {
                results.emplace_back(passage, 0.1f);
            }
            rerank_done = true;
            break; // success
        }
        if (!rerank_done) {
            results.emplace_back(passage, 0.5f);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; }
    );

    return results;
}

} // namespace graphiti
