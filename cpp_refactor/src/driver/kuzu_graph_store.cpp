// KuzuGraphStore — self-contained Kuzu implementation of GraphStore

#include "kuzu_graph_store.h"
#include "kuzu_schema.h"
#include "search/search_filters.h"
#include "utils/datetime.h"

#include <main/kuzu.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>

namespace graphiti {

namespace {

// ============================================================================
// Type aliases
// ============================================================================

using ParamMap = std::unordered_map<std::string, std::unique_ptr<kuzu::common::Value>>;

// ============================================================================
// Value creation helpers
// ============================================================================

auto str_val(std::string_view s) -> std::unique_ptr<kuzu::common::Value> {
    return std::make_unique<kuzu::common::Value>(std::string(s));
}

auto int_val(int64_t v) -> std::unique_ptr<kuzu::common::Value> {
    return std::make_unique<kuzu::common::Value>(v);
}

auto float_val(double v) -> std::unique_ptr<kuzu::common::Value> {
    return std::make_unique<kuzu::common::Value>(v);
}

auto ts_val(TimePoint tp) -> std::unique_ptr<kuzu::common::Value> {
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                      tp.time_since_epoch())
                      .count();
    return std::make_unique<kuzu::common::Value>(kuzu::common::timestamp_t(micros));
}

auto opt_ts_val(std::optional<TimePoint> tp) -> std::unique_ptr<kuzu::common::Value> {
    if (!tp) {
        return std::make_unique<kuzu::common::Value>(
            kuzu::common::Value::createNullValue(
                kuzu::common::LogicalType(kuzu::common::LogicalTypeID::TIMESTAMP)));
    }
    return ts_val(*tp);
}

auto float_list_val(const std::vector<float>& v) -> std::unique_ptr<kuzu::common::Value> {
    std::vector<std::unique_ptr<kuzu::common::Value>> children;
    children.reserve(v.size());
    for (float f : v)
        children.push_back(std::make_unique<kuzu::common::Value>(f));
    return std::make_unique<kuzu::common::Value>(
        kuzu::common::LogicalType::LIST(
            kuzu::common::LogicalType(kuzu::common::LogicalTypeID::FLOAT)),
        std::move(children));
}

auto opt_float_list_val(const std::optional<std::vector<float>>& v)
    -> std::unique_ptr<kuzu::common::Value> {
    if (!v) {
        return std::make_unique<kuzu::common::Value>(
            kuzu::common::Value::createNullValue(kuzu::common::LogicalType::LIST(
                kuzu::common::LogicalType(kuzu::common::LogicalTypeID::FLOAT))));
    }
    return float_list_val(*v);
}

auto string_list_val(const std::vector<std::string>& v)
    -> std::unique_ptr<kuzu::common::Value> {
    std::vector<std::unique_ptr<kuzu::common::Value>> children;
    children.reserve(v.size());
    for (const auto& s : v)
        children.push_back(std::make_unique<kuzu::common::Value>(s));
    return std::make_unique<kuzu::common::Value>(
        kuzu::common::LogicalType::LIST(
            kuzu::common::LogicalType(kuzu::common::LogicalTypeID::STRING)),
        std::move(children));
}

// ============================================================================
// Value extraction helpers
// ============================================================================

auto get_str(kuzu::common::Value* v) -> std::string {
    if (!v || v->isNull()) return {};
    return v->getValue<std::string>();
}

auto get_ts(kuzu::common::Value* v) -> TimePoint {
    if (!v || v->isNull()) return {};
    auto ts = v->getValue<kuzu::common::timestamp_t>();
    return TimePoint{std::chrono::microseconds{ts.value}};
}

auto get_opt_ts(kuzu::common::Value* v) -> std::optional<TimePoint> {
    if (!v || v->isNull()) return std::nullopt;
    auto ts = v->getValue<kuzu::common::timestamp_t>();
    return TimePoint{std::chrono::microseconds{ts.value}};
}

auto get_string_list(kuzu::common::Value* v) -> std::vector<std::string> {
    if (!v || v->isNull()) return {};
    std::vector<std::string> result;
    auto size = kuzu::common::NestedVal::getChildrenSize(v);
    result.reserve(size);
    for (uint32_t i = 0; i < size; ++i) {
        auto* child = kuzu::common::NestedVal::getChildVal(v, i);
        result.push_back(child->getValue<std::string>());
    }
    return result;
}

auto get_opt_float_list(kuzu::common::Value* v) -> std::optional<std::vector<float>> {
    if (!v || v->isNull()) return std::nullopt;
    std::vector<float> result;
    auto size = kuzu::common::NestedVal::getChildrenSize(v);
    result.reserve(size);
    for (uint32_t i = 0; i < size; ++i) {
        auto* child = kuzu::common::NestedVal::getChildVal(v, i);
        result.push_back(child->getValue<float>());
    }
    return result;
}

auto get_json_attr(kuzu::common::Value* v) -> nlohmann::json {
    auto s = get_str(v);
    if (s.empty()) return nlohmann::json::object();
    auto parsed = nlohmann::json::parse(s, nullptr, false);
    if (parsed.is_discarded()) return nlohmann::json::object();
    return parsed;
}

// ============================================================================
// Row -> struct converters
// ============================================================================

// Columns: uuid(0), name(1), group_id(2), labels(3), created_at(4), summary(5), attributes(6),
//          traits(7), agent_ids(8), source_ids(9), source_contexts(10), participant_ids(11)
auto entity_from_row(kuzu::main::QueryResult* result) -> EntityNode {
    auto tuple = result->getNext();
    return EntityNode{
        .uuid = get_str(tuple->getValue(0)),
        .name = get_str(tuple->getValue(1)),
        .group_id = get_str(tuple->getValue(2)),
        .labels = get_string_list(tuple->getValue(3)),
        .created_at = get_ts(tuple->getValue(4)),
        .name_embedding = std::nullopt,
        .summary = get_str(tuple->getValue(5)),
        .attributes = get_json_attr(tuple->getValue(6)),
        .traits = get_string_list(tuple->getValue(7)),
        .agent_ids = get_string_list(tuple->getValue(8)),
        .source_ids = get_string_list(tuple->getValue(9)),
        .source_contexts = get_string_list(tuple->getValue(10)),
        .participant_ids = get_string_list(tuple->getValue(11)),
    };
}

auto collect_entities(kuzu::main::QueryResult* result) -> std::vector<EntityNode> {
    std::vector<EntityNode> nodes;
    while (result->hasNext()) {
        auto tuple = result->getNext();
        nodes.push_back(EntityNode{
            .uuid = get_str(tuple->getValue(0)),
            .name = get_str(tuple->getValue(1)),
            .group_id = get_str(tuple->getValue(2)),
            .labels = get_string_list(tuple->getValue(3)),
            .created_at = get_ts(tuple->getValue(4)),
            .name_embedding = std::nullopt,
            .summary = get_str(tuple->getValue(5)),
            .attributes = get_json_attr(tuple->getValue(6)),
            .traits = get_string_list(tuple->getValue(7)),
            .agent_ids = get_string_list(tuple->getValue(8)),
            .source_ids = get_string_list(tuple->getValue(9)),
            .source_contexts = get_string_list(tuple->getValue(10)),
            .participant_ids = get_string_list(tuple->getValue(11)),
        });
    }
    return nodes;
}

// Columns: uuid(0), name(1), group_id(2), created_at(3), source(4),
//          source_description(5), content(6), valid_at(7), entity_edges(8),
//          agent_id(9), source_id(10), source_context(11), participant_ids(12)
auto episodic_from_row(kuzu::main::QueryResult* result) -> EpisodicNode {
    auto tuple = result->getNext();
    return EpisodicNode{
        .uuid = get_str(tuple->getValue(0)),
        .name = get_str(tuple->getValue(1)),
        .group_id = get_str(tuple->getValue(2)),
        .created_at = get_ts(tuple->getValue(3)),
        .source = episode_type_from_string(get_str(tuple->getValue(4))),
        .source_description = get_str(tuple->getValue(5)),
        .content = get_str(tuple->getValue(6)),
        .valid_at = get_ts(tuple->getValue(7)),
        .entity_edges = get_string_list(tuple->getValue(8)),
        .agent_id = get_str(tuple->getValue(9)),
        .source_id = get_str(tuple->getValue(10)),
        .source_context = get_str(tuple->getValue(11)),
        .participant_ids = get_string_list(tuple->getValue(12)),
    };
}

auto collect_episodes(kuzu::main::QueryResult* result) -> std::vector<EpisodicNode> {
    std::vector<EpisodicNode> nodes;
    while (result->hasNext()) {
        auto tuple = result->getNext();
        nodes.push_back(EpisodicNode{
            .uuid = get_str(tuple->getValue(0)),
            .name = get_str(tuple->getValue(1)),
            .group_id = get_str(tuple->getValue(2)),
            .created_at = get_ts(tuple->getValue(3)),
            .source = episode_type_from_string(get_str(tuple->getValue(4))),
            .source_description = get_str(tuple->getValue(5)),
            .content = get_str(tuple->getValue(6)),
            .valid_at = get_ts(tuple->getValue(7)),
            .entity_edges = get_string_list(tuple->getValue(8)),
            .agent_id = get_str(tuple->getValue(9)),
            .source_id = get_str(tuple->getValue(10)),
            .source_context = get_str(tuple->getValue(11)),
            .participant_ids = get_string_list(tuple->getValue(12)),
        });
    }
    return nodes;
}

// Columns: uuid(0), source_node_uuid(1), target_node_uuid(2), group_id(3),
//          created_at(4), name(5), fact(6), episodes(7),
//          expired_at(8), valid_at(9), invalid_at(10), attributes(11),
//          agent_ids(12), source_ids(13), source_contexts(14), participant_ids(15)
auto collect_entity_edges(kuzu::main::QueryResult* result) -> std::vector<EntityEdge> {
    std::vector<EntityEdge> edges;
    while (result->hasNext()) {
        auto tuple = result->getNext();
        edges.push_back(EntityEdge{
            .uuid = get_str(tuple->getValue(0)),
            .group_id = get_str(tuple->getValue(3)),
            .source_node_uuid = get_str(tuple->getValue(1)),
            .target_node_uuid = get_str(tuple->getValue(2)),
            .name = get_str(tuple->getValue(5)),
            .fact = get_str(tuple->getValue(6)),
            .fact_embedding = std::nullopt,
            .episodes = get_string_list(tuple->getValue(7)),
            .created_at = get_ts(tuple->getValue(4)),
            .expired_at = get_opt_ts(tuple->getValue(8)),
            .valid_at = get_opt_ts(tuple->getValue(9)),
            .invalid_at = get_opt_ts(tuple->getValue(10)),
            .attributes = get_json_attr(tuple->getValue(11)),
            .agent_ids = get_string_list(tuple->getValue(12)),
            .source_ids = get_string_list(tuple->getValue(13)),
            .source_contexts = get_string_list(tuple->getValue(14)),
            .participant_ids = get_string_list(tuple->getValue(15)),
        });
    }
    return edges;
}

// Entity edge RETURN clause (reused across queries)
constexpr std::string_view ENTITY_EDGE_RETURN = R"(
    e.uuid AS uuid,
    n.uuid AS source_node_uuid,
    m.uuid AS target_node_uuid,
    e.group_id AS group_id,
    e.created_at AS created_at,
    e.name AS name,
    e.fact AS fact,
    e.episodes AS episodes,
    e.expired_at AS expired_at,
    e.valid_at AS valid_at,
    e.invalid_at AS invalid_at,
    e.attributes AS attributes,
    e.agent_ids AS agent_ids,
    e.source_ids AS source_ids,
    e.source_contexts AS source_contexts,
    e.participant_ids AS participant_ids
)";

