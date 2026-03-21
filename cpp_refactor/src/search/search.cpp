#include "search.h"

#include "driver/kuzu_driver.h"
#include "search/bfs_search.h"
#include "search/rerankers.h"
#include "search/search_utils.h"

#include <graphiti/embedder.h>
#include <graphiti/llm_client.h>

#include <format>
#include <unordered_map>
#include <unordered_set>

namespace graphiti {

// ============================================================================
// Hybrid edge search
// ============================================================================

Result<SearchResult> hybrid_edge_search(
    KuzuDriver& driver,
    EmbedderClient& embedder,
    std::string_view query,
    std::string_view group_id,
    int limit,
    float min_score,
    const SearchFilters* filters,
    const std::vector<std::string>* bfs_origin_uuids,
    int bfs_max_depth
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

    // Run BM25 and cosine searches
    auto bm25_result = driver.search_entity_edges_bm25(query, group_id, limit, filters);
    auto cosine_result = driver.search_entity_edges_cosine(query_embedding, group_id, 0.0f, limit, filters);

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

    // BFS search: if origins provided, run alongside BM25/cosine.
    // If not provided, self-seed from BM25/cosine source nodes.
    std::vector<std::string> bfs_uuids;
    std::vector<std::string> self_seeded_origins;

    const std::vector<std::string>* origins = bfs_origin_uuids;
    if (!origins) {
        // Self-seed: use source nodes from BM25 and cosine results
        std::unordered_map<std::string, bool> seen_origins;
        for (auto& [uuid, edge] : edge_map) {
            if (!seen_origins.contains(edge.source_node_uuid)) {
                seen_origins[edge.source_node_uuid] = true;
                self_seeded_origins.push_back(edge.source_node_uuid);
            }
        }
        if (!self_seeded_origins.empty()) {
            origins = &self_seeded_origins;
        }
    }

    if (origins && !origins->empty()) {
        auto bfs_result = edge_bfs_search(
            driver, *origins, bfs_max_depth, filters, group_id, 2 * limit
        );
        if (bfs_result.has_value()) {
            for (auto& edge : bfs_result.value()) {
                bfs_uuids.push_back(edge.uuid);
                if (!edge_map.contains(edge.uuid)) {
                    edge_map.emplace(edge.uuid, std::move(edge));
                }
            }
        }
    }

    // Merge all result lists with RRF
    auto [merged_uuids, scores] = rrf({bm25_uuids, cosine_uuids, bfs_uuids}, 1, min_score);

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

// ============================================================================
// Hybrid node search
// ============================================================================

Result<NodeSearchResult> hybrid_node_search(
    KuzuDriver& driver,
    EmbedderClient& embedder,
    std::string_view query,
    std::string_view group_id,
    int limit,
    float min_score,
    const SearchFilters* filters,
    const std::vector<std::string>* bfs_origin_uuids,
    int bfs_max_depth
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

    // Run BM25 and cosine searches
    auto bm25_result = driver.search_entity_nodes_bm25(query, group_id, limit, filters);
    auto cosine_result = driver.search_entity_nodes_cosine(query_embedding, group_id, 0.0f, limit, filters);

    // Collect all nodes by UUID for final assembly
    std::unordered_map<std::string, EntityNode> node_map;

    std::vector<std::string> bm25_uuids;
    if (bm25_result.has_value()) {
        for (auto& node : bm25_result.value()) {
            bm25_uuids.push_back(node.uuid);
            node_map.emplace(node.uuid, std::move(node));
        }
    }

    std::vector<std::string> cosine_uuids;
    if (cosine_result.has_value()) {
        for (auto& node : cosine_result.value()) {
            cosine_uuids.push_back(node.uuid);
            if (!node_map.contains(node.uuid)) {
                node_map.emplace(node.uuid, std::move(node));
            }
        }
    }

    // BFS search for nodes
    std::vector<std::string> bfs_uuids;
    if (bfs_origin_uuids && !bfs_origin_uuids->empty()) {
        auto bfs_result = node_bfs_search(
            driver, *bfs_origin_uuids, bfs_max_depth, filters, group_id, 2 * limit
        );
        if (bfs_result.has_value()) {
            for (auto& node : bfs_result.value()) {
                bfs_uuids.push_back(node.uuid);
                if (!node_map.contains(node.uuid)) {
                    node_map.emplace(node.uuid, std::move(node));
                }
            }
        }
    }

    // Merge with RRF
    auto [merged_uuids, scores] = rrf({bm25_uuids, cosine_uuids, bfs_uuids}, 1, min_score);

    // Assemble final results
    NodeSearchResult result;
    for (size_t i = 0; i < merged_uuids.size() && static_cast<int>(i) < limit; ++i) {
        auto it = node_map.find(merged_uuids[i]);
        if (it != node_map.end()) {
            result.nodes.push_back(std::move(it->second));
            result.scores.push_back(scores[i]);
        }
    }

    return result;
}

// ============================================================================
// Episode search (BM25 only)
// ============================================================================

Result<EpisodeSearchResult> episode_search(
    KuzuDriver& driver,
    std::string_view query,
    std::string_view group_id,
    int limit
) {
    auto bm25_result = driver.search_episodes_bm25(query, group_id, limit);
    if (!bm25_result.has_value()) return std::unexpected(bm25_result.error());

    EpisodeSearchResult result;
    result.episodes = std::move(bm25_result.value());
    // BM25 doesn't provide scores from Kuzu FTS, assign rank-based scores
    for (size_t i = 0; i < result.episodes.size(); ++i) {
        result.scores.push_back(1.0f / static_cast<float>(i + 1));
    }

    return result;
}

// ============================================================================
// Search orchestrator (search_advanced implementation)
// ============================================================================

Result<SearchResults> search_orchestrator(
    KuzuDriver& driver,
    EmbedderClient& embedder,
    LLMClient& llm,
    std::string_view query,
    std::string_view group_id,
    const SearchConfig& config,
    const SearchFilters* filters,
    const std::string* center_node_uuid,
    const std::vector<std::string>* bfs_origin_node_uuids
) {
    SearchResults results;
    int limit = config.limit;
    float reranker_min = config.reranker_min_score;

    // --- Edge search ---
    if (config.edge_config.has_value()) {
        auto& ec = config.edge_config.value();

        // Determine which methods to run
        bool do_bm25 = false, do_cosine = false, do_bfs = false;
        for (auto m : ec.search_methods) {
            switch (m) {
                case EdgeSearchMethod::bm25: do_bm25 = true; break;
                case EdgeSearchMethod::cosine_similarity: do_cosine = true; break;
                case EdgeSearchMethod::bfs: do_bfs = true; break;
            }
        }

        // Generate embedding if needed
        std::vector<float> query_embedding;
        if (do_cosine) {
            try {
                query_embedding = embedder.create(query);
            } catch (const std::exception& e) {
                return std::unexpected(GraphitiError{
                    ErrorCode::embedding_error,
                    std::format("Failed to generate query embedding: {}", e.what())
                });
            }
        }

        // Run searches
        std::unordered_map<std::string, EntityEdge> edge_map;
        std::vector<std::string> bm25_uuids, cosine_uuids, bfs_uuids;

        if (do_bm25) {
            auto r = driver.search_entity_edges_bm25(query, group_id, limit, filters);
            if (r.has_value()) {
                for (auto& edge : r.value()) {
                    bm25_uuids.push_back(edge.uuid);
                    edge_map.emplace(edge.uuid, std::move(edge));
                }
            }
        }

        if (do_cosine) {
            auto r = driver.search_entity_edges_cosine(
                query_embedding, group_id, ec.sim_min_score, limit, filters);
            if (r.has_value()) {
                for (auto& edge : r.value()) {
                    cosine_uuids.push_back(edge.uuid);
                    if (!edge_map.contains(edge.uuid))
                        edge_map.emplace(edge.uuid, std::move(edge));
                }
            }
        }

        if (do_bfs && bfs_origin_node_uuids && !bfs_origin_node_uuids->empty()) {
            auto r = edge_bfs_search(
                driver, *bfs_origin_node_uuids, ec.bfs_max_depth, filters, group_id, 2 * limit);
            if (r.has_value()) {
                for (auto& edge : r.value()) {
                    bfs_uuids.push_back(edge.uuid);
                    if (!edge_map.contains(edge.uuid))
                        edge_map.emplace(edge.uuid, std::move(edge));
                }
            }
        }

        // Rerank
        std::vector<std::string> ranked_uuids;
        std::vector<float> ranked_scores;

        switch (ec.reranker) {
            case Reranker::rrf: {
                auto [u, s] = rrf({bm25_uuids, cosine_uuids, bfs_uuids}, 1, reranker_min);
                ranked_uuids = std::move(u);
                ranked_scores = std::move(s);
                break;
            }
            case Reranker::episode_mentions: {
                auto r = episode_mentions_reranker(
                    driver, {bm25_uuids, cosine_uuids, bfs_uuids}, reranker_min);
                if (r.has_value()) {
                    ranked_uuids = std::move(r.value().first);
                    ranked_scores = std::move(r.value().second);
                }
                break;
            }
            case Reranker::node_distance: {
                // Flatten all UUIDs, then rerank by distance to center
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                if (center_node_uuid) {
                    auto r = node_distance_reranker(
                        driver, rrf_uuids, *center_node_uuid, reranker_min);
                    if (r.has_value()) {
                        ranked_uuids = std::move(r.value().first);
                        ranked_scores = std::move(r.value().second);
                    }
                } else {
                    ranked_uuids = std::move(rrf_uuids);
                    ranked_scores.resize(ranked_uuids.size(), 1.0f);
                }
                break;
            }
            case Reranker::mmr: {
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                // Build candidate embeddings map
                std::unordered_map<std::string, std::vector<float>> candidates;
                for (auto& uuid : rrf_uuids) {
                    auto emb = driver.load_entity_edge_embedding(uuid);
                    if (emb.has_value() && emb.value().has_value()) {
                        candidates[uuid] = std::move(emb.value().value());
                    }
                }
                if (!candidates.empty() && !query_embedding.empty()) {
                    auto [u, s] = maximal_marginal_relevance(
                        query_embedding, candidates, ec.mmr_lambda, reranker_min);
                    ranked_uuids = std::move(u);
                    ranked_scores = std::move(s);
                } else {
                    ranked_uuids = std::move(rrf_uuids);
                    ranked_scores.resize(ranked_uuids.size(), 0.0f);
                }
                break;
            }
            case Reranker::cross_encoder: {
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                // Build passages from edge facts
                std::vector<std::string> passages;
                std::vector<std::string> passage_uuids;
                for (auto& uuid : rrf_uuids) {
                    auto it = edge_map.find(uuid);
                    if (it != edge_map.end()) {
                        passages.push_back(it->second.fact);
                        passage_uuids.push_back(uuid);
                    }
                }
                auto r = cross_encoder_rerank(llm, query, passages);
                if (r.has_value()) {
                    // Map passage back to UUID via position
                    std::unordered_map<std::string, std::string> passage_to_uuid;
                    for (size_t i = 0; i < passages.size(); ++i) {
                        passage_to_uuid[passages[i]] = passage_uuids[i];
                    }
                    for (auto& [passage, score] : r.value()) {
                        if (score >= reranker_min) {
                            auto it = passage_to_uuid.find(passage);
                            if (it != passage_to_uuid.end()) {
                                ranked_uuids.push_back(it->second);
                                ranked_scores.push_back(score);
                            }
                        }
                    }
                }
                break;
            }
        }

        // Assemble edges in ranked order
        for (size_t i = 0; i < ranked_uuids.size() && static_cast<int>(i) < limit; ++i) {
            auto it = edge_map.find(ranked_uuids[i]);
            if (it != edge_map.end()) {
                results.edges.push_back(std::move(it->second));
                results.edge_scores.push_back(ranked_scores[i]);
            }
        }
    }

    // --- Node search ---
    if (config.node_config.has_value()) {
        auto& nc = config.node_config.value();

        bool do_bm25 = false, do_cosine = false, do_bfs = false;
        for (auto m : nc.search_methods) {
            switch (m) {
                case NodeSearchMethod::bm25: do_bm25 = true; break;
                case NodeSearchMethod::cosine_similarity: do_cosine = true; break;
                case NodeSearchMethod::bfs: do_bfs = true; break;
            }
        }

        std::vector<float> query_embedding;
        if (do_cosine) {
            try {
                query_embedding = embedder.create(query);
            } catch (const std::exception& e) {
                return std::unexpected(GraphitiError{
                    ErrorCode::embedding_error,
                    std::format("Failed to generate query embedding: {}", e.what())
                });
            }
        }

        std::unordered_map<std::string, EntityNode> node_map;
        std::vector<std::string> bm25_uuids, cosine_uuids, bfs_uuids;

        if (do_bm25) {
            auto r = driver.search_entity_nodes_bm25(query, group_id, limit, filters);
            if (r.has_value()) {
                for (auto& node : r.value()) {
                    bm25_uuids.push_back(node.uuid);
                    node_map.emplace(node.uuid, std::move(node));
                }
            }
        }

        if (do_cosine) {
            auto r = driver.search_entity_nodes_cosine(
                query_embedding, group_id, nc.sim_min_score, limit, filters);
            if (r.has_value()) {
                for (auto& node : r.value()) {
                    cosine_uuids.push_back(node.uuid);
                    if (!node_map.contains(node.uuid))
                        node_map.emplace(node.uuid, std::move(node));
                }
            }
        }

        if (do_bfs && bfs_origin_node_uuids && !bfs_origin_node_uuids->empty()) {
            auto r = node_bfs_search(
                driver, *bfs_origin_node_uuids, nc.bfs_max_depth, filters, group_id, 2 * limit);
            if (r.has_value()) {
                for (auto& node : r.value()) {
                    bfs_uuids.push_back(node.uuid);
                    if (!node_map.contains(node.uuid))
                        node_map.emplace(node.uuid, std::move(node));
                }
            }
        }

        // Rerank nodes
        std::vector<std::string> ranked_uuids;
        std::vector<float> ranked_scores;

        switch (nc.reranker) {
            case Reranker::rrf: {
                auto [u, s] = rrf({bm25_uuids, cosine_uuids, bfs_uuids}, 1, reranker_min);
                ranked_uuids = std::move(u);
                ranked_scores = std::move(s);
                break;
            }
            case Reranker::episode_mentions: {
                auto r = episode_mentions_reranker(
                    driver, {bm25_uuids, cosine_uuids, bfs_uuids}, reranker_min);
                if (r.has_value()) {
                    ranked_uuids = std::move(r.value().first);
                    ranked_scores = std::move(r.value().second);
                }
                break;
            }
            case Reranker::node_distance: {
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                if (center_node_uuid) {
                    auto r = node_distance_reranker(
                        driver, rrf_uuids, *center_node_uuid, reranker_min);
                    if (r.has_value()) {
                        ranked_uuids = std::move(r.value().first);
                        ranked_scores = std::move(r.value().second);
                    }
                } else {
                    ranked_uuids = std::move(rrf_uuids);
                    ranked_scores.resize(ranked_uuids.size(), 1.0f);
                }
                break;
            }
            case Reranker::mmr: {
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                std::unordered_map<std::string, std::vector<float>> candidates;
                for (auto& uuid : rrf_uuids) {
                    auto emb = driver.load_entity_node_embedding(uuid);
                    if (emb.has_value() && emb.value().has_value()) {
                        candidates[uuid] = std::move(emb.value().value());
                    }
                }
                if (!candidates.empty() && !query_embedding.empty()) {
                    auto [u, s] = maximal_marginal_relevance(
                        query_embedding, candidates, nc.mmr_lambda, reranker_min);
                    ranked_uuids = std::move(u);
                    ranked_scores = std::move(s);
                } else {
                    ranked_uuids = std::move(rrf_uuids);
                    ranked_scores.resize(ranked_uuids.size(), 0.0f);
                }
                break;
            }
            case Reranker::cross_encoder: {
                auto [rrf_uuids, _] = rrf({bm25_uuids, cosine_uuids, bfs_uuids});
                std::vector<std::string> passages;
                std::vector<std::string> passage_uuids;
                for (auto& uuid : rrf_uuids) {
                    auto it = node_map.find(uuid);
                    if (it != node_map.end()) {
                        passages.push_back(it->second.name);
                        passage_uuids.push_back(uuid);
                    }
                }
                auto r = cross_encoder_rerank(llm, query, passages);
                if (r.has_value()) {
                    std::unordered_map<std::string, std::string> passage_to_uuid;
                    for (size_t i = 0; i < passages.size(); ++i) {
                        passage_to_uuid[passages[i]] = passage_uuids[i];
                    }
                    for (auto& [passage, score] : r.value()) {
                        if (score >= reranker_min) {
                            auto it = passage_to_uuid.find(passage);
                            if (it != passage_to_uuid.end()) {
                                ranked_uuids.push_back(it->second);
                                ranked_scores.push_back(score);
                            }
                        }
                    }
                }
                break;
            }
        }

        // Assemble nodes in ranked order
        for (size_t i = 0; i < ranked_uuids.size() && static_cast<int>(i) < limit; ++i) {
            auto it = node_map.find(ranked_uuids[i]);
            if (it != node_map.end()) {
                results.nodes.push_back(std::move(it->second));
                results.node_scores.push_back(ranked_scores[i]);
            }
        }
    }

    // --- Episode search ---
    if (config.episode_config.has_value()) {
        auto ep_result = episode_search(driver, query, group_id, limit);
        if (ep_result.has_value()) {
            results.episodes = std::move(ep_result.value().episodes);
            results.episode_scores = std::move(ep_result.value().scores);
        }
    }

    // --- Community search ---
    if (config.community_config.has_value()) {
        auto& cc = config.community_config.value();

        bool do_bm25 = false, do_cosine = false;
        for (auto m : cc.search_methods) {
            switch (m) {
                case CommunitySearchMethod::bm25: do_bm25 = true; break;
                case CommunitySearchMethod::cosine_similarity: do_cosine = true; break;
            }
        }

        std::vector<float> query_embedding;
        if (do_cosine) {
            try {
                query_embedding = embedder.create(query);
            } catch (...) {}
        }

        std::unordered_map<std::string, CommunityNode> community_map;
        std::vector<std::string> bm25_uuids, cosine_uuids;

        if (do_bm25) {
            auto r = driver.search_communities_bm25(query, group_id, limit);
            if (r.has_value()) {
                for (auto& c : r.value()) {
                    bm25_uuids.push_back(c.uuid);
                    community_map.emplace(c.uuid, std::move(c));
                }
            }
        }

        if (do_cosine && !query_embedding.empty()) {
            auto r = driver.search_communities_cosine(
                query_embedding, group_id, cc.sim_min_score, limit);
            if (r.has_value()) {
                for (auto& c : r.value()) {
                    cosine_uuids.push_back(c.uuid);
                    if (!community_map.contains(c.uuid))
                        community_map.emplace(c.uuid, std::move(c));
                }
            }
        }

        auto [ranked_uuids, ranked_scores] = rrf({bm25_uuids, cosine_uuids}, 1, reranker_min);

        for (size_t i = 0; i < ranked_uuids.size() && static_cast<int>(i) < limit; ++i) {
            auto it = community_map.find(ranked_uuids[i]);
            if (it != community_map.end()) {
                results.communities.push_back(std::move(it->second));
                results.community_scores.push_back(ranked_scores[i]);
            }
        }
    }

    // --- Resolve edge node references ---
    // Edges reference source/target nodes by UUID. If those nodes weren't
    // returned by the node search, fetch them so callers always get names.
    if (!results.edges.empty()) {
        std::unordered_set<std::string> known_uuids;
        for (auto& n : results.nodes)
            known_uuids.insert(n.uuid);

        std::vector<std::string> missing_uuids;
        for (auto& e : results.edges) {
            if (!known_uuids.contains(e.source_node_uuid)) {
                known_uuids.insert(e.source_node_uuid);
                missing_uuids.push_back(e.source_node_uuid);
            }
            if (!known_uuids.contains(e.target_node_uuid)) {
                known_uuids.insert(e.target_node_uuid);
                missing_uuids.push_back(e.target_node_uuid);
            }
        }

        if (!missing_uuids.empty()) {
            auto fetched = driver.get_entity_nodes(missing_uuids);
            if (fetched.has_value()) {
                for (auto& n : fetched.value()) {
                    results.nodes.push_back(std::move(n));
                    results.node_scores.push_back(0.0f);
                }
            }
        }
    }

    return results;
}

} // namespace graphiti
