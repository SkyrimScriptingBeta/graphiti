#include "kuzu_driver.h"
#include "kuzu_schema.h"
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

// Columns: uuid(0), name(1), group_id(2), labels(3), created_at(4), summary(5), attributes(6)
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
        });
    }
    return nodes;
}

// Columns: uuid(0), name(1), group_id(2), created_at(3), source(4),
//          source_description(5), content(6), valid_at(7), entity_edges(8)
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
        });
    }
    return nodes;
}

// Columns: uuid(0), source_node_uuid(1), target_node_uuid(2), group_id(3),
//          created_at(4), name(5), fact(6), episodes(7),
//          expired_at(8), valid_at(9), invalid_at(10), attributes(11)
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
    e.attributes AS attributes
)";

// Entity node RETURN clause
constexpr std::string_view ENTITY_NODE_RETURN = R"(
    n.uuid AS uuid,
    n.name AS name,
    n.group_id AS group_id,
    n.labels AS labels,
    n.created_at AS created_at,
    n.summary AS summary,
    n.attributes AS attributes
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
    e.entity_edges AS entity_edges
)";

} // anonymous namespace

// ============================================================================
// KuzuDriver::Impl
// ============================================================================

struct KuzuDriver::Impl {
    std::unique_ptr<kuzu::main::Database> db;
    std::unique_ptr<kuzu::main::Connection> conn;