// Entity node RETURN clause
constexpr std::string_view ENTITY_NODE_RETURN = R"(
    n.uuid AS uuid,
    n.name AS name,
    n.group_id AS group_id,
    n.labels AS labels,
    n.created_at AS created_at,
    n.summary AS summary,
    n.attributes AS attributes,
    n.traits AS traits,
    n.agent_ids AS agent_ids,
    n.source_ids AS source_ids,
    n.source_contexts AS source_contexts,
    n.participant_ids AS participant_ids
)";

// Episodic node RETURN clause
constexpr std::string_view EPISODIC_NODE_RETURN = R"(
    e.uuid AS uuid,
    e.name AS name,
    e.group_id AS group_id,
    e.created_at AS created_at,
    e.source AS source,
    e.source_description AS source_description,
    e.content AS content,
    e.valid_at AS valid_at,
    e.entity_edges AS entity_edges,
    e.agent_id AS agent_id,
    e.source_id AS source_id,
    e.source_context AS source_context,
    e.participant_ids AS participant_ids
)";

// Community node RETURN clause
constexpr std::string_view COMMUNITY_NODE_RETURN = R"(
    c.uuid AS uuid,
    c.name AS name,
    c.group_id AS group_id,
    c.created_at AS created_at,
    c.name_embedding AS name_embedding,
    c.summary AS summary,
    c.agent_ids AS agent_ids,
    c.source_ids AS source_ids,
    c.source_contexts AS source_contexts,
    c.participant_ids AS participant_ids
)";

auto collect_communities(kuzu::main::QueryResult* result) -> std::vector<CommunityNode> {
    std::vector<CommunityNode> nodes;
    while (result->hasNext()) {
        auto tuple = result->getNext();
        nodes.push_back(CommunityNode{
            .uuid = get_str(tuple->getValue(0)),
            .name = get_str(tuple->getValue(1)),
            .group_id = get_str(tuple->getValue(2)),
            .created_at = get_ts(tuple->getValue(3)),
            .name_embedding = get_opt_float_list(tuple->getValue(4)),
            .summary = get_str(tuple->getValue(5)),
            .agent_ids = get_string_list(tuple->getValue(6)),
            .source_ids = get_string_list(tuple->getValue(7)),
            .source_contexts = get_string_list(tuple->getValue(8)),
            .participant_ids = get_string_list(tuple->getValue(9)),
        });
    }
    return nodes;
}

} // anonymous namespace

// ============================================================================
// KuzuGraphStore::Impl
// ============================================================================

struct KuzuGraphStore::Impl {
    std::unique_ptr<kuzu::main::Database> owned_db;  // null if using external db
    kuzu::main::Database* db_ptr = nullptr;           // always valid
    std::unique_ptr<kuzu::main::Connection> conn;

    explicit Impl(std::string_view db_path, bool read_only = false) {
        if (read_only) {
            kuzu::main::SystemConfig cfg;
            cfg.readOnly = true;
            owned_db = std::make_unique<kuzu::main::Database>(db_path, cfg);
        } else {
            owned_db = std::make_unique<kuzu::main::Database>(db_path);
        }
        db_ptr = owned_db.get();
        conn = std::make_unique<kuzu::main::Connection>(db_ptr);
    }

    explicit Impl(kuzu::main::Database& shared_db) {
        db_ptr = &shared_db;
        conn = std::make_unique<kuzu::main::Connection>(db_ptr);
    }

