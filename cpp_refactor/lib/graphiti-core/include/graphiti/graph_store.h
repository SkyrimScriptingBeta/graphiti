// 🏴‍☠️ GraphStore — the domain-level storage interface for Graphiti
//
// This is NOT a database driver. It's the contract between Graphiti's pipeline
// and whatever backend stores the knowledge graph. Each method represents a
// domain operation — what Graphiti needs, not how any particular DB does it.
//
// Implementations: KuzuGraphStore (now), AgeGraphStore (next), Neo4jGraphStore (later)

#pragma once

#include <graphiti/error.h>
#include <graphiti/search_filters.h>
#include <graphiti/types.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace graphiti {

class GraphStore {
public:
    virtual ~GraphStore() = default;

    // --- Infrastructure ---
    // Ensure the graph schema exists (tables, constraints, etc.)
    virtual VoidResult setup_schema() = 0;
    // Rebuild full-text search indices after inserts
    virtual VoidResult rebuild_indices() = 0;
    // Delete everything in a group
    virtual VoidResult clear_group(const std::vector<std::string>& group_ids) = 0;

    // --- Entity Persistence ---
    // Persist an entity node. If embedding is provided, store it alongside.
    virtual VoidResult persist_entity(const EntityNode& node,
                                       const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual Result<EntityNode> get_entity(std::string_view uuid) = 0;
    virtual Result<std::vector<EntityNode>> get_entities(const std::vector<std::string>& uuids) = 0;
    virtual VoidResult delete_entity(std::string_view uuid) = 0;
    // Store/update just the embedding for an existing entity
    virtual VoidResult persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) = 0;
    virtual Result<std::optional<std::vector<float>>> load_entity_embedding(std::string_view uuid) = 0;

    // --- Edge Persistence ---
    // Persist an entity edge (fact). If embedding is provided, store it alongside.
    // Also handles invalidation — if expired_at is set on the edge, the backend stores that.
    virtual VoidResult persist_edge(const EntityEdge& edge,
                                     const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual Result<EntityEdge> get_edge(std::string_view uuid) = 0;
    virtual Result<std::vector<EntityEdge>> get_edges(const std::vector<std::string>& uuids) = 0;
    virtual VoidResult delete_edge(std::string_view uuid) = 0;
    // Store/update just the embedding for an existing edge
    virtual VoidResult persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) = 0;
    virtual Result<std::optional<std::vector<float>>> load_edge_embedding(std::string_view uuid) = 0;
    // Find existing edges between two entities (for dedup)
    virtual Result<std::vector<EntityEdge>> get_edges_between(
        std::string_view source_uuid, std::string_view target_uuid) = 0;

    // --- Episode Management ---
    virtual VoidResult persist_episode(const EpisodicNode& episode) = 0;
    virtual Result<EpisodicNode> get_episode(std::string_view uuid) = 0;
    virtual VoidResult delete_episode(std::string_view uuid) = 0;
    // Retrieve recent episodes for temporal context
    virtual Result<std::vector<EpisodicNode>> retrieve_episodes(
        std::string_view group_id, TimePoint reference_time, int last_n,
        std::optional<EpisodeType> source = std::nullopt) = 0;
    virtual Result<std::vector<EpisodicNode>> retrieve_episodes_by_saga(
        std::string_view saga_name, std::string_view group_id,
        TimePoint reference_time, int last_n) = 0;
    // Create a MENTIONS edge (episode → entity)
    virtual VoidResult persist_mention(const EpisodicEdge& edge) = 0;
    // Which entity edges reference this episode?
    virtual Result<std::vector<std::string>> get_edge_uuids_by_episode(std::string_view episode_uuid) = 0;
    // Which entities does this episode mention?
    virtual Result<std::vector<std::string>> get_mentioned_entity_uuids(std::string_view episode_uuid) = 0;

    // --- Saga Management ---
    virtual Result<std::optional<SagaNode>> find_saga(std::string_view name, std::string_view group_id) = 0;
    virtual VoidResult persist_saga(const SagaNode& saga) = 0;
    virtual Result<std::optional<std::string>> get_last_saga_episode(
        std::string_view saga_uuid, std::string_view exclude_episode_uuid = "") = 0;
    // Link an episode to a saga (HAS_EPISODE)
    virtual VoidResult link_saga_episode(std::string_view uuid, std::string_view saga_uuid,
                                          std::string_view episode_uuid, std::string_view group_id,
                                          TimePoint created_at) = 0;
    // Link two episodes in sequence (NEXT_EPISODE)
    virtual VoidResult link_episode_sequence(std::string_view uuid, std::string_view prev_episode_uuid,
                                              std::string_view next_episode_uuid, std::string_view group_id,
                                              TimePoint created_at) = 0;

    // --- Community Management ---
    virtual VoidResult persist_community(const CommunityNode& node,
                                          const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual VoidResult persist_community_membership(const CommunityEdge& edge) = 0;
    virtual VoidResult remove_all_communities() = 0;
    virtual Result<std::optional<CommunityNode>> get_entity_community(std::string_view entity_uuid) = 0;
    virtual Result<std::vector<CommunityNode>> get_neighbor_communities(std::string_view entity_uuid) = 0;
    virtual Result<std::vector<EntityNode>> get_entities_by_group(std::string_view group_id) = 0;
    struct Neighbor { std::string node_uuid; int64_t edge_count; };
    virtual Result<std::vector<Neighbor>> get_entity_neighbors(
        std::string_view uuid, std::string_view group_id) = 0;
    virtual Result<std::vector<std::string>> get_all_group_ids() = 0;

    // --- Search: Text (BM25) ---
    virtual Result<std::vector<EntityNode>> search_entities_bm25(
        std::string_view query, std::string_view group_id, int limit,
        const SearchFilters* filters = nullptr) = 0;
    virtual Result<std::vector<EntityEdge>> search_edges_bm25(
        std::string_view query, std::string_view group_id, int limit,
        const SearchFilters* filters = nullptr) = 0;
    virtual Result<std::vector<EpisodicNode>> search_episodes_bm25(
        std::string_view query, std::string_view group_id, int limit) = 0;
    virtual Result<std::vector<CommunityNode>> search_communities_bm25(
        std::string_view query, std::string_view group_id, int limit) = 0;

    // --- Search: Semantic (cosine similarity) ---
    virtual Result<std::vector<EntityNode>> search_entities_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit, const SearchFilters* filters = nullptr) = 0;
    virtual Result<std::vector<EntityEdge>> search_edges_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit, const SearchFilters* filters = nullptr) = 0;
    virtual Result<std::vector<CommunityNode>> search_communities_cosine(
        const std::vector<float>& embedding, std::string_view group_id,
        float min_score, int limit) = 0;

    // --- Search: Graph Traversal (BFS) ---
    virtual Result<std::vector<EntityEdge>> search_edges_bfs(
        const std::vector<std::string>& origins, std::string_view group_id,
        int max_depth, int limit, const SearchFilters* filters = nullptr) = 0;
    virtual Result<std::vector<EntityNode>> search_nodes_bfs(
        const std::vector<std::string>& origins, std::string_view group_id,
        int max_depth, int limit, const SearchFilters* filters = nullptr) = 0;

    // --- Overview & Analytics ---
    struct NodeSummary {
        std::string uuid; std::string name; std::vector<std::string> labels;
        std::vector<std::string> agent_ids, source_ids, source_contexts, participant_ids;
    };
    struct EdgeSummary {
        std::string uuid; std::string name; std::string source_node_uuid; std::string target_node_uuid;
        std::vector<std::string> agent_ids, source_ids, source_contexts, participant_ids;
    };
    virtual Result<std::vector<NodeSummary>> get_node_summaries(std::string_view group_id) = 0;
    virtual Result<std::vector<EdgeSummary>> get_edge_summaries(
        const std::set<std::string>& node_uuids, std::string_view group_id) = 0;
    // How many episodes mention this entity/edge? (for reranking)
    virtual Result<int64_t> count_episode_mentions(std::string_view uuid) = 0;
    // Are these two nodes adjacent? (for reranking)
    virtual Result<bool> check_node_adjacency(std::string_view center_uuid, std::string_view candidate_uuid) = 0;
};

} // namespace graphiti
