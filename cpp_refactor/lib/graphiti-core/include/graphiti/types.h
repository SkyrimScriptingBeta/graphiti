#pragma once

#include <graphiti/fwd.h>

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace graphiti {

enum class EpisodeType { message, json, text };

std::string to_string(EpisodeType type);
EpisodeType episode_type_from_string(std::string_view s);

// --- Node Types ---

struct EntityNode {
    std::string uuid;
    std::string name;
    std::string group_id;
    std::vector<std::string> labels;
    TimePoint created_at;
    std::optional<std::vector<float>> name_embedding;
    std::string summary;
    nlohmann::json attributes = nlohmann::json::object();
    std::vector<std::string> traits;
    bool is_system = false;
    bool is_identity = false;  // true for the Person node that Self → SAME_AS points to
    std::vector<std::string> agent_ids;
    std::vector<std::string> source_ids;
    std::vector<std::string> source_contexts;
    std::vector<std::string> participant_ids;
};

struct EpisodicNode {
    std::string uuid;
    std::string name;
    std::string group_id;
    TimePoint created_at;
    EpisodeType source = EpisodeType::message;
    std::string source_description;
    std::string content;
    TimePoint valid_at;
    std::vector<std::string> entity_edges;
    std::string agent_id;
    std::string source_id;
    std::string source_context;
    std::vector<std::string> participant_ids;
};

struct CommunityNode {
    std::string uuid;
    std::string name;
    std::string group_id;
    TimePoint created_at;
    std::optional<std::vector<float>> name_embedding;
    std::string summary;
    std::vector<std::string> agent_ids;
    std::vector<std::string> source_ids;
    std::vector<std::string> source_contexts;
    std::vector<std::string> participant_ids;
};

struct SagaNode {
    std::string uuid;
    std::string name;
    std::string group_id;
    TimePoint created_at;
};

// --- Edge Types ---

struct EntityEdge {
    std::string uuid;
    std::string group_id;
    std::string source_node_uuid;
    std::string target_node_uuid;
    std::string name;
    std::string fact;
    std::optional<std::vector<float>> fact_embedding;
    std::vector<std::string> episodes;
    TimePoint created_at;
    std::optional<TimePoint> expired_at;
    std::optional<TimePoint> valid_at;
    std::optional<TimePoint> invalid_at;
    nlohmann::json attributes = nlohmann::json::object();
    bool is_system = false;
    std::vector<std::string> agent_ids;
    std::vector<std::string> source_ids;
    std::vector<std::string> source_contexts;
    std::vector<std::string> participant_ids;
};

struct EpisodicEdge {
    std::string uuid;
    std::string group_id;
    std::string source_node_uuid;
    std::string target_node_uuid;
    TimePoint created_at;
    std::string agent_id;
    std::string source_id;
    std::string source_context;
    std::vector<std::string> participant_ids;
};

struct CommunityEdge {
    std::string uuid;
    std::string group_id;
    std::string source_node_uuid;
    std::string target_node_uuid;
    TimePoint created_at;
};

struct HasEpisodeEdge {
    std::string uuid;
    std::string group_id;
    std::string source_node_uuid;
    std::string target_node_uuid;
    TimePoint created_at;
};

struct NextEpisodeEdge {
    std::string uuid;
    std::string group_id;
    std::string source_node_uuid;
    std::string target_node_uuid;
    TimePoint created_at;
};

// --- JSON Serialization ---

void to_json(nlohmann::json& j, const EntityNode& n);
void from_json(const nlohmann::json& j, EntityNode& n);

void to_json(nlohmann::json& j, const EpisodicNode& n);
void from_json(const nlohmann::json& j, EpisodicNode& n);

void to_json(nlohmann::json& j, const CommunityNode& n);
void from_json(const nlohmann::json& j, CommunityNode& n);

void to_json(nlohmann::json& j, const SagaNode& n);
void from_json(const nlohmann::json& j, SagaNode& n);

void to_json(nlohmann::json& j, const EntityEdge& e);
void from_json(const nlohmann::json& j, EntityEdge& e);

void to_json(nlohmann::json& j, const EpisodicEdge& e);
void from_json(const nlohmann::json& j, EpisodicEdge& e);

void to_json(nlohmann::json& j, const CommunityEdge& e);
void from_json(const nlohmann::json& j, CommunityEdge& e);

void to_json(nlohmann::json& j, const HasEpisodeEdge& e);
void from_json(const nlohmann::json& j, HasEpisodeEdge& e);

void to_json(nlohmann::json& j, const NextEpisodeEdge& e);
void from_json(const nlohmann::json& j, NextEpisodeEdge& e);

} // namespace graphiti