    // Execute a simple query (no parameters)
    auto run(const std::string& cypher) -> VoidResult {
        auto result = conn->query(cypher);
        if (!result->isSuccess()) {
            return std::unexpected(
                GraphitiError{ErrorCode::db_error, result->getErrorMessage()});
        }
        return {};
    }

    // Execute a parameterized query, discard results
    auto run_params(const std::string& cypher, ParamMap params) -> VoidResult {
        auto prepared = conn->prepare(cypher);
        if (!prepared->isSuccess()) {
            return std::unexpected(
                GraphitiError{ErrorCode::db_error, prepared->getErrorMessage()});
        }
        auto result = conn->executeWithParams(prepared.get(), std::move(params));
        if (!result->isSuccess()) {
            return std::unexpected(
                GraphitiError{ErrorCode::db_error, result->getErrorMessage()});
        }
        return {};
    }

    // Execute a parameterized query, return the QueryResult for iteration
    auto query_params(const std::string& cypher, ParamMap params)
        -> Result<std::unique_ptr<kuzu::main::QueryResult>> {
        auto prepared = conn->prepare(cypher);
        if (!prepared->isSuccess()) {
            return std::unexpected(
                GraphitiError{ErrorCode::db_error, prepared->getErrorMessage()});
        }
        auto result = conn->executeWithParams(prepared.get(), std::move(params));
        if (!result->isSuccess()) {
            return std::unexpected(
                GraphitiError{ErrorCode::db_error, result->getErrorMessage()});
        }
        return result;
    }
};

// ============================================================================
// Constructor / Destructor / Move
// ============================================================================

KuzuGraphStore::KuzuGraphStore(std::string_view db_path, bool read_only)
    : impl_(std::make_unique<Impl>(db_path, read_only)) {}

KuzuGraphStore::KuzuGraphStore(kuzu::main::Database& shared_db)
    : impl_(std::make_unique<Impl>(shared_db)) {}

KuzuGraphStore::~KuzuGraphStore() = default;
KuzuGraphStore::KuzuGraphStore(KuzuGraphStore&&) noexcept = default;
KuzuGraphStore& KuzuGraphStore::operator=(KuzuGraphStore&&) noexcept = default;

// ============================================================================
// Infrastructure
// ============================================================================

VoidResult KuzuGraphStore::setup_schema() {
    // Install FTS extension
    for (auto q : kuzu_schema::EXTENSION_QUERIES) {
        auto r = impl_->run(std::string(q));
        // FTS extension install may fail if already loaded or unavailable — continue
    }

    // Create all tables
    for (auto q : kuzu_schema::SCHEMA_QUERIES) {
        auto r = impl_->run(std::string(q));
        if (!r) return r;
    }

    return {};
}

VoidResult KuzuGraphStore::rebuild_indices() {
    // Drop existing indices first (ignore errors if they don't exist)
    (void)impl_->run("CALL DROP_FTS_INDEX('Episodic', 'episode_content')");
    (void)impl_->run("CALL DROP_FTS_INDEX('Entity', 'node_name_and_summary')");
    (void)impl_->run("CALL DROP_FTS_INDEX('Community', 'community_name')");
    (void)impl_->run("CALL DROP_FTS_INDEX('RelatesToNode_', 'edge_name_and_fact')");

    for (auto q : kuzu_schema::FTS_INDEX_QUERIES) {
        auto r = impl_->run(std::string(q));
        if (!r) return r;
    }

    return {};
}

VoidResult KuzuGraphStore::clear_group(const std::vector<std::string>& group_ids) {
    if (group_ids.empty()) {
        // Clear everything
        return impl_->run("MATCH (n) DETACH DELETE n");
    }

    // Delete by group_ids, in order to respect foreign key constraints
    static const std::string tables[] = {
        "RelatesToNode_", "Entity", "Episodic", "Community", "Saga"};

    for (const auto& table : tables) {
        auto cypher = std::format(
            "MATCH (n:{}) WHERE n.group_id IN $group_ids DETACH DELETE n", table);
        ParamMap params;
        params["group_ids"] = string_list_val(group_ids);
        auto r = impl_->run_params(cypher, std::move(params));
        if (!r) return r;
    }

    return {};
}

// ============================================================================
// Entity Persistence
// ============================================================================

VoidResult KuzuGraphStore::persist_entity(const EntityNode& node,
                                            const std::optional<std::vector<float>>& embedding) {
    static const std::string query = std::format(
        R"(MERGE (n:Entity {{uuid: $uuid}})
SET
    n.name = $name,
    n.group_id = $group_id,
    n.labels = $labels,
    n.created_at = $created_at,
    n.name_embedding = $name_embedding,
    n.summary = $summary,
    n.attributes = $attributes,
    n.traits = $traits,
    n.is_system = $is_system,
    n.is_identity = $is_identity,
    n.agent_ids = $agent_ids,
    n.source_ids = $source_ids,
    n.source_contexts = $source_contexts,
    n.participant_ids = $participant_ids
RETURN n.uuid AS uuid)");

    ParamMap params;
    params["uuid"] = str_val(node.uuid);
    params["name"] = str_val(node.name);
    params["group_id"] = str_val(node.group_id);
    params["labels"] = string_list_val(node.labels);
    params["created_at"] = ts_val(node.created_at);
    params["name_embedding"] = opt_float_list_val(node.name_embedding);
    params["summary"] = str_val(node.summary);
    params["attributes"] = str_val(node.attributes.dump());
    params["traits"] = string_list_val(node.traits);
    params["is_system"] = std::make_unique<kuzu::common::Value>(node.is_system);
    params["is_identity"] = std::make_unique<kuzu::common::Value>(node.is_identity);
    params["agent_ids"] = string_list_val(node.agent_ids);
    params["source_ids"] = string_list_val(node.source_ids);
    params["source_contexts"] = string_list_val(node.source_contexts);
    params["participant_ids"] = string_list_val(node.participant_ids);

    auto r = impl_->run_params(query, std::move(params));
    if (!r) return r;

    if (embedding.has_value()) {
        return persist_entity_embedding(node.uuid, embedding.value());
    }
    return {};
}

Result<EntityNode> KuzuGraphStore::get_entity(std::string_view uuid) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity {{uuid: $uuid}})
RETURN {})", ENTITY_NODE_RETURN);

    ParamMap params;
    params["uuid"] = str_val(uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto& qr = *result;
    if (!qr->hasNext()) {
        return std::unexpected(
            GraphitiError{ErrorCode::not_found,
                          std::format("Entity node '{}' not found", uuid)});
    }
    return entity_from_row(qr.get());
}

Result<std::vector<EntityNode>> KuzuGraphStore::get_entities(
    const std::vector<std::string>& uuids) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity)
WHERE n.uuid IN $uuids
RETURN {})", ENTITY_NODE_RETURN);

    ParamMap params;
    params["uuids"] = string_list_val(uuids);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

VoidResult KuzuGraphStore::delete_entity(std::string_view uuid) {
    // First delete related RelatesToNode_ intermediate nodes
    {
        ParamMap params;
        params["uuid"] = str_val(uuid);
        auto r = impl_->run_params(
            R"(MATCH (n:Entity {uuid: $uuid})-[:RELATES_TO]->(r:RelatesToNode_)
DETACH DELETE r)",
            std::move(params));
        if (!r) return r;
    }

    // Then delete the entity node itself
    {
        ParamMap params;
        params["uuid"] = str_val(uuid);
        return impl_->run_params(
            R"(MATCH (n:Entity {uuid: $uuid})
DETACH DELETE n)",
            std::move(params));
    }
}

