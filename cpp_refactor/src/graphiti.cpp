#include <graphiti/graphiti.h>

#include <main/kuzu.h>

#include "driver/kuzu_driver.h"
#include "remote/kuzu_writer_client.h"
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
#include "llm/logging_llm_client.h"
#include "embedder/logging_embedder.h"
#include "search/search.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <graphiti/log.h>

#include <algorithm>
#include <format>
#include <future>
#include <mutex>
#include <numeric>
#include <semaphore>
#include <set>
#include <unordered_set>

namespace graphiti {

struct Graphiti::Impl {
    std::mutex mu; // Serializes all public method calls (Kuzu Connection is not thread-safe)
    GraphitiConfig config;
    KuzuDriver driver;
    std::unique_ptr<LLMClient> llm_owned;
    std::unique_ptr<EmbedderClient> embedder_owned;
    LLMClient* llm;
    EmbedderClient* embedder;
    std::unique_ptr<KuzuWriterClient> writer_client;  // set when kuzu_writer_uri is configured
    std::vector<GraphitiLogger*> loggers;                    // non-owned
    std::vector<std::unique_ptr<GraphitiLogger>> owned_loggers;  // owned

    void log_llm(const GraphitiLogger::LLMCallInfo& info) {
        for (auto* l : loggers) l->on_llm_call(info);
    }
    void log_embedding(const GraphitiLogger::EmbeddingCallInfo& info) {
        for (auto* l : loggers) l->on_embedding_call(info);
    }
    void log_pipeline(const GraphitiLogger::PipelineStepInfo& info) {
        for (auto* l : loggers) l->on_pipeline_step(info);
    }

    // When kuzu_writer_uri is set, local driver opens read-only (daemon handles writes)
    static bool local_read_only(const GraphitiConfig& c) {
        return c.read_only || !c.kuzu_writer_uri.empty();
    }

    // Default: create OpenAI clients from config
    Impl(GraphitiConfig cfg)
        : config(std::move(cfg))
        , driver(config.db_path, local_read_only(config))
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

    // Shared DB + default OpenAI clients
    Impl(GraphitiConfig cfg, kuzu::main::Database& shared_db)
        : config(std::move(cfg))
        , driver(shared_db)
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

    // Custom providers (nullptr = use OpenAI default from config)
    Impl(GraphitiConfig cfg,
         std::unique_ptr<LLMClient> custom_llm,
         std::unique_ptr<EmbedderClient> custom_embedder)
        : config(std::move(cfg))
        , driver(config.db_path, local_read_only(config))
        , llm_owned(custom_llm ? std::move(custom_llm)
                                : std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(custom_embedder ? std::move(custom_embedder)
                                          : std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

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
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

    void init_writer_client() {
        if (!config.kuzu_writer_uri.empty()) {
            writer_client = std::make_unique<KuzuWriterClient>(
                config.kuzu_writer_uri, config.kuzu_writer_target_db);
            auto r = writer_client->connect();
            if (!r.has_value()) {
                log_info("[graphiti] ❌ Failed to connect to kuzu-writer-server: %s\n",
                        r.error().message.c_str());
                log_info("[graphiti] Falling back to direct Kuzu writes\n");
                writer_client.reset();
            } else {
                log_info("[graphiti] 🔧 Connected to kuzu-writer-server at %s (target=%s)\n",
                        config.kuzu_writer_uri.c_str(), config.kuzu_writer_target_db.c_str());
            }
        }
    }

    bool has_writer() const { return writer_client != nullptr; }

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
    auto step_start = std::chrono::steady_clock::now();
    auto log_step = [&](const char* label) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] add_episode %s: %lldms\n", label, elapsed);
        step_start = std::chrono::steady_clock::now();
    };

    log_debug("[graphiti] add_episode: body=%zu chars, group=%s, name=%s\n",
              opts.body.size(), gid.c_str(), opts.name.c_str());

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

