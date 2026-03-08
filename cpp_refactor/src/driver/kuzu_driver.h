#pragma once

#include <graphiti/error.h>
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

    // Episodic edge operations (MENTIONS)
    VoidResult save_episodic_edge(const EpisodicEdge& edge);

    // Embedding operations
    VoidResult save_entity_node_embedding(std::string_view uuid, const std::vector<float>& embedding);
    Result<std::optional<std::vector<float>>> load_entity_node_embedding(std::string_view uuid);
    VoidResult save_entity_edge_embedding(std::string_view uuid, const std::vector<float>& embedding);
    Result<std::optional<std::vector<float>>> load_entity_edge_embedding(std::string_view uuid);

    // Search operations
    Result<std::vector<EntityNode>> search_entity_nodes_bm25(
        std::string_view query, std::string_view group_id, int limit = 10
    );
    Result<std::vector<EntityNode>> search_entity_nodes_cosine(
        const std::vector<float>& query_embedding, std::string_view group_id,
        float min_score = 0.0f, int limit = 10
    );
    Result<std::vector<EntityEdge>> search_entity_edges_bm25(
        std::string_view query, std::string_view group_id, int limit = 10
    );
    Result<std::vector<EntityEdge>> search_entity_edges_cosine(
        const std::vector<float>& query_embedding, std::string_view group_id,
        float min_score = 0.0f, int limit = 10
    );

    // Maintenance
    VoidResult clear_data(const std::vector<std::string>& group_ids);

    // Raw access for tests
    kuzu::main::Database* database() const;
    kuzu::main::Connection* connection() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