VoidResult KuzuGraphStore::persist_entity_embedding(
    std::string_view uuid, const std::vector<float>& embedding) {
    static const std::string query =
        R"(MATCH (n:Entity {uuid: $uuid})
SET n.name_embedding = $embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["embedding"] = float_list_val(embedding);

    return impl_->run_params(query, std::move(params));
}

Result<std::optional<std::vector<float>>> KuzuGraphStore::load_entity_embedding(
    std::string_view uuid) {
    static const std::string query =
        R"(MATCH (n:Entity {uuid: $uuid})
RETURN n.name_embedding AS name_embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto& qr = *result;
    if (!qr->hasNext()) return std::optional<std::vector<float>>{std::nullopt};

    auto tuple = qr->getNext();
    return get_opt_float_list(tuple->getValue(0));
}

// ============================================================================
// Edge Persistence
// ============================================================================

VoidResult KuzuGraphStore::persist_edge(const EntityEdge& edge,
                                          const std::optional<std::vector<float>>& embedding) {
    static const std::string query =
        R"(MATCH (source:Entity {uuid: $source_uuid})
MATCH (target:Entity {uuid: $target_uuid})
MERGE (source)-[:RELATES_TO]->(e:RelatesToNode_ {uuid: $uuid})-[:RELATES_TO]->(target)
SET
    e.group_id = $group_id,
    e.created_at = $created_at,
    e.name = $name,
    e.fact = $fact,
    e.fact_embedding = $fact_embedding,
    e.episodes = $episodes,
    e.expired_at = $expired_at,
    e.valid_at = $valid_at,
    e.invalid_at = $invalid_at,
    e.attributes = $attributes,
    e.agent_ids = $agent_ids,
    e.source_ids = $source_ids,
    e.source_contexts = $source_contexts,
    e.participant_ids = $participant_ids
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["source_uuid"] = str_val(edge.source_node_uuid);
    params["target_uuid"] = str_val(edge.target_node_uuid);
    params["uuid"] = str_val(edge.uuid);
    params["group_id"] = str_val(edge.group_id);
    params["created_at"] = ts_val(edge.created_at);
    params["name"] = str_val(edge.name);
    params["fact"] = str_val(edge.fact);
    params["fact_embedding"] = opt_float_list_val(edge.fact_embedding);
    params["episodes"] = string_list_val(edge.episodes);
    params["expired_at"] = opt_ts_val(edge.expired_at);
    params["valid_at"] = opt_ts_val(edge.valid_at);
    params["invalid_at"] = opt_ts_val(edge.invalid_at);
    params["attributes"] = str_val(edge.attributes.dump());
    params["agent_ids"] = string_list_val(edge.agent_ids);
    params["source_ids"] = string_list_val(edge.source_ids);
    params["source_contexts"] = string_list_val(edge.source_contexts);
    params["participant_ids"] = string_list_val(edge.participant_ids);

    auto r = impl_->run_params(query, std::move(params));
    if (!r) return r;

    if (embedding.has_value()) {
        return persist_edge_embedding(edge.uuid, embedding.value());
    }
    return {};
}

Result<EntityEdge> KuzuGraphStore::get_edge(std::string_view uuid) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_ {{uuid: $uuid}})-[:RELATES_TO]->(m:Entity)
RETURN {})", ENTITY_EDGE_RETURN);

    ParamMap params;
    params["uuid"] = str_val(uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto& qr = *result;
    if (!qr->hasNext()) {
        return std::unexpected(
            GraphitiError{ErrorCode::not_found,
                          std::format("Entity edge '{}' not found", uuid)});
    }

    auto edges = collect_entity_edges(qr.get());
    if (edges.empty()) {
        return std::unexpected(
            GraphitiError{ErrorCode::not_found,
                          std::format("Entity edge '{}' not found", uuid)});
    }
    return std::move(edges[0]);
}

Result<std::vector<EntityEdge>> KuzuGraphStore::get_edges(
    const std::vector<std::string>& uuids) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
WHERE e.uuid IN $uuids
RETURN {})", ENTITY_EDGE_RETURN);

    ParamMap params;
    params["uuids"] = string_list_val(uuids);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

VoidResult KuzuGraphStore::delete_edge(std::string_view uuid) {
    ParamMap params;
    params["uuid"] = str_val(uuid);
    return impl_->run_params(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_ {uuid: $uuid})-[:RELATES_TO]->(m:Entity)
DETACH DELETE e)",
        std::move(params));
}

VoidResult KuzuGraphStore::persist_edge_embedding(
    std::string_view uuid, const std::vector<float>& embedding) {
    static const std::string query =
        R"(MATCH (e:RelatesToNode_ {uuid: $uuid})
SET e.fact_embedding = $embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["embedding"] = float_list_val(embedding);

    return impl_->run_params(query, std::move(params));
}

Result<std::optional<std::vector<float>>> KuzuGraphStore::load_edge_embedding(
    std::string_view uuid) {
    static const std::string query =
        R"(MATCH (e:RelatesToNode_ {uuid: $uuid})
RETURN e.fact_embedding AS fact_embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto& qr = *result;
    if (!qr->hasNext()) return std::optional<std::vector<float>>{std::nullopt};

    auto tuple = qr->getNext();
    return get_opt_float_list(tuple->getValue(0));
}

Result<std::vector<EntityEdge>> KuzuGraphStore::get_edges_between(
    std::string_view source_uuid, std::string_view target_uuid) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity {{uuid: $source_uuid}})-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity {{uuid: $target_uuid}})
RETURN {})", ENTITY_EDGE_RETURN);

    ParamMap params;
    params["source_uuid"] = str_val(source_uuid);
    params["target_uuid"] = str_val(target_uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

// ============================================================================
// Episode Management
// ============================================================================

VoidResult KuzuGraphStore::persist_episode(const EpisodicNode& node) {
    static const std::string query =
        R"(MERGE (n:Episodic {uuid: $uuid})
SET
    n.name = $name,
    n.group_id = $group_id,
    n.created_at = $created_at,
    n.source = $source,
    n.source_description = $source_description,
    n.content = $content,
    n.valid_at = $valid_at,
    n.entity_edges = $entity_edges,
    n.agent_id = $agent_id,
    n.source_id = $source_id,
    n.source_context = $source_context,
    n.participant_ids = $participant_ids
RETURN n.uuid AS uuid)";

    ParamMap params;
    params["uuid"] = str_val(node.uuid);
    params["name"] = str_val(node.name);
    params["group_id"] = str_val(node.group_id);
    params["created_at"] = ts_val(node.created_at);
    params["source"] = str_val(to_string(node.source));
    params["source_description"] = str_val(node.source_description);
    params["content"] = str_val(node.content);
    params["valid_at"] = ts_val(node.valid_at);
    params["entity_edges"] = string_list_val(node.entity_edges);
    params["agent_id"] = str_val(node.agent_id);
    params["source_id"] = str_val(node.source_id);
    params["source_context"] = str_val(node.source_context);
    params["participant_ids"] = string_list_val(node.participant_ids);

    return impl_->run_params(query, std::move(params));
}