    explicit Impl(std::string_view db_path) {
        db = std::make_unique<kuzu::main::Database>(db_path);
        conn = std::make_unique<kuzu::main::Connection>(db.get());
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

KuzuDriver::KuzuDriver(std::string_view db_path)
    : impl_(std::make_unique<Impl>(db_path)) {}

KuzuDriver::~KuzuDriver() = default;

KuzuDriver::KuzuDriver(KuzuDriver&&) noexcept = default;
KuzuDriver& KuzuDriver::operator=(KuzuDriver&&) noexcept = default;

// ============================================================================
// Schema Setup
// ============================================================================

VoidResult KuzuDriver::setup_schema() {
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

VoidResult KuzuDriver::build_fts_indices() {
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

// ============================================================================
// Entity Node Operations
// ============================================================================

VoidResult KuzuDriver::save_entity_node(const EntityNode& node) {
    static const std::string query = std::format(
        R"(MERGE (n:Entity {{uuid: $uuid}})
SET
    n.name = $name,
    n.group_id = $group_id,
    n.labels = $labels,
    n.created_at = $created_at,
    n.name_embedding = $name_embedding,
    n.summary = $summary,
    n.attributes = $attributes
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

    return impl_->run_params(query, std::move(params));
}

Result<EntityNode> KuzuDriver::get_entity_node(std::string_view uuid) {
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

Result<std::vector<EntityNode>> KuzuDriver::get_entity_nodes(
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

VoidResult KuzuDriver::delete_entity_node(std::string_view uuid) {
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

// ============================================================================
// Episodic Node Operations
// ============================================================================

VoidResult KuzuDriver::save_episodic_node(const EpisodicNode& node) {
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
    n.entity_edges = $entity_edges
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

    return impl_->run_params(query, std::move(params));
}

Result<EpisodicNode> KuzuDriver::get_episodic_node(std::string_view uuid) {
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

Result<std::vector<EpisodicNode>> KuzuDriver::retrieve_episodes(
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

// ============================================================================
// Entity Edge Operations (RelatesToNode_ intermediate pattern)
// ============================================================================

VoidResult KuzuDriver::save_entity_edge(const EntityEdge& edge) {
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
    e.attributes = $attributes
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

    return impl_->run_params(query, std::move(params));
}

Result<EntityEdge> KuzuDriver::get_entity_edge(std::string_view uuid) {
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

Result<std::vector<EntityEdge>> KuzuDriver::get_entity_edges(
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

Result<std::vector<EntityEdge>> KuzuDriver::get_edges_between_nodes(
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

Result<std::vector<EntityEdge>> KuzuDriver::get_edges_by_node(
    std::string_view node_uuid) {
    static const std::string query = std::format(
        R"(MATCH (n:Entity {{uuid: $node_uuid}})-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
RETURN {})", ENTITY_EDGE_RETURN);

    ParamMap params;
    params["node_uuid"] = str_val(node_uuid);

    auto result = impl_->query_params(query, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

VoidResult KuzuDriver::delete_entity_edge(std::string_view uuid) {
    ParamMap params;
    params["uuid"] = str_val(uuid);
    return impl_->run_params(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_ {uuid: $uuid})-[:RELATES_TO]->(m:Entity)
DETACH DELETE e)",
        std::move(params));
}

// ============================================================================
// Episodic Edge Operations (MENTIONS)
// ============================================================================

VoidResult KuzuDriver::save_episodic_edge(const EpisodicEdge& edge) {
    static const std::string query =
        R"(MATCH (episode:Episodic {uuid: $episode_uuid})
MATCH (node:Entity {uuid: $entity_uuid})
MERGE (episode)-[e:MENTIONS {uuid: $uuid}]->(node)
SET
    e.group_id = $group_id,
    e.created_at = $created_at
RETURN e.uuid AS uuid)";

    ParamMap params;
    params["episode_uuid"] = str_val(edge.source_node_uuid);
    params["entity_uuid"] = str_val(edge.target_node_uuid);
    params["uuid"] = str_val(edge.uuid);
    params["group_id"] = str_val(edge.group_id);
    params["created_at"] = ts_val(edge.created_at);

    return impl_->run_params(query, std::move(params));
}

// ============================================================================
// Embedding Operations
// ============================================================================

VoidResult KuzuDriver::save_entity_node_embedding(
    std::string_view uuid, const std::vector<float>& embedding) {
    static const std::string query =
        R"(MATCH (n:Entity {uuid: $uuid})
SET n.name_embedding = $embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["embedding"] = float_list_val(embedding);

    return impl_->run_params(query, std::move(params));
}

Result<std::optional<std::vector<float>>> KuzuDriver::load_entity_node_embedding(
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

VoidResult KuzuDriver::save_entity_edge_embedding(
    std::string_view uuid, const std::vector<float>& embedding) {
    static const std::string query =
        R"(MATCH (e:RelatesToNode_ {uuid: $uuid})
SET e.fact_embedding = $embedding)";

    ParamMap params;
    params["uuid"] = str_val(uuid);
    params["embedding"] = float_list_val(embedding);

    return impl_->run_params(query, std::move(params));
}

Result<std::optional<std::vector<float>>> KuzuDriver::load_entity_edge_embedding(
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

// ============================================================================
// Search Operations
// ============================================================================

Result<std::vector<EntityNode>> KuzuDriver::search_entity_nodes_bm25(
    std::string_view query, std::string_view group_id, int limit) {
    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('Entity', 'node_name_and_summary', $query, TOP := {})
WITH node AS n, score
WHERE n.group_id = $group_id
RETURN {}
ORDER BY score DESC
LIMIT {})", limit, ENTITY_NODE_RETURN, limit);

    ParamMap params;
    params["query"] = str_val(query);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

Result<std::vector<EntityNode>> KuzuDriver::search_entity_nodes_cosine(
    const std::vector<float>& query_embedding, std::string_view group_id,
    float min_score, int limit) {
    auto dim = query_embedding.size();
    auto cypher = std::format(
        R"(MATCH (n:Entity)
WHERE n.group_id = $group_id
WITH n, array_cosine_similarity(n.name_embedding, CAST($search_vector AS FLOAT[{}])) AS score
WHERE score > $min_score
RETURN {}
ORDER BY score DESC
LIMIT {})", dim, ENTITY_NODE_RETURN, limit);

    ParamMap params;
    params["group_id"] = str_val(group_id);
    params["search_vector"] = float_list_val(query_embedding);
    params["min_score"] = float_val(static_cast<double>(min_score));

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entities(result->get());
}

Result<std::vector<EntityEdge>> KuzuDriver::search_entity_edges_bm25(
    std::string_view query, std::string_view group_id, int limit) {
    auto cypher = std::format(
        R"(CALL QUERY_FTS_INDEX('RelatesToNode_', 'edge_name_and_fact', $query, TOP := {})
WITH node AS e, score
MATCH (n:Entity)-[:RELATES_TO]->(e)-[:RELATES_TO]->(m:Entity)
WHERE e.group_id = $group_id
RETURN {}
ORDER BY score DESC
LIMIT {})", limit, ENTITY_EDGE_RETURN, limit);

    ParamMap params;
    params["query"] = str_val(query);
    params["group_id"] = str_val(group_id);

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

Result<std::vector<EntityEdge>> KuzuDriver::search_entity_edges_cosine(
    const std::vector<float>& query_embedding, std::string_view group_id,
    float min_score, int limit) {
    auto dim = query_embedding.size();
    auto cypher = std::format(
        R"(MATCH (n:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(m:Entity)
WHERE e.group_id = $group_id
WITH DISTINCT e, n, m, array_cosine_similarity(e.fact_embedding, CAST($search_vector AS FLOAT[{}])) AS score
WHERE score > $min_score
RETURN {}
ORDER BY score DESC
LIMIT {})", dim, ENTITY_EDGE_RETURN, limit);

    ParamMap params;
    params["group_id"] = str_val(group_id);
    params["search_vector"] = float_list_val(query_embedding);
    params["min_score"] = float_val(static_cast<double>(min_score));

    auto result = impl_->query_params(cypher, std::move(params));
    if (!result) return std::unexpected(result.error());

    return collect_entity_edges(result->get());
}

// ============================================================================
// Maintenance
// ============================================================================

VoidResult KuzuDriver::clear_data(const std::vector<std::string>& group_ids) {
    if (group_ids.empty()) {
        // Clear everything
        return impl_->run("MATCH (n) DETACH DELETE n");
    }

    // Delete by group_ids, in order to respect foreign key constraints
    static const std::string tables[] = {
        "RelatesToNode_", "Entity", "Episodic", "Community"};

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
// Raw Access (for tests)
// ============================================================================

kuzu::main::Database* KuzuDriver::database() const { return impl_->db.get(); }
kuzu::main::Connection* KuzuDriver::connection() const { return impl_->conn.get(); }

} // namespace graphiti
