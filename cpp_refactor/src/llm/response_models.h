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
// JSON Schemas (injected into LLM prompts for structured output)
// ============================================================================

namespace response_schemas {

constexpr std::string_view EXTRACTED_ENTITIES = R"({
  "type": "object",
  "properties": {
    "extracted_entities": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name": {"type": "string", "description": "Name of the extracted entity"},
          "entity_type_id": {"type": "integer", "description": "ID of the classified entity type. Must be one of the provided entity_type_id integers."}
        },
        "required": ["name", "entity_type_id"]
      },
      "description": "List of extracted entities"
    }
  },
  "required": ["extracted_entities"],
  "title": "ExtractedEntities"
})";

constexpr std::string_view EXTRACTED_EDGES = R"({
  "type": "object",
  "properties": {
    "edges": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "source_entity_name": {"type": "string", "description": "The name of the source entity from the ENTITIES list"},
          "target_entity_name": {"type": "string", "description": "The name of the target entity from the ENTITIES list"},
          "relation_type": {"type": "string", "description": "The type of relationship in SCREAMING_SNAKE_CASE"},
          "fact": {"type": "string", "description": "A natural language description of the relationship"},
          "valid_at": {"type": ["string", "null"], "description": "ISO 8601 datetime when the fact became true, or null"},
          "invalid_at": {"type": ["string", "null"], "description": "ISO 8601 datetime when the fact stopped being true, or null"}
        },
        "required": ["source_entity_name", "target_entity_name", "relation_type", "fact"]
      }
    }
  },
  "required": ["edges"],
  "title": "ExtractedEdges"
})";

constexpr std::string_view NODE_RESOLUTIONS = R"({
  "type": "object",
  "properties": {
    "entity_resolutions": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "integer", "description": "Integer id of the entity"},
          "name": {"type": "string", "description": "Name of the entity. Should be the most complete and descriptive name."},
          "duplicate_name": {"type": "string", "description": "Name of the duplicate entity from EXISTING ENTITIES. Empty string if no duplicate found."}
        },
        "required": ["id", "name", "duplicate_name"]
      },
      "description": "List of resolved nodes"
    }
  },
  "required": ["entity_resolutions"],
  "title": "NodeResolutions"
})";

constexpr std::string_view EDGE_DUPLICATE = R"({
  "type": "object",
  "properties": {
    "duplicate_facts": {
      "type": "array",
      "items": {"type": "integer"},
      "description": "List of idx values of duplicate facts. Empty list if none."
    },
    "contradicted_facts": {
      "type": "array",
      "items": {"type": "integer"},
      "description": "List of idx values of contradicted facts. Empty list if none."
    }
  },
  "required": ["duplicate_facts", "contradicted_facts"],
  "title": "EdgeDuplicate"
})";

constexpr std::string_view ENTITY_SUMMARY = R"({
  "type": "object",
  "properties": {
    "summary": {"type": "string", "description": "Summary of the entity"}
  },
  "required": ["summary"],
  "title": "EntitySummary"
})";

constexpr std::string_view SUMMARIZED_ENTITIES = R"({
  "type": "object",
  "properties": {
    "summaries": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name": {"type": "string", "description": "Name of the entity being summarized"},
          "summary": {"type": "string", "description": "Updated summary for the entity"}
        },
        "required": ["name", "summary"]
      },
      "description": "List of entity summaries. Only include entities that need summary updates."
    }
  },
  "required": ["summaries"],
  "title": "SummarizedEntities"
})";

constexpr std::string_view SUMMARY = R"({
  "type": "object",
  "properties": {
    "summary": {"type": "string", "description": "Summary containing important information. Under 250 characters."}
  },
  "required": ["summary"],
  "title": "Summary"
})";

constexpr std::string_view SUMMARY_DESCRIPTION = R"({
  "type": "object",
  "properties": {
    "description": {"type": "string", "description": "One sentence description of the provided summary"}
  },
  "required": ["description"],
  "title": "SummaryDescription"
})";

} // namespace response_schemas

} // namespace graphiti