Result<EpisodicNode> KuzuGraphStore::get_episode(std::string_view uuid) {
    static const std::string query = std::format(
        R"(MATCH (e:Episodic {{uuid: $uuid}})
RETURN {})", EPISODIC_NODE_RETURN);

    ParamMap params;
    params["uuid"] = str_val(uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto& qr = *result;
    if (!qr->hasNext()) {
        return std::unexpected(
            GraphitiError{ErrorCode::not_found,
                          std::format("Episodic node '{}' not found", uuid)});
    }
    return episodic_from_row(qr.get());
}

VoidResult KuzuGraphStore::delete_episode(std::string_view uuid) {
    static const std::string query =
        R"(MATCH (e:Episodic {uuid: $uuid})
DETACH DELETE e)";

    ParamMap params;
    params["uuid"] = str_val(uuid);

    return impl_->run_params(query, std::move(params));
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::retrieve_episodes(
    std::string_view group_id, TimePoint reference_time, int last_n,
    std::optional<EpisodeType> source) {
    // Build query dynamically based on optional source filter
    std::string query = std::format(
        R"(MATCH (e:Episodic)
WHERE e.valid_at <= $reference_time
AND e.group_id = $group_id{}
RETURN {}
ORDER BY e.valid_at DESC
LIMIT $num_episodes)",
        source ? "\nAND e.source = $source" : "",
        EPISODIC_NODE_RETURN);

    ParamMap params;
    params["reference_time"] = ts_val(reference_time);
    params["group_id"] = str_val(group_id);
    params["num_episodes"] = int_val(last_n);
    if (source) {
        params["source"] = str_val(to_string(*source));
    }

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_episodes(result->get());
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::retrieve_episodes_by_saga(
    std::string_view saga_name, std::string_view group_id,
    TimePoint reference_time, int last_n) {
    auto cypher = std::format(
        R"(MATCH (s:Saga {{name: $saga_name, group_id: $group_id}})-[:HAS_EPISODE]->(e:Episodic)
WHERE e.valid_at <= $ref_time
RETURN {}
ORDER BY e.valid_at DESC
LIMIT $limit)", EPISODIC_NODE_RETURN);

    ParamMap params;
    params["saga_name"] = str_val(saga_name);
    params["group_id"] = str_val(group_id);
    params["ref_time"] = ts_val(reference_time);
    params["limit"] = int_val(last_n);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_episodes(result->get());
}

VoidResult KuzuGraphStore::persist_mention(const EpisodicEdge& edge) {
    static const std::string query =
        R"(MATCH (episode:Episodic {uuid: $episode_uuid})
MATCH (node:Entity {uuid: $entity_uuid})
MERGE (episode)-[e:MENTIONS {uuid: $uuid}]->(node)
SET
    e.group_id = $group_id,
    e.created_at = $created_at,
    e.agent_id = $agent_id,
    e.source_id = $source_id,
    e.source_context = $source_context,
    e.participant_ids = $participant_ids
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["episode_uuid"] = str_val(edge.source_node_uuid);
    params["entity_uuid"] = str_val(edge.target_node_uuid);
    params["uuid"] = str_val(edge.uuid);
    params["group_id"] = str_val(edge.group_id);
    params["created_at"] = ts_val(edge.created_at);
    params["agent_id"] = str_val(edge.agent_id);
    params["source_id"] = str_val(edge.source_id);
    params["source_context"] = str_val(edge.source_context);
    params["participant_ids"] = string_list_val(edge.participant_ids);

    return impl_->run_params(query, std::move(params));
}

Result<std::vector<std::string>> KuzuGraphStore::get_edge_uuids_by_episode(std::string_view episode_uuid) {
    static const std::string query =
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
WHERE list_contains(e.episodes, $episode_uuid)
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["episode_uuid"] = str_val(episode_uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<std::string> uuids;
    auto& qr = *result;
    while (qr->hasNext()) {
        auto row = qr->getNext();
        uuids.push_back(row->getValue(0)->getValue<std::string>());
    }
    return uuids;
}

Result<std::vector<std::string>> KuzuGraphStore::get_mentioned_entity_uuids(std::string_view episode_uuid) {
    static const std::string query =
        R"(MATCH (ep:Episodic {uuid: $episode_uuid})-[:MENTIONS]->(n:Entity)
RETURN n.uuid AS uuid)";

    ParamMap params;
    params["episode_uuid"] = str_val(episode_uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<std::string> uuids;
    auto& qr = *result;
    while (qr->hasNext()) {
        auto row = qr->getNext();
        uuids.push_back(row->getValue(0)->getValue<std::string>());
    }
    return uuids;
}

// ============================================================================
// Saga Management
// ============================================================================

Result<std::optional<SagaNode>> KuzuGraphStore::find_saga(
    std::string_view name, std::string_view group_id) {
    static const std::string query =
        R"(MATCH (s:Saga {name: $name, group_id: $group_id})
RETURN s.uuid AS uuid, s.name AS name, s.group_id AS group_id, s.created_at AS created_at)";

    ParamMap params;
    params["name"] = str_val(name);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto* qr = result->get();
    if (!qr->hasNext()) {
        return std::optional<SagaNode>(std::nullopt);
    }

    auto tuple = qr->getNext();
    return std::optional<SagaNode>(SagaNode{
        .uuid = get_str(tuple->getValue(0)),
        .name = get_str(tuple->getValue(1)),
        .group_id = get_str(tuple->getValue(2)),
        .created_at = get_ts(tuple->getValue(3)),
    });
}

VoidResult KuzuGraphStore::persist_saga(const SagaNode& node) {
    static const std::string query =
        R"(MERGE (n:Saga {uuid: $uuid})
SET
    n.name = $name,
    n.group_id = $group_id,
    n.created_at = $created_at
RETURN n.uuid AS uuid)";

    ParamMap params;
    params["uuid"] = str_val(node.uuid);
    params["name"] = str_val(node.name);
    params["group_id"] = str_val(node.group_id);
    params["created_at"] = ts_val(node.created_at);

    return impl_->run_params(query, std::move(params));
}

Result<std::optional<std::string>> KuzuGraphStore::get_last_saga_episode(
    std::string_view saga_uuid, std::string_view exclude_episode_uuid) {
    std::string cypher;
    ParamMap params;
    params["saga_uuid"] = str_val(saga_uuid);

    if (exclude_episode_uuid.empty()) {
        cypher = R"(MATCH (s:Saga {uuid: $saga_uuid})-[:HAS_EPISODE]->(e:Episodic)
RETURN e.uuid AS uuid
ORDER BY e.valid_at DESC, e.created_at DESC
LIMIT 1)";
    } else {
        cypher = R"(MATCH (s:Saga {uuid: $saga_uuid})-[:HAS_EPISODE]->(e:Episodic)
WHERE e.uuid <> $exclude_uuid
RETURN e.uuid AS uuid
ORDER BY e.valid_at DESC, e.created_at DESC
LIMIT 1)";
        params["exclude_uuid"] = str_val(exclude_episode_uuid);
    }

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto* qr = result->get();
    if (!qr->hasNext()) return std::optional<std::string>(std::nullopt);

    auto tuple = qr->getNext();
    return std::optional<std::string>(get_str(tuple->getValue(0)));
}

VoidResult KuzuGraphStore::link_saga_episode(
    std::string_view uuid, std::string_view saga_uuid,
    std::string_view episode_uuid, std::string_view group_id,
    TimePoint created_at) {
    static const std::string query =
        R"(MATCH (saga:Saga {uuid: $saga_uuid})
MATCH (episode:Episodic {uuid: $episode_uuid})
MERGE (saga)-[e:HAS_EPISODE {uuid: $uuid}]->(episode)
SET
    e.group_id = $group_id,
    e.created_at = $created_at
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["saga_uuid"] = str_val(saga_uuid);
    params["episode_uuid"] = str_val(episode_uuid);
    params["group_id"] = str_val(group_id);
    params["created_at"] = ts_val(created_at);

    return impl_->run_params(query, std::move(params));
}

