// 🏴‍☠️ KuzuGraphStore — delegates to KuzuDriver, bundling node+embedding ops

#include "kuzu_graph_store.h"

namespace graphiti {

// ============================================================================
// Construction
// ============================================================================

KuzuGraphStore::KuzuGraphStore(std::string_view db_path, bool read_only)
    : driver_(db_path, read_only) {}

KuzuGraphStore::KuzuGraphStore(kuzu::main::Database& shared_db)
    : driver_(shared_db) {}

KuzuGraphStore::~KuzuGraphStore() = default;
KuzuGraphStore::KuzuGraphStore(KuzuGraphStore&&) noexcept = default;
KuzuGraphStore& KuzuGraphStore::operator=(KuzuGraphStore&&) noexcept = default;

// ============================================================================
// Infrastructure
// ============================================================================

VoidResult KuzuGraphStore::setup_schema() { return driver_.setup_schema(); }
VoidResult KuzuGraphStore::rebuild_indices() { return driver_.build_fts_indices(); }
VoidResult KuzuGraphStore::clear_group(const std::vector<std::string>& group_ids) { return driver_.clear_data(group_ids); }

// ============================================================================
// Entity Persistence
// ============================================================================

VoidResult KuzuGraphStore::persist_entity(const EntityNode& node,
                                            const std::optional<std::vector<float>>& embedding) {
    auto r = driver_.save_entity_node(node);
    if (!r) return r;
    if (embedding.has_value()) {
        return driver_.save_entity_node_embedding(node.uuid, embedding.value());
    }
    return {};
}

Result<EntityNode> KuzuGraphStore::get_entity(std::string_view uuid) {
    return driver_.get_entity_node(uuid);
}

Result<std::vector<EntityNode>> KuzuGraphStore::get_entities(const std::vector<std::string>& uuids) {
    return driver_.get_entity_nodes(uuids);
}

VoidResult KuzuGraphStore::delete_entity(std::string_view uuid) {
    return driver_.delete_entity_node(uuid);
}

VoidResult KuzuGraphStore::persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return driver_.save_entity_node_embedding(uuid, embedding);
}

Result<std::optional<std::vector<float>>> KuzuGraphStore::load_entity_embedding(std::string_view uuid) {
    return driver_.load_entity_node_embedding(uuid);
}

// ============================================================================
// Edge Persistence
// ============================================================================

VoidResult KuzuGraphStore::persist_edge(const EntityEdge& edge,
                                          const std::optional<std::vector<float>>& embedding) {
    auto r = driver_.save_entity_edge(edge);
    if (!r) return r;
    if (embedding.has_value()) {
        return driver_.save_entity_edge_embedding(edge.uuid, embedding.value());
    }
    return {};
}

Result<EntityEdge> KuzuGraphStore::get_edge(std::string_view uuid) {
    return driver_.get_entity_edge(uuid);
}

Result<std::vector<EntityEdge>> KuzuGraphStore::get_edges(const std::vector<std::string>& uuids) {
    return driver_.get_entity_edges(uuids);
}

VoidResult KuzuGraphStore::delete_edge(std::string_view uuid) {
    return driver_.delete_entity_edge(uuid);
}

VoidResult KuzuGraphStore::persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return driver_.save_entity_edge_embedding(uuid, embedding);
}

Result<std::optional<std::vector<float>>> KuzuGraphStore::load_edge_embedding(std::string_view uuid) {
    return driver_.load_entity_edge_embedding(uuid);
}

Result<std::vector<EntityEdge>> KuzuGraphStore::get_edges_between(
    std::string_view source_uuid, std::string_view target_uuid) {
    return driver_.get_edges_between_nodes(source_uuid, target_uuid);
}

// ============================================================================
// Episode Management
// ============================================================================

VoidResult KuzuGraphStore::persist_episode(const EpisodicNode& episode) {
    return driver_.save_episodic_node(episode);
}

Result<EpisodicNode> KuzuGraphStore::get_episode(std::string_view uuid) {
    return driver_.get_episodic_node(uuid);
}

