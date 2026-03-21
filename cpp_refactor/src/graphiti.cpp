#include <graphiti/graphiti.h>

#include <main/kuzu.h>

#include "driver/kuzu_driver.h"
#include "embedder/openai_embedder.h"
#include "llm/openai_client.h"
#include "pipeline/bulk_utils.h"
#include "pipeline/community_ops.h"
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
    std::mutex mu; // Serializes all public method calls (Kuzu Connection is not thread-safe)
    GraphitiConfig config;
    KuzuDriver driver;
    std::unique_ptr<LLMClient> llm_owned;
    std::unique_ptr<EmbedderClient> embedder_owned;
    LLMClient& llm;
    EmbedderClient& embedder;

    // Default: create OpenAI clients from config
    Impl(GraphitiConfig cfg)
        : config(std::move(cfg))
        , driver(config.db_path, config.read_only)
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(*llm_owned)
        , embedder(*embedder_owned) {}

    // Shared DB + default OpenAI clients
    Impl(GraphitiConfig cfg, kuzu::main::Database& shared_db)
        : config(std::move(cfg))
        , driver(shared_db)
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(*llm_owned)
        , embedder(*embedder_owned) {}

    // Custom providers (nullptr = use OpenAI default from config)
    Impl(GraphitiConfig cfg,
         std::unique_ptr<LLMClient> custom_llm,
         std::unique_ptr<EmbedderClient> custom_embedder)
        : config(std::move(cfg))
        , driver(config.db_path, config.read_only)
        , llm_owned(custom_llm ? std::move(custom_llm)
                                : std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(custom_embedder ? std::move(custom_embedder)
                                          : std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(*llm_owned)
        , embedder(*embedder_owned) {}

    // Shared DB + custom providers (nullptr = use OpenAI default from config)
    Impl(GraphitiConfig cfg,
         kuzu::main::Database& shared_db,
         std::unique_ptr<LLMClient> custom_llm,
         std::unique_ptr<EmbedderClient> custom_embedder)
        : config(std::move(cfg))
        , driver(shared_db)
        , llm_owned(custom_llm ? std::move(custom_llm)
                                : std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(custom_embedder ? std::move(custom_embedder)
                                          : std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(*llm_owned)
        , embedder(*embedder_owned) {}

    std::string resolve_group_id(std::string_view group_id) {
        if (!group_id.empty()) return std::string(group_id);
        if (config.default_group_id.has_value()) return config.default_group_id.value();
        return "default";
    }
};

Graphiti::Graphiti(GraphitiConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Graphiti::Graphiti(GraphitiConfig config,
                   std::unique_ptr<LLMClient> llm,
                   std::unique_ptr<EmbedderClient> embedder)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(llm), std::move(embedder))) {}

Graphiti::Graphiti(GraphitiConfig config, kuzu::main::Database& shared_db)
    : impl_(std::make_unique<Impl>(std::move(config), shared_db)) {}

Graphiti::Graphiti(GraphitiConfig config,
                   kuzu::main::Database& shared_db,
                   std::unique_ptr<LLMClient> llm,
                   std::unique_ptr<EmbedderClient> embedder)
    : impl_(std::make_unique<Impl>(std::move(config), shared_db, std::move(llm), std::move(embedder))) {}

Graphiti::~Graphiti() = default;
Graphiti::Graphiti(Graphiti&&) noexcept = default;
Graphiti& Graphiti::operator=(Graphiti&&) noexcept = default;

VoidResult Graphiti::build_indices() {
    std::lock_guard lock(impl_->mu);
    auto r = impl_->driver.setup_schema();
    if (!r.has_value()) return r;
    return impl_->driver.build_fts_indices();
}

Result<AddEpisodeResult> Graphiti::add_episode(AddEpisodeOptions opts) {
    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(opts.group_id);
    auto& aid = opts.agent_id;
    auto& sid = opts.source_id;
    auto& sctx = opts.source_context;
    auto& pids = opts.participant_ids;
    auto now = std::chrono::system_clock::now();

    // 1. Retrieve previous episodes for context
    auto prev_result = impl_->driver.retrieve_episodes(gid, opts.reference_time, 10, opts.source);
    nlohmann::json previous_episodes = nlohmann::json::array();
    if (prev_result.has_value()) {
        for (auto& ep : prev_result.value()) {
            previous_episodes.push_back({{"content", ep.content}});
        }
    }

    // 2. Create episodic node
    EpisodicNode episode;
    episode.uuid = uuid::generate();
    episode.name = std::move(opts.name);
    episode.group_id = gid;
    episode.created_at = now;
    episode.source = opts.source;
    episode.source_description = std::move(opts.source_description);
    episode.content = std::move(opts.body);
    episode.valid_at = opts.reference_time;
    episode.agent_id = aid;
    episode.source_id = sid;
    episode.source_context = sctx;
    episode.participant_ids = pids;

    if (!impl_->config.store_raw_episode_content) {
        episode.content.clear();
    }

    auto save_ep = impl_->driver.save_episodic_node(episode);
    if (!save_ep.has_value()) return std::unexpected(save_ep.error());

    // 3. Extract entities via LLM
    pipeline::ExtractNodesInput extract_input;
    extract_input.episode_content = episode.content;
    extract_input.episode_type = opts.source;
    if (opts.type_defs) {
        extract_input.entity_types = opts.type_defs->entity_types_prompt_json();
        extract_input.type_defs = opts.type_defs;
    } else {
        extract_input.entity_types = R"([{"entity_type_id": 0, "entity_type_name": "Entity", "entity_type_description": "A general entity"}])";
    }
    extract_input.previous_episodes = previous_episodes;
    extract_input.group_id = gid;
    extract_input.source_description = episode.source_description;
    if (opts.custom_instructions.has_value()) {
        extract_input.custom_instructions = opts.custom_instructions.value();
    }

    auto nodes_result = pipeline::extract_nodes(impl_->llm, extract_input);
    if (!nodes_result.has_value()) return std::unexpected(nodes_result.error());
    auto extracted_nodes = std::move(nodes_result.value());

    // Set attribution ids on extracted nodes
    for (auto& node : extracted_nodes) {
        if (!aid.empty()) node.agent_ids = {aid};
        if (!sid.empty()) node.source_ids = {sid};
        if (!sctx.empty()) node.source_contexts = {sctx};
        if (!pids.empty()) node.participant_ids = pids;
    }

    // 4. Deduplicate nodes against existing graph
    auto dedup_result = pipeline::dedupe_nodes(
        impl_->llm, impl_->driver, impl_->embedder,
        extracted_nodes, previous_episodes, episode.content, gid
    );
    if (!dedup_result.has_value()) return std::unexpected(dedup_result.error());
    auto& dedup = dedup_result.value();
    auto& nodes = dedup.nodes;
    auto& uuid_map = dedup.uuid_map;

    // 5. Extract edges via LLM
    pipeline::ExtractEdgesInput edge_input;
    edge_input.episode_content = episode.content;
    edge_input.previous_episodes = previous_episodes;
    edge_input.nodes = nodes;
    edge_input.reference_time = datetime::to_iso8601(opts.reference_time);
    edge_input.group_id = gid;
    if (opts.type_defs && !opts.type_defs->edge_types.empty()) {
        edge_input.edge_types = opts.type_defs->edge_types_prompt_json();
    }
    if (opts.custom_instructions.has_value()) {
        edge_input.custom_instructions = opts.custom_instructions.value();
    }

    auto edges_result = pipeline::extract_edges(impl_->llm, edge_input);
    if (!edges_result.has_value()) return std::unexpected(edges_result.error());
    auto extracted_edges = std::move(edges_result.value());

    // Set attribution ids on extracted edges
    for (auto& edge : extracted_edges) {
        if (!aid.empty()) edge.agent_ids = {aid};
        if (!sid.empty()) edge.source_ids = {sid};
        if (!sctx.empty()) edge.source_contexts = {sctx};
        if (!pids.empty()) edge.participant_ids = pids;
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
    (void)pipeline::enrich_node_summaries(impl_->llm, nodes, previous_episodes, episode.content);

    // 7b. Extract custom attributes for typed entities
    if (opts.type_defs) {
        (void)pipeline::extract_entity_attributes(impl_->llm, nodes, *opts.type_defs, episode.content);
    }

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
        // Check if this node was dedup-matched to an existing one
        bool is_existing = false;
        for (auto& [old_uuid, new_uuid] : uuid_map) {
            if (new_uuid == node.uuid && old_uuid != node.uuid) {
                is_existing = true;
                break;
            }
        }
        if (is_existing) {
            // Merge attribution ids into the existing node
            auto existing = impl_->driver.get_entity_node(node.uuid);
            if (existing.has_value()) {
                auto& ex = existing.value();
                bool changed = false;

                auto merge_id = [&](std::vector<std::string>& vec, const std::string& id) {
                    if (id.empty()) return;
                    for (auto& v : vec) { if (v == id) return; }
                    vec.push_back(id);
                    changed = true;
                };
                auto merge_ids = [&](std::vector<std::string>& vec, const std::vector<std::string>& ids) {
                    for (auto& id : ids) merge_id(vec, id);
                };

                merge_id(ex.agent_ids, aid);
                merge_id(ex.source_ids, sid);
                merge_id(ex.source_contexts, sctx);
                merge_ids(ex.participant_ids, pids);

                if (changed) {
                    (void)impl_->driver.save_entity_node(ex);
                }
            }
        } else {
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
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        // Get or create saga node
        auto existing = impl_->driver.get_saga_by_name(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            (void)impl_->driver.save_saga_node(saga_node);
        }

        // Find previous episode in saga (if not explicitly provided)
        std::string prev_ep_uuid;
        if (opts.saga_previous_episode_uuid.has_value() && !opts.saga_previous_episode_uuid.value().empty()) {
            prev_ep_uuid = opts.saga_previous_episode_uuid.value();
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

    // 12. Update communities if requested
    if (opts.update_communities) {
        for (auto& node : nodes) {
            (void)pipeline::update_community(
                impl_->driver, impl_->llm, impl_->embedder, node);
        }
    }

    // Discard raw episode content from DB if configured
    if (!impl_->config.store_raw_episode_content) {
        episode.content.clear();
        (void)impl_->driver.save_episodic_node(episode);
    }

    // Build result
    AddEpisodeResult result;
    result.episode = std::move(episode);
    result.nodes = std::move(nodes);
    result.edges = std::move(new_edges);

    return result;
}

Result<AddBulkEpisodeResults> Graphiti::add_episode_bulk(AddEpisodeBulkOptions opts) {
    if (opts.episodes.empty()) {
        return AddBulkEpisodeResults{};
    }

    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(opts.group_id);
    auto& aid = opts.agent_id;
    auto& sid = opts.source_id;
    auto& sctx = opts.source_context;
    auto& pids = opts.participant_ids;
    auto now = std::chrono::system_clock::now();

    // ========================================================================
    // Step 1: Create and save episodic nodes for all episodes
    // ========================================================================
    std::vector<EpisodicNode> episodes;
    episodes.reserve(opts.episodes.size());

    for (auto& raw : opts.episodes) {
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
        // Per-episode source_id/source_context/participant_ids override batch-level defaults
        ep.source_id = raw.source_id.empty() ? sid : raw.source_id;
        ep.source_context = raw.source_context.empty() ? sctx : raw.source_context;
        ep.participant_ids = raw.participant_ids.empty() ? pids : raw.participant_ids;
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
        if (opts.type_defs) {
            input.entity_types = opts.type_defs->entity_types_prompt_json();
            input.type_defs = opts.type_defs;
        } else {
            input.entity_types = R"([{"entity_type_id": 0, "entity_type_name": "Entity", "entity_type_description": "A general entity"}])";
        }
        input.previous_episodes = episode_contexts[i];
        input.group_id = gid;
        input.source_description = episodes[i].source_description;
        if (opts.custom_instructions.has_value()) {
            input.custom_instructions = opts.custom_instructions.value();
        }

        auto result = pipeline::extract_nodes(impl_->llm, input);
        if (result.has_value()) {
            auto& nodes = result.value();
            auto& ep_sid = episodes[i].source_id;
            auto& ep_sctx = episodes[i].source_context;
            auto& ep_pids = episodes[i].participant_ids;
            for (auto& node : nodes) {
                if (!aid.empty()) node.agent_ids = {aid};
                if (!ep_sid.empty()) node.source_ids = {ep_sid};
                if (!ep_sctx.empty()) node.source_contexts = {ep_sctx};
                if (!ep_pids.empty()) node.participant_ids = ep_pids;
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
        if (opts.type_defs && !opts.type_defs->edge_types.empty()) {
            input.edge_types = opts.type_defs->edge_types_prompt_json();
        }
        if (opts.custom_instructions.has_value()) {
            input.custom_instructions = opts.custom_instructions.value();
        }

        auto result = pipeline::extract_edges(impl_->llm, input);
        if (result.has_value()) {
            auto& ep_sid = episodes[i].source_id;
            auto& ep_sctx = episodes[i].source_context;
            auto& ep_pids = episodes[i].participant_ids;
            for (auto& edge : result.value()) {
                edge.episodes.push_back(episodes[i].uuid);
                if (!aid.empty()) edge.agent_ids = {aid};
                if (!ep_sid.empty()) edge.source_ids = {ep_sid};
                if (!ep_sctx.empty()) edge.source_contexts = {ep_sctx};
                if (!ep_pids.empty()) edge.participant_ids = ep_pids;
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

    // Step 9b: Extract custom attributes for typed entities
    if (opts.type_defs) {
        (void)pipeline::extract_entity_attributes(
            impl_->llm, deduped_nodes, *opts.type_defs, combined_content
        );
    }

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
        if (is_existing) {
            // Merge attribution ids into the existing node
            auto existing = impl_->driver.get_entity_node(node.uuid);
            if (existing.has_value()) {
                auto& ex = existing.value();
                bool changed = false;

                auto merge_id = [&](std::vector<std::string>& vec, const std::string& id) {
                    if (id.empty()) return;
                    for (auto& v : vec) { if (v == id) return; }
                    vec.push_back(id);
                    changed = true;
                };
                auto merge_ids = [&](std::vector<std::string>& vec, const std::vector<std::string>& ids) {
                    for (auto& id : ids) merge_id(vec, id);
                };

                merge_id(ex.agent_ids, aid);
                // Merge all source_ids, source_contexts, and participant_ids from this node
                merge_ids(ex.source_ids, node.source_ids);
                merge_ids(ex.source_contexts, node.source_contexts);
                merge_ids(ex.participant_ids, node.participant_ids);

                if (changed) {
                    (void)impl_->driver.save_entity_node(ex);
                }
            }
        } else {
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
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        auto existing = impl_->driver.get_saga_by_name(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
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
    // Discard raw episode content from DB if configured
    if (!impl_->config.store_raw_episode_content) {
        for (auto& ep : episodes) {
            ep.content.clear();
            (void)impl_->driver.save_episodic_node(ep);
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

Result<std::vector<EntityEdge>> Graphiti::search(SearchOptions opts) {
    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(opts.group_id);

    auto result = hybrid_edge_search(
        impl_->driver, impl_->embedder, opts.query, gid, opts.num_results, 0.0f,
        opts.filters.has_value() ? &opts.filters.value() : nullptr
    );

    if (!result.has_value()) return std::unexpected(result.error());

    return std::move(result.value().edges);
}

Result<SearchResults> Graphiti::search_advanced(SearchAdvancedOptions opts) {
    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(opts.group_id);

    const std::vector<std::string>* bfs_ptr =
        opts.bfs_origin_node_uuids.has_value() ? &opts.bfs_origin_node_uuids.value() : nullptr;

    return search_orchestrator(
        impl_->driver, impl_->embedder, impl_->llm,
        opts.query, gid, opts.config,
        opts.filters.has_value() ? &opts.filters.value() : nullptr,
        opts.center_node_uuid.has_value() ? &opts.center_node_uuid.value() : nullptr,
        bfs_ptr
    );
}

Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>>
Graphiti::build_communities(const std::vector<std::string>& group_ids) {
    std::lock_guard lock(impl_->mu);
    return pipeline::build_communities(
        impl_->driver, impl_->llm, impl_->embedder, group_ids);
}

VoidResult Graphiti::delete_group(std::string_view group_id) {
    std::lock_guard lock(impl_->mu);
    return impl_->driver.clear_data({std::string(group_id)});
}

// ============================================================================
// retrieve_episodes
// ============================================================================

Result<std::vector<EpisodicNode>> Graphiti::retrieve_episodes(
    TimePoint reference_time,
    int last_n,
    std::string_view group_id,
    std::optional<EpisodeType> source,
    std::optional<std::string> saga
) {
    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(group_id);

    if (saga.has_value() && !saga.value().empty()) {
        return impl_->driver.retrieve_episodes_by_saga(
            saga.value(), gid, reference_time, last_n
        );
    }

    return impl_->driver.retrieve_episodes(gid, reference_time, last_n, source);
}

// ============================================================================
// get_nodes_and_edges_by_episode
// ============================================================================

Result<SearchResults> Graphiti::get_nodes_and_edges_by_episode(
    const std::vector<std::string>& episode_uuids
) {
    std::lock_guard lock(impl_->mu);

    SearchResults results;

    for (auto& ep_uuid : episode_uuids) {
        // Get the episode itself
        auto ep = impl_->driver.get_episodic_node(ep_uuid);
        if (ep.has_value()) {
            results.episodes.push_back(std::move(ep.value()));
        }

        // Get entity edges that reference this episode
        auto edge_uuids = impl_->driver.get_edge_uuids_by_episode(ep_uuid);
        if (edge_uuids.has_value()) {
            auto edges = impl_->driver.get_entity_edges(edge_uuids.value());
            if (edges.has_value()) {
                for (auto& edge : edges.value()) {
                    results.edges.push_back(std::move(edge));
                }
            }
        }

        // Get mentioned entity nodes
        auto node_uuids = impl_->driver.get_mentioned_entity_uuids(ep_uuid);
        if (node_uuids.has_value()) {
            auto nodes = impl_->driver.get_entity_nodes(node_uuids.value());
            if (nodes.has_value()) {
                for (auto& node : nodes.value()) {
                    results.nodes.push_back(std::move(node));
                }
            }
        }
    }

    return results;
}

// ============================================================================
// remove_episode
// ============================================================================

VoidResult Graphiti::remove_episode(std::string_view episode_uuid) {
    std::lock_guard lock(impl_->mu);

    // Get edges that reference this episode
    auto edge_uuids = impl_->driver.get_edge_uuids_by_episode(episode_uuid);
    if (edge_uuids.has_value()) {
        for (auto& edge_uuid : edge_uuids.value()) {
            auto edge = impl_->driver.get_entity_edge(edge_uuid);
            if (edge.has_value()) {
                // Only delete edges first created by this episode
                if (!edge.value().episodes.empty() &&
                    edge.value().episodes[0] == std::string(episode_uuid)) {
                    (void)impl_->driver.delete_entity_edge(edge_uuid);
                }
            }
        }
    }

    // Get mentioned entities and delete those only referenced by this episode
    auto node_uuids = impl_->driver.get_mentioned_entity_uuids(episode_uuid);
    if (node_uuids.has_value()) {
        for (auto& node_uuid : node_uuids.value()) {
            auto count = impl_->driver.count_episode_mentions(node_uuid);
            if (count.has_value() && count.value() <= 1) {
                (void)impl_->driver.delete_entity_node(node_uuid);
            }
        }
    }

    // Delete the episode itself (DETACH DELETE removes MENTIONS edges too)
    return impl_->driver.delete_episodic_node(episode_uuid);
}

// ============================================================================
// add_triplet
// ============================================================================

Result<Graphiti::AddTripletResult> Graphiti::add_triplet(
    EntityNode source_node,
    EntityEdge edge,
    EntityNode target_node
) {
    std::lock_guard lock(impl_->mu);

    // Generate UUIDs if not set
    if (source_node.uuid.empty()) source_node.uuid = uuid::generate();
    if (target_node.uuid.empty()) target_node.uuid = uuid::generate();
    if (edge.uuid.empty()) edge.uuid = uuid::generate();

    // Generate embeddings if missing
    if (!source_node.name_embedding.has_value() && !source_node.name.empty()) {
        try {
            source_node.name_embedding = impl_->embedder.create(source_node.name);
        } catch (...) {}
    }
    if (!target_node.name_embedding.has_value() && !target_node.name.empty()) {
        try {
            target_node.name_embedding = impl_->embedder.create(target_node.name);
        } catch (...) {}
    }
    if (!edge.fact_embedding.has_value() && !edge.fact.empty()) {
        try {
            edge.fact_embedding = impl_->embedder.create(edge.fact);
        } catch (...) {}
    }

    // Try to find existing nodes by name (simple name-based dedup)
    auto resolve_node = [&](EntityNode& node) {
        if (!node.name_embedding.has_value()) return;
        auto results = impl_->driver.search_entity_nodes_cosine(
            node.name_embedding.value(), node.group_id, 0.9f, 1
        );
        if (results.has_value() && !results.value().empty()) {
            auto& existing = results.value()[0];
            // Merge: keep existing UUID, update labels/attributes/summary
            node.uuid = existing.uuid;
            if (node.labels.empty()) node.labels = existing.labels;
            if (node.summary.empty()) node.summary = existing.summary;
            if (node.attributes.empty()) node.attributes = existing.attributes;
        }
    };

    resolve_node(source_node);
    resolve_node(target_node);

    // Wire edge to resolved node UUIDs
    edge.source_node_uuid = source_node.uuid;
    edge.target_node_uuid = target_node.uuid;

    // Save nodes
    (void)impl_->driver.save_entity_node(source_node);
    if (source_node.name_embedding.has_value()) {
        (void)impl_->driver.save_entity_node_embedding(
            source_node.uuid, source_node.name_embedding.value());
    }

    (void)impl_->driver.save_entity_node(target_node);
    if (target_node.name_embedding.has_value()) {
        (void)impl_->driver.save_entity_node_embedding(
            target_node.uuid, target_node.name_embedding.value());
    }

    // Save edge
    (void)impl_->driver.save_entity_edge(edge);
    if (edge.fact_embedding.has_value()) {
        (void)impl_->driver.save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
    }

    AddTripletResult result;
    result.nodes = {std::move(source_node), std::move(target_node)};
    result.edges = {std::move(edge)};
    return result;
}

const TokenTracker& Graphiti::token_tracker() const {
    return impl_->llm.token_tracker;
}

kuzu::main::Database& Graphiti::database() const {
    return *impl_->driver.database();
}

} // namespace graphiti