VoidResult KuzuGraphStore::link_episode_sequence(
    std::string_view uuid, std::string_view prev_episode_uuid,
    std::string_view next_episode_uuid, std::string_view group_id,
    TimePoint created_at) {
    static const std::string query =
        R"(MATCH (source:Episodic {uuid: $source_uuid})
MATCH (target:Episodic {uuid: $target_uuid})
MERGE (source)-[e:NEXT_EPISODE {uuid: $uuid}]->(target)
SET
    e.group_id = $group_id,
    e.created_at = $created_at
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["source_uuid"] = str_val(prev_episode_uuid);
    params["target_uuid"] = str_val(next_episode_uuid);
    params["group_id"] = str_val(group_id);
    params["created_at"] = ts_val(created_at);

    return impl_->run_params(query, std::move(params));
}

// ============================================================================
// Community Management
// ============================================================================

VoidResult KuzuGraphStore::persist_community(const CommunityNode& node,
                                               const std::optional<std::vector<float>>& embedding) {
    static const std::string query =
        R"(MERGE (n:Community {uuid: $uuid})
SET
    n.name = $name,
    n.group_id = $group_id,
    n.created_at = $created_at,
    n.name_embedding = $name_embedding,
    n.summary = $summary,
    n.agent_ids = $agent_ids,
    n.source_ids = $source_ids,
    n.source_contexts = $source_contexts,
    n.participant_ids = $participant_ids
RETURN n.uuid AS uuid)";

    ParamMap params;
    params["uuid"] = str_val(node.uuid);
    params["name"] = str_val(node.name);
    params["group_id"] = str_val(node.group_id);
    params["created_at"] = ts_val(node.created_at);
    params["name_embedding"] = opt_float_list_val(node.name_embedding);
    params["summary"] = str_val(node.summary);
    params["agent_ids"] = string_list_val(node.agent_ids);
    params["source_ids"] = string_list_val(node.source_ids);
    params["source_contexts"] = string_list_val(node.source_contexts);
    params["participant_ids"] = string_list_val(node.participant_ids);

    auto r = impl_->run_params(query, std::move(params));
    if (!r) return r;

    if (embedding.has_value()) {
        static const std::string emb_query =
            R"(MATCH (n:Community {uuid: $uuid})
SET n.name_embedding = $embedding)";

        ParamMap emb_params;
        emb_params["uuid"] = str_val(node.uuid);
        emb_params["embedding"] = float_list_val(embedding.value());

        return impl_->run_params(emb_query, std::move(emb_params));
    }
    return {};
}

VoidResult KuzuGraphStore::persist_community_membership(const CommunityEdge& edge) {
    // Try Entity target first, then Community target (UNION pattern)
    static const std::string query =
        R"(MATCH (community:Community {uuid: $community_uuid})
MATCH (node:Entity {uuid: $entity_uuid})
MERGE (community)-[e:HAS_MEMBER {uuid: $uuid}]->(node)
SET
    e.group_id = $group_id,
    e.created_at = $created_at
RETURN e.uuid AS uuid
UNION
MATCH (community:Community {uuid: $community_uuid})
MATCH (node:Community {uuid: $entity_uuid})
MERGE (community)-[e:HAS_MEMBER {uuid: $uuid}]->(node)
SET
    e.group_id = $group_id,
    e.created_at = $created_at
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["community_uuid"] = str_val(edge.source_node_uuid);
    params["entity_uuid"] = str_val(edge.target_node_uuid);
    params["uuid"] = str_val(edge.uuid);
    params["group_id"] = str_val(edge.group_id);
    params["created_at"] = ts_val(edge.created_at);

    return impl_->run_params(query, std::move(params));
}

VoidResult KuzuGraphStore::remove_all_communities() {
    return impl_->run("MATCH (c:Community) DETACH DELETE c");
}

Result<std::optional<CommunityNode>> KuzuGraphStore::get_entity_community(
    std::string_view entity_uuid) {
    auto cypher = std::format(
        R"(MATCH (c:Community)-[:HAS_MEMBER]->(n:Entity {{uuid: $entity_uuid}})
RETURN {})", COMMUNITY_NODE_RETURN);

    ParamMap params;
    params["entity_uuid"] = str_val(entity_uuid);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto* qr = result->get();
    if (!qr->hasNext()) return std::optional<CommunityNode>(std::nullopt);

    auto nodes = collect_communities(qr);
    return std::optional<CommunityNode>(std::move(nodes[0]));
}

Result<std::vector<CommunityNode>> KuzuGraphStore::get_neighbor_communities(
    std::string_view entity_uuid) {
    auto cypher = std::format(
        R"(MATCH (c:Community)-[:HAS_MEMBER]->(m:Entity)-[:RELATES_TO]-(e:RelatesToNode_)-[:RELATES_TO]-(n:Entity {{uuid: $entity_uuid}})
RETURN {})", COMMUNITY_NODE_RETURN);

    ParamMap params;
    params["entity_uuid"] = str_val(entity_uuid);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_communities(result->get());
}

Result<std::vector<EntityNode>> KuzuGraphStore::get_entities_by_group(
    std::string_view group_id) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity {{group_id: $group_id}})
RETURN {})", ENTITY_NODE_RETURN);

    ParamMap params;
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

Result<std::vector<GraphStore::Neighbor>> KuzuGraphStore::get_entity_neighbors(
    std::string_view uuid, std::string_view group_id) {
    static const std::string cypher =
        R"(MATCH (n:Entity {group_id: $group_id, uuid: $uuid})-[:RELATES_TO]-(e:RelatesToNode_)-[:RELATES_TO]-(m:Entity {group_id: $group_id})
WITH count(e) AS cnt, m.uuid AS uuid
RETURN uuid, cnt)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<Neighbor> neighbors;
    auto* qr = result->get();
    while (qr->hasNext()) {
        auto tuple = qr->getNext();
        neighbors.push_back({
            .node_uuid = get_str(tuple->getValue(0)),
            .edge_count = tuple->getValue(1)->getValue<int64_t>(),
        });
    }
    return neighbors;
}

Result<std::vector<std::string>> KuzuGraphStore::get_all_group_ids() {
    static const std::string query =
        R"(MATCH (n:Entity)
WHERE n.group_id IS NOT NULL
RETURN DISTINCT n.group_id AS group_id)";

    ParamMap params;
    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<std::string> group_ids;
    auto* qr = result->get();
    while (qr->hasNext()) {
        auto tuple = qr->getNext();
        group_ids.push_back(get_str(tuple->getValue(0)));
    }
    return group_ids;
}

// ============================================================================
// Search: Text (BM25)
// ============================================================================

