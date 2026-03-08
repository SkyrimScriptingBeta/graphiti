#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {

class KuzuDriver;
class LLMClient;

// Episode mentions reranker: scores nodes by how many episodes mention them.
// Nodes mentioned in more episodes rank higher.
Result<std::pair<std::vector<std::string>, std::vector<float>>> episode_mentions_reranker(
    KuzuDriver& driver,
    const std::vector<std::vector<std::string>>& node_uuid_lists,
    float min_score = 0.0f
);

// Node distance reranker: scores nodes by 1-hop adjacency to a center node.
// Connected = score 1.0, center = 10.0, unconnected = 0.0.
Result<std::pair<std::vector<std::string>, std::vector<float>>> node_distance_reranker(
    KuzuDriver& driver,
    const std::vector<std::string>& node_uuids,
    std::string_view center_node_uuid,
    float min_score = 0.0f
);

// MMR (Maximal Marginal Relevance) reranker: balances relevance to query
// with diversity among results.
// mmr_lambda: 0.0 = pure diversity, 1.0 = pure relevance, 0.5 = balanced.
std::pair<std::vector<std::string>, std::vector<float>> maximal_marginal_relevance(
    const std::vector<float>& query_vector,
    const std::unordered_map<std::string, std::vector<float>>& candidates,
    float mmr_lambda = 0.5f,
    float min_score = -2.0f
);

// Cross-encoder reranker: uses LLM to score passage relevance.
// Returns (passage_text, score) pairs sorted descending.
Result<std::vector<std::pair<std::string, float>>> cross_encoder_rerank(
    LLMClient& llm,
    std::string_view query,
    const std::vector<std::string>& passages
);

} // namespace graphiti
