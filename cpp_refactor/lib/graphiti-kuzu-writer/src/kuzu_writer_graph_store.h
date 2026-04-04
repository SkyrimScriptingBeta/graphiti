// 🏴‍☠️ KuzuWriterGraphStore — reads local Kuzu, writes via WebSocket to writer daemon
//
// Inherits all read methods from KuzuGraphStore (local in-process Kuzu).
// Overrides write methods to send over WebSocket to a centralized kuzu-writer-server.
// Also overrides 3 read methods that need the writer's consistent view:
//   get_entity, find_saga, get_last_saga_episode
//
// This exists for multi-machine setups where Kuzu can't handle concurrent writes.

#pragma once

#include <driver/kuzu_graph_store.h>

#include <memory>
#include <string>

namespace graphiti {

class KuzuWriterClient;

class KuzuWriterGraphStore : public KuzuGraphStore {
public:
    // db_path: local Kuzu database (opened read-only for local reads)
    // writer_uri: WebSocket URI of the kuzu-writer-server (e.g. "ws://127.0.0.1:9876")
    // target_db: which database on the writer server to target
    KuzuWriterGraphStore(std::string_view db_path, std::string writer_uri, std::string target_db);
    ~KuzuWriterGraphStore() override;

    KuzuWriterGraphStore(KuzuWriterGraphStore&&) noexcept;
    KuzuWriterGraphStore& operator=(KuzuWriterGraphStore&&) noexcept;

    // Connect to the writer daemon. Must be called before any writes.
    VoidResult connect();

    // --- Write overrides: go through WebSocket instead of local Kuzu ---
    VoidResult persist_entity(const EntityNode& node,
                               const std::optional<std::vector<float>>& embedding = std::nullopt) override;
    VoidResult persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) override;
    VoidResult persist_edge(const EntityEdge& edge,
                             const std::optional<std::vector<float>>& embedding = std::nullopt) override;
    VoidResult persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) override;
    VoidResult persist_episode(const EpisodicNode& episode) override;
    VoidResult persist_mention(const EpisodicEdge& edge) override;
    VoidResult persist_saga(const SagaNode& saga) override;
    VoidResult link_saga_episode(std::string_view uuid, std::string_view saga_uuid,
                                  std::string_view episode_uuid, std::string_view group_id,
                                  TimePoint created_at) override;
    VoidResult link_episode_sequence(std::string_view uuid, std::string_view prev_episode_uuid,
                                      std::string_view next_episode_uuid, std::string_view group_id,
                                      TimePoint created_at) override;
    VoidResult rebuild_indices() override;

    // --- Read overrides: routed through daemon for write-locked consistency ---
    Result<EntityNode> get_entity(std::string_view uuid) override;
    Result<std::optional<SagaNode>> find_saga(std::string_view name, std::string_view group_id) override;
    Result<std::optional<std::string>> get_last_saga_episode(
        std::string_view saga_uuid, std::string_view exclude_episode_uuid = "") override;

private:
    std::unique_ptr<KuzuWriterClient> writer_;
};

} // namespace graphiti
