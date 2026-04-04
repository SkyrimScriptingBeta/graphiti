#include <graphiti/graphiti.h>

#include <main/kuzu.h>

#include <graphiti/graph_store.h>
#include "driver/kuzu_graph_store.h"
// OLD_KUZU: was #include "driver/kuzu_driver.h" — absorbed into kuzu_graph_store
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
#include "llm/response_models.h"
#include "llm/logging_llm_client.h"
#include "embedder/logging_embedder.h"
#include "search/search.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <graphiti/log.h>
#include <graphiti/callsite_log.h>

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
    KuzuGraphStore kuzu_store;     // Kuzu-specific store (owns the database)
    GraphStore& store;             // Abstract interface — all call sites go through this
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
        , kuzu_store(config.db_path, local_read_only(config))
        , store(kuzu_store)
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

    // Shared DB + default OpenAI clients
    Impl(GraphitiConfig cfg, kuzu::main::Database& shared_db)
        : config(std::move(cfg))
        , kuzu_store(shared_db)
        , store(kuzu_store)
        , llm_owned(std::make_unique<OpenAIClient>(config.llm))
        , embedder_owned(std::make_unique<OpenAIEmbedder>(config.embedder))
        , llm(llm_owned.get())
        , embedder(embedder_owned.get()) { init_writer_client(); }

    // Custom providers (nullptr = use OpenAI default from config)
    Impl(GraphitiConfig cfg,
         std::unique_ptr<LLMClient> custom_llm,
         std::unique_ptr<EmbedderClient> custom_embedder)
        : config(std::move(cfg))
        , kuzu_store(config.db_path, local_read_only(config))
        , store(kuzu_store)
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
        , kuzu_store(shared_db)
        , store(kuzu_store)
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
    graphiti::log_callsite("build-indices-setup-schema");
    auto r = impl_->store.setup_schema();
    if (!r.has_value()) return r;
    graphiti::log_callsite("build-indices-fts");
    return impl_->store.rebuild_indices();
}

VoidResult Graphiti::initialize_self(const AgentIdentity& id) {
    std::lock_guard lock(impl_->mu);
    auto now = std::chrono::system_clock::now();

    // 1. Create Self node — the identity anchor
    EntityNode self_node;
    self_node.uuid = "self";
    self_node.name = "Self";
    self_node.group_id = id.group_id;
    self_node.labels = {"Entity", "Identity"};
    self_node.created_at = now;
    self_node.summary = id.name + " — this is me. This node represents the owner of this graph.";
    self_node.is_system = true;

    graphiti::log_callsite("init-self-save-self-node");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(self_node);
    else
        (void)impl_->store.persist_entity(self_node);

    // 2. Create Person entity for the agent — UUID is just the lowercase name
    //    so extraction naturally dedupes into it (extraction produces "keel" → merges with this node)
    auto person_uuid = id.name;
    for (auto& c : person_uuid) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    EntityNode person_node;
    person_node.uuid = person_uuid;
    person_node.name = id.name;
    person_node.group_id = id.group_id;
    person_node.labels = {"Entity", "Person"};
    person_node.created_at = now;
    person_node.summary = id.name + " is the " + id.role + " on the " + id.team + " team.";
    person_node.is_system = true;
    person_node.is_identity = true;

    graphiti::log_callsite("init-self-save-person-node");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(person_node);
    else
        (void)impl_->store.persist_entity(person_node);

    // 3. Create Role entity
    EntityNode role_node;
    role_node.uuid = "role_" + id.role;
    role_node.name = id.role;
    role_node.group_id = id.group_id;
    role_node.labels = {"Entity", "Role"};
    role_node.created_at = now;
    role_node.summary = id.role_description;
    role_node.is_system = true;

    graphiti::log_callsite("init-self-save-role-node");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(role_node);
    else
        (void)impl_->store.persist_entity(role_node);

    // 4. Wire identity edges
    auto create_identity_edge = [&](const std::string& src_uuid, const std::string& tgt_uuid,
                                     const std::string& edge_name, const std::string& fact) {
        EntityEdge edge;
        edge.uuid = uuid::generate();
        edge.group_id = id.group_id;
        edge.source_node_uuid = src_uuid;
        edge.target_node_uuid = tgt_uuid;
        edge.name = edge_name;
        edge.fact = fact;
        edge.created_at = now;
        edge.is_system = true;

        graphiti::log_callsite("init-self-save-identity-edges");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge(edge);
        else
            (void)impl_->store.persist_edge(edge);
    };

    // Self → SAME_AS → Person
    create_identity_edge("self", person_node.uuid, "SAME_AS",
        "Self is " + id.name + ". " + id.name + " is Self.");

    // Self → HAS_ROLE → Role
    create_identity_edge("self", role_node.uuid, "HAS_ROLE",
        id.name + " is the " + id.role + ".");

    // Person → HAS_ROLE → Role (so the person entity also connects to its role)
    create_identity_edge(person_node.uuid, role_node.uuid, "HAS_ROLE",
        id.name + " has the role of " + id.role + ".");

    log_trace("[graphiti] ✅ Self node initialized: %s (%s) on team %s\n",
              id.name.c_str(), id.role.c_str(), id.team.c_str());

    // Rebuild FTS so dedup can find these entities
    graphiti::log_callsite("init-self-rebuild-fts");
    if (impl_->has_writer())
        (void)impl_->writer_client->build_fts_indices();
    else
        (void)impl_->store.rebuild_indices();

    return {};
}

