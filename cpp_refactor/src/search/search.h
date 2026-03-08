#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <string>
#include <vector>

namespace graphiti {

class KuzuDriver;
class EmbedderClient;

struct SearchResult {
    std::vector<EntityEdge> edges;
    std::vector<float> scores;
};

// Hybrid search: BM25 fulltext + cosine similarity with RRF reranking
// This is the Phase 1 default search strategy.
Result<SearchResult> hybrid_edge_search(
    KuzuDriver& driver,
    EmbedderClient& embedder,
    std::string_view query,
    std::string_view group_id,
    int limit = 10,
    float min_score = 0.0f
);

} // namespace graphiti
