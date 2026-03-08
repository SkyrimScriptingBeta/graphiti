#include <graphiti/graphiti.h>

#include "driver/kuzu_driver.h"
#include "embedder/openai_embedder.h"
#include "llm/openai_client.h"
#include "pipeline/bulk_utils.h"
#include "pipeline/dedupe_edges.h"
#include "pipeline/dedupe_nodes.h"
#include "pipeline/episodic_edges.h"
#include "pipeline/extract_edges.h"
#include "pipeline/extract_nodes.h"
#include "pipeline/node_enrichment.h"
#include "search/search.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <numeric>
#include <set>
#include <unordered_set>

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
    std::string_view agent_id,
    std::optional<std::string> custom_instructions,
    std::optional<std::string> saga,
    std::optional<std::string> saga_previous_episode_uuid
) {
    auto gid = impl_->resolve_group_id(group_id);
    auto aid = std::string(agent_id);
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
    episode.agent_id = aid;

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

    // Set agent_ids on extracted nodes
    if (!aid.empty()) {
        for (auto& node : extracted_nodes) {
            node.agent_ids = {aid};
        }
    }

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

    // Set agent_ids on extracted edges
    if (!aid.empty()) {
        for (auto& edge : extracted_edges) {
            edge.agent_ids = {aid};
        }
    }

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

    // 11. Saga processing (if saga name provided)
    if (saga.has_value() && !saga.value().empty()) {
        // Get or create saga node
        auto existing = impl_->driver.get_saga_by_name(saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            (void)impl_->driver.save_saga_node(saga_node);
        }

        // Find previous episode in saga (if not explicitly provided)
        std::string prev_ep_uuid;
        if (saga_previous_episode_uuid.has_value() && !saga_previous_episode_uuid.value().empty()) {
            prev_ep_uuid = saga_previous_episode_uuid.value();
        } else {
            auto last = impl_->driver.get_last_episode_in_saga(saga_node.uuid, episode.uuid);
            if (last.has_value() && last.value().has_value()) {
                prev_ep_uuid = last.value().value();
            }
        }

        // Create NEXT_EPISODE edge (prev -> current)
        if (!prev_ep_uuid.empty()) {
            (void)impl_->driver.save_next_episode_edge(
                uuid::generate(), prev_ep_uuid, episode.uuid, gid, now);
        }

        // Create HAS_EPISODE edge (saga -> current episode)
        (void)impl_->driver.save_has_episode_edge(
            uuid::generate(), saga_node.uuid, episode.uuid, gid, now);
    }

    // Build result
    AddEpisodeResult result;
    result.episode = std::move(episode);
    result.nodes = std::move(nodes);
    result.edges = std::move(new_edges);

    return result;
}