Result<std::vector<EntityNode>> KuzuGraphStore::search_entities_bm25(
    std::string_view query, std::string_view group_id, int limit,
    const SearchFilters* filters) {
    std::string extra_where;
    if (filters) {
        auto fc = build_node_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('Entity', 'node_name_and_summary', $query, TOP := {})
WITH node AS n, score
WHERE n.group_id = $group_id{}
RETURN {}
ORDER BY score DESC
LIMIT {})", limit, extra_where, ENTITY_NODE_RETURN, limit);

    // Sanitize FTS query — strip special characters that break FTS syntax
    std::string sanitized_query(query);
    for (auto& c : sanitized_query) {
        if (c == ':' || c == '"' || c == '\'' || c == '*' || c == '(' || c == ')' || c == '~' || c == '^' || c == '\\')
            c = ' ';
    }

    ParamMap params;
    params["query"] = str_val(sanitized_query);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_bm25(
    std::string_view query, std::string_view group_id, int limit,
    const SearchFilters* filters) {
    std::string extra_where;
    if (filters) {
        auto fc = build_edge_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('RelatesToNode_', 'edge_name_and_fact', $query, TOP := {})
WITH node AS e, score
MATCH (n:Entity)-[:RELATES_TO]->(e)-[:RELATES_TO]->(m:Entity)
WHERE e.group_id = $group_id{}
RETURN {}
ORDER BY score DESC
LIMIT {})", limit, extra_where, ENTITY_EDGE_RETURN, limit);

    // Sanitize FTS query — strip special characters that break FTS syntax
    std::string sanitized_query(query);
    for (auto& c : sanitized_query) {
        if (c == ':' || c == '"' || c == '\'' || c == '*' || c == '(' || c == ')' || c == '~' || c == '^' || c == '\\')
            c = ' ';
    }

    ParamMap params;
    params["query"] = str_val(sanitized_query);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

Result<std::vector<EpisodicNode>> KuzuGraphStore::search_episodes_bm25(
    std::string_view query, std::string_view group_id, int limit) {

    std::string group_filter;
    if (!group_id.empty()) {
        group_filter = "\nAND e.group_id = $group_id";
    }

    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('Episodic', 'episode_content', $query, TOP := {})
WITH node AS e, score
WHERE true{}
RETURN {}
ORDER BY score DESC
LIMIT {})", limit, group_filter, EPISODIC_NODE_RETURN, limit);

    ParamMap params;
    params["query"] = str_val(query);
    if (!group_id.empty()) params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_episodes(result->get());
}

Result<std::vector<CommunityNode>> KuzuGraphStore::search_communities_bm25(
    std::string_view query, std::string_view group_id, int limit) {
    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('Community', 'community_name', $query) YIELD node, score
WITH node AS c, score
WHERE c.group_id = $group_id
RETURN {}
ORDER BY score DESC
LIMIT $limit)", COMMUNITY_NODE_RETURN);

    ParamMap params;
    params["query"] = str_val(query);
    params["group_id"] = str_val(group_id);
    params["limit"] = int_val(limit);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_communities(result->get());
}

// ============================================================================
// Search: Semantic (cosine)
// ============================================================================

