#include "search.h"

#include "driver/kuzu_driver.h"
#include "search/search_utils.h"

#include <graphiti/embedder.h>

#include <format>
#include <unordered_map>

namespace graphiti {

Result<SearchResult> hybrid_edge_search(
    KuzuDriver& driver,
    EmbedderClient& embedder,
    std::string_view query,
    std::string_view group_id,
    int limit,
    float min_score
) {
    // Generate query embedding
    std::vector<float> query_embedding;
    try {
        query_embedding = embedder.create(query);
    } catch (const std::exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::embedding_error,
            std::format("Failed to generate query embedding: {}", e.what())
        });
    }

    // Run BM25 and cosine searches in parallel (but synchronously for Phase 1)
    auto bm25_result = driver.search_entity_edges_bm25(query, group_id, limit);
    auto cosine_result = driver.search_entity_edges_cosine(query_embedding, group_id, 0.0f, limit);

    // Collect all edges by UUID for final assembly
    std::unordered_map<std::string, EntityEdge> edge_map;

    std::vector<std::string> bm25_uuids;
    if (bm25_result.has_value()) {
        for (auto& edge : bm25_result.value()) {
            bm25_uuids.push_back(edge.uuid);
            edge_map.emplace(edge.uuid, std::move(edge));
        }
    }

    std::vector<std::string> cosine_uuids;
    if (cosine_result.has_value()) {
        for (auto& edge : cosine_result.value()) {
            cosine_uuids.push_back(edge.uuid);
            if (!edge_map.contains(edge.uuid)) {
                edge_map.emplace(edge.uuid, std::move(edge));
            }
        }
    }

    // Merge with RRF
    auto [merged_uuids, scores] = rrf({bm25_uuids, cosine_uuids}, 1, min_score);

    // Assemble final results in ranked order, capped at limit
    SearchResult result;
    for (size_t i = 0; i < merged_uuids.size() && static_cast<int>(i) < limit; ++i) {
        auto it = edge_map.find(merged_uuids[i]);
        if (it != edge_map.end()) {
            result.edges.push_back(std::move(it->second));
            result.scores.push_back(scores[i]);
        }
    }

    return result;
}

} // namespace graphiti
