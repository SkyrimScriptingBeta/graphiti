#include <graphiti/search_config.h>

namespace graphiti {

// ============================================================================
// Edge-only recipes
// ============================================================================

SearchConfig edge_hybrid_search_rrf() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

SearchConfig edge_hybrid_search_mmr() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::mmr,
    };
    return config;
}

SearchConfig edge_hybrid_search_node_distance() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::node_distance,
    };
    return config;
}

SearchConfig edge_hybrid_search_episode_mentions() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::episode_mentions,
    };
    return config;
}

SearchConfig edge_hybrid_search_cross_encoder() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25, EdgeSearchMethod::bfs},
        .reranker = Reranker::cross_encoder,
    };
    config.limit = 10;
    return config;
}

// ============================================================================
// Node-only recipes
// ============================================================================

SearchConfig node_hybrid_search_rrf() {
    SearchConfig config;
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

SearchConfig node_hybrid_search_mmr() {
    SearchConfig config;
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::mmr,
    };
    return config;
}

SearchConfig node_hybrid_search_node_distance() {
    SearchConfig config;
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::node_distance,
    };
    return config;
}

SearchConfig node_hybrid_search_episode_mentions() {
    SearchConfig config;
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::episode_mentions,
    };
    return config;
}

// ============================================================================
// Combined recipes (edges + nodes + episodes)
// ============================================================================

SearchConfig combined_hybrid_search_rrf() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    config.episode_config = EpisodeSearchConfig{
        .search_methods = {EpisodeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

SearchConfig combined_hybrid_search_mmr() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::mmr,
    };
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25},
        .reranker = Reranker::mmr,
    };
    config.episode_config = EpisodeSearchConfig{
        .search_methods = {EpisodeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

SearchConfig combined_hybrid_search_cross_encoder() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25, EdgeSearchMethod::bfs},
        .reranker = Reranker::cross_encoder,
    };
    config.node_config = NodeSearchConfig{
        .search_methods = {NodeSearchMethod::cosine_similarity, NodeSearchMethod::bm25, NodeSearchMethod::bfs},
        .reranker = Reranker::cross_encoder,
    };
    config.episode_config = EpisodeSearchConfig{
        .search_methods = {EpisodeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

} // namespace graphiti
