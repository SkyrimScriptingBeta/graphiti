#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {

// JSON-RPC WebSocket client for sending Kuzu writes to a centralized writer daemon.
// When GraphitiConfig::kuzu_writer_uri is set, all save_* calls route through this
// instead of writing directly to Kuzu. Reads stay local.
class KuzuWriterClient {
public:
    explicit KuzuWriterClient(std::string uri, std::string target_db);
    ~KuzuWriterClient();

    KuzuWriterClient(const KuzuWriterClient&) = delete;
    KuzuWriterClient& operator=(const KuzuWriterClient&) = delete;

    // Connect to the writer daemon. Blocks until connected or fails.
    VoidResult connect();

    // --- Write methods (mirror KuzuDriver::save_* signatures) ---

    VoidResult save_entity_node(const EntityNode& node);
    VoidResult save_entity_node_embedding(std::string_view uuid, const std::vector<float>& embedding);
    VoidResult save_entity_edge(const EntityEdge& edge);
    VoidResult save_entity_edge_embedding(std::string_view uuid, const std::vector<float>& embedding);
    VoidResult save_episodic_node(const EpisodicNode& node);
    VoidResult save_episodic_edge(const EpisodicEdge& edge);
    VoidResult save_saga_node(const SagaNode& node);
    VoidResult save_has_episode_edge(std::string_view uuid, std::string_view saga_uuid,
                                     std::string_view episode_uuid, std::string_view group_id,
                                     TimePoint created_at);
    VoidResult save_next_episode_edge(std::string_view uuid, std::string_view source_episode_uuid,
                                      std::string_view target_episode_uuid, std::string_view group_id,
                                      TimePoint created_at);

    VoidResult build_fts_indices();

    // --- Read methods (for operations that need the daemon's write-locked view) ---

    Result<EntityNode> get_entity_node(std::string_view uuid);
    Result<std::optional<SagaNode>> get_saga_by_name(std::string_view name, std::string_view group_id);
    Result<std::optional<std::string>> get_last_episode_in_saga(std::string_view saga_uuid,
                                                                 std::string_view exclude_episode_uuid = "");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
