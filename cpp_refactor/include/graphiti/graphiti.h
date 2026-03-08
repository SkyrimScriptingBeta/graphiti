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
        std::optional<std::string> custom_instructions = std::nullopt
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

    VoidResult delete_group(std::string_view group_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphiti
