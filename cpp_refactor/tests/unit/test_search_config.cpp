#include <catch2/catch_test_macros.hpp>
#include <graphiti/search_config.h>

using namespace graphiti;

TEST_CASE("edge_hybrid_search_rrf recipe", "[search_config]") {
    auto config = edge_hybrid_search_rrf();
    REQUIRE(config.edge_config.has_value());
    REQUIRE_FALSE(config.node_config.has_value());
    REQUIRE_FALSE(config.episode_config.has_value());

    auto& ec = config.edge_config.value();
    REQUIRE(ec.search_methods.size() == 2);
    REQUIRE(ec.search_methods[0] == EdgeSearchMethod::cosine_similarity);
    REQUIRE(ec.search_methods[1] == EdgeSearchMethod::bm25);
    REQUIRE(ec.reranker == Reranker::rrf);
}

TEST_CASE("edge_hybrid_search_mmr recipe", "[search_config]") {
    auto config = edge_hybrid_search_mmr();
    REQUIRE(config.edge_config.has_value());
    REQUIRE(config.edge_config->reranker == Reranker::mmr);
}

TEST_CASE("edge_hybrid_search_node_distance recipe", "[search_config]") {
    auto config = edge_hybrid_search_node_distance();
    REQUIRE(config.edge_config.has_value());
    REQUIRE(config.edge_config->reranker == Reranker::node_distance);
}

TEST_CASE("edge_hybrid_search_episode_mentions recipe", "[search_config]") {
    auto config = edge_hybrid_search_episode_mentions();
    REQUIRE(config.edge_config.has_value());
    REQUIRE(config.edge_config->reranker == Reranker::episode_mentions);
}

TEST_CASE("edge_hybrid_search_cross_encoder recipe", "[search_config]") {
    auto config = edge_hybrid_search_cross_encoder();
    REQUIRE(config.edge_config.has_value());
    REQUIRE(config.edge_config->reranker == Reranker::cross_encoder);
    // Cross-encoder includes BFS
    REQUIRE(config.edge_config->search_methods.size() == 3);
}

TEST_CASE("node_hybrid_search_rrf recipe", "[search_config]") {
    auto config = node_hybrid_search_rrf();
    REQUIRE_FALSE(config.edge_config.has_value());
    REQUIRE(config.node_config.has_value());
    REQUIRE(config.node_config->reranker == Reranker::rrf);
    REQUIRE(config.node_config->search_methods.size() == 2);
}

TEST_CASE("node_hybrid_search_mmr recipe", "[search_config]") {
    auto config = node_hybrid_search_mmr();
    REQUIRE(config.node_config.has_value());
    REQUIRE(config.node_config->reranker == Reranker::mmr);
}

TEST_CASE("node_hybrid_search_node_distance recipe", "[search_config]") {
    auto config = node_hybrid_search_node_distance();
    REQUIRE(config.node_config.has_value());
    REQUIRE(config.node_config->reranker == Reranker::node_distance);
}

TEST_CASE("node_hybrid_search_episode_mentions recipe", "[search_config]") {
    auto config = node_hybrid_search_episode_mentions();
    REQUIRE(config.node_config.has_value());
    REQUIRE(config.node_config->reranker == Reranker::episode_mentions);
}

TEST_CASE("combined_hybrid_search_rrf recipe", "[search_config]") {
    auto config = combined_hybrid_search_rrf();
    REQUIRE(config.edge_config.has_value());
    REQUIRE(config.node_config.has_value());
    REQUIRE(config.episode_config.has_value());
    REQUIRE(config.edge_config->reranker == Reranker::rrf);
    REQUIRE(config.node_config->reranker == Reranker::rrf);
    REQUIRE(config.episode_config->reranker == Reranker::rrf);
}

TEST_CASE("combined_hybrid_search_mmr recipe", "[search_config]") {
    auto config = combined_hybrid_search_mmr();
    REQUIRE(config.edge_config->reranker == Reranker::mmr);
    REQUIRE(config.node_config->reranker == Reranker::mmr);
    // Episodes don't support MMR, so they stay RRF
    REQUIRE(config.episode_config->reranker == Reranker::rrf);
}

TEST_CASE("combined_hybrid_search_cross_encoder recipe", "[search_config]") {
    auto config = combined_hybrid_search_cross_encoder();
    REQUIRE(config.edge_config->reranker == Reranker::cross_encoder);
    REQUIRE(config.node_config->reranker == Reranker::cross_encoder);
    // Episodes don't support cross-encoder, so they stay RRF
    REQUIRE(config.episode_config->reranker == Reranker::rrf);
}

TEST_CASE("SearchConfig defaults", "[search_config]") {
    SearchConfig config;
    REQUIRE(config.limit == 10);
    REQUIRE(config.reranker_min_score == 0.0f);
    REQUIRE_FALSE(config.edge_config.has_value());
    REQUIRE_FALSE(config.node_config.has_value());
    REQUIRE_FALSE(config.episode_config.has_value());
}

TEST_CASE("EdgeSearchConfig defaults", "[search_config]") {
    EdgeSearchConfig ec;
    REQUIRE(ec.search_methods.size() == 2);
    REQUIRE(ec.reranker == Reranker::rrf);
    REQUIRE(ec.sim_min_score == 0.0f);
    REQUIRE(ec.mmr_lambda == 0.5f);
    REQUIRE(ec.bfs_max_depth == 3);
}

TEST_CASE("NodeSearchConfig defaults", "[search_config]") {
    NodeSearchConfig nc;
    REQUIRE(nc.search_methods.size() == 2);
    REQUIRE(nc.reranker == Reranker::rrf);
    REQUIRE(nc.sim_min_score == 0.0f);
    REQUIRE(nc.mmr_lambda == 0.5f);
    REQUIRE(nc.bfs_max_depth == 3);
}

TEST_CASE("EpisodeSearchConfig defaults", "[search_config]") {
    EpisodeSearchConfig ec;
    REQUIRE(ec.search_methods.size() == 1);
    REQUIRE(ec.search_methods[0] == EpisodeSearchMethod::bm25);
    REQUIRE(ec.reranker == Reranker::rrf);
}

TEST_CASE("CommunitySearchConfig defaults", "[search_config]") {
    CommunitySearchConfig cc;
    REQUIRE(cc.search_methods.size() == 2);
    REQUIRE(cc.search_methods[0] == CommunitySearchMethod::cosine_similarity);
    REQUIRE(cc.search_methods[1] == CommunitySearchMethod::bm25);
    REQUIRE(cc.reranker == Reranker::rrf);
    REQUIRE(cc.sim_min_score == 0.0f);
    REQUIRE(cc.mmr_lambda == 0.5f);
}

TEST_CASE("community_hybrid_search_rrf recipe", "[search_config]") {
    auto config = community_hybrid_search_rrf();
    REQUIRE(config.community_config.has_value());
    REQUIRE(config.community_config->reranker == Reranker::rrf);
    REQUIRE(config.limit == 3);
}

TEST_CASE("community_hybrid_search_mmr recipe", "[search_config]") {
    auto config = community_hybrid_search_mmr();
    REQUIRE(config.community_config.has_value());
    REQUIRE(config.community_config->reranker == Reranker::mmr);
}
