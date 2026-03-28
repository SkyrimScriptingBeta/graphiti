#include <graphiti/types.h>

#include "utils/datetime.h"

namespace graphiti {

// --- EpisodeType ---

std::string to_string(EpisodeType type) {
    switch (type) {
        case EpisodeType::message: return "message";
        case EpisodeType::json: return "json";
        case EpisodeType::text: return "text";
    }
    return "message";
}

EpisodeType episode_type_from_string(std::string_view s) {
    if (s == "json") return EpisodeType::json;
    if (s == "text") return EpisodeType::text;
    return EpisodeType::message;
}

// --- JSON helpers for TimePoint ---

static nlohmann::json timepoint_to_json(TimePoint tp) {
    return datetime::to_iso8601(tp);
}

static TimePoint timepoint_from_json(const nlohmann::json& j) {
    if (j.is_null()) return {};
    return datetime::from_iso8601(j.get<std::string>());
}

static nlohmann::json optional_timepoint_to_json(const std::optional<TimePoint>& tp) {
    if (!tp) return nullptr;
    return datetime::to_iso8601(*tp);
}

static std::optional<TimePoint> optional_timepoint_from_json(const nlohmann::json& j) {
    if (j.is_null()) return std::nullopt;
    return datetime::from_iso8601(j.get<std::string>());
}

// --- EntityNode ---

void to_json(nlohmann::json& j, const EntityNode& n) {
    j = {
        {"uuid", n.uuid},
        {"name", n.name},
        {"group_id", n.group_id},
        {"labels", n.labels},
        {"created_at", timepoint_to_json(n.created_at)},
        {"summary", n.summary},
        {"attributes", n.attributes},
        {"traits", n.traits},
        {"is_system", n.is_system},
        {"is_identity", n.is_identity},
        {"agent_ids", n.agent_ids},
        {"source_ids", n.source_ids},
        {"source_contexts", n.source_contexts},
        {"participant_ids", n.participant_ids},
    };
    if (n.name_embedding)
        j["name_embedding"] = *n.name_embedding;
    else
        j["name_embedding"] = nullptr;
}

void from_json(const nlohmann::json& j, EntityNode& n) {
    j.at("uuid").get_to(n.uuid);
    j.at("name").get_to(n.name);
    j.at("group_id").get_to(n.group_id);
    j.at("labels").get_to(n.labels);
    n.created_at = timepoint_from_json(j.at("created_at"));
    j.at("summary").get_to(n.summary);
    j.at("attributes").get_to(n.attributes);
    if (j.contains("name_embedding") && !j.at("name_embedding").is_null())
        n.name_embedding = j.at("name_embedding").get<std::vector<float>>();
    else
        n.name_embedding = std::nullopt;
    if (j.contains("traits"))
        j.at("traits").get_to(n.traits);
    if (j.contains("is_system"))
        j.at("is_system").get_to(n.is_system);
    if (j.contains("is_identity"))
        j.at("is_identity").get_to(n.is_identity);
    if (j.contains("agent_ids"))
        j.at("agent_ids").get_to(n.agent_ids);
    if (j.contains("source_ids"))
        j.at("source_ids").get_to(n.source_ids);
    if (j.contains("source_contexts"))
        j.at("source_contexts").get_to(n.source_contexts);
    if (j.contains("participant_ids"))
        j.at("participant_ids").get_to(n.participant_ids);
}

// --- EpisodicNode ---

void to_json(nlohmann::json& j, const EpisodicNode& n) {
    j = {
        {"uuid", n.uuid},
        {"name", n.name},
        {"group_id", n.group_id},
        {"created_at", timepoint_to_json(n.created_at)},
        {"source", to_string(n.source)},
        {"source_description", n.source_description},
        {"content", n.content},
        {"valid_at", timepoint_to_json(n.valid_at)},
        {"entity_edges", n.entity_edges},
        {"agent_id", n.agent_id},
        {"source_id", n.source_id},
        {"source_context", n.source_context},
        {"participant_ids", n.participant_ids},
    };
}

void from_json(const nlohmann::json& j, EpisodicNode& n) {
    j.at("uuid").get_to(n.uuid);
    j.at("name").get_to(n.name);
    j.at("group_id").get_to(n.group_id);
    n.created_at = timepoint_from_json(j.at("created_at"));
    n.source = episode_type_from_string(j.at("source").get<std::string>());
    j.at("source_description").get_to(n.source_description);
    j.at("content").get_to(n.content);
    n.valid_at = timepoint_from_json(j.at("valid_at"));
    j.at("entity_edges").get_to(n.entity_edges);
    if (j.contains("agent_id"))
        j.at("agent_id").get_to(n.agent_id);
    if (j.contains("source_id"))
        j.at("source_id").get_to(n.source_id);
    if (j.contains("source_context"))
        j.at("source_context").get_to(n.source_context);
    if (j.contains("participant_ids"))
        j.at("participant_ids").get_to(n.participant_ids);
}

// --- CommunityNode ---

void to_json(nlohmann::json& j, const CommunityNode& n) {
    j = {
        {"uuid", n.uuid},
        {"name", n.name},
        {"group_id", n.group_id},
        {"created_at", timepoint_to_json(n.created_at)},
        {"summary", n.summary},
        {"agent_ids", n.agent_ids},
        {"source_ids", n.source_ids},
        {"source_contexts", n.source_contexts},
        {"participant_ids", n.participant_ids},
    };
    if (n.name_embedding)
        j["name_embedding"] = *n.name_embedding;
    else
        j["name_embedding"] = nullptr;
}

void from_json(const nlohmann::json& j, CommunityNode& n) {
    j.at("uuid").get_to(n.uuid);
    j.at("name").get_to(n.name);
    j.at("group_id").get_to(n.group_id);
    n.created_at = timepoint_from_json(j.at("created_at"));
    j.at("summary").get_to(n.summary);
    if (j.contains("name_embedding") && !j.at("name_embedding").is_null())
        n.name_embedding = j.at("name_embedding").get<std::vector<float>>();
    else
        n.name_embedding = std::nullopt;
    if (j.contains("agent_ids"))
        j.at("agent_ids").get_to(n.agent_ids);
    if (j.contains("source_ids"))
        j.at("source_ids").get_to(n.source_ids);
    if (j.contains("source_contexts"))
        j.at("source_contexts").get_to(n.source_contexts);
    if (j.contains("participant_ids"))
        j.at("participant_ids").get_to(n.participant_ids);
}

// --- SagaNode ---

void to_json(nlohmann::json& j, const SagaNode& n) {
    j = {
        {"uuid", n.uuid},
        {"name", n.name},
        {"group_id", n.group_id},
        {"created_at", timepoint_to_json(n.created_at)},
    };
}

void from_json(const nlohmann::json& j, SagaNode& n) {
    j.at("uuid").get_to(n.uuid);
    j.at("name").get_to(n.name);
    j.at("group_id").get_to(n.group_id);
    n.created_at = timepoint_from_json(j.at("created_at"));
}

// --- EntityEdge ---

void to_json(nlohmann::json& j, const EntityEdge& e) {
    j = {
        {"uuid", e.uuid},
        {"group_id", e.group_id},
        {"source_node_uuid", e.source_node_uuid},
        {"target_node_uuid", e.target_node_uuid},
        {"name", e.name},
        {"fact", e.fact},
        {"episodes", e.episodes},
        {"created_at", timepoint_to_json(e.created_at)},
        {"expired_at", optional_timepoint_to_json(e.expired_at)},
        {"valid_at", optional_timepoint_to_json(e.valid_at)},
        {"invalid_at", optional_timepoint_to_json(e.invalid_at)},
        {"attributes", e.attributes},
        {"is_system", e.is_system},
        {"agent_ids", e.agent_ids},
        {"source_ids", e.source_ids},
        {"source_contexts", e.source_contexts},
        {"participant_ids", e.participant_ids},
    };
    if (e.fact_embedding)
        j["fact_embedding"] = *e.fact_embedding;
    else
        j["fact_embedding"] = nullptr;
}

void from_json(const nlohmann::json& j, EntityEdge& e) {
    j.at("uuid").get_to(e.uuid);
    j.at("group_id").get_to(e.group_id);
    j.at("source_node_uuid").get_to(e.source_node_uuid);
    j.at("target_node_uuid").get_to(e.target_node_uuid);
    j.at("name").get_to(e.name);
    j.at("fact").get_to(e.fact);
    j.at("episodes").get_to(e.episodes);
    e.created_at = timepoint_from_json(j.at("created_at"));
    e.expired_at = optional_timepoint_from_json(j.at("expired_at"));
    e.valid_at = optional_timepoint_from_json(j.at("valid_at"));
    e.invalid_at = optional_timepoint_from_json(j.at("invalid_at"));
    j.at("attributes").get_to(e.attributes);
    if (j.contains("is_system"))
        j.at("is_system").get_to(e.is_system);
    if (j.contains("fact_embedding") && !j.at("fact_embedding").is_null())
        e.fact_embedding = j.at("fact_embedding").get<std::vector<float>>();
    else
        e.fact_embedding = std::nullopt;
    if (j.contains("agent_ids"))
        j.at("agent_ids").get_to(e.agent_ids);
    if (j.contains("source_ids"))
        j.at("source_ids").get_to(e.source_ids);
    if (j.contains("source_contexts"))
        j.at("source_contexts").get_to(e.source_contexts);
    if (j.contains("participant_ids"))
        j.at("participant_ids").get_to(e.participant_ids);
}

// --- Simple edge types (all share the same structure) ---

static void simple_edge_to_json(nlohmann::json& j, const std::string& uuid,
                                const std::string& group_id, const std::string& src,
                                const std::string& tgt, TimePoint created_at) {
    j = {
        {"uuid", uuid},
        {"group_id", group_id},
        {"source_node_uuid", src},
        {"target_node_uuid", tgt},
        {"created_at", timepoint_to_json(created_at)},
    };
}

static void simple_edge_from_json(const nlohmann::json& j, std::string& uuid,
                                  std::string& group_id, std::string& src, std::string& tgt,
                                  TimePoint& created_at) {
    j.at("uuid").get_to(uuid);
    j.at("group_id").get_to(group_id);
    j.at("source_node_uuid").get_to(src);
    j.at("target_node_uuid").get_to(tgt);
    created_at = timepoint_from_json(j.at("created_at"));
}

void to_json(nlohmann::json& j, const EpisodicEdge& e) {
    simple_edge_to_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                        e.created_at);
    j["agent_id"] = e.agent_id;
    j["source_id"] = e.source_id;
    j["source_context"] = e.source_context;
    j["participant_ids"] = e.participant_ids;
}
void from_json(const nlohmann::json& j, EpisodicEdge& e) {
    simple_edge_from_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                          e.created_at);
    if (j.contains("agent_id"))
        j.at("agent_id").get_to(e.agent_id);
    if (j.contains("source_id"))
        j.at("source_id").get_to(e.source_id);
    if (j.contains("source_context"))
        j.at("source_context").get_to(e.source_context);
    if (j.contains("participant_ids"))
        j.at("participant_ids").get_to(e.participant_ids);
}

void to_json(nlohmann::json& j, const CommunityEdge& e) {
    simple_edge_to_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                        e.created_at);
}
void from_json(const nlohmann::json& j, CommunityEdge& e) {
    simple_edge_from_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                          e.created_at);
}

void to_json(nlohmann::json& j, const HasEpisodeEdge& e) {
    simple_edge_to_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                        e.created_at);
}
void from_json(const nlohmann::json& j, HasEpisodeEdge& e) {
    simple_edge_from_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                          e.created_at);
}

void to_json(nlohmann::json& j, const NextEpisodeEdge& e) {
    simple_edge_to_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                        e.created_at);
}
void from_json(const nlohmann::json& j, NextEpisodeEdge& e) {
    simple_edge_from_json(j, e.uuid, e.group_id, e.source_node_uuid, e.target_node_uuid,
                          e.created_at);
}

} // namespace graphiti
