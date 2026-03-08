#pragma once

#include <graphiti/types.h>

#include <optional>
#include <string>
#include <vector>

namespace graphiti {

enum class EdgeSearchMethod { cosine_similarity, bm25, bfs };
enum class NodeSearchMethod { cosine_similarity, bm25, bfs };
enum class EpisodeSearchMethod { bm25 };

enum class Reranker { rrf, node_distance, episode_mentions, mmr, cross_encoder };

struct EdgeSearchConfig {
    std::vector<EdgeSearchMethod> search_methods = {EdgeSearchMethod::cosine_similarity,
                                                    EdgeSearchMethod::bm25};
    Reranker reranker = Reranker::rrf;
    float sim_min_score = 0.0f;
    float mmr_lambda = 0.5f;
    int bfs_max_depth = 3;
};

struct NodeSearchConfig {
    std::vector<NodeSearchMethod> search_methods = {NodeSearchMethod::cosine_similarity,
                                                    NodeSearchMethod::bm25};
    Reranker reranker = Reranker::rrf;
    float sim_min_score = 0.0f;
    float mmr_lambda = 0.5f;
    int bfs_max_depth = 3;
};

struct EpisodeSearchConfig {
    std::vector<EpisodeSearchMethod> search_methods = {EpisodeSearchMethod::bm25};
    Reranker reranker = Reranker::rrf;
};

struct SearchConfig {
    std::optional<EdgeSearchConfig> edge_config;
    std::optional<NodeSearchConfig> node_config;
    std::optional<EpisodeSearchConfig> episode_config;
    int limit = 10;
    float reranker_min_score = 0.0f;
};

struct SearchResults {
    std::vector<EntityEdge> edges;
    std::vector<float> edge_scores;
    std::vector<EntityNode> nodes;
    std::vector<float> node_scores;
    std::vector<EpisodicNode> episodes;
    std::vector<float> episode_scores;
};

// Pre-built search config recipes

// Edge-only recipes
SearchConfig edge_hybrid_search_rrf();
SearchConfig edge_hybrid_search_mmr();
SearchConfig edge_hybrid_search_node_distance();
SearchConfig edge_hybrid_search_episode_mentions();
SearchConfig edge_hybrid_search_cross_encoder();

// Node-only recipes
SearchConfig node_hybrid_search_rrf();
SearchConfig node_hybrid_search_mmr();
SearchConfig node_hybrid_search_node_distance();
SearchConfig node_hybrid_search_episode_mentions();

// Combined recipes (edges + nodes + episodes)
SearchConfig combined_hybrid_search_rrf();
SearchConfig combined_hybrid_search_mmr();
SearchConfig combined_hybrid_search_cross_encoder();

} // namespace graphiti
