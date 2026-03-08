#pragma once

#include <graphiti/config.h>
#include <graphiti/error.h>
#include <graphiti/search_config.h>
#include <graphiti/search_filters.h>
#include <graphiti/types.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
        std::optional<std::string> saga_previous_episode_uuid = std::nullopt
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
        std::optional<std::string> saga = std::nullopt
    );

    VoidResult delete_group(std::string_view group_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