Result<std::vector<EntityNode>> KuzuGraphStore::search_entities_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit, const SearchFilters* filters) {
    std::string extra_where;
    if (filters) {
        auto fc = build_node_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    auto dim = embedding.size();
    auto cypher = std::format(
        R"(MATCH (n:Entity)
WHERE n.group_id = $group_id{}
WITH n, array_cosine_similarity(n.name_embedding, CAST($search_vector AS FLOAT[{}])) AS score
WHERE score > $min_score
RETURN {}
ORDER BY score DESC
LIMIT {})", extra_where, dim, ENTITY_NODE_RETURN, limit);

    ParamMap params;
    params["group_id"] = str_val(group_id);
    params["search_vector"] = float_list_val(embedding);
    params["min_score"] = float_val(static_cast<double>(min_score));

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit, const SearchFilters* filters) {
    std::string extra_where;
    if (filters) {
        auto fc = build_edge_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    auto dim = embedding.size();
    auto cypher = std::format(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
WHERE e.group_id = $group_id{}
WITH DISTINCT e, n, m, array_cosine_similarity(e.fact_embedding, CAST($search_vector AS FLOAT[{}])) AS score
WHERE score > $min_score
RETURN {}
ORDER BY score DESC
LIMIT {})", extra_where, dim, ENTITY_EDGE_RETURN, limit);

    ParamMap params;
    params["group_id"] = str_val(group_id);
    params["search_vector"] = float_list_val(embedding);
    params["min_score"] = float_val(static_cast<double>(min_score));

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

Result<std::vector<CommunityNode>> KuzuGraphStore::search_communities_cosine(
    const std::vector<float>& embedding, std::string_view group_id,
    float min_score, int limit) {
    // Kuzu array_cosine_similarity with CAST
    auto cypher = std::format(
        R"(MATCH (c:Community)
WHERE c.group_id = $group_id AND c.name_embedding IS NOT NULL
WITH c, array_cosine_similarity(c.name_embedding, CAST($embedding AS FLOAT[{}])) AS score
WHERE score > $min_score
RETURN {}
ORDER BY score DESC
LIMIT $limit)", embedding.size(), COMMUNITY_NODE_RETURN);

    ParamMap params;
    params["embedding"] = float_list_val(embedding);
    params["group_id"] = str_val(group_id);
    params["min_score"] = float_val(static_cast<double>(min_score));
    params["limit"] = int_val(limit);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_communities(result->get());
}

// ============================================================================
// Search: BFS
// ============================================================================

Result<std::vector<EntityEdge>> KuzuGraphStore::search_edges_bfs(
    const std::vector<std::string>& origins, std::string_view group_id,
    int max_depth, int limit, const SearchFilters* filters) {

    if (origins.empty()) return std::vector<EntityEdge>{};

    std::string extra_where;
    if (filters) {
        auto fc = build_edge_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    std::string group_filter;
    if (!group_id.empty()) {
        group_filter = "\nAND e.group_id = $group_id";
    }

    // Collect edges from all origins, deduplicate by UUID
    std::unordered_map<std::string, EntityEdge> seen;
    int doubled_depth = max_depth * 2;

    for (auto& origin_uuid : origins) {
        if (static_cast<int>(seen.size()) >= limit) break;

        // Query 1: Entity origin -> traverse to RelatesToNode_ edges
        {
            auto cypher = std::format(
                R"(MATCH (origin:Entity {{uuid: $origin_uuid}})-[:RELATES_TO*2..{}]->(e:RelatesToNode_)
MATCH (n:Entity)-[:RELATES_TO]->(e)-[:RELATES_TO]->(m:Entity)
WHERE true{}{}
RETURN DISTINCT {}
LIMIT {})", doubled_depth, group_filter, extra_where, ENTITY_EDGE_RETURN, limit);

            ParamMap params;
            params["origin_uuid"] = str_val(origin_uuid);
            if (!group_id.empty()) params["group_id"] = str_val(group_id);

            auto result = impl_->query_params(cypher, std::move(params));
            if (result.has_value()) {
                for (auto& edge : collect_entity_edges(result->get())) {
                    if (!seen.contains(edge.uuid)) {
                        seen.emplace(edge.uuid, std::move(edge));
                    }
                }
            }
        }

        // Query 2: Episodic origin -> MENTIONS -> Entity -> edges
        {
            auto cypher = std::format(
                R"(MATCH (origin:Episodic {{uuid: $origin_uuid}})-[:MENTIONS]->(start:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
MATCH (n:Entity)-[:RELATES_TO]->(e)
WHERE true{}{}
RETURN DISTINCT {}
LIMIT {})", group_filter, extra_where, ENTITY_EDGE_RETURN, limit);

            ParamMap params;
            params["origin_uuid"] = str_val(origin_uuid);
            if (!group_id.empty()) params["group_id"] = str_val(group_id);

            auto result = impl_->query_params(cypher, std::move(params));
            if (result.has_value()) {
                for (auto& edge : collect_entity_edges(result->get())) {
                    if (!seen.contains(edge.uuid)) {
                        seen.emplace(edge.uuid, std::move(edge));
                    }
                }
            }
        }
    }

    // Collect results up to limit
    std::vector<EntityEdge> results;
    results.reserve(std::min(static_cast<int>(seen.size()), limit));
    for (auto& [uuid, edge] : seen) {
        results.push_back(std::move(edge));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}

Result<std::vector<EntityNode>> KuzuGraphStore::search_nodes_bfs(
    const std::vector<std::string>& origins, std::string_view group_id,
    int max_depth, int limit, const SearchFilters* filters) {

    if (origins.empty()) return std::vector<EntityNode>{};

    std::string extra_where;
    if (filters) {
        auto fc = build_node_filter_clauses(*filters);
        auto joined = join_filter_clauses(fc.clauses);
        if (!joined.empty()) extra_where = "\nAND " + joined;
    }

    std::string group_filter = "\nWHERE n.group_id = origin.group_id";

    // Collect nodes from all origins, deduplicate by UUID
    std::unordered_map<std::string, EntityNode> seen;
    int doubled_depth = max_depth * 2;

    for (auto& origin_uuid : origins) {
        if (static_cast<int>(seen.size()) >= limit) break;

        // Query 1: Episodic -> MENTIONS -> Entity
        {
            auto cypher = std::format(
                R"(MATCH (origin:Episodic {{uuid: $origin_uuid}})-[:MENTIONS]->(n:Entity){}{}
RETURN {}
LIMIT {})", group_filter, extra_where, ENTITY_NODE_RETURN, limit);

            ParamMap params;
            params["origin_uuid"] = str_val(origin_uuid);

            auto result = impl_->query_params(cypher, std::move(params));
            if (result.has_value()) {
                for (auto& node : collect_entities(result->get())) {
                    if (!seen.contains(node.uuid)) {
                        seen.emplace(node.uuid, std::move(node));
                    }
                }
            }
        }

        // Query 2: Entity -> RELATES_TO*(2..depth*2) -> Entity
        {
            auto cypher = std::format(
                R"(MATCH (origin:Entity {{uuid: $origin_uuid}})-[:RELATES_TO*2..{}]->(n:Entity){}{}
RETURN {}
LIMIT {})", doubled_depth, group_filter, extra_where, ENTITY_NODE_RETURN, limit);

            ParamMap params;
            params["origin_uuid"] = str_val(origin_uuid);

            auto result = impl_->query_params(cypher, std::move(params));
            if (result.has_value()) {
                for (auto& node : collect_entities(result->get())) {
                    if (!seen.contains(node.uuid)) {
                        seen.emplace(node.uuid, std::move(node));
                    }
                }
            }
        }

        // Query 3: Episodic -> MENTIONS -> Entity -> RELATES_TO -> Entity (if depth > 1)
        if (max_depth > 1) {
            int combined_depth = (max_depth - 1) * 2;
            auto cypher = std::format(
                R"(MATCH (origin:Episodic {{uuid: $origin_uuid}})-[:MENTIONS]->(:Entity)-[:RELATES_TO*2..{}]->(n:Entity){}{}
RETURN {}
LIMIT {})", combined_depth, group_filter, extra_where, ENTITY_NODE_RETURN, limit);

            ParamMap params;
            params["origin_uuid"] = str_val(origin_uuid);

            auto result = impl_->query_params(cypher, std::move(params));
            if (result.has_value()) {
                for (auto& node : collect_entities(result->get())) {
                    if (!seen.contains(node.uuid)) {
                        seen.emplace(node.uuid, std::move(node));
                    }
                }
            }
        }
    }

    // Collect results up to limit
    std::vector<EntityNode> results;
    results.reserve(std::min(static_cast<int>(seen.size()), limit));
    for (auto& [uuid, node] : seen) {
        results.push_back(std::move(node));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}

// ============================================================================
// Overview & Analytics
// ============================================================================

Result<std::vector<GraphStore::NodeSummary>> KuzuGraphStore::get_node_summaries(std::string_view group_id) {
    static const std::string query =
        R"(MATCH (n:Entity {group_id: $group_id})
RETURN n.uuid AS uuid, n.name AS name, n.labels AS labels,
       n.agent_ids AS agent_ids, n.source_ids AS source_ids,
       n.source_contexts AS source_contexts, n.participant_ids AS participant_ids)";

    ParamMap params;
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<NodeSummary> nodes;
    auto* qr = result->get();
    while (qr->hasNext()) {
        auto tuple = qr->getNext();
        nodes.push_back({
            .uuid             = get_str(tuple->getValue(0)),
            .name             = get_str(tuple->getValue(1)),
            .labels           = get_string_list(tuple->getValue(2)),
            .agent_ids        = get_string_list(tuple->getValue(3)),
            .source_ids       = get_string_list(tuple->getValue(4)),
            .source_contexts  = get_string_list(tuple->getValue(5)),
            .participant_ids  = get_string_list(tuple->getValue(6)),
        });
    }
    return nodes;
}

Result<std::vector<GraphStore::EdgeSummary>> KuzuGraphStore::get_edge_summaries(
    const std::set<std::string>& node_uuids, std::string_view group_id) {
    // Edges use intermediate RelatesToNode_ pattern:
    //   Entity -[:RELATES_TO]-> RelatesToNode_ -[:RELATES_TO]-> Entity
    static const std::string query =
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_ {group_id: $group_id})-[:RELATES_TO]->(m:Entity)
RETURN e.uuid AS uuid, e.name AS name, n.uuid AS src, m.uuid AS tgt,
       e.agent_ids AS agent_ids, e.source_ids AS source_ids,
       e.source_contexts AS source_contexts, e.participant_ids AS participant_ids)";

    ParamMap params;
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    std::vector<EdgeSummary> edges;
    auto* qr = result->get();
    while (qr->hasNext()) {
        auto tuple = qr->getNext();
        auto src = get_str(tuple->getValue(2));
        auto tgt = get_str(tuple->getValue(3));
        if (node_uuids.count(src) && node_uuids.count(tgt)) {
            edges.push_back({
                .uuid             = get_str(tuple->getValue(0)),
                .name             = get_str(tuple->getValue(1)),
                .source_node_uuid = std::move(src),
                .target_node_uuid = std::move(tgt),
                .agent_ids        = get_string_list(tuple->getValue(4)),
                .source_ids       = get_string_list(tuple->getValue(5)),
                .source_contexts  = get_string_list(tuple->getValue(6)),
                .participant_ids  = get_string_list(tuple->getValue(7)),
            });
        }
    }
    return edges;
}

Result<int64_t> KuzuGraphStore::count_episode_mentions(std::string_view uuid) {
    auto cypher = R"(MATCH (episode:Episodic)-[r:MENTIONS]->(n:Entity {uuid: $node_uuid})
RETURN count(*) AS cnt)";

    ParamMap params;
    params["node_uuid"] = str_val(uuid);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    auto* qr = result->get();
    if (qr->hasNext()) {
        auto row = qr->getNext();
        return row->getValue(0)->getValue<int64_t>();
    }
    return int64_t{0};
}

Result<bool> KuzuGraphStore::check_node_adjacency(std::string_view center_uuid, std::string_view candidate_uuid) {
    auto cypher = R"(MATCH (center:Entity {uuid: $center_uuid})-[:RELATES_TO]->(:RelatesToNode_)-[:RELATES_TO]-(n:Entity {uuid: $node_uuid})
RETURN 1 AS score LIMIT 1)";

    ParamMap params;
    params["center_uuid"] = str_val(center_uuid);
    params["node_uuid"] = str_val(candidate_uuid);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return result->get()->hasNext();
}

// ============================================================================
// Kuzu-specific: raw access
// ============================================================================

kuzu::main::Database* KuzuGraphStore::database() const { return impl_->db_ptr; }
kuzu::main::Connection* KuzuGraphStore::connection() const { return impl_->conn.get(); }

} // namespace graphiti