    // Save content for extraction before potentially clearing for storage
    auto episode_body = episode.content;

    if (!impl_->config.store_raw_episode_content) {
        episode.content.clear();
    }

    auto save_ep = impl_->has_writer()
        ? impl_->writer_client->save_episodic_node(episode)
        : impl_->driver.save_episodic_node(episode);
    if (!save_ep.has_value()) return std::unexpected(save_ep.error());
    log_step("Step 1-2 (episode context + save)");

    // 3. Extract entities via LLM
    pipeline::ExtractNodesInput extract_input;
    extract_input.episode_content = episode_body;
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

    auto nodes_result = pipeline::extract_nodes(*impl_->llm, extract_input);
    if (!nodes_result.has_value()) return std::unexpected(nodes_result.error());
    auto extracted_nodes = std::move(nodes_result.value());
    log_step("Step 3 (extract nodes — LLM)");
    log_debug("[graphiti]   → %zu nodes extracted\n", extracted_nodes.size());

    // Set attribution ids on extracted nodes
    for (auto& node : extracted_nodes) {
        if (!aid.empty()) node.agent_ids = {aid};
        if (!sid.empty()) node.source_ids = {sid};
        if (!sctx.empty()) node.source_contexts = {sctx};
        if (!pids.empty()) node.participant_ids = pids;
    }

    // 4. Deduplicate nodes against existing graph
    auto dedup_result = pipeline::dedupe_nodes(
        *impl_->llm, impl_->driver, *impl_->embedder,
        extracted_nodes, previous_episodes, episode_body, gid
    );
    if (!dedup_result.has_value()) return std::unexpected(dedup_result.error());
    auto& dedup = dedup_result.value();
    auto& nodes = dedup.nodes;
    auto& uuid_map = dedup.uuid_map;
    log_step("Step 4 (dedupe nodes — LLM + embeddings)");
    log_debug("[graphiti]   → %zu nodes after dedup, %zu mappings\n", nodes.size(), uuid_map.size());

    // 5. Extract edges via LLM
    pipeline::ExtractEdgesInput edge_input;
    edge_input.episode_content = episode_body;
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

    auto edges_result = pipeline::extract_edges(*impl_->llm, edge_input);
    if (!edges_result.has_value()) return std::unexpected(edges_result.error());
    auto extracted_edges = std::move(edges_result.value());
    log_step("Step 5 (extract edges — LLM)");
    log_debug("[graphiti]   → %zu edges extracted\n", extracted_edges.size());

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
        *impl_->llm, impl_->driver, extracted_edges
    );
    if (!edge_dedup_result.has_value()) return std::unexpected(edge_dedup_result.error());
    auto& edge_dedup = edge_dedup_result.value();
    auto& new_edges = edge_dedup.new_edges;
    log_step("Step 6 (dedupe edges — LLM)");
    log_debug("[graphiti]   → %zu new edges, %zu invalidated\n",
              new_edges.size(), edge_dedup.invalidated_uuids.size());

    // 7. Enrich node summaries via LLM
    (void)pipeline::enrich_node_summaries(*impl_->llm, nodes, previous_episodes, episode_body);

    log_step("Step 7 (enrich node summaries — LLM)");

    // 7b. Extract custom attributes for typed entities
    if (opts.type_defs) {
        (void)pipeline::extract_entity_attributes(*impl_->llm, nodes, *opts.type_defs, episode_body);
        log_step("Step 7b (extract attributes — LLM)");
    }

    // 8. Generate embeddings for nodes and edges
    for (auto& node : nodes) {
        try {
            node.name_embedding = impl_->embedder->create(node.name);
        } catch (...) {}
    }
    for (auto& edge : new_edges) {
        try {
            edge.fact_embedding = impl_->embedder->create(edge.fact);
        } catch (...) {}
    }
    log_step("Step 8 (embeddings)");
    log_debug("[graphiti]   → %zu node embeddings, %zu edge embeddings\n", nodes.size(), new_edges.size());

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
            auto existing = impl_->has_writer()
                ? impl_->writer_client->get_entity_node(node.uuid)
                : impl_->driver.get_entity_node(node.uuid);
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
                    if (impl_->has_writer())
                        (void)impl_->writer_client->save_entity_node(ex);
                    else
                        (void)impl_->driver.save_entity_node(ex);
                }
            }
        } else {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node(node);
            else
                (void)impl_->driver.save_entity_node(node);
        }
        if (node.name_embedding.has_value()) {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node_embedding(node.uuid, node.name_embedding.value());
            else
                (void)impl_->driver.save_entity_node_embedding(node.uuid, node.name_embedding.value());
        }
    }

    for (auto& edge : new_edges) {
        edge.episodes.push_back(episode.uuid);
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge(edge);
        else
            (void)impl_->driver.save_entity_edge(edge);
        if (edge.fact_embedding.has_value()) {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
            else
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
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge(edge);
            else
                (void)impl_->driver.save_entity_edge(edge);
        }
    }

    // 10. Create episodic edges (MENTIONS)
    if (impl_->has_writer()) {
        auto edge_now = std::chrono::system_clock::now();
        for (auto& enode : nodes) {
            EpisodicEdge ee;
            ee.uuid = uuid::generate();
            ee.group_id = episode.group_id;
            ee.source_node_uuid = episode.uuid;
            ee.target_node_uuid = enode.uuid;
            ee.created_at = edge_now;
            ee.agent_id = episode.agent_id;
            ee.source_id = episode.source_id;
            ee.source_context = episode.source_context;
            ee.participant_ids = episode.participant_ids;
            (void)impl_->writer_client->save_episodic_edge(ee);
        }
    } else {
        (void)pipeline::create_episodic_edges(impl_->driver, episode, nodes);
    }

    // 11. Saga processing (if saga name provided)
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        // Get or create saga node
        auto existing = impl_->has_writer()
            ? impl_->writer_client->get_saga_by_name(opts.saga.value(), gid)
            : impl_->driver.get_saga_by_name(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            if (impl_->has_writer())
                (void)impl_->writer_client->save_saga_node(saga_node);
            else
                (void)impl_->driver.save_saga_node(saga_node);
        }

        // Find previous episode in saga (if not explicitly provided)
        std::string prev_ep_uuid;
        if (opts.saga_previous_episode_uuid.has_value() && !opts.saga_previous_episode_uuid.value().empty()) {
            prev_ep_uuid = opts.saga_previous_episode_uuid.value();
        } else {
            auto last = impl_->has_writer()
                ? impl_->writer_client->get_last_episode_in_saga(saga_node.uuid, episode.uuid)
                : impl_->driver.get_last_episode_in_saga(saga_node.uuid, episode.uuid);
            if (last.has_value() && last.value().has_value()) {
                prev_ep_uuid = last.value().value();
            }
        }

        // Create NEXT_EPISODE edge (prev -> current)
        if (!prev_ep_uuid.empty()) {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_next_episode_edge(
                    uuid::generate(), prev_ep_uuid, episode.uuid, gid, now);
            else
                (void)impl_->driver.save_next_episode_edge(
                    uuid::generate(), prev_ep_uuid, episode.uuid, gid, now);
        }

        // Create HAS_EPISODE edge (saga -> current episode)
        if (impl_->has_writer())
            (void)impl_->writer_client->save_has_episode_edge(
                uuid::generate(), saga_node.uuid, episode.uuid, gid, now);
        else
            (void)impl_->driver.save_has_episode_edge(
                uuid::generate(), saga_node.uuid, episode.uuid, gid, now);
    }

    // 12. Update communities if requested
    if (opts.update_communities) {
        for (auto& node : nodes) {
            (void)pipeline::update_community(
                impl_->driver, *impl_->llm, *impl_->embedder, node);
        }
    }

    // Discard raw episode content from DB if configured
    if (!impl_->config.store_raw_episode_content) {
        episode.content.clear();
        if (impl_->has_writer())
            (void)impl_->writer_client->save_episodic_node(episode);
        else
            (void)impl_->driver.save_episodic_node(episode);
    }

    log_step("Step 9-12 (persist + episodic edges + saga + communities)");

    // Build result
    AddEpisodeResult result;
    result.episode = std::move(episode);
    result.nodes = std::move(nodes);
    result.edges = std::move(new_edges);

    log_debug("[graphiti] add_episode complete: %zu nodes, %zu edges\n",
              result.nodes.size(), result.edges.size());
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
    auto bulk_start = std::chrono::steady_clock::now();
    auto step_start = bulk_start;
    log_info("[graphiti] add_episode_bulk: %zu episodes, max_parallel=%d\n",
            opts.episodes.size(), std::max(1, impl_->config.max_parallel_extractions));

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
        auto r = impl_->has_writer()
            ? impl_->writer_client->save_episodic_node(ep)
            : impl_->driver.save_episodic_node(ep);
        if (!r.has_value()) return std::unexpected(r.error());
    }

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 1 (save episodic nodes): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
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

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 2 (episode context retrieval): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
    }

    // ========================================================================
    // Step 3: Extract nodes from each episode (PARALLEL)
    // ========================================================================
    // nodes_by_episode[i] = nodes extracted from episodes[i]
    int max_workers = std::max(1, impl_->config.max_parallel_extractions);
    std::vector<std::vector<EntityNode>> nodes_by_episode(episodes.size());

    log_debug("[graphiti] Step 3: extracting nodes from %zu episodes (max_parallel=%d)\n",
            episodes.size(), max_workers);
    auto step3_start = std::chrono::steady_clock::now();

    {
        std::counting_semaphore sem(max_workers);
        std::vector<std::future<void>> futures;
        futures.reserve(episodes.size());

        for (size_t i = 0; i < episodes.size(); ++i) {
            // Build input on the main thread (reads shared state)
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

            // Capture per-episode attribution
            auto ep_aid = aid;
            auto ep_sid = episodes[i].source_id;
            auto ep_sctx = episodes[i].source_context;
            auto ep_pids = episodes[i].participant_ids;
            auto& llm_config = impl_->config.llm;
            auto& dst = nodes_by_episode[i];

            auto ep_index = i;
            auto ep_name = episodes[i].name;
            futures.push_back(std::async(std::launch::async,
                [&sem, &llm_config, &dst, input = std::move(input),
                 ep_aid, ep_sid, ep_sctx, ep_pids, ep_index, ep_name]() mutable {
                    sem.acquire();
                    log_trace("[graphiti]   node extraction %zu \"%s\" started\n",
                              ep_index, ep_name.c_str());
                    auto t0 = std::chrono::steady_clock::now();
                    OpenAIClient llm(llm_config);
                    auto result = pipeline::extract_nodes(llm, input);
                    auto t1 = std::chrono::steady_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                    log_trace("[graphiti]   node extraction %zu done (%lldms, %zu nodes)\n",
                              ep_index, ms, result.has_value() ? result->size() : 0);
                    sem.release();

                    if (result.has_value()) {
                        for (auto& node : result.value()) {
                            if (!ep_aid.empty()) node.agent_ids = {ep_aid};
                            if (!ep_sid.empty()) node.source_ids = {ep_sid};
                            if (!ep_sctx.empty()) node.source_contexts = {ep_sctx};
                            if (!ep_pids.empty()) node.participant_ids = ep_pids;
                        }
                        dst = std::move(result.value());
                    }
                }
            ));
        }

        // Wait for all node extractions to complete
        for (auto& f : futures) f.get();
    }

    {
        auto step3_end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(step3_end - step3_start).count();
        log_debug("[graphiti] Step 3 complete: %lldms\n", ms);
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

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 4 (cross-dedup nodes): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
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
        *impl_->llm, impl_->driver, *impl_->embedder,
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

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 5 (dedupe nodes vs graph — LLM): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
    }

    // ========================================================================
    // Step 6: Extract edges from each episode (PARALLEL)
    // ========================================================================
    std::vector<std::vector<EntityEdge>> edges_by_episode(episodes.size());

    log_debug("[graphiti] Step 6: extracting edges from %zu episodes (max_parallel=%d)\n",
            episodes.size(), max_workers);
    auto step6_start = std::chrono::steady_clock::now();

    {
        std::counting_semaphore sem(max_workers);
        std::vector<std::future<void>> futures;
        futures.reserve(episodes.size());

        for (size_t i = 0; i < episodes.size(); ++i) {
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

            auto ep_aid = aid;
            auto ep_sid = episodes[i].source_id;
            auto ep_sctx = episodes[i].source_context;
            auto ep_pids = episodes[i].participant_ids;
            auto ep_uuid = episodes[i].uuid;
            auto& llm_config = impl_->config.llm;
            auto& dst = edges_by_episode[i];

            auto ep_index = i;
            auto ep_name2 = episodes[i].name;
            futures.push_back(std::async(std::launch::async,
                [&sem, &llm_config, &dst, input = std::move(input),
                 ep_aid, ep_sid, ep_sctx, ep_pids, ep_uuid, ep_index, ep_name2]() mutable {
                    sem.acquire();
                    log_trace("[graphiti]   edge extraction %zu \"%s\" started\n",
                              ep_index, ep_name2.c_str());
                    auto t0 = std::chrono::steady_clock::now();
                    OpenAIClient llm(llm_config);
                    auto result = pipeline::extract_edges(llm, input);
                    auto t1 = std::chrono::steady_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                    log_trace("[graphiti]   edge extraction %zu done (%lldms, %zu edges)\n",
                              ep_index, ms, result.has_value() ? result->size() : 0);
                    sem.release();

                    if (result.has_value()) {
                        for (auto& edge : result.value()) {
                            edge.episodes.push_back(ep_uuid);
                            if (!ep_aid.empty()) edge.agent_ids = {ep_aid};
                            if (!ep_sid.empty()) edge.source_ids = {ep_sid};
                            if (!ep_sctx.empty()) edge.source_contexts = {ep_sctx};
                            if (!ep_pids.empty()) edge.participant_ids = ep_pids;
                        }
                        dst = std::move(result.value());
                    }
                }
            ));
        }

        for (auto& f : futures) f.get();
    }

    {
        auto step6_end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(step6_end - step6_start).count();
        log_debug("[graphiti] Step 6 complete: %lldms\n", ms);
    }

    // Flatten edges_by_episode into all_edges
    std::vector<EntityEdge> all_edges;
    for (auto& ep_edges : edges_by_episode) {
        for (auto& edge : ep_edges) {
            all_edges.push_back(std::move(edge));
        }
    }

    // ========================================================================
    // Step 7: Remap edge pointers using combined UUID map
    // ========================================================================
    step_start = std::chrono::steady_clock::now();
    pipeline::resolve_edge_pointers(all_edges, combined_uuid_map);

    // ========================================================================
    // Step 8: Deduplicate edges against existing graph
    // ========================================================================
    // NOTE: Bulk mode skips edge invalidation for speed
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 7 (remap edge pointers): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
    }
    auto edge_dedup = pipeline::dedupe_edges(*impl_->llm, impl_->driver, all_edges);
    std::vector<EntityEdge> final_edges;
    if (edge_dedup.has_value()) {
        final_edges = std::move(edge_dedup.value().new_edges);
    } else {
        final_edges = std::move(all_edges);
    }

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 8 (dedupe edges vs graph — LLM): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
    }

    // ========================================================================
    // Step 9: Enrich node summaries
    // ========================================================================
    (void)pipeline::enrich_node_summaries(
        *impl_->llm, deduped_nodes, nlohmann::json::array(), combined_content
    );

    // Step 9b: Extract custom attributes for typed entities
    if (opts.type_defs) {
        (void)pipeline::extract_entity_attributes(
            *impl_->llm, deduped_nodes, *opts.type_defs, combined_content
        );
    }

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 9 (enrich node summaries — LLM): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
    }

    // ========================================================================
    // Step 10: Generate embeddings
    // ========================================================================
    for (auto& node : deduped_nodes) {
        try {
            node.name_embedding = impl_->embedder->create(node.name);
        } catch (...) {}
    }
    for (auto& edge : final_edges) {
        try {
            edge.fact_embedding = impl_->embedder->create(edge.fact);
        } catch (...) {}
    }

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Step 10 (embeddings): %lldms\n", ms);
        step_start = std::chrono::steady_clock::now();
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
            auto existing = impl_->has_writer()
                ? impl_->writer_client->get_entity_node(node.uuid)
                : impl_->driver.get_entity_node(node.uuid);
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
                    if (impl_->has_writer())
                        (void)impl_->writer_client->save_entity_node(ex);
                    else
                        (void)impl_->driver.save_entity_node(ex);
                }
            }
        } else {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node(node);
            else
                (void)impl_->driver.save_entity_node(node);
        }
        if (node.name_embedding.has_value()) {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node_embedding(node.uuid, node.name_embedding.value());
            else
                (void)impl_->driver.save_entity_node_embedding(node.uuid, node.name_embedding.value());
        }
    }

    for (auto& edge : final_edges) {
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge(edge);
        else
            (void)impl_->driver.save_entity_edge(edge);
        if (edge.fact_embedding.has_value()) {
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
            else
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

        if (impl_->has_writer()) {
            // Inline episodic edge creation for remote writer path
            auto edge_now = std::chrono::system_clock::now();
            for (auto& enode : ep_unique_nodes) {
                EpisodicEdge edge;
                edge.uuid = uuid::generate();
                edge.group_id = episodes[i].group_id;
                edge.source_node_uuid = episodes[i].uuid;
                edge.target_node_uuid = enode.uuid;
                edge.created_at = edge_now;
                edge.agent_id = episodes[i].agent_id;
                edge.source_id = episodes[i].source_id;
                edge.source_context = episodes[i].source_context;
                edge.participant_ids = episodes[i].participant_ids;
                (void)impl_->writer_client->save_episodic_edge(edge);
            }
        } else {
            (void)pipeline::create_episodic_edges(impl_->driver, episodes[i], ep_unique_nodes);
        }
    }

    // ========================================================================
    // Step 13: Saga processing (if saga name provided)
    // ========================================================================
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        auto existing = impl_->has_writer()
            ? impl_->writer_client->get_saga_by_name(opts.saga.value(), gid)
            : impl_->driver.get_saga_by_name(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            if (impl_->has_writer())
                (void)impl_->writer_client->save_saga_node(saga_node);
            else
                (void)impl_->driver.save_saga_node(saga_node);
        }

        // Sort episodes by valid_at for correct NEXT_EPISODE ordering
        std::vector<size_t> sorted_indices(episodes.size());
        std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](size_t a, size_t b) { return episodes[a].valid_at < episodes[b].valid_at; });

        // Find previous episode already in saga
        std::string prev_ep_uuid;
        auto last = impl_->has_writer()
            ? impl_->writer_client->get_last_episode_in_saga(saga_node.uuid)
            : impl_->driver.get_last_episode_in_saga(saga_node.uuid);
        if (last.has_value() && last.value().has_value()) {
            prev_ep_uuid = last.value().value();
        }

        for (auto idx : sorted_indices) {
            auto& ep = episodes[idx];

            if (!prev_ep_uuid.empty()) {
                if (impl_->has_writer())
                    (void)impl_->writer_client->save_next_episode_edge(
                        uuid::generate(), prev_ep_uuid, ep.uuid, gid, now);
                else
                    (void)impl_->driver.save_next_episode_edge(
                        uuid::generate(), prev_ep_uuid, ep.uuid, gid, now);
            }

            if (impl_->has_writer())
                (void)impl_->writer_client->save_has_episode_edge(
                    uuid::generate(), saga_node.uuid, ep.uuid, gid, now);
            else
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
            if (impl_->has_writer())
                (void)impl_->writer_client->save_episodic_node(ep);
            else
                (void)impl_->driver.save_episodic_node(ep);
        }
    }

    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] Steps 11-13 (persist + episodic edges + saga): %lldms\n", ms);
    }
    {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bulk_start).count();
        log_info("[graphiti] add_episode_bulk TOTAL: %lldms\n", total_ms);
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
        impl_->driver, *impl_->embedder, opts.query, gid, opts.num_results, 0.0f,
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
        impl_->driver, *impl_->embedder, *impl_->llm,
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
        impl_->driver, *impl_->llm, *impl_->embedder, group_ids);
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
            source_node.name_embedding = impl_->embedder->create(source_node.name);
        } catch (...) {}
    }
    if (!target_node.name_embedding.has_value() && !target_node.name.empty()) {
        try {
            target_node.name_embedding = impl_->embedder->create(target_node.name);
        } catch (...) {}
    }
    if (!edge.fact_embedding.has_value() && !edge.fact.empty()) {
        try {
            edge.fact_embedding = impl_->embedder->create(edge.fact);
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
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(source_node);
    else
        (void)impl_->driver.save_entity_node(source_node);
    if (source_node.name_embedding.has_value()) {
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_node_embedding(
                source_node.uuid, source_node.name_embedding.value());
        else
            (void)impl_->driver.save_entity_node_embedding(
                source_node.uuid, source_node.name_embedding.value());
    }

    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(target_node);
    else
        (void)impl_->driver.save_entity_node(target_node);
    if (target_node.name_embedding.has_value()) {
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_node_embedding(
                target_node.uuid, target_node.name_embedding.value());
        else
            (void)impl_->driver.save_entity_node_embedding(
                target_node.uuid, target_node.name_embedding.value());
    }

    // Save edge
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_edge(edge);
    else
        (void)impl_->driver.save_entity_edge(edge);
    if (edge.fact_embedding.has_value()) {
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
        else
            (void)impl_->driver.save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
    }

    AddTripletResult result;
    result.nodes = {std::move(source_node), std::move(target_node)};
    result.edges = {std::move(edge)};
    return result;
}

const TokenTracker& Graphiti::token_tracker() const {
    return impl_->llm->token_tracker;
}

Result<SearchResults> Graphiti::get_graph_overview(std::string_view group_id, int max_nodes, int max_edges) {
    std::lock_guard lock(impl_->mu);
    auto gid = impl_->resolve_group_id(group_id);

    // Step 1: Lightweight fetch — only uuid, name, labels per node
    auto summaries_result = impl_->driver.get_node_summaries_by_group(gid);
    if (!summaries_result) return std::unexpected(summaries_result.error());
    auto& summaries = *summaries_result;

    // Step 2: Get all edges (lightweight — uuid, name, src, tgt only)
    // Build full node set first to filter edges
    std::set<std::string> all_uuids;
    for (auto& s : summaries) all_uuids.insert(s.uuid);

    auto edges_result = impl_->driver.get_edge_summaries_by_nodes(all_uuids, gid);
    if (!edges_result) return std::unexpected(edges_result.error());
    auto& edge_summaries = *edges_result;

    // Step 3: Count edges per node for ranking
    std::unordered_map<std::string, int> edge_counts;
    for (auto& e : edge_summaries) {
        edge_counts[e.source_node_uuid]++;
        edge_counts[e.target_node_uuid]++;
    }

    // Step 4: Sort nodes by edge count, cap at max_nodes
    std::sort(summaries.begin(), summaries.end(),
        [&](const KuzuDriver::NodeSummary& a, const KuzuDriver::NodeSummary& b) {
            return edge_counts[a.uuid] > edge_counts[b.uuid];
        });
    if ((int)summaries.size() > max_nodes)
        summaries.resize(max_nodes);

    // Step 5: Build kept set, filter edges to only kept endpoints
    std::set<std::string> kept;
    for (auto& s : summaries) kept.insert(s.uuid);

    std::vector<KuzuDriver::EdgeSummary> kept_edges;
    for (auto& e : edge_summaries) {
        if (kept.count(e.source_node_uuid) && kept.count(e.target_node_uuid))
            kept_edges.push_back(std::move(e));
    }

    // Step 6: Cap edges, preferring high-degree node connections
    if ((int)kept_edges.size() > max_edges) {
        std::sort(kept_edges.begin(), kept_edges.end(),
            [&](const KuzuDriver::EdgeSummary& a, const KuzuDriver::EdgeSummary& b) {
                return (edge_counts[a.source_node_uuid] + edge_counts[a.target_node_uuid])
                     > (edge_counts[b.source_node_uuid] + edge_counts[b.target_node_uuid]);
            });
        kept_edges.resize(max_edges);
    }

    // Step 7: Convert to SearchResults (minimal EntityNode/EntityEdge — no heavy fields)
    SearchResults results;
    for (auto& s : summaries) {
        EntityNode node;
        node.uuid             = std::move(s.uuid);
        node.name             = std::move(s.name);
        node.labels           = std::move(s.labels);
        node.agent_ids        = std::move(s.agent_ids);
        node.source_ids       = std::move(s.source_ids);
        node.source_contexts  = std::move(s.source_contexts);
        node.participant_ids  = std::move(s.participant_ids);
        results.nodes.push_back(std::move(node));
    }
    results.node_scores.assign(results.nodes.size(), 0.0f);

    for (auto& e : kept_edges) {
        EntityEdge edge;
        edge.uuid             = std::move(e.uuid);
        edge.name             = std::move(e.name);
        edge.source_node_uuid = std::move(e.source_node_uuid);
        edge.target_node_uuid = std::move(e.target_node_uuid);
        edge.agent_ids        = std::move(e.agent_ids);
        edge.source_ids       = std::move(e.source_ids);
        edge.source_contexts  = std::move(e.source_contexts);
        edge.participant_ids  = std::move(e.participant_ids);
        results.edges.push_back(std::move(edge));
    }
    results.edge_scores.assign(results.edges.size(), 0.0f);

    return results;
}

void Graphiti::add_logger(GraphitiLogger* logger) {
    bool first = impl_->loggers.empty();
    impl_->loggers.push_back(logger);
    if (first) {
        // Wrap LLM and embedder with logging decorators
        impl_->llm_owned = std::make_unique<LoggingLLMClient>(std::move(impl_->llm_owned), impl_->loggers);
        impl_->llm = impl_->llm_owned.get();
        impl_->embedder_owned = std::make_unique<LoggingEmbedder>(std::move(impl_->embedder_owned), impl_->loggers);
        impl_->embedder = impl_->embedder_owned.get();
    }
}

void Graphiti::add_logger(std::unique_ptr<GraphitiLogger> logger) {
    bool first = impl_->loggers.empty();
    impl_->loggers.push_back(logger.get());
    impl_->owned_loggers.push_back(std::move(logger));
    if (first) {
        impl_->llm_owned = std::make_unique<LoggingLLMClient>(std::move(impl_->llm_owned), impl_->loggers);
        impl_->llm = impl_->llm_owned.get();
        impl_->embedder_owned = std::make_unique<LoggingEmbedder>(std::move(impl_->embedder_owned), impl_->loggers);
        impl_->embedder = impl_->embedder_owned.get();
    }
}

kuzu::main::Database& Graphiti::database() const {
    return *impl_->driver.database();
}

} // namespace graphiti
