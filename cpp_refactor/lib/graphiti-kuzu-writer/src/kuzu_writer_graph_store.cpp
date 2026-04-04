// 🏴‍☠️ KuzuWriterGraphStore — writes via WebSocket, reads local Kuzu

#include "kuzu_writer_graph_store.h"
#include "remote/kuzu_writer_client.h"

namespace graphiti {

KuzuWriterGraphStore::KuzuWriterGraphStore(std::string_view db_path, std::string writer_uri, std::string target_db)
    : KuzuGraphStore(db_path, /*read_only=*/true)
    , writer_(std::make_unique<KuzuWriterClient>(std::move(writer_uri), std::move(target_db))) {}

KuzuWriterGraphStore::~KuzuWriterGraphStore() = default;
KuzuWriterGraphStore::KuzuWriterGraphStore(KuzuWriterGraphStore&&) noexcept = default;
KuzuWriterGraphStore& KuzuWriterGraphStore::operator=(KuzuWriterGraphStore&&) noexcept = default;

VoidResult KuzuWriterGraphStore::connect() {
    return writer_->connect();
}

// ============================================================================
// Write overrides → WebSocket
// ============================================================================

VoidResult KuzuWriterGraphStore::persist_entity(const EntityNode& node,
                                                  const std::optional<std::vector<float>>& embedding) {
    auto r = writer_->save_entity_node(node);
    if (!r) return r;
    if (embedding.has_value()) {
        return writer_->save_entity_node_embedding(node.uuid, embedding.value());
    }
    return {};
}

VoidResult KuzuWriterGraphStore::persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return writer_->save_entity_node_embedding(uuid, embedding);
}

VoidResult KuzuWriterGraphStore::persist_edge(const EntityEdge& edge,
                                                const std::optional<std::vector<float>>& embedding) {
    auto r = writer_->save_entity_edge(edge);
    if (!r) return r;
    if (embedding.has_value()) {
        return writer_->save_entity_edge_embedding(edge.uuid, embedding.value());
    }
    return {};
}

VoidResult KuzuWriterGraphStore::persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return writer_->save_entity_edge_embedding(uuid, embedding);
}

VoidResult KuzuWriterGraphStore::persist_episode(const EpisodicNode& episode) {
    return writer_->save_episodic_node(episode);
}

VoidResult KuzuWriterGraphStore::persist_mention(const EpisodicEdge& edge) {
    return writer_->save_episodic_edge(edge);
}

VoidResult KuzuWriterGraphStore::persist_saga(const SagaNode& saga) {
    return writer_->save_saga_node(saga);
}

VoidResult KuzuWriterGraphStore::link_saga_episode(std::string_view uuid, std::string_view saga_uuid,
                                                     std::string_view episode_uuid, std::string_view group_id,
                                                     TimePoint created_at) {
    return writer_->save_has_episode_edge(uuid, saga_uuid, episode_uuid, group_id, created_at);
}

VoidResult KuzuWriterGraphStore::link_episode_sequence(std::string_view uuid, std::string_view prev_episode_uuid,
                                                         std::string_view next_episode_uuid, std::string_view group_id,
                                                         TimePoint created_at) {
    return writer_->save_next_episode_edge(uuid, prev_episode_uuid, next_episode_uuid, group_id, created_at);
}

VoidResult KuzuWriterGraphStore::rebuild_indices() {
    return writer_->build_fts_indices();
}

// ============================================================================
// Read overrides → through daemon for write-locked consistency
// ============================================================================

Result<EntityNode> KuzuWriterGraphStore::get_entity(std::string_view uuid) {
    return writer_->get_entity_node(uuid);
}

Result<std::optional<SagaNode>> KuzuWriterGraphStore::find_saga(std::string_view name, std::string_view group_id) {
    return writer_->get_saga_by_name(name, group_id);
}

Result<std::optional<std::string>> KuzuWriterGraphStore::get_last_saga_episode(
    std::string_view saga_uuid, std::string_view exclude_episode_uuid) {
    return writer_->get_last_episode_in_saga(saga_uuid, exclude_episode_uuid);
}

} // namespace graphiti