Result<int> Graphiti::sweep_orphans(std::string_view group_id) {
    std::lock_guard lock(impl_->mu);
    std::string gid(group_id);

    // Find all entities in this group
    graphiti::log_callsite("sweep-orphans-find-all-entities");
    auto all_result = impl_->store.search_entities_bm25("*", gid, 500);
    if (!all_result) return 0;

    // Find entities that appear in edges
    auto edges_query = std::format(
        R"(MATCH (a:Entity)-[:RELATES_TO]->(r:RelatesToNode_)-[:RELATES_TO]->(b:Entity)
WHERE a.group_id = '{0}' OR b.group_id = '{0}'
RETURN DISTINCT a.uuid AS uuid
UNION ALL
MATCH (a:Entity)-[:RELATES_TO]->(r:RelatesToNode_)-[:RELATES_TO]->(b:Entity)
WHERE a.group_id = '{0}' OR b.group_id = '{0}'
RETURN DISTINCT b.uuid AS uuid)", gid);

    auto* db = impl_->kuzu_store.database();
    auto conn = std::make_unique<kuzu::main::Connection>(db);
    auto edge_result = conn->query(edges_query);

    std::set<std::string> connected_uuids;
    if (edge_result && edge_result->isSuccess()) {
        while (edge_result->hasNext()) {
            auto tuple = edge_result->getNext();
            auto* val = tuple->getValue(0);
            if (!val->isNull()) {
                connected_uuids.insert(val->getValue<std::string>());
            }
        }
    }

    // Split into orphans and connected
    std::vector<EntityNode> orphans, connected;
    for (auto& node : *all_result) {
        if (node.is_system) continue;
        if (connected_uuids.count(node.uuid))
            connected.push_back(node);
        else
            orphans.push_back(node);
    }

    if (orphans.empty()) return 0;

    log_trace("[graphiti] sweep_orphans: %zu orphans, %zu connected\n", orphans.size(), connected.size());

    // Build connected entity context
    std::string connected_context;
    for (auto& c : connected) {
        if (connected_context.size() > 4000) break;
        connected_context += "- " + c.name;
        if (!c.summary.empty()) connected_context += ": \"" + c.summary.substr(0, 100) + "\"";
        connected_context += "\n";
    }

    int total_connected = 0;

    // One LLM call per orphan
    for (size_t i = 0; i < orphans.size(); ++i) {
        auto& orphan = orphans[i];
        log_trace("[graphiti]   orphan %zu/%zu: \"%s\"\n", i + 1, orphans.size(), orphan.name.c_str());

        std::string sys = "You are an expert at finding relationships between entities in a knowledge graph.\n"
                          "Do not escape unicode characters.\n";

        std::string user = std::format(
            R"(Entity: {} ({})
{}
Connected entities in the graph (prefer these):
{}
What is the SINGLE most meaningful relationship from "{}" to one of the entities above?
Use ONLY names from the list above. Do NOT invent new names.
Respond with ONE edge:
{{"edges": [{{"source_entity_name": "...", "target_entity_name": "...", "relation_type": "...", "fact": "...", "valid_at": null, "invalid_at": null}}]}}
If there is genuinely no relationship, respond with {{"edges": []}})",
            orphan.name,
            orphan.labels.size() > 1 ? orphan.labels[1] : "Entity",
            orphan.summary.empty() ? "" : "Summary: " + orphan.summary + "\n",
            connected_context,
            orphan.name);

        std::vector<Message> msgs = {{"system", std::move(sys)}, {"user", std::move(user)}};
        impl_->llm->prompt_name = "sweep_orphan";
        auto llm_result = impl_->llm->generate_response(msgs, response_schemas::EXTRACTED_EDGES, ModelSize::small);

        if (!llm_result) {
            log_trace("[graphiti]     → LLM failed, skipping\n");
            continue;
        }

        try {
            auto edges = llm_result->get<ExtractedEdges>();
            for (auto& se : edges.edges) {
                // Resolve names to UUIDs
                std::string src_uuid, tgt_uuid;
                for (auto& node : *all_result) {
                    if (node.name == se.source_entity_name) src_uuid = node.uuid;
                    if (node.name == se.target_entity_name) tgt_uuid = node.uuid;
                }

                if (!src_uuid.empty() && !tgt_uuid.empty() && src_uuid != tgt_uuid) {
                    EntityEdge edge;
                    edge.uuid = uuid::generate();
                    edge.source_node_uuid = src_uuid;
                    edge.target_node_uuid = tgt_uuid;
                    edge.name = se.relation_type;
                    edge.fact = se.fact;
                    edge.group_id = gid;
                    edge.created_at = std::chrono::system_clock::now();

                    if (impl_->has_writer())
                        (void)impl_->writer_client->save_entity_edge(edge);
                    else
                        (void)impl_->store.persist_edge(edge);

                    log_trace("[graphiti]     → %s %s %s\n",
                              se.source_entity_name.c_str(), se.relation_type.c_str(), se.target_entity_name.c_str());
                    total_connected++;
                } else {
                    log_trace("[graphiti]     → name not found, skipping\n");
                }
            }
        } catch (...) {
            log_trace("[graphiti]     → parse failed, skipping\n");
        }
    }

    // Rebuild FTS after new edges
    if (impl_->has_writer())
        (void)impl_->writer_client->build_fts_indices();
    else
        (void)impl_->store.rebuild_indices();

    return total_connected;
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
    auto log_step = [&](const char* label, bool success = true, int items_in = 0, int items_out = 0,
                         std::string_view error_msg = "") {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - step_start).count();
        log_debug("[graphiti] add_episode %s: %lldms\n", label, elapsed);

        GraphitiLogger::PipelineStepInfo pinfo;
        pinfo.step_name = label;
        pinfo.latency_ms = static_cast<double>(elapsed);
        pinfo.success = success;
        pinfo.error_message = error_msg;
        pinfo.items_in = items_in;
        pinfo.items_out = items_out;
        impl_->log_pipeline(pinfo);

        step_start = std::chrono::steady_clock::now();
    };

    // Estimate tokens: use word count × 1.3 heuristic (same as CollabLite chunker)
    int est_words = 0;
    { bool in_w = false;
      for (char c : opts.body) {
          if (c == ' ' || c == '\t' || c == '\n' || c == '\r') in_w = false;
          else if (!in_w) { in_w = true; ++est_words; }
      }
    }
    int est_tokens = static_cast<int>(est_words * 1.3f + 0.5f);
    log_debug("[graphiti] add_episode: body=%zu chars (~%d tokens), group=%s, name=%s\n",
              opts.body.size(), est_tokens, gid.c_str(), opts.name.c_str());

    // 1. Retrieve previous episodes for context
    log_trace("[graphiti] → Step 1: retrieve_episodes...\n");
    graphiti::log_callsite("episode-retrieve-prior-episodes");
    auto prev_result = impl_->store.retrieve_episodes(gid, opts.reference_time, 10, opts.source);
    log_trace("[graphiti] ✓ Step 1: retrieve_episodes done (%s)\n",
              prev_result.has_value() ? std::to_string(prev_result->size()).c_str() : "failed");
    nlohmann::json previous_episodes = nlohmann::json::array();
    if (prev_result.has_value()) {
        for (auto& ep : prev_result.value()) {
            previous_episodes.push_back({{"content", ep.content}});
        }
    }

    // 2. Create episodic node
    log_trace("[graphiti] → Step 2: save_episodic_node...\n");
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

    graphiti::log_callsite("episode-save-episodic-node");
    auto save_ep = impl_->has_writer()
        ? impl_->writer_client->save_episodic_node(episode)
        : impl_->store.persist_episode(episode);
    log_trace("[graphiti] ✓ Step 2: save_episodic_node done (%s)\n",
              save_ep.has_value() ? "ok" : save_ep.error().message.c_str());
    if (!save_ep.has_value()) return std::unexpected(save_ep.error());
    log_step("save_episode");

    // 3. Extract entities via LLM
    log_trace("[graphiti] → Step 3: extract_nodes (LLM)...\n");
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
    log_trace("[graphiti] ✓ Step 3: extract_nodes done (%s)\n",
              nodes_result.has_value() ? std::to_string(nodes_result->size()).c_str() : nodes_result.error().message.c_str());
    if (!nodes_result.has_value()) return std::unexpected(nodes_result.error());
    auto extracted_nodes = std::move(nodes_result.value());
    log_step("extract_nodes", true, 1, static_cast<int>(extracted_nodes.size()));
    log_debug("[graphiti]   → %zu nodes extracted\n", extracted_nodes.size());

    // Set attribution ids on extracted nodes
    for (auto& node : extracted_nodes) {
        if (!aid.empty()) node.agent_ids = {aid};
        if (!sid.empty()) node.source_ids = {sid};
        if (!sctx.empty()) node.source_contexts = {sctx};
        if (!pids.empty()) node.participant_ids = pids;
    }

    // 3b. Deduplicate nodes within the batch by name (programmatic, no LLM)
    {
        std::unordered_map<std::string, size_t> seen;
        std::vector<EntityNode> unique_nodes;
        for (auto& node : extracted_nodes) {
            auto it = seen.find(node.name);
            if (it == seen.end()) {
                seen[node.name] = unique_nodes.size();
                unique_nodes.push_back(std::move(node));
            }
            // else: duplicate name, skip it
        }
        if (unique_nodes.size() < extracted_nodes.size()) {
            log_debug("[graphiti]   → deduped %zu → %zu nodes by name\n",
                      extracted_nodes.size(), unique_nodes.size());
        }
        extracted_nodes = std::move(unique_nodes);
    }

    // 3d. Enrich node summaries BEFORE dedup — so the dedup LLM has context on both sides
    log_trace("[graphiti] → Step 3d: enrich_node_summaries (pre-dedup)...\n");
    (void)pipeline::enrich_node_summaries(*impl_->llm, extracted_nodes, previous_episodes, episode_body);
    log_trace("[graphiti] ✓ Step 3d: enrich_node_summaries done\n");
    log_step("enrich_node_summaries_pre_dedup", true,
             static_cast<int>(extracted_nodes.size()), static_cast<int>(extracted_nodes.size()));

    // 4. Deduplicate nodes against existing graph
    log_trace("[graphiti] → Step 4: dedupe_nodes vs graph...\n");
    auto dedup_result = pipeline::dedupe_nodes(
        *impl_->llm, impl_->store, *impl_->embedder,
        extracted_nodes, previous_episodes, episode_body, gid
    );
    log_trace("[graphiti] ✓ Step 4: dedupe_nodes done (%s)\n",
              dedup_result.has_value() ? "ok" : dedup_result.error().message.c_str());
    if (!dedup_result.has_value()) return std::unexpected(dedup_result.error());
    auto& dedup = dedup_result.value();
    auto& nodes = dedup.nodes;
    auto& uuid_map = dedup.uuid_map;
    log_step("dedupe_nodes", true, static_cast<int>(extracted_nodes.size()), static_cast<int>(nodes.size()));
    log_debug("[graphiti]   → %zu nodes after dedup, %zu mappings\n", nodes.size(), uuid_map.size());

    // 5. Extract edges via LLM
    log_trace("[graphiti] → Step 5: extract_edges (LLM)\n");
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
    edge_input.shard_size = impl_->config.llm.edge_shard_size;
    edge_input.max_edges = impl_->config.llm.max_edges;

    // Scale edge extraction token budget based on entity count
    {
        int n = static_cast<int>(nodes.size());
        int threshold = impl_->config.llm.entity_scaling_threshold;
        int extra_per = impl_->config.llm.extra_tokens_per_entity;
        if (n > threshold && extra_per > 0) {
            int extra = (n - threshold) * extra_per;
            impl_->llm->max_tokens_override = impl_->config.llm.max_tokens + extra;
            log_debug("[graphiti] 📏 edge extraction: %d entities, token budget %d → %d (+%d)\n",
                      n, impl_->config.llm.max_tokens, impl_->llm->max_tokens_override, extra);
        }
    }

    auto edges_result = pipeline::extract_edges(*impl_->llm, edge_input);
    impl_->llm->max_tokens_override = 0;  // reset after edge extraction
    log_trace("[graphiti] ✓ Step 5: extract_edges done (%s)\n",
              edges_result.has_value() ? std::to_string(edges_result->size()).c_str() : edges_result.error().message.c_str());
    if (!edges_result.has_value()) return std::unexpected(edges_result.error());
    auto extracted_edges = std::move(edges_result.value());
    log_step("extract_edges", true, static_cast<int>(nodes.size()), static_cast<int>(extracted_edges.size()));
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

    // 5c. Deduplicate edges within this batch (same source+target+name = duplicate)
    {
        std::set<std::string> seen_edges;
        std::vector<EntityEdge> unique_edges;
        for (auto& edge : extracted_edges) {
            auto key = edge.source_node_uuid + "|" + edge.target_node_uuid + "|" + edge.name;
            if (seen_edges.insert(key).second)
                unique_edges.push_back(std::move(edge));
        }
        if (unique_edges.size() < extracted_edges.size()) {
            log_trace("[graphiti]   → batch edge dedup: %zu → %zu (dropped %zu duplicates)\n",
                      extracted_edges.size(), unique_edges.size(),
                      extracted_edges.size() - unique_edges.size());
        }
        extracted_edges = std::move(unique_edges);
    }

    // 6. Deduplicate edges against existing graph
    log_trace("[graphiti] → Step 6: dedupe_edges vs graph...\n");
    auto edge_dedup_result = pipeline::dedupe_edges(
        *impl_->llm, impl_->store, extracted_edges
    );
    if (!edge_dedup_result.has_value()) return std::unexpected(edge_dedup_result.error());
    auto& edge_dedup = edge_dedup_result.value();
    auto& new_edges = edge_dedup.new_edges;
    log_step("dedupe_edges", true, static_cast<int>(extracted_edges.size()), static_cast<int>(new_edges.size()));
    log_debug("[graphiti]   → %zu new edges, %zu invalidated\n",
              new_edges.size(), edge_dedup.invalidated_uuids.size());

    // 6b. Orphan sweep — find entities with no edges and connect them
    {
        // Find which entities appear in edges
        std::set<std::string> entities_with_edges;
        for (auto& edge : new_edges) {
            entities_with_edges.insert(edge.source_node_uuid);
            entities_with_edges.insert(edge.target_node_uuid);
        }

        // Collect orphans and connected entities
        std::vector<const EntityNode*> orphans;
        std::vector<const EntityNode*> connected;
        for (auto& node : nodes) {
            if (entities_with_edges.count(node.uuid))
                connected.push_back(&node);
            else
                orphans.push_back(&node);
        }

        if (!orphans.empty() && !connected.empty()) {
            log_trace("[graphiti] → Step 6b: orphan sweep (%zu orphans, %zu connected)...\n",
                      orphans.size(), connected.size());

            // Build the prompt — include summaries so the 7B knows WHAT each entity is
            std::string orphan_list;
            nlohmann::json orphan_names = nlohmann::json::array();
            for (auto* node : orphans) {
                std::string label = node->labels.size() > 1 ? node->labels[1] : "Entity";
                if (!node->summary.empty())
                    orphan_list += std::format("- {} ({}): \"{}\"\n", node->name, label, node->summary);
                else
                    orphan_list += std::format("- {} ({})\n", node->name, label);
                orphan_names.push_back(node->name);
            }

            std::string connected_list;
            for (auto* node : connected) {
                std::string label = node->labels.size() > 1 ? node->labels[1] : "Entity";
                // Find edges for this node
                std::string edge_info;
                for (auto& edge : new_edges) {
                    if (edge.source_node_uuid == node->uuid || edge.target_node_uuid == node->uuid) {
                        if (!edge_info.empty()) edge_info += ", ";
                        edge_info += edge.name;
                    }
                }
                connected_list += std::format("- {} ({}) [edges: {}]\n", node->name, label,
                                               edge_info.empty() ? "none" : edge_info);
            }

            // Build LLM messages for orphan sweep
            std::string sys = "You are an expert at finding relationships between entities in a knowledge graph. "
                              "Extract fact triples connecting disconnected entities to the graph.\n"
                              "Do not escape unicode characters.\n";

            std::string user = std::format(
                R"(These entities have NO relationships yet (orphans):
{}
These entities ARE connected to the graph:
{}
<EPISODE>
{}
</EPISODE>

For each orphan entity, extract its SINGLE most meaningful relationship to ANY other entity (orphan or connected).
Prefer connecting orphans to already-connected entities, but connecting an orphan to another orphan is better than leaving it disconnected.
Every orphan should get exactly ONE edge. Only create edges that are clearly supported by the episode text.
If an orphan genuinely has no relationship to any other entity, skip it.

CRITICAL: You MUST use ONLY entity names from the orphan list or the connected list above.
Do NOT invent new entity names. If the target entity is not in either list, do not create the edge.

Return edges in this format:
{{"edges": [{{"source_entity_name": "...", "target_entity_name": "...", "relation_type": "...", "fact": "...", "valid_at": null, "invalid_at": null}}]}})",
                orphan_list, connected_list, episode_body);

            std::vector<Message> sweep_messages = {{"system", std::move(sys)}, {"user", std::move(user)}};
            impl_->llm->prompt_name = "orphan_sweep";
            auto sweep_result = impl_->llm->generate_response(
                sweep_messages, response_schemas::EXTRACTED_EDGES, ModelSize::small);

            if (sweep_result.has_value()) {
                try {
                    auto sweep_edges = sweep_result->get<ExtractedEdges>();
                    int connected_orphans = 0;

                    for (auto& se : sweep_edges.edges) {
                        // Resolve entity names to UUIDs
                        std::string src_uuid, tgt_uuid;
                        for (auto& node : nodes) {
                            if (node.name == se.source_entity_name) src_uuid = node.uuid;
                            if (node.name == se.target_entity_name) tgt_uuid = node.uuid;
                        }

                        if (!src_uuid.empty() && !tgt_uuid.empty() && src_uuid != tgt_uuid) {
                            EntityEdge edge;
                            edge.uuid = uuid::generate();
                            edge.source_node_uuid = src_uuid;
                            edge.target_node_uuid = tgt_uuid;
                            edge.name = se.relation_type;
                            edge.fact = se.fact;
                            edge.group_id = gid;
                            edge.created_at = std::chrono::system_clock::now();
                            if (!aid.empty()) edge.agent_ids = {aid};
                            if (!sid.empty()) edge.source_ids = {sid};
                            if (!sctx.empty()) edge.source_contexts = {sctx};
                            if (!pids.empty()) edge.participant_ids = pids;
                            new_edges.push_back(std::move(edge));
                            connected_orphans++;
                        }
                    }
                    log_trace("[graphiti] ✓ Step 6b: orphan sweep connected %d/%zu orphans\n",
                              connected_orphans, orphans.size());
                } catch (const std::exception& e) {
                    log_trace("[graphiti] ⚠️ Step 6b: orphan sweep parse failed: %s\n", e.what());
                }
            } else {
                log_trace("[graphiti] ⚠️ Step 6b: orphan sweep LLM call failed\n");
            }
            log_step("orphan_sweep");
        }
    }

    // 7. Enrich node summaries via LLM
    log_trace("[graphiti] → Step 7: enrich_node_summaries...\n");
    (void)pipeline::enrich_node_summaries(*impl_->llm, nodes, previous_episodes, episode_body);
    log_trace("[graphiti] ✓ Step 7: enrich_node_summaries done\n");

    log_step("enrich_node_summaries", true, static_cast<int>(nodes.size()), static_cast<int>(nodes.size()));

    // 7b. Extract custom attributes for typed entities
    if (opts.type_defs) {
        (void)pipeline::extract_entity_attributes(*impl_->llm, nodes, *opts.type_defs, episode_body);
        log_step("extract_entity_attributes");
    }

    // 8. Generate embeddings for nodes and edges
    log_trace("[graphiti] → Step 8: embeddings...\n");
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
    log_step("embeddings");
    log_debug("[graphiti]   → %zu node embeddings, %zu edge embeddings\n", nodes.size(), new_edges.size());

    log_trace("[graphiti] ✓ Step 8: embeddings done\n");

    // 9. Persist everything to Kuzu
    log_trace("[graphiti] → Step 9: persist to Kuzu...\n");
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
            graphiti::log_callsite("episode-fetch-existing-for-merge");
            auto existing = impl_->has_writer()
                ? impl_->writer_client->get_entity_node(node.uuid)
                : impl_->store.get_entity(node.uuid);
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
                // Merge traits (UNION — accumulate over time)
                auto old_trait_count = ex.traits.size();
                merge_ids(ex.traits, node.traits);
                if (ex.traits.size() > old_trait_count) {
                    std::string trait_list;
                    for (size_t i = 0; i < ex.traits.size(); ++i) {
                        if (i > 0) trait_list += ", ";
                        trait_list += ex.traits[i];
                    }
                    log_trace("[graphiti]     → merged traits for \"%s\": [%s]\n", ex.name.c_str(), trait_list.c_str());
                }

                if (changed) {
                    graphiti::log_callsite("episode-save-merged-entity");
                    if (impl_->has_writer())
                        (void)impl_->writer_client->save_entity_node(ex);
                    else
                        (void)impl_->store.persist_entity(ex);
                }
            }
        } else {
            graphiti::log_callsite("episode-save-new-entity");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node(node);
            else
                (void)impl_->store.persist_entity(node);
        }
        if (node.name_embedding.has_value()) {
            graphiti::log_callsite("episode-save-entity-embedding");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node_embedding(node.uuid, node.name_embedding.value());
            else
                (void)impl_->store.persist_entity_embedding(node.uuid, node.name_embedding.value());
        }
    }

    // TODO: Filter edges that duplicate system edges (same source+target+type).
    // Needs a lightweight query method — deferred until driver supports targeted system edge lookup.

    for (auto& edge : new_edges) {
        edge.episodes.push_back(episode.uuid);
        graphiti::log_callsite("episode-save-entity-edge");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge(edge);
        else
            (void)impl_->store.persist_edge(edge);
        if (edge.fact_embedding.has_value()) {
            graphiti::log_callsite("episode-save-edge-embedding");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
            else
                (void)impl_->store.persist_edge_embedding(edge.uuid, edge.fact_embedding.value());
        }
    }

    // Invalidate contradicted edges
    for (auto& uuid : edge_dedup.invalidated_uuids) {
        graphiti::log_callsite("episode-fetch-contradicted-edge");
        auto edge_result = impl_->store.get_edge(uuid);
        if (edge_result.has_value()) {
            auto& edge = edge_result.value();
            edge.expired_at = now;
            edge.invalid_at = now;
            graphiti::log_callsite("episode-invalidate-contradicted-edge");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge(edge);
            else
                (void)impl_->store.persist_edge(edge);
        }
    }

    log_trace("[graphiti] ✓ Step 9: persist done\n");

    // 10. Create episodic edges (MENTIONS)
    log_trace("[graphiti] → Step 10: episodic edges...\n");
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
        (void)pipeline::create_episodic_edges(impl_->store, episode, nodes);
    }

    log_trace("[graphiti] ✓ Step 10: episodic edges done\n");

    // 10b. Rebuild FTS indices so subsequent BM25 searches find newly persisted entities.
    // Kuzu FTS indices are NOT incremental — they must be dropped and re-created after inserts.
    log_trace("[graphiti] → Rebuilding FTS indices...\n");
    graphiti::log_callsite("episode-rebuild-fts-after-persist");
    if (impl_->has_writer())
        (void)impl_->writer_client->build_fts_indices();
    else
        (void)impl_->store.rebuild_indices();
    log_trace("[graphiti] ✓ FTS indices rebuilt\n");

    // 11. Saga processing (if saga name provided)
    log_trace("[graphiti] → Step 11: saga processing...\n");
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        // Get or create saga node
        graphiti::log_callsite("episode-find-saga");
        auto existing = impl_->has_writer()
            ? impl_->writer_client->get_saga_by_name(opts.saga.value(), gid)
            : impl_->store.find_saga(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            graphiti::log_callsite("episode-save-saga-node");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_saga_node(saga_node);
            else
                (void)impl_->store.persist_saga(saga_node);
        }

        // Find previous episode in saga (if not explicitly provided)
        std::string prev_ep_uuid;
        if (opts.saga_previous_episode_uuid.has_value() && !opts.saga_previous_episode_uuid.value().empty()) {
            prev_ep_uuid = opts.saga_previous_episode_uuid.value();
        } else {
            graphiti::log_callsite("episode-find-last-in-saga");
            auto last = impl_->has_writer()
                ? impl_->writer_client->get_last_episode_in_saga(saga_node.uuid, episode.uuid)
                : impl_->store.get_last_saga_episode(saga_node.uuid, episode.uuid);
            if (last.has_value() && last.value().has_value()) {
                prev_ep_uuid = last.value().value();
            }
        }

        // Create NEXT_EPISODE edge (prev -> current)
        if (!prev_ep_uuid.empty()) {
            graphiti::log_callsite("episode-save-next-episode-edge");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_next_episode_edge(
                    uuid::generate(), prev_ep_uuid, episode.uuid, gid, now);
            else
                (void)impl_->store.link_episode_sequence(
                    uuid::generate(), prev_ep_uuid, episode.uuid, gid, now);
        }

        // Create HAS_EPISODE edge (saga -> current episode)
        graphiti::log_callsite("episode-save-has-episode-edge");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_has_episode_edge(
                uuid::generate(), saga_node.uuid, episode.uuid, gid, now);
        else
            (void)impl_->store.link_saga_episode(
                uuid::generate(), saga_node.uuid, episode.uuid, gid, now);
    }

    log_trace("[graphiti] ✓ Step 11: saga done\n");

    // 12. Update communities if requested
    log_trace("[graphiti] → Step 12: communities...\n");
    if (opts.update_communities) {
        for (auto& node : nodes) {
            (void)pipeline::update_community(
                impl_->store, *impl_->llm, *impl_->embedder, node);
        }
    }

    // Discard raw episode content from DB if configured
    if (!impl_->config.store_raw_episode_content) {
        episode.content.clear();
        graphiti::log_callsite("episode-resave-cleared-content");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_episodic_node(episode);
        else
            (void)impl_->store.persist_episode(episode);
    }

    log_step("persist_and_communities");

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
        graphiti::log_callsite("bulk-save-episodic-node");
        auto r = impl_->has_writer()
            ? impl_->writer_client->save_episodic_node(ep)
            : impl_->store.persist_episode(ep);
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
        graphiti::log_callsite("bulk-retrieve-prior-episodes");
        auto prev = impl_->store.retrieve_episodes(gid, ep.valid_at, 10, ep.source);
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
            auto* loggers_ptr = &impl_->loggers;  // thread-safe: logger has its own mutex
            futures.push_back(std::async(std::launch::async,
                [&sem, &llm_config, &dst, input = std::move(input),
                 ep_aid, ep_sid, ep_sctx, ep_pids, ep_index, ep_name, loggers_ptr]() mutable {
                    sem.acquire();
                    log_trace("[graphiti]   node extraction %zu \"%s\" started\n",
                              ep_index, ep_name.c_str());
                    auto t0 = std::chrono::steady_clock::now();
                    OpenAIClient llm(llm_config);
                    // Wire per-attempt logging if loggers are registered
                    if (loggers_ptr && !loggers_ptr->empty()) {
                        std::string model = llm_config.small_model;
                        int attempt_num = 0;
                        llm.on_attempt = [loggers_ptr, model, &attempt_num](
                            const std::vector<Message>& msgs, const Result<nlohmann::json>& result, bool is_pre) {
                            if (is_pre) { attempt_num++; return; }
                            GraphitiLogger::LLMCallInfo info;
                            info.model = model;
                            info.prompt_name = "extract_nodes_bulk";
                            info.attempt = attempt_num;
                            for (auto& m : msgs) info.request_messages.push_back({m.role, m.content});
                            if (result.has_value()) {
                                info.success = true;
                                info.response_body = result->dump();
                                if (result->contains("__token_usage__")) {
                                    auto& tu = (*result)["__token_usage__"];
                                    if (tu.contains("input_tokens")) info.input_tokens = tu["input_tokens"].get<int64_t>();
                                    if (tu.contains("output_tokens")) info.output_tokens = tu["output_tokens"].get<int64_t>();
                                }
                            } else {
                                info.success = false;
                                info.error_message = result.error().message;
                                info.error_code = "error";
                            }
                            for (auto* l : *loggers_ptr) l->on_llm_call(info);
                        };
                    }
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
        *impl_->llm, impl_->store, *impl_->embedder,
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
            input.shard_size = impl_->config.llm.edge_shard_size;
            input.max_edges = impl_->config.llm.max_edges;

            auto ep_aid = aid;
            auto ep_sid = episodes[i].source_id;
            auto ep_sctx = episodes[i].source_context;
            auto ep_pids = episodes[i].participant_ids;
            auto ep_uuid = episodes[i].uuid;
            auto& llm_config = impl_->config.llm;
            auto& dst = edges_by_episode[i];

            auto ep_index = i;
            auto ep_name2 = episodes[i].name;
            auto* loggers_ptr2 = &impl_->loggers;
            futures.push_back(std::async(std::launch::async,
                [&sem, &llm_config, &dst, input = std::move(input),
                 ep_aid, ep_sid, ep_sctx, ep_pids, ep_uuid, ep_index, ep_name2, loggers_ptr2]() mutable {
                    sem.acquire();
                    log_trace("[graphiti]   edge extraction %zu \"%s\" started\n",
                              ep_index, ep_name2.c_str());
                    auto t0 = std::chrono::steady_clock::now();
                    OpenAIClient llm(llm_config);
                    if (loggers_ptr2 && !loggers_ptr2->empty()) {
                        std::string model = llm_config.small_model;
                        int attempt_num = 0;
                        llm.on_attempt = [loggers_ptr2, model, &attempt_num](
                            const std::vector<Message>& msgs, const Result<nlohmann::json>& result, bool is_pre) {
                            if (is_pre) { attempt_num++; return; }
                            GraphitiLogger::LLMCallInfo info;
                            info.model = model;
                            info.prompt_name = "extract_edges_bulk";
                            info.attempt = attempt_num;
                            for (auto& m : msgs) info.request_messages.push_back({m.role, m.content});
                            if (result.has_value()) {
                                info.success = true;
                                info.response_body = result->dump();
                                if (result->contains("__token_usage__")) {
                                    auto& tu = (*result)["__token_usage__"];
                                    if (tu.contains("input_tokens")) info.input_tokens = tu["input_tokens"].get<int64_t>();
                                    if (tu.contains("output_tokens")) info.output_tokens = tu["output_tokens"].get<int64_t>();
                                }
                            } else {
                                info.success = false;
                                info.error_message = result.error().message;
                                info.error_code = "error";
                            }
                            for (auto* l : *loggers_ptr2) l->on_llm_call(info);
                        };
                    }
                    // Scale token budget based on entity count
                    {
                        int n = static_cast<int>(input.nodes.size());
                        int threshold = llm_config.entity_scaling_threshold;
                        int extra_per = llm_config.extra_tokens_per_entity;
                        if (n > threshold && extra_per > 0) {
                            llm.max_tokens_override = llm_config.max_tokens + (n - threshold) * extra_per;
                        }
                    }
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
    // 7b. Deduplicate edges within this batch (same source+target+name = duplicate)
    {
        std::set<std::string> seen_edges;
        std::vector<EntityEdge> unique_edges;
        for (auto& edge : all_edges) {
            auto key = edge.source_node_uuid + "|" + edge.target_node_uuid + "|" + edge.name;
            if (seen_edges.insert(key).second)
                unique_edges.push_back(std::move(edge));
        }
        if (unique_edges.size() < all_edges.size()) {
            log_trace("[graphiti]   → batch edge dedup: %zu → %zu (dropped %zu duplicates)\n",
                      all_edges.size(), unique_edges.size(),
                      all_edges.size() - unique_edges.size());
        }
        all_edges = std::move(unique_edges);
    }

    auto edge_dedup = pipeline::dedupe_edges(*impl_->llm, impl_->store, all_edges);
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
            graphiti::log_callsite("bulk-fetch-existing-for-merge");
            auto existing = impl_->has_writer()
                ? impl_->writer_client->get_entity_node(node.uuid)
                : impl_->store.get_entity(node.uuid);
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
                // Merge traits (UNION — accumulate over time)
                auto old_trait_count = ex.traits.size();
                merge_ids(ex.traits, node.traits);
                if (ex.traits.size() > old_trait_count) {
                    std::string trait_list;
                    for (size_t i = 0; i < ex.traits.size(); ++i) {
                        if (i > 0) trait_list += ", ";
                        trait_list += ex.traits[i];
                    }
                    log_trace("[graphiti]     → merged traits for \"%s\": [%s]\n", ex.name.c_str(), trait_list.c_str());
                }

                if (changed) {
                    graphiti::log_callsite("bulk-save-merged-entity");
                    if (impl_->has_writer())
                        (void)impl_->writer_client->save_entity_node(ex);
                    else
                        (void)impl_->store.persist_entity(ex);
                }
            }
        } else {
            graphiti::log_callsite("bulk-save-new-entity");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node(node);
            else
                (void)impl_->store.persist_entity(node);
        }
        if (node.name_embedding.has_value()) {
            graphiti::log_callsite("bulk-save-entity-embedding");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_node_embedding(node.uuid, node.name_embedding.value());
            else
                (void)impl_->store.persist_entity_embedding(node.uuid, node.name_embedding.value());
        }
    }

    for (auto& edge : final_edges) {
        graphiti::log_callsite("bulk-save-entity-edge");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge(edge);
        else
            (void)impl_->store.persist_edge(edge);
        if (edge.fact_embedding.has_value()) {
            graphiti::log_callsite("bulk-save-edge-embedding");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
            else
                (void)impl_->store.persist_edge_embedding(edge.uuid, edge.fact_embedding.value());
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
            (void)pipeline::create_episodic_edges(impl_->store, episodes[i], ep_unique_nodes);
        }
    }

    // ========================================================================
    // Step 12b: Rebuild FTS indices so subsequent BM25 searches find newly persisted entities.
    // Kuzu FTS indices are NOT incremental — they must be dropped and re-created after inserts.
    // ========================================================================
    log_trace("[graphiti] → Rebuilding FTS indices...\n");
    graphiti::log_callsite("bulk-rebuild-fts-after-persist");
    if (impl_->has_writer())
        (void)impl_->writer_client->build_fts_indices();
    else
        (void)impl_->store.rebuild_indices();
    log_trace("[graphiti] ✓ FTS indices rebuilt\n");

    // ========================================================================
    // Step 13: Saga processing (if saga name provided)
    // ========================================================================
    if (opts.saga.has_value() && !opts.saga.value().empty()) {
        graphiti::log_callsite("bulk-find-saga");
        auto existing = impl_->has_writer()
            ? impl_->writer_client->get_saga_by_name(opts.saga.value(), gid)
            : impl_->store.find_saga(opts.saga.value(), gid);
        SagaNode saga_node;
        if (existing.has_value() && existing.value().has_value()) {
            saga_node = std::move(existing.value().value());
        } else {
            saga_node.uuid = uuid::generate();
            saga_node.name = opts.saga.value();
            saga_node.group_id = gid;
            saga_node.created_at = now;
            graphiti::log_callsite("bulk-save-saga-node");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_saga_node(saga_node);
            else
                (void)impl_->store.persist_saga(saga_node);
        }

        // Sort episodes by valid_at for correct NEXT_EPISODE ordering
        std::vector<size_t> sorted_indices(episodes.size());
        std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](size_t a, size_t b) { return episodes[a].valid_at < episodes[b].valid_at; });

        // Find previous episode already in saga
        std::string prev_ep_uuid;
        graphiti::log_callsite("bulk-find-last-in-saga");
        auto last = impl_->has_writer()
            ? impl_->writer_client->get_last_episode_in_saga(saga_node.uuid)
            : impl_->store.get_last_saga_episode(saga_node.uuid);
        if (last.has_value() && last.value().has_value()) {
            prev_ep_uuid = last.value().value();
        }

        for (auto idx : sorted_indices) {
            auto& ep = episodes[idx];

            if (!prev_ep_uuid.empty()) {
                graphiti::log_callsite("bulk-save-next-episode-edge");
                if (impl_->has_writer())
                    (void)impl_->writer_client->save_next_episode_edge(
                        uuid::generate(), prev_ep_uuid, ep.uuid, gid, now);
                else
                    (void)impl_->store.link_episode_sequence(
                        uuid::generate(), prev_ep_uuid, ep.uuid, gid, now);
            }

            graphiti::log_callsite("bulk-save-has-episode-edge");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_has_episode_edge(
                    uuid::generate(), saga_node.uuid, ep.uuid, gid, now);
            else
                (void)impl_->store.link_saga_episode(
                    uuid::generate(), saga_node.uuid, ep.uuid, gid, now);

            prev_ep_uuid = ep.uuid;
        }
    }

    // ========================================================================
    // Discard raw episode content from DB if configured
    if (!impl_->config.store_raw_episode_content) {
        for (auto& ep : episodes) {
            ep.content.clear();
            graphiti::log_callsite("bulk-resave-cleared-content");
            if (impl_->has_writer())
                (void)impl_->writer_client->save_episodic_node(ep);
            else
                (void)impl_->store.persist_episode(ep);
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
        impl_->store, *impl_->embedder, opts.query, gid, opts.num_results, 0.0f,
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
        impl_->store, *impl_->embedder, *impl_->llm,
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
        impl_->store, *impl_->llm, *impl_->embedder, group_ids);
}

VoidResult Graphiti::delete_group(std::string_view group_id) {
    std::lock_guard lock(impl_->mu);
    graphiti::log_callsite("delete-group-clear-data");
    return impl_->store.clear_group({std::string(group_id)});
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
        graphiti::log_callsite("retrieve-episodes-by-saga");
        return impl_->store.retrieve_episodes_by_saga(
            saga.value(), gid, reference_time, last_n
        );
    }

    graphiti::log_callsite("retrieve-episodes-by-time");
    return impl_->store.retrieve_episodes(gid, reference_time, last_n, source);
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
        graphiti::log_callsite("get-by-episode-fetch-episode");
        auto ep = impl_->store.get_episode(ep_uuid);
        if (ep.has_value()) {
            results.episodes.push_back(std::move(ep.value()));
        }

        // Get entity edges that reference this episode
        graphiti::log_callsite("get-by-episode-edge-uuids");
        auto edge_uuids = impl_->store.get_edge_uuids_by_episode(ep_uuid);
        if (edge_uuids.has_value()) {
            graphiti::log_callsite("get-by-episode-fetch-edges");
            auto edges = impl_->store.get_edges(edge_uuids.value());
            if (edges.has_value()) {
                for (auto& edge : edges.value()) {
                    results.edges.push_back(std::move(edge));
                }
            }
        }

        // Get mentioned entity nodes
        graphiti::log_callsite("get-by-episode-mentioned-uuids");
        auto node_uuids = impl_->store.get_mentioned_entity_uuids(ep_uuid);
        if (node_uuids.has_value()) {
            graphiti::log_callsite("get-by-episode-fetch-nodes");
            auto nodes = impl_->store.get_entities(node_uuids.value());
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
    graphiti::log_callsite("remove-episode-get-edge-uuids");
    auto edge_uuids = impl_->store.get_edge_uuids_by_episode(episode_uuid);
    if (edge_uuids.has_value()) {
        for (auto& edge_uuid : edge_uuids.value()) {
            graphiti::log_callsite("remove-episode-check-edge-origin");
            auto edge = impl_->store.get_edge(edge_uuid);
            if (edge.has_value()) {
                // Only delete edges first created by this episode
                if (!edge.value().episodes.empty() &&
                    edge.value().episodes[0] == std::string(episode_uuid)) {
                    graphiti::log_callsite("remove-episode-delete-edge");
                    (void)impl_->store.delete_edge(edge_uuid);
                }
            }
        }
    }

    // Get mentioned entities and delete those only referenced by this episode
    graphiti::log_callsite("remove-episode-get-mentioned-uuids");
    auto node_uuids = impl_->store.get_mentioned_entity_uuids(episode_uuid);
    if (node_uuids.has_value()) {
        for (auto& node_uuid : node_uuids.value()) {
            auto count = impl_->store.count_episode_mentions(node_uuid);
            if (count.has_value() && count.value() <= 1) {
                graphiti::log_callsite("remove-episode-delete-orphan-node");
                (void)impl_->store.delete_entity(node_uuid);
            }
        }
    }

    // Delete the episode itself (DETACH DELETE removes MENTIONS edges too)
    graphiti::log_callsite("remove-episode-delete-episode");
    return impl_->store.delete_episode(episode_uuid);
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
        graphiti::log_callsite("triplet-dedup-cosine-search");
        auto results = impl_->store.search_entities_cosine(
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
    graphiti::log_callsite("triplet-save-source-node");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(source_node);
    else
        (void)impl_->store.persist_entity(source_node);
    if (source_node.name_embedding.has_value()) {
        graphiti::log_callsite("triplet-save-source-embedding");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_node_embedding(
                source_node.uuid, source_node.name_embedding.value());
        else
            (void)impl_->store.persist_entity_embedding(
                source_node.uuid, source_node.name_embedding.value());
    }

    graphiti::log_callsite("triplet-save-target-node");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_node(target_node);
    else
        (void)impl_->store.persist_entity(target_node);
    if (target_node.name_embedding.has_value()) {
        graphiti::log_callsite("triplet-save-target-embedding");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_node_embedding(
                target_node.uuid, target_node.name_embedding.value());
        else
            (void)impl_->store.persist_entity_embedding(
                target_node.uuid, target_node.name_embedding.value());
    }

    // Save edge
    graphiti::log_callsite("triplet-save-edge");
    if (impl_->has_writer())
        (void)impl_->writer_client->save_entity_edge(edge);
    else
        (void)impl_->store.persist_edge(edge);
    if (edge.fact_embedding.has_value()) {
        graphiti::log_callsite("triplet-save-edge-embedding");
        if (impl_->has_writer())
            (void)impl_->writer_client->save_entity_edge_embedding(edge.uuid, edge.fact_embedding.value());
        else
            (void)impl_->store.persist_edge_embedding(edge.uuid, edge.fact_embedding.value());
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
    graphiti::log_callsite("overview-get-node-summaries");
    auto summaries_result = impl_->store.get_node_summaries(gid);
    if (!summaries_result) return std::unexpected(summaries_result.error());
    auto& summaries = *summaries_result;

    // Step 2: Get all edges (lightweight — uuid, name, src, tgt only)
    // Build full node set first to filter edges
    std::set<std::string> all_uuids;
    for (auto& s : summaries) all_uuids.insert(s.uuid);

    graphiti::log_callsite("overview-get-edge-summaries");
    auto edges_result = impl_->store.get_edge_summaries(all_uuids, gid);
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
        [&](const GraphStore::NodeSummary& a, const GraphStore::NodeSummary& b) {
            return edge_counts[a.uuid] > edge_counts[b.uuid];
        });
    if ((int)summaries.size() > max_nodes)
        summaries.resize(max_nodes);

    // Step 5: Build kept set, filter edges to only kept endpoints
    std::set<std::string> kept;
    for (auto& s : summaries) kept.insert(s.uuid);

    std::vector<GraphStore::EdgeSummary> kept_edges;
    for (auto& e : edge_summaries) {
        if (kept.count(e.source_node_uuid) && kept.count(e.target_node_uuid))
            kept_edges.push_back(std::move(e));
    }

    // Step 6: Cap edges, preferring high-degree node connections
    if ((int)kept_edges.size() > max_edges) {
        std::sort(kept_edges.begin(), kept_edges.end(),
            [&](const GraphStore::EdgeSummary& a, const GraphStore::EdgeSummary& b) {
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
        impl_->llm_owned = std::make_unique<LoggingLLMClient>(
            std::move(impl_->llm_owned), impl_->loggers,
            impl_->config.llm.model, impl_->config.llm.small_model);
        impl_->llm = impl_->llm_owned.get();
        impl_->embedder_owned = std::make_unique<LoggingEmbedder>(
            std::move(impl_->embedder_owned), impl_->loggers, impl_->config.embedder.model);
        impl_->embedder = impl_->embedder_owned.get();
    }
}

void Graphiti::add_logger(std::unique_ptr<GraphitiLogger> logger) {
    bool first = impl_->loggers.empty();
    impl_->loggers.push_back(logger.get());
    impl_->owned_loggers.push_back(std::move(logger));
    if (first) {
        impl_->llm_owned = std::make_unique<LoggingLLMClient>(
            std::move(impl_->llm_owned), impl_->loggers,
            impl_->config.llm.model, impl_->config.llm.small_model);
        impl_->llm = impl_->llm_owned.get();
        impl_->embedder_owned = std::make_unique<LoggingEmbedder>(
            std::move(impl_->embedder_owned), impl_->loggers, impl_->config.embedder.model);
        impl_->embedder = impl_->embedder_owned.get();
    }
}

Result<std::vector<EntityNode>> Graphiti::search_entity_nodes_bm25(
    std::string_view query, std::string_view group_id, int limit,
    const SearchFilters* filters) {
    graphiti::log_callsite("public-bm25-node-search");
    return impl_->store.search_entities_bm25(query, group_id, limit, filters);
}

kuzu::main::Database& Graphiti::database() const {
    return *impl_->kuzu_store.database();
}

} // namespace graphiti
