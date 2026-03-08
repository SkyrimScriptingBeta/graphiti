#include <graphiti/graphiti.h>

#include "driver/kuzu_driver.h"
#include "embedder/openai_embedder.h"
#include "llm/openai_client.h"
#include "pipeline/dedupe_edges.h"
#include "pipeline/dedupe_nodes.h"
#include "pipeline/episodic_edges.h"
#include "pipeline/extract_edges.h"
#include "pipeline/extract_nodes.h"
#include "pipeline/node_enrichment.h"
#include "search/search.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <format>

namespace graphiti {

struct Graphiti::Impl {
    GraphitiConfig config;
    KuzuDriver driver;
    OpenAIClient llm;
    OpenAIEmbedder embedder;

    Impl(GraphitiConfig cfg)
        : config(std::move(cfg))
        , driver(config.db_path)
        , llm(config.llm)
        , embedder(config.embedder) {}

    std::string resolve_group_id(std::string_view group_id) {
        if (!group_id.empty()) return std::string(group_id);
        if (config.default_group_id.has_value()) return config.default_group_id.value();
        return "default";
    }
};

Graphiti::Graphiti(GraphitiConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Graphiti::~Graphiti() = default;
Graphiti::Graphiti(Graphiti&&) noexcept = default;
Graphiti& Graphiti::operator=(Graphiti&&) noexcept = default;

VoidResult Graphiti::build_indices() {
    auto r = impl_->driver.setup_schema();
    if (!r.has_value()) return r;
    return impl_->driver.build_fts_indices();
}

Result<AddEpisodeResult> Graphiti::add_episode(
    std::string_view name,
    std::string_view episode_body,
    std::string_view source_description,
    TimePoint reference_time,
    EpisodeType source,
    std::string_view group_id,
    std::optional<std::string> custom_instructions
) {
    auto gid = impl_->resolve_group_id(group_id);
    auto now = std::chrono::system_clock::now();

    // 1. Retrieve previous episodes for context
    auto prev_result = impl_->driver.retrieve_episodes(gid, reference_time, 10, source);
    nlohmann::json previous_episodes = nlohmann::json::array();
    if (prev_result.has_value()) {
        for (auto& ep : prev_result.value()) {
            previous_episodes.push_back({{"content", ep.content}});
        }
    }

    // 2. Create episodic node
    EpisodicNode episode;
    episode.uuid = uuid::generate();
    episode.name = std::string(name);
    episode.group_id = gid;
    episode.created_at = now;
    episode.source = source;
    episode.source_description = std::string(source_description);
    episode.content = std::string(episode_body);
    episode.valid_at = reference_time;

    auto save_ep = impl_->driver.save_episodic_node(episode);
    if (!save_ep.has_value()) return std::unexpected(save_ep.error());

    // 3. Extract entities via LLM
    pipeline::ExtractNodesInput extract_input;
    extract_input.episode_content = std::string(episode_body);
    extract_input.episode_type = source;
    extract_input.entity_types = R"([{"entity_type_id": 0, "entity_type_name": "Entity", "entity_type_description": "A general entity"}])";
    extract_input.previous_episodes = previous_episodes;
    extract_input.group_id = gid;
    if (custom_instructions.has_value()) {
        extract_input.custom_instructions = custom_instructions.value();
    }

    auto nodes_result = pipeline::extract_nodes(impl_->llm, extract_input);
    if (!nodes_result.has_value()) return std::unexpected(nodes_result.error());
    auto extracted_nodes = std::move(nodes_result.value());

    // 4. Deduplicate nodes against existing graph
    auto dedup_result = pipeline::dedupe_nodes(
        impl_->llm, impl_->driver, impl_->embedder,
        extracted_nodes, previous_episodes, episode_body, gid
    );
    if (!dedup_result.has_value()) return std::unexpected(dedup_result.error());
    auto& dedup = dedup_result.value();
    auto& nodes = dedup.nodes;
    auto& uuid_map = dedup.uuid_map;

    // 5. Extract edges via LLM
    pipeline::ExtractEdgesInput edge_input;
    edge_input.episode_content = std::string(episode_body);
    edge_input.previous_episodes = previous_episodes;
    edge_input.nodes = nodes;
    edge_input.reference_time = datetime::to_iso8601(reference_time);
    edge_input.group_id = gid;
    if (custom_instructions.has_value()) {
        edge_input.custom_instructions = custom_instructions.value();
    }

    auto edges_result = pipeline::extract_edges(impl_->llm, edge_input);
    if (!edges_result.has_value()) return std::unexpected(edges_result.error());
    auto extracted_edges = std::move(edges_result.value());

    // 5b. Remap edge pointers using uuid_map from node dedup
    for (auto& edge : extracted_edges) {
        if (auto it = uuid_map.find(edge.source_node_uuid); it != uuid_map.end()) {
            edge.source_node_uuid = it->second;
        }
        if (auto it = uuid_map.find(edge.target_node_uuid); it != uuid_map.end()) {
            edge.target_node_uuid = it->second;
        }
    }

    // 6. Deduplicate edges against existing graph
    auto edge_dedup_result = pipeline::dedupe_edges(
        impl_->llm, impl_->driver, extracted_edges
    );
    if (!edge_dedup_result.has_value()) return std::unexpected(edge_dedup_result.error());
    auto& edge_dedup = edge_dedup_result.value();
    auto& new_edges = edge_dedup.new_edges;

    // 7. Enrich node summaries via LLM
    (void)pipeline::enrich_node_summaries(impl_->llm, nodes, previous_episodes, episode_body);

    // 8. Generate embeddings for nodes and edges
    for (auto& node : nodes) {
        try {
            node.name_embedding = impl_->embedder.create(node.name);
        } catch (...) {}
    }
    for (auto& edge : new_edges) {
        try {
            edge.fact_embedding = impl_->embedder.create(edge.fact);
        } catch (...) {}
    }

    // 9. Persist everything to Kuzu
    for (auto& node : nodes) {
        // Only save if it's not an existing node (no entry in uuid_map or self-mapped)
        bool is_new = true;
        for (auto& [old_uuid, new_uuid] : uuid_map) {
            if (new_uuid == node.uuid && old_uuid != node.uuid) {
                is_new = false;
                break;
            }
        }
        if (is_new) {
            (void)impl_->driver.save_entity_node(node);
        }
        if (node.name_embedding.has_value()) {
            (void)impl_->driver.save_entity_node_embedding(node.uuid, node.name_embedding.value());
        }
    }

    for (auto& edge : new_edges) {
        edge.episodes.push_back(episode.uuid);
        (void)impl_->driver.save_entity_edge(edge);
        if (edge.fact_embedding.has_value()) {
            (void)impl_->driver.save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
        }
    }

    // Invalidate contradicted edges
    for (auto& uuid : edge_dedup.invalidated_uuids) {
        auto edge_result = impl_->driver.get_entity_edge(uuid);
        if (edge_result.has_value()) {
            auto& edge = edge_result.value();
            edge.expired_at = now;
            edge.invalid_at = now;
            (void)impl_->driver.save_entity_edge(edge);
        }
    }

    // 10. Create episodic edges (MENTIONS)
    auto episodic_result = pipeline::create_episodic_edges(impl_->driver, episode, nodes);

    // Build result
    AddEpisodeResult result;
    result.episode = std::move(episode);
    result.nodes = std::move(nodes);
    result.edges = std::move(new_edges);

    return result;
}

Result<std::vector<EntityEdge>> Graphiti::search(
    std::string_view query,
    std::string_view group_id,
    int num_results,
    std::optional<SearchFilters> filters
) {
    auto gid = impl_->resolve_group_id(group_id);

    auto result = hybrid_edge_search(
        impl_->driver, impl_->embedder, query, gid, num_results, 0.0f,
        filters.has_value() ? &filters.value() : nullptr
    );

    if (!result.has_value()) return std::unexpected(result.error());

    return std::move(result.value().edges);
}

Result<SearchResults> Graphiti::search_advanced(
    std::string_view query,
    SearchConfig config,
    std::string_view group_id,
    std::optional<SearchFilters> filters,
    std::optional<std::string> center_node_uuid,
    const std::vector<std::string>* bfs_origin_node_uuids
) {
    auto gid = impl_->resolve_group_id(group_id);

    return search_orchestrator(
        impl_->driver, impl_->embedder, impl_->llm,
        query, gid, config,
        filters.has_value() ? &filters.value() : nullptr,
        center_node_uuid.has_value() ? &center_node_uuid.value() : nullptr,
        bfs_origin_node_uuids
    );
}

VoidResult Graphiti::delete_group(std::string_view group_id) {
    return impl_->driver.clear_data({std::string(group_id)});
}

} // namespace graphiti
