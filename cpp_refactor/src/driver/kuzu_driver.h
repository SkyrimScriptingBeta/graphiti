#pragma once

#include <graphiti/error.h>
#include <graphiti/search_filters.h>
#include <graphiti/types.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kuzu::main {
class Database;
class Connection;
} // namespace kuzu::main

namespace graphiti {

class KuzuDriver {
public:
    explicit KuzuDriver(std::string_view db_path);

    // Construct with an externally-owned Database (not owned by this driver).
    explicit KuzuDriver(kuzu::main::Database& shared_db);

    ~KuzuDriver();

    KuzuDriver(const KuzuDriver&) = delete;
    KuzuDriver& operator=(const KuzuDriver&) = delete;
    KuzuDriver(KuzuDriver&&) noexcept;
    KuzuDriver& operator=(KuzuDriver&&) noexcept;

    // Schema setup
    VoidResult setup_schema();
    VoidResult build_fts_indices();

    // Entity node operations
    VoidResult save_entity_node(const EntityNode& node);
    Result<EntityNode> get_entity_node(std::string_view uuid);
    Result<std::vector<EntityNode>> get_entity_nodes(const std::vector<std::string>& uuids);
    VoidResult delete_entity_node(std::string_view uuid);

    // Episodic node operations
    VoidResult save_episodic_node(const EpisodicNode& node);
    Result<EpisodicNode> get_episodic_node(std::string_view uuid);
    Result<std::vector<EpisodicNode>> retrieve_episodes(
        std::string_view group_id, TimePoint reference_time, int last_n = 20,
        std::optional<EpisodeType> source = std::nullopt
    );

    // Entity edge operations (uses RelatesToNode_ intermediate pattern)
    VoidResult save_entity_edge(const EntityEdge& edge);
    Result<EntityEdge> get_entity_edge(std::string_view uuid);
    Result<std::vector<EntityEdge>> get_entity_edges(const std::vector<std::string>& uuids);
    Result<std::vector<EntityEdge>> get_edges_between_nodes(
        std::string_view source_uuid, std::string_view target_uuid
    );
    Result<std::vector<EntityEdge>> get_edges_by_node(std::string_view node_uuid);
    VoidResult delete_entity_edge(std::string_view uuid);

    // Episodic node deletion
    VoidResult delete_episodic_node(std::string_view uuid);

    // Episodic edge operations (MENTIONS)
    VoidResult save_episodic_edge(const EpisodicEdge& edge);

    // Get entity UUIDs mentioned by an episode
    Result<std::vector<std::string>> get_mentioned_entity_uuids(std::string_view episode_uuid);

    // Get entity edge UUIDs that reference an episode in their episodes list
    Result<std::vector<std::string>> get_edge_uuids_by_episode(std::string_view episode_uuid);

    // Embedding operations
    VoidResult save_entity_node_embedding(std::string_view uuid, const std::vector<float>& embedding);
    Result<std::optional<std::vector<float>>> load_entity_node_embedding(std::string_view uuid);
    VoidResult save_entity_edge_embedding(std::string_view uuid, const std::vector<float>& embedding);
    Result<std::optional<std::vector<float>>> load_entity_edge_embedding(std::string_view uuid);

    // Search operations
    Result<std::vector<EntityNode>> search_entity_nodes_bm25(
        std::string_view query, std::string_view group_id, int limit = 10,
        const SearchFilters* filters = nullptr
    );
    Result<std::vector<EntityNode>> search_entity_nodes_cosine(
        const std::vector<float>& query_embedding, std::string_view group_id,
        float min_score = 0.0f, int limit = 10,
        const SearchFilters* filters = nullptr
    );
    Result<std::vector<EntityEdge>> search_entity_edges_bm25(
        std::string_view query, std::string_view group_id, int limit = 10,
        const SearchFilters* filters = nullptr
    );
    Result<std::vector<EntityEdge>> search_entity_edges_cosine(
        const std::vector<float>& query_embedding, std::string_view group_id,
        float min_score = 0.0f, int limit = 10,
        const SearchFilters* filters = nullptr
    );

    // Episode search operations
    Result<std::vector<EpisodicNode>> search_episodes_bm25(
        std::string_view query, std::string_view group_id, int limit = 10
    );

    // BFS search operations
    Result<std::vector<EntityEdge>> search_entity_edges_bfs(
        const std::vector<std::string>& origin_uuids,
        std::string_view group_id, int max_depth = 3, int limit = 20,
        const SearchFilters* filters = nullptr
    );
    Result<std::vector<EntityNode>> search_entity_nodes_bfs(
        const std::vector<std::string>& origin_uuids,
        std::string_view group_id, int max_depth = 3, int limit = 20,
        const SearchFilters* filters = nullptr
    );

    // Community node operations
    VoidResult save_community_node(const CommunityNode& node);
    Result<CommunityNode> get_community_node(std::string_view uuid);
    VoidResult delete_community_node(std::string_view uuid);
    VoidResult save_community_node_embedding(std::string_view uuid, const std::vector<float>& embedding);

    // Community edge operations (HAS_MEMBER: Community -> Entity or Community -> Community)
    VoidResult save_community_edge(const CommunityEdge& edge);
    VoidResult delete_community_edge(std::string_view uuid);

    // Community queries
    Result<std::optional<CommunityNode>> get_entity_community(std::string_view entity_uuid);
    Result<std::vector<CommunityNode>> get_neighbor_communities(std::string_view entity_uuid);
    struct Neighbor { std::string node_uuid; int64_t edge_count; };
    Result<std::vector<Neighbor>> get_entity_neighbors(std::string_view entity_uuid, std::string_view group_id);

    // Community search
    Result<std::vector<CommunityNode>> search_communities_bm25(
        std::string_view query, std::string_view group_id, int limit = 10);
    Result<std::vector<CommunityNode>> search_communities_cosine(
        const std::vector<float>& query_embedding, std::string_view group_id,
        float min_score = 0.0f, int limit = 10);

    // Saga node operations
    VoidResult save_saga_node(const SagaNode& node);
    Result<SagaNode> get_saga_node(std::string_view uuid);
    Result<std::optional<SagaNode>> get_saga_by_name(std::string_view name, std::string_view group_id);
    VoidResult delete_saga_node(std::string_view uuid);

    // HAS_EPISODE edge operations (Saga -> Episodic)
    VoidResult save_has_episode_edge(std::string_view uuid, std::string_view saga_uuid,
                                     std::string_view episode_uuid, std::string_view group_id,
                                     TimePoint created_at);

    // NEXT_EPISODE edge operations (Episodic -> Episodic)
    VoidResult save_next_episode_edge(std::string_view uuid, std::string_view source_episode_uuid,
                                      std::string_view target_episode_uuid, std::string_view group_id,
                                      TimePoint created_at);

    // Saga queries
    Result<std::optional<std::string>> get_last_episode_in_saga(std::string_view saga_uuid,
                                                                 std::string_view exclude_episode_uuid = "");

    // Saga-aware episode retrieval
    Result<std::vector<EpisodicNode>> retrieve_episodes_by_saga(
        std::string_view saga_name, std::string_view group_id,
        TimePoint reference_time, int last_n = 20
    );

    // Entity queries by group
    Result<std::vector<EntityNode>> get_entity_nodes_by_group(std::string_view group_id);
    Result<std::vector<std::string>> get_all_group_ids();

    // Reranker queries
    Result<int64_t> count_episode_mentions(std::string_view entity_uuid);
    Result<bool> check_node_adjacency(std::string_view center_uuid, std::string_view node_uuid);

    // Maintenance
    VoidResult remove_all_communities();
    VoidResult clear_data(const std::vector<std::string>& group_ids);

    // Raw access for tests
    kuzu::main::Database* database() const;
    kuzu::main::Connection* connection() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