VoidResult KuzuGraphStore::delete_episode(std::string_view uuid) {
    return driver_.delete_episodic_node(uuid);
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::retrieve_episodes(
    std::string_view group_id, TimePoint reference_time, int last_n,
    std::optional<EpisodeType> source) {
    return driver_.retrieve_episodes(group_id, reference_time, last_n, source);
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::retrieve_episodes_by_saga(
    std::string_view saga_name, std::string_view group_id,
    TimePoint reference_time, int last_n) {
    return driver_.retrieve_episodes_by_saga(saga_name, group_id, reference_time, last_n);
}

VoidResult KuzuGraphStore::persist_mention(const EpisodicEdge& edge) {
    return driver_.save_episodic_edge(edge);
}

Result<std::vector<std::string>> KuzuGraphStore::get_edge_uuids_by_episode(std::string_view episode_uuid) {
    return driver_.get_edge_uuids_by_episode(episode_uuid);
}

Result<std::vector<std::string>> KuzuGraphStore::get_mentioned_entity_uuids(std::string_view episode_uuid) {
    return driver_.get_mentioned_entity_uuids(episode_uuid);
}

// ============================================================================
// Saga Management
// ============================================================================

Result<std::optional<SagaNode>> KuzuGraphStore::find_saga(std::string_view name, std::string_view group_id) {
    return driver_.get_saga_by_name(name, group_id);
}

VoidResult KuzuGraphStore::persist_saga(const SagaNode& saga) {
    return driver_.save_saga_node(saga);
}

Result<std::optional<std::string>> KuzuGraphStore::get_last_saga_episode(
    std::string_view saga_uuid, std::string_view exclude_episode_uuid) {
    return driver_.get_last_episode_in_saga(saga_uuid, exclude_episode_uuid);
}

VoidResult KuzuGraphStore::link_saga_episode(std::string_view uuid, std::string_view saga_uuid,
                                               std::string_view episode_uuid, std::string_view group_id,
                                               TimePoint created_at) {
    return driver_.save_has_episode_edge(uuid, saga_uuid, episode_uuid, group_id, created_at);
}

VoidResult KuzuGraphStore::link_episode_sequence(std::string_view uuid, std::string_view prev_episode_uuid,
                                                   std::string_view next_episode_uuid, std::string_view group_id,
                                                   TimePoint created_at) {
    return driver_.save_next_episode_edge(uuid, prev_episode_uuid, next_episode_uuid, group_id, created_at);
}

// ============================================================================
// Community Management
// ============================================================================

VoidResult KuzuGraphStore::persist_community(const CommunityNode& node,
                                               const std::optional<std::vector<float>>& embedding) {
    auto r = driver_.save_community_node(node);
    if (!r) return r;
    if (embedding.has_value()) {
        return driver_.save_community_node_embedding(node.uuid, embedding.value());
    }
    return {};
}

VoidResult KuzuGraphStore::persist_community_membership(const CommunityEdge& edge) {
    return driver_.save_community_edge(edge);
}

VoidResult KuzuGraphStore::remove_all_communities() {
    return driver_.remove_all_communities();
}

Result<std::optional<CommunityNode>> KuzuGraphStore::get_entity_community(std::string_view entity_uuid) {
    return driver_.get_entity_community(entity_uuid);
}

Result<std::vector<CommunityNode>> KuzuGraphStore::get_neighbor_communities(std::string_view entity_uuid) {
    return driver_.get_neighbor_communities(entity_uuid);
}

Result<std::vector<EntityNode>> KuzuGraphStore::get_entities_by_group(std::string_view group_id) {
    return driver_.get_entity_nodes_by_group(group_id);
}

Result<std::vector<GraphStore::Neighbor>> KuzuGraphStore::get_entity_neighbors(
    std::string_view uuid, std::string_view group_id) {
    auto result = driver_.get_entity_neighbors(uuid, group_id);
    if (!result) return std::unexpected(result.error());
    // Convert KuzuDriver::Neighbor → GraphStore::Neighbor
    std::vector<GraphStore::Neighbor> out;
    out.reserve(result.value().size());
    for (auto& n : result.value()) {
        out.push_back({std::move(n.node_uuid), n.edge_count});
    }
    return out;
}

Result<std::vector<std::string>> KuzuGraphStore::get_all_group_ids() {
    return driver_.get_all_group_ids();
}

// ============================================================================
// Search: Text (BM25)
// ============================================================================

Result<std::vector<EntityNode>> KuzuGraphStore::search_entities_bm25(
    std::string_view query, std::string_view group_id, int limit, const SearchFilters* filters) {
    return driver_.search_entity_nodes_bm25(query, group_id, limit, filters);
}

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_bm25(
    std::string_view query, std::string_view group_id, int limit, const SearchFilters* filters) {
    return driver_.search_entity_edges_bm25(query, group_id, limit, filters);
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::search_episodes_bm25(
    std::string_view query, std::string_view group_id, int limit) {
    return driver_.search_episodes_bm25(query, group_id, limit);
}

Result<std::vector<CommunityNode>> KuzuGraphStore::search_communities_bm25(
    std::string_view query, std::string_view group_id, int limit) {
    return driver_.search_communities_bm25(query, group_id, limit);
}

// ============================================================================
// Search: Semantic (cosine)
// ============================================================================

Result<std::vector<EntityNode>> KuzuGraphStore::search_entities_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit, const SearchFilters* filters) {
    return driver_.search_entity_nodes_cosine(embedding, group_id, min_score, limit, filters);
}

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit, const SearchFilters* filters) {
    return driver_.search_entity_edges_cosine(embedding, group_id, min_score, limit, filters);
}

Result<std::vector<CommunityNode>> KuzuGraphStore::search_communities_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit) {
    return driver_.search_communities_cosine(embedding, group_id, min_score, limit);
}

