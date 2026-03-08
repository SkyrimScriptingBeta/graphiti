#include <graphiti/search_config.h>

namespace graphiti {

SearchConfig edge_hybrid_search_rrf() {
    SearchConfig config;
    config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
        .reranker = Reranker::rrf,
    };
    return config;
}

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
    return config;
}

} // namespace graphiti
