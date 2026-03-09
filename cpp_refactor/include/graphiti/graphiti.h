#pragma once

#include <graphiti/config.h>
#include <graphiti/error.h>
#include <graphiti/search_config.h>
#include <graphiti/search_filters.h>
#include <graphiti/token_tracker.h>
#include <graphiti/type_definitions.h>
#include <graphiti/types.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kuzu::main {
class Database;
} // namespace kuzu::main

namespace graphiti {

struct AddEpisodeResult {
    EpisodicNode episode;
    std::vector<EpisodicEdge> episodic_edges;
    std::vector<EntityNode> nodes;
    std::vector<EntityEdge> edges;
};

// Input for bulk episode ingestion
struct RawEpisode {
    std::string name;
    std::string content;
    std::string source_description;
    TimePoint reference_time;
    EpisodeType source = EpisodeType::message;
    std::optional<std::string> uuid; // If set, reuses existing episode
};

struct AddBulkEpisodeResults {
    std::vector<EpisodicNode> episodes;
    std::vector<EntityNode> nodes;
    std::vector<EntityEdge> edges;
};

// Thread safety: All public methods are serialized internally via a mutex.
// Concurrent calls from multiple threads are safe but will execute sequentially.
// For maximum throughput with concurrent reads, create separate Graphiti instances
// pointing to the same database (Kuzu Database objects are thread-safe to share,
// but each Connection is not).
class Graphiti {
public:
    explicit Graphiti(GraphitiConfig config);

    // Construct with an externally-owned Kuzu Database.
    // The Database must outlive this Graphiti instance.
    // Graphiti creates its own Connection to the shared Database.
    Graphiti(GraphitiConfig config, kuzu::main::Database& shared_db);

    ~Graphiti();

    Graphiti(const Graphiti&) = delete;
    Graphiti& operator=(const Graphiti&) = delete;
    Graphiti(Graphiti&&) noexcept;
    Graphiti& operator=(Graphiti&&) noexcept;

    VoidResult build_indices();

    Result<AddEpisodeResult> add_episode(
        std::string_view name,
        std::string_view episode_body,
        std::string_view source_description,
        TimePoint reference_time,
        EpisodeType source = EpisodeType::message,
        std::string_view group_id = "",
        std::string_view agent_id = "",
        std::optional<std::string> custom_instructions = std::nullopt,
        std::optional<std::string> saga = std::nullopt,
        std::optional<std::string> saga_previous_episode_uuid = std::nullopt,
        bool update_communities = false,
        const TypeDefinitions* type_defs = nullptr
    );

    Result<std::vector<EntityEdge>> search(
        std::string_view query,
        std::string_view group_id = "",
        int num_results = 10,
        std::optional<SearchFilters> filters = std::nullopt
    );

    Result<SearchResults> search_advanced(
        std::string_view query,
        SearchConfig config,
        std::string_view group_id = "",
        std::optional<SearchFilters> filters = std::nullopt,
        std::optional<std::string> center_node_uuid = std::nullopt,
        const std::vector<std::string>* bfs_origin_node_uuids = nullptr
    );

    // Bulk episode ingestion: processes all episodes together for efficiency.
    // Cross-deduplicates nodes and edges within the batch before graph dedup.
    // NOTE: Skips edge invalidation and community updates for speed.
    Result<AddBulkEpisodeResults> add_episode_bulk(
        const std::vector<RawEpisode>& bulk_episodes,
        std::string_view group_id = "",
        std::string_view agent_id = "",
        std::optional<std::string> custom_instructions = std::nullopt,
        std::optional<std::string> saga = std::nullopt,
        const TypeDefinitions* type_defs = nullptr
    );

    // Build communities: cluster entities via label propagation, then summarize
    // each cluster via LLM. Clears existing communities before rebuilding.
    // If group_ids is empty, processes all groups in the graph.
    Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>>
        build_communities(const std::vector<std::string>& group_ids = {});

    VoidResult delete_group(std::string_view group_id);

    // Retrieve the last N episodes before reference_time.
    // Optionally filter by source type and/or saga name.
    Result<std::vector<EpisodicNode>> retrieve_episodes(
        TimePoint reference_time,
        int last_n = 20,
        std::string_view group_id = "",
        std::optional<EpisodeType> source = std::nullopt,
        std::optional<std::string> saga = std::nullopt
    );

    // Get all nodes and edges associated with the given episode UUIDs.
    Result<SearchResults> get_nodes_and_edges_by_episode(
        const std::vector<std::string>& episode_uuids
    );

    // Remove an episode and cascade-delete edges/nodes that only belong to it.
    VoidResult remove_episode(std::string_view episode_uuid);

    // Manually insert a source_node -> edge -> target_node triplet.
    // Generates embeddings and resolves against existing graph nodes.
    struct AddTripletResult {
        std::vector<EntityNode> nodes;
        std::vector<EntityEdge> edges;
    };
    Result<AddTripletResult> add_triplet(
        EntityNode source_node,
        EntityEdge edge,
        EntityNode target_node
    );

    // Access the LLM token usage tracker.
    // Records input/output tokens per prompt across all LLM calls.
    const TokenTracker& token_tracker() const;

    // Access the underlying Kuzu Database instance.
    // Use this to create additional Connections for your own queries.
    kuzu::main::Database& database() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