Result<AddBulkEpisodeResults> Graphiti::add_episode_bulk(
    const std::vector<RawEpisode>& bulk_episodes,
    std::string_view group_id,
    std::string_view agent_id,
    std::optional<std::string> custom_instructions,
    std::optional<std::string> saga
) {
    if (bulk_episodes.empty()) {
        return AddBulkEpisodeResults{};
    }

    auto gid = impl_->resolve_group_id(group_id);
    auto aid = std::string(agent_id);
    auto now = std::chrono::system_clock::now();

    // ========================================================================
    // Step 1: Create and save episodic nodes for all episodes
    // ========================================================================
    std::vector<EpisodicNode> episodes;
    episodes.reserve(bulk_episodes.size());

    for (auto& raw : bulk_episodes) {
        EpisodicNode ep;
        ep.uuid = raw.uuid.value_or(uuid::generate());
        ep.name = raw.name;
        ep.group_id = gid;
        ep.created_at = now;
        ep.source = raw.source;
        ep.source_description = raw.source_description;
        ep.content = raw.content;
        ep.valid_at = raw.reference_time;
        ep.agent_id = aid;
        episodes.push_back(std::move(ep));
    }

    for (auto& ep : episodes) {
        auto r = impl_->driver.save_episodic_node(ep);
        if (!r.has_value()) return std::unexpected(r.error());
    }

    // ========================================================================
    // Step 2: Retrieve previous episode context for each episode
    // ========================================================================
    std::vector<nlohmann::json> episode_contexts;
    episode_contexts.reserve(episodes.size());

    for (auto& ep : episodes) {
        auto prev = impl_->driver.retrieve_episodes(gid, ep.valid_at, 10, ep.source);
        nlohmann::json ctx = nlohmann::json::array();
        if (prev.has_value()) {
            for (auto& pe : prev.value()) {
                ctx.push_back({{"content", pe.content}});
            }
        }
        episode_contexts.push_back(std::move(ctx));
    }

    // ========================================================================
    // Step 3: Extract nodes from each episode
    // ========================================================================
    // nodes_by_episode[i] = nodes extracted from episodes[i]
    std::vector<std::vector<EntityNode>> nodes_by_episode;
    nodes_by_episode.reserve(episodes.size());

    for (size_t i = 0; i < episodes.size(); ++i) {
        pipeline::ExtractNodesInput input;
        input.episode_content = episodes[i].content;
        input.episode_type = episodes[i].source;
        input.entity_types = R"([{"entity_type_id": 0, "entity_type_name": "Entity", "entity_type_description": "A general entity"}])";
        input.previous_episodes = episode_contexts[i];
        input.group_id = gid;
        if (custom_instructions.has_value()) {
            input.custom_instructions = custom_instructions.value();
        }

        auto result = pipeline::extract_nodes(impl_->llm, input);
        if (result.has_value()) {
            auto& nodes = result.value();
            if (!aid.empty()) {
                for (auto& node : nodes) {
                    node.agent_ids = {aid};
                }
            }
            nodes_by_episode.push_back(std::move(nodes));
        } else {
            nodes_by_episode.push_back({});
        }
    }

    // ========================================================================
    // Step 4: Cross-deduplicate nodes within the batch
    // ========================================================================
    // Build a name -> first occurrence map. When two nodes from different
    // episodes share the same name (case-insensitive), they're duplicates.
    std::unordered_map<std::string, std::string> name_to_uuid; // lowercase name -> canonical uuid
    std::vector<std::pair<std::string, std::string>> node_dup_pairs;

    for (auto& nodes : nodes_by_episode) {
        for (auto& node : nodes) {
            std::string lower_name = node.name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

            auto it = name_to_uuid.find(lower_name);
            if (it == name_to_uuid.end()) {
                name_to_uuid[lower_name] = node.uuid;
            } else if (it->second != node.uuid) {
                // Duplicate: map this node to the first occurrence
                node_dup_pairs.push_back({node.uuid, it->second});
            }
        }
    }

    auto batch_uuid_map = pipeline::build_directed_uuid_map(node_dup_pairs);

    // Apply batch UUID map to collapse duplicates within the batch
    for (auto& nodes : nodes_by_episode) {
        for (auto& node : nodes) {
            if (auto it = batch_uuid_map.find(node.uuid); it != batch_uuid_map.end()) {
                if (it->second != node.uuid) {
                    node.uuid = it->second;
                }
            }
        }
    }

    // ========================================================================
    // Step 5: Deduplicate nodes against existing graph
    // ========================================================================
    // Collect unique nodes (by UUID) across all episodes
    std::unordered_set<std::string> seen_uuids;
    std::vector<EntityNode> all_unique_nodes;

    for (auto& nodes : nodes_by_episode) {
        for (auto& node : nodes) {
            if (seen_uuids.insert(node.uuid).second) {
                all_unique_nodes.push_back(node);
            }
        }
    }

    // Combine all episode content for graph dedup context
    std::string combined_content;
    for (auto& ep : episodes) {
        if (!combined_content.empty()) combined_content += "\n---\n";
        combined_content += ep.content;
    }

    auto dedup_result = pipeline::dedupe_nodes(
        impl_->llm, impl_->driver, impl_->embedder,
        all_unique_nodes, nlohmann::json::array(), combined_content, gid
    );

    std::unordered_map<std::string, std::string> graph_uuid_map;
    std::vector<EntityNode> deduped_nodes;

    if (dedup_result.has_value()) {
        deduped_nodes = std::move(dedup_result.value().nodes);
        graph_uuid_map = std::move(dedup_result.value().uuid_map);
    } else {
        deduped_nodes = std::move(all_unique_nodes);
    }

    // Merge batch_uuid_map and graph_uuid_map into a combined map
    std::unordered_map<std::string, std::string> combined_uuid_map = batch_uuid_map;
    for (auto& [old_uuid, new_uuid] : graph_uuid_map) {
        combined_uuid_map[old_uuid] = new_uuid;
    }
    // Chase chains: if A->B and B->C, resolve A->C
    for (auto& [k, v] : combined_uuid_map) {
        std::string resolved = v;
        for (int depth = 0; depth < 10; ++depth) {
            auto it = combined_uuid_map.find(resolved);
            if (it == combined_uuid_map.end() || it->second == resolved) break;
            resolved = it->second;
        }
        combined_uuid_map[k] = resolved;
    }

    // ========================================================================
    // Step 6: Extract edges from each episode
    // ========================================================================
    std::vector<EntityEdge> all_edges;

    for (size_t i = 0; i < episodes.size(); ++i) {
        // Build node list for this episode (using deduped nodes)
        auto& ep_nodes = nodes_by_episode[i];

        pipeline::ExtractEdgesInput input;
        input.episode_content = episodes[i].content;
        input.previous_episodes = episode_contexts[i];
        input.nodes = ep_nodes;
        input.reference_time = datetime::to_iso8601(episodes[i].valid_at);
        input.group_id = gid;
        if (custom_instructions.has_value()) {
            input.custom_instructions = custom_instructions.value();
        }

        auto result = pipeline::extract_edges(impl_->llm, input);
        if (result.has_value()) {
            for (auto& edge : result.value()) {
                edge.episodes.push_back(episodes[i].uuid);
                if (!aid.empty()) {
                    edge.agent_ids = {aid};
                }
                all_edges.push_back(std::move(edge));
            }
        }
    }

    // ========================================================================
    // Step 7: Remap edge pointers using combined UUID map
    // ========================================================================
    pipeline::resolve_edge_pointers(all_edges, combined_uuid_map);

    // ========================================================================
    // Step 8: Deduplicate edges against existing graph
    // ========================================================================
    // NOTE: Bulk mode skips edge invalidation for speed
    auto edge_dedup = pipeline::dedupe_edges(impl_->llm, impl_->driver, all_edges);
    std::vector<EntityEdge> final_edges;
    if (edge_dedup.has_value()) {
        final_edges = std::move(edge_dedup.value().new_edges);
    } else {
        final_edges = std::move(all_edges);
    }

    // ========================================================================
    // Step 9: Enrich node summaries
    // ========================================================================
    (void)pipeline::enrich_node_summaries(
        impl_->llm, deduped_nodes, nlohmann::json::array(), combined_content
    );

    // ========================================================================
    // Step 10: Generate embeddings
    // ========================================================================
    for (auto& node : deduped_nodes) {
        try {
            node.name_embedding = impl_->embedder.create(node.name);
        } catch (...) {}
    }
    for (auto& edge : final_edges) {
        try {
            edge.fact_embedding = impl_->embedder.create(edge.fact);
        } catch (...) {}
    }

    // ========================================================================
    // Step 11: Persist everything to Kuzu
    // ========================================================================
    for (auto& node : deduped_nodes) {
        bool is_existing = false;
        for (auto& [old_uuid, new_uuid] : graph_uuid_map) {
            if (new_uuid == node.uuid && old_uuid != node.uuid) {
                is_existing = true;
                break;
            }
        }
        if (!is_existing) {
            (void)impl_->driver.save_entity_node(node);
        }
        if (node.name_embedding.has_value()) {
            (void)impl_->driver.save_entity_node_embedding(node.uuid, node.name_embedding.value());
        }
    }

    for (auto& edge : final_edges) {
        (void)impl_->driver.save_entity_edge(edge);
        if (edge.fact_embedding.has_value()) {
            (void)impl_->driver.save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
        }
    }

    // ========================================================================
    // Step 12: Create episodic edges (MENTIONS)
    // ========================================================================
    // Map episode UUID to its deduped nodes (using combined_uuid_map)
    for (size_t i = 0; i < episodes.size(); ++i) {
        // Resolve node UUIDs and collect unique nodes for this episode
        std::unordered_set<std::string> ep_node_uuids;
        std::vector<EntityNode> ep_unique_nodes;

        for (auto& node : nodes_by_episode[i]) {
            std::string resolved = node.uuid;
            if (auto it = combined_uuid_map.find(resolved); it != combined_uuid_map.end()) {
                resolved = it->second;
            }
            if (ep_node_uuids.insert(resolved).second) {
                EntityNode resolved_node = node;
                resolved_node.uuid = resolved;
                ep_unique_nodes.push_back(std::move(resolved_node));
            }
        }

        (void)pipeline::create_episodic_edges(impl_->driver, episodes[i], ep_unique_nodes);
    }

    // ========================================================================
    // Step 13: Saga processing (if saga name provided)
    // ========================================================================
    if (saga.has_value() && !saga.value().empty()) {
        auto existing = impl_->driver.get_saga_by_name(saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            (void)impl_->driver.save_saga_node(saga_node);
        }

        // Sort episodes by valid_at for correct NEXT_EPISODE ordering
        std::vector<size_t> sorted_indices(episodes.size());
        std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](size_t a, size_t b) { return episodes[a].valid_at < episodes[b].valid_at; });

        // Find previous episode already in saga
        std::string prev_ep_uuid;
        auto last = impl_->driver.get_last_episode_in_saga(saga_node.uuid);
        if (last.has_value() && last.value().has_value()) {
            prev_ep_uuid = last.value().value();
        }

        for (auto idx : sorted_indices) {
            auto& ep = episodes[idx];

            if (!prev_ep_uuid.empty()) {
                (void)impl_->driver.save_next_episode_edge(
                    uuid::generate(), prev_ep_uuid, ep.uuid, gid, now);
            }

            (void)impl_->driver.save_has_episode_edge(
                uuid::generate(), saga_node.uuid, ep.uuid, gid, now);

            prev_ep_uuid = ep.uuid;
        }
    }

    // ========================================================================
    // Build result
    // ========================================================================
    AddBulkEpisodeResults result;
    result.episodes = std::move(episodes);
    result.nodes = std::move(deduped_nodes);
    result.edges = std::move(final_edges);
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