// ============================================================================
// Search: BFS
// ============================================================================

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_bfs(
    const std::vector<std::string>& origins, std::string_view group_id,
    int max_depth, int limit, const SearchFilters* filters) {
    return driver_.search_entity_edges_bfs(origins, group_id, max_depth, limit, filters);
}

Result<std::vector<EntityNode>> KuzuGraphStore::search_nodes_bfs(
    const std::vector<std::string>& origins, std::string_view group_id,
    int max_depth, int limit, const SearchFilters* filters) {
    return driver_.search_entity_nodes_bfs(origins, group_id, max_depth, limit, filters);
}

// ============================================================================
// Overview & Analytics
// ============================================================================

Result<std::vector<GraphStore::NodeSummary>> KuzuGraphStore::get_node_summaries(std::string_view group_id) {
    auto result = driver_.get_node_summaries_by_group(group_id);
    if (!result) return std::unexpected(result.error());
    std::vector<GraphStore::NodeSummary> out;
    out.reserve(result.value().size());
    for (auto& s : result.value()) {
        out.push_back({std::move(s.uuid), std::move(s.name), std::move(s.labels),
                       std::move(s.agent_ids), std::move(s.source_ids),
                       std::move(s.source_contexts), std::move(s.participant_ids)});
    }
    return out;
}

Result<std::vector<GraphStore::EdgeSummary>> KuzuGraphStore::get_edge_summaries(
    const std::set<std::string>& node_uuids, std::string_view group_id) {
    auto result = driver_.get_edge_summaries_by_nodes(node_uuids, group_id);
    if (!result) return std::unexpected(result.error());
    std::vector<GraphStore::EdgeSummary> out;
    out.reserve(result.value().size());
    for (auto& s : result.value()) {
        out.push_back({std::move(s.uuid), std::move(s.name),
                       std::move(s.source_node_uuid), std::move(s.target_node_uuid),
                       std::move(s.agent_ids), std::move(s.source_ids),
                       std::move(s.source_contexts), std::move(s.participant_ids)});
    }
    return out;
}

Result<int64_t> KuzuGraphStore::count_episode_mentions(std::string_view uuid) {
    return driver_.count_episode_mentions(uuid);
}

Result<bool> KuzuGraphStore::check_node_adjacency(std::string_view center_uuid, std::string_view candidate_uuid) {
    return driver_.check_node_adjacency(center_uuid, candidate_uuid);
}

// ============================================================================
// Kuzu-specific: raw access
// ============================================================================

kuzu::main::Database* KuzuGraphStore::database() const { return driver_.database(); }
kuzu::main::Connection* KuzuGraphStore::connection() const { return driver_.connection(); }

} // namespace graphiti
