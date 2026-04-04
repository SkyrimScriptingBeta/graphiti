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
    std::vector<std::string> traits;  // optional soft descriptors: ["fluffy", "reliable"]
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
{"extracted_entities": [{"name": "Zenith Corp", "entity_type_id": 0}, {"name": "Mara Chen", "entity_type_id": 0}, {"name": "PostgreSQL", "entity_type_id": 0}]}

Example 2:
{"extracted_entities": [{"name": "Sprint 3", "entity_type_id": 0}, {"name": "Terraform", "entity_type_id": 0}, {"name": "auth-service", "entity_type_id": 0}, {"name": "Juno", "entity_type_id": 0}, {"name": "Rex", "entity_type_id": 0}]})";

constexpr std::string_view EXTRACTED_EDGES = R"(Preferred relation types (use these when they fit, or derive your own in SCREAMING_SNAKE_CASE):
HAS_ROLE, MEMBER_OF, LEADS, SAME_AS, WORKS_WITH, WRITES, PRODUCES, REVIEWS, VALIDATES, USES, ATTACHES, DEPENDS_ON, IMPLEMENTS, BUILT_WITH, RESPONSIBLE_FOR, REQUIRES, TRANSITIONS_TO, BLOCKS, UNBLOCKS, KICKED_OFF_BY, DISCOVERED, LEARNED_FROM, CONTRADICTS, SUPERSEDES, CAUSED_BY, FOLLOWS, VIOLATES, CITES, INTRODUCED_IN, REMOVED_IN, REPLACED_BY, CHANGED_FROM, IS_A, DEVELOPS

Example 1:
{"edges": [{"source_entity_name": "Mara Chen", "target_entity_name": "Zenith Corp", "relation_type": "MEMBER_OF", "fact": "Mara Chen is a member of Zenith Corp", "valid_at": "2025-01-15T00:00:00Z", "invalid_at": null}, {"source_entity_name": "Mara Chen", "target_entity_name": "PostgreSQL", "relation_type": "WORKS_WITH", "fact": "Mara Chen works with PostgreSQL for data storage", "valid_at": null, "invalid_at": null}]}

Example 2:
{"edges": [{"source_entity_name": "Juno", "target_entity_name": "auth-service", "relation_type": "DEVELOPS", "fact": "Juno develops the auth-service", "valid_at": null, "invalid_at": null}, {"source_entity_name": "Rex", "target_entity_name": "Juno", "relation_type": "HAS_ROLE", "fact": "Rex has the role of architect on the team", "valid_at": "2025-03-01T00:00:00Z", "invalid_at": null}, {"source_entity_name": "auth-service", "target_entity_name": "Sprint 3", "relation_type": "INTRODUCED_IN", "fact": "auth-service was introduced in Sprint 3", "valid_at": null, "invalid_at": null}, {"source_entity_name": "Terraform", "target_entity_name": "auth-service", "relation_type": "DEPENDS_ON", "fact": "auth-service depends on Terraform for infrastructure", "valid_at": null, "invalid_at": null}]})";

constexpr std::string_view NODE_RESOLUTIONS = R"(Example 1 (duplicate found):
{"entity_resolutions": [{"id": 0, "name": "Mara Chen", "duplicate_name": "M. Chen"}]}

Example 2 (no duplicate):
{"entity_resolutions": [{"id": 0, "name": "Juno", "duplicate_name": ""}, {"id": 1, "name": "Zenith Corp", "duplicate_name": ""}]})";

constexpr std::string_view EDGE_DUPLICATE = R"(Example 1 (duplicates and contradictions found):
{"duplicate_facts": [2], "contradicted_facts": [1, 3]}

Example 2 (no duplicates or contradictions):
{"duplicate_facts": [], "contradicted_facts": []})";

constexpr std::string_view ENTITY_SUMMARY = R"(Example 1:
{"summary": "Mara Chen is the lead database engineer at Zenith Corp, responsible for PostgreSQL infrastructure."}

Example 2:
{"summary": "Sprint 3 focused on auth-service delivery, introduced Terraform for infrastructure management."})";

constexpr std::string_view SUMMARIZED_ENTITIES = R"(Example 1:
{"summaries": [{"name": "Juno", "summary": "Backend developer building auth-service, works with Terraform."}, {"name": "Zenith Corp", "summary": "Software company building distributed systems."}]}

Example 2:
{"summaries": [{"name": "Rex", "summary": "Architect responsible for system design and PR reviews since March 2025."}]})";

constexpr std::string_view SUMMARY = R"(Example 1:
{"summary": "Mara Chen joined Zenith Corp as lead database engineer, responsible for PostgreSQL infrastructure."}

Example 2:
{"summary": "Sprint 3 planning covered auth-service delivery timeline and Terraform migration."})";

constexpr std::string_view SUMMARY_DESCRIPTION = R"(Example 1:
{"description": "Role and responsibility information for a database engineer."}

Example 2:
{"description": "Summary of a sprint planning session about service delivery and infrastructure."})";

} // namespace response_schemas

} // namespace graphiti
