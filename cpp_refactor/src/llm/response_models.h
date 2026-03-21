#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace graphiti {

// ============================================================================
// Entity Extraction
// ============================================================================

struct ExtractedEntity {
    std::string name;
    int entity_type_id = 0;
};

struct ExtractedEntities {
    std::vector<ExtractedEntity> extracted_entities;
};

// ============================================================================
// Edge Extraction
// ============================================================================

struct ExtractedEdge {
    std::string source_entity_name;
    std::string target_entity_name;
    std::string relation_type;
    std::string fact;
    std::optional<std::string> valid_at;   // ISO8601 or null
    std::optional<std::string> invalid_at; // ISO8601 or null
};

struct ExtractedEdges {
    std::vector<ExtractedEdge> edges;
};

// ============================================================================
// Node Deduplication
// ============================================================================

struct NodeDuplicate {
    int id = 0;
    std::string name;
    std::string duplicate_name; // empty string if no duplicate found
};

struct NodeResolutions {
    std::vector<NodeDuplicate> entity_resolutions;
};

// ============================================================================
// Edge Deduplication
// ============================================================================

struct EdgeDuplicate {
    std::vector<int> duplicate_facts;
    std::vector<int> contradicted_facts;
};

// ============================================================================
// Summarization
// ============================================================================

struct EntitySummary {
    std::string summary;
};

struct SummarizedEntity {
    std::string name;
    std::string summary;
};

struct SummarizedEntities {
    std::vector<SummarizedEntity> summaries;
};

struct Summary {
    std::string summary;
};

struct SummaryDescription {
    std::string description;
};

// ============================================================================
// JSON Serialization (from_json only — these are LLM output, not input)
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEntity& v);
void from_json(const nlohmann::json& j, ExtractedEntities& v);
void from_json(const nlohmann::json& j, ExtractedEdge& v);
void from_json(const nlohmann::json& j, ExtractedEdges& v);
void from_json(const nlohmann::json& j, NodeDuplicate& v);
void from_json(const nlohmann::json& j, NodeResolutions& v);
void from_json(const nlohmann::json& j, EdgeDuplicate& v);
void from_json(const nlohmann::json& j, EntitySummary& v);
void from_json(const nlohmann::json& j, SummarizedEntity& v);
void from_json(const nlohmann::json& j, SummarizedEntities& v);
void from_json(const nlohmann::json& j, Summary& v);
void from_json(const nlohmann::json& j, SummaryDescription& v);

// to_json for completeness (useful for testing round-trips)
void to_json(nlohmann::json& j, const ExtractedEntity& v);
void to_json(nlohmann::json& j, const ExtractedEntities& v);
void to_json(nlohmann::json& j, const ExtractedEdge& v);
void to_json(nlohmann::json& j, const ExtractedEdges& v);
void to_json(nlohmann::json& j, const NodeDuplicate& v);
void to_json(nlohmann::json& j, const NodeResolutions& v);
void to_json(nlohmann::json& j, const EdgeDuplicate& v);
void to_json(nlohmann::json& j, const EntitySummary& v);
void to_json(nlohmann::json& j, const SummarizedEntity& v);
void to_json(nlohmann::json& j, const SummarizedEntities& v);
void to_json(nlohmann::json& j, const Summary& v);
void to_json(nlohmann::json& j, const SummaryDescription& v);

// ============================================================================
// Few-shot examples (injected into LLM prompts to show expected JSON format)
// ============================================================================

namespace response_schemas {

constexpr std::string_view EXTRACTED_ENTITIES = R"(Example 1:
{"extracted_entities": [{"name": "Alice", "entity_type_id": 0}, {"name": "Acme Corp", "entity_type_id": 1}]}

Example 2:
{"extracted_entities": [{"name": "Bob", "entity_type_id": 0}, {"name": "Project Atlas", "entity_type_id": 2}, {"name": "React", "entity_type_id": 3}]})";

constexpr std::string_view EXTRACTED_EDGES = R"(Example 1:
{"edges": [{"source_entity_name": "Alice", "target_entity_name": "Acme Corp", "relation_type": "WORKS_AT", "fact": "Alice works at Acme Corp as an engineer", "valid_at": "2025-01-15T00:00:00Z", "invalid_at": null}]}

Example 2:
{"edges": [{"source_entity_name": "Bob", "target_entity_name": "Alice", "relation_type": "REPORTS_TO", "fact": "Bob reports to Alice on the infrastructure team", "valid_at": "2025-03-01T00:00:00Z", "invalid_at": null}, {"source_entity_name": "Bob", "target_entity_name": "Project Atlas", "relation_type": "CONTRIBUTES_TO", "fact": "Bob is a contributor to Project Atlas", "valid_at": null, "invalid_at": null}]})";

constexpr std::string_view NODE_RESOLUTIONS = R"(Example 1 (duplicate found):
{"entity_resolutions": [{"id": 0, "name": "Robert Smith", "duplicate_name": "Bob Smith"}]}

Example 2 (no duplicate):
{"entity_resolutions": [{"id": 0, "name": "Alice Johnson", "duplicate_name": ""}, {"id": 1, "name": "Acme Corp", "duplicate_name": ""}]})";

constexpr std::string_view EDGE_DUPLICATE = R"(Example 1 (duplicates and contradictions found):
{"duplicate_facts": [2], "contradicted_facts": [1, 3]}

Example 2 (no duplicates or contradictions):
{"duplicate_facts": [], "contradicted_facts": []})";

constexpr std::string_view ENTITY_SUMMARY = R"(Example 1:
{"summary": "Alice is a senior engineer at Acme Corp who leads the infrastructure team."}

Example 2:
{"summary": "Project Atlas is a cloud migration initiative started in Q1 2025, currently in phase 2."})";

constexpr std::string_view SUMMARIZED_ENTITIES = R"(Example 1:
{"summaries": [{"name": "Alice", "summary": "Senior engineer at Acme Corp, leads infrastructure team."}, {"name": "Acme Corp", "summary": "Technology company focused on cloud infrastructure."}]}

Example 2:
{"summaries": [{"name": "Bob", "summary": "Junior developer contributing to Project Atlas since March 2025."}]})";

constexpr std::string_view SUMMARY = R"(Example 1:
{"summary": "Alice joined Acme Corp as a senior engineer and leads the infrastructure team."}

Example 2:
{"summary": "Q3 planning meeting covered the Atlas migration timeline and resource allocation."})";

constexpr std::string_view SUMMARY_DESCRIPTION = R"(Example 1:
{"description": "Employment and role information for a senior engineer."}

Example 2:
{"description": "Summary of a quarterly planning meeting about project timelines."})";

} // namespace response_schemas

} // namespace graphiti
