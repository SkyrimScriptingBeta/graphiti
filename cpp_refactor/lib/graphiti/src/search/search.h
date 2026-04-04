#pragma once

#include <graphiti/error.h>
#include <graphiti/search_config.h>
#include <graphiti/search_filters.h>
#include <graphiti/types.h>

#include <string>
#include <vector>

namespace graphiti {

class GraphStore;
class EmbedderClient;
class LLMClient;

struct SearchResult {
    std::vector<EntityEdge> edges;
    std::vector<float> scores;
};

struct NodeSearchResult {
    std::vector<EntityNode> nodes;
    std::vector<float> scores;
};

struct EpisodeSearchResult {
    std::vector<EpisodicNode> episodes;
    std::vector<float> scores;
};

// Hybrid edge search: BM25 + cosine + optional BFS, merged with RRF.
Result<SearchResult> hybrid_edge_search(
    GraphStore& store,
    EmbedderClient& embedder,
    std::string_view query,
    std::string_view group_id,
    int limit = 10,
    float min_score = 0.0f,
    const SearchFilters* filters = nullptr,
    const std::vector<std::string>* bfs_origin_uuids = nullptr,
    int bfs_max_depth = 3
);

// Episode search: BM25 fulltext on episode content.
Result<EpisodeSearchResult> episode_search(
    GraphStore& store,
    std::string_view query,
    std::string_view group_id,
    int limit = 10
);

// Full advanced search orchestrator: runs edge, node, and episode searches
// based on config, then applies rerankers.
Result<SearchResults> search_orchestrator(
    GraphStore& store,
    EmbedderClient& embedder,
    LLMClient& llm,
    std::string_view query,
    std::string_view group_id,
    const SearchConfig& config,
    const SearchFilters* filters = nullptr,
    const std::string* center_node_uuid = nullptr,
    const std::vector<std::string>* bfs_origin_node_uuids = nullptr
);

} // namespace graphiti
