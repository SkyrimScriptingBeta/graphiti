// KuzuGraphStore — Kuzu implementation of the GraphStore interface
//
// Self-contained: owns the database connection and all query logic directly.

#pragma once

#include <graphiti/graph_store.h>

#include <memory>
#include <string>

namespace kuzu::main {
class Database;
class Connection;
} // namespace kuzu::main

namespace graphiti {

class KuzuGraphStore : public GraphStore {
public:
    explicit KuzuGraphStore(std::string_view db_path, bool read_only = false);
    explicit KuzuGraphStore(kuzu::main::Database& shared_db);
    ~KuzuGraphStore() override;

    KuzuGraphStore(KuzuGraphStore&&) noexcept;
    KuzuGraphStore& operator=(KuzuGraphStore&&) noexcept;

    // --- Infrastructure ---
    VoidResult setup_schema() override;
    VoidResult rebuild_indices() override;
    VoidResult clear_group(const std::vector<std::string>& group_ids) override;

    // --- Entity Persistence ---
    VoidResult persist_entity(const EntityNode& node,
                               const std::optional<std::vector<float>>& embedding = std::nullopt) override;
    Result<EntityNode> get_entity(std::string_view uuid) override;
    Result<std::vector<EntityNode>> get_entities(const std::vector<std::string>& uuids) override;
    VoidResult delete_entity(std::string_view uuid) override;
    VoidResult persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) override;
    Result<std::optional<std::vector<float>>> load_entity_embedding(std::string_view uuid) override;

    // --- Edge Persistence ---
    VoidResult persist_edge(const EntityEdge& edge,
                             const std::optional<std::vector<float>>& embedding = std::nullopt) override;
    Result<EntityEdge> get_edge(std::string_view uuid) override;
    Result<std::vector<EntityEdge>> get_edges(const std::vector<std::string>& uuids) override;
    VoidResult delete_edge(std::string_view uuid) override;
    VoidResult persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) override;
    Result<std::optional<std::vector<float>>> load_edge_embedding(std::string_view uuid) override;
    Result<std::vector<EntityEdge>> get_edges_between(
        std::string_view source_uuid, std::string_view target_uuid) override;

    // --- Episode Management ---
    VoidResult persist_episode(const EpisodicNode& episode) override;
    Result<EpisodicNode> get_episode(std::string_view uuid) override;
    VoidResult delete_episode(std::string_view uuid) override;
    Result<std::vector<EpisodicNode>> retrieve_episodes(
        std::string_view group_id, TimePoint reference_time, int last_n,
        std::optional<EpisodeType> source = std::nullopt) override;
    Result<std::vector<EpisodicNode>> retrieve_episodes_by_saga(
        std::string_view saga_name, std::string_view group_id,
        TimePoint reference_time, int last_n) override;
    VoidResult persist_mention(const EpisodicEdge& edge) override;
    Result<std::vector<std::string>> get_edge_uuids_by_episode(std::string_view episode_uuid) override;
    Result<std::vector<std::string>> get_mentioned_entity_uuids(std::string_view episode_uuid) override;

    // --- Saga Management ---
    Result<std::optional<SagaNode>> find_saga(std::string_view name, std::string_view group_id) override;
    VoidResult persist_saga(const SagaNode& saga) override;
    Result<std::optional<std::string>> get_last_saga_episode(
        std::string_view saga_uuid, std::string_view exclude_episode_uuid = "") override;
    VoidResult link_saga_episode(std::string_view uuid, std::string_view saga_uuid,
                                  std::string_view episode_uuid, std::string_view group_id,
                                  TimePoint created_at) override;
    VoidResult link_episode_sequence(std::string_view uuid, std::string_view prev_episode_uuid,
                                      std::string_view next_episode_uuid, std::string_view group_id,
                                      TimePoint created_at) override;

    // --- Community Management ---
    VoidResult persist_community(const CommunityNode& node,
                                  const std::optional<std::vector<float>>& embedding = std::nullopt) override;
    VoidResult persist_community_membership(const CommunityEdge& edge) override;
    VoidResult remove_all_communities() override;
    Result<std::optional<CommunityNode>> get_entity_community(std::string_view entity_uuid) override;
    Result<std::vector<CommunityNode>> get_neighbor_communities(std::string_view entity_uuid) override;
    Result<std::vector<EntityNode>> get_entities_by_group(std::string_view group_id) override;
    Result<std::vector<Neighbor>> get_entity_neighbors(
        std::string_view uuid, std::string_view group_id) override;
    Result<std::vector<std::string>> get_all_group_ids() override;

    // --- Search: Text ---
    Result<std::vector<EntityNode>> search_entities_bm25(
        std::string_view query, std::string_view group_id, int limit,
        const SearchFilters* filters = nullptr) override;
    Result<std::vector<EntityEdge>> search_edges_bm25(
        std::string_view query, std::string_view group_id, int limit,
        const SearchFilters* filters = nullptr) override;
    Result<std::vector<EpisodicNode>> search_episodes_bm25(
        std::string_view query, std::string_view group_id, int limit) override;
    Result<std::vector<CommunityNode>> search_communities_bm25(
        std::string_view query, std::string_view group_id, int limit) override;

    // --- Search: Semantic ---
    Result<std::vector<EntityNode>> search_entities_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit, const SearchFilters* filters = nullptr) override;
    Result<std::vector<EntityEdge>> search_edges_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit, const SearchFilters* filters = nullptr) override;
    Result<std::vector<CommunityNode>> search_communities_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit) override;

    // --- Search: BFS ---
    Result<std::vector<EntityEdge>> search_edges_bfs(
        const std::vector<std::string>& origins, std::string_view group_id,
        int max_depth, int limit, const SearchFilters* filters = nullptr) override;
    Result<std::vector<EntityNode>> search_nodes_bfs(
        const std::vector<std::string>& origins, std::string_view group_id,
        int max_depth, int limit, const SearchFilters* filters = nullptr) override;

    // --- Overview & Analytics ---
    Result<std::vector<NodeSummary>> get_node_summaries(std::string_view group_id) override;
    Result<std::vector<EdgeSummary>> get_edge_summaries(
        const std::set<std::string>& node_uuids, std::string_view group_id) override;
    Result<int64_t> count_episode_mentions(std::string_view uuid) override;
    Result<bool> check_node_adjacency(std::string_view center_uuid, std::string_view candidate_uuid) override;

    // --- Kuzu-specific: raw access for tests and Graphiti::database() ---
    kuzu::main::Database* database() const;
    kuzu::main::Connection* connection() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
