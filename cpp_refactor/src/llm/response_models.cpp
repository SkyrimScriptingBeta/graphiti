#include "response_models.h"

namespace graphiti {

// ============================================================================
// ExtractedEntity
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEntity& v) {
    j.at("name").get_to(v.name);
    j.at("entity_type_id").get_to(v.entity_type_id);
}

void to_json(nlohmann::json& j, const ExtractedEntity& v) {
    j = {{"name", v.name}, {"entity_type_id", v.entity_type_id}};
}

// ============================================================================
// ExtractedEntities
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEntities& v) {
    j.at("extracted_entities").get_to(v.extracted_entities);
}

void to_json(nlohmann::json& j, const ExtractedEntities& v) {
    j = {{"extracted_entities", v.extracted_entities}};
}

// ============================================================================
// ExtractedEdge
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEdge& v) {
    j.at("source_entity_name").get_to(v.source_entity_name);
    j.at("target_entity_name").get_to(v.target_entity_name);
    j.at("relation_type").get_to(v.relation_type);
    j.at("fact").get_to(v.fact);
    if (j.contains("valid_at") && !j["valid_at"].is_null()) {
        v.valid_at = j["valid_at"].get<std::string>();
    }
    if (j.contains("invalid_at") && !j["invalid_at"].is_null()) {
        v.invalid_at = j["invalid_at"].get<std::string>();
    }
}

void to_json(nlohmann::json& j, const ExtractedEdge& v) {
    j = {
        {"source_entity_name", v.source_entity_name},
        {"target_entity_name", v.target_entity_name},
        {"relation_type", v.relation_type},
        {"fact", v.fact},
        {"valid_at", v.valid_at ? nlohmann::json(*v.valid_at) : nlohmann::json(nullptr)},
        {"invalid_at", v.invalid_at ? nlohmann::json(*v.invalid_at) : nlohmann::json(nullptr)},
    };
}

// ============================================================================
// ExtractedEdges
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEdges& v) {
    j.at("edges").get_to(v.edges);
}

void to_json(nlohmann::json& j, const ExtractedEdges& v) {
    j = {{"edges", v.edges}};
}

// ============================================================================
// NodeDuplicate
// ============================================================================

void from_json(const nlohmann::json& j, NodeDuplicate& v) {
    j.at("id").get_to(v.id);
    j.at("name").get_to(v.name);
    j.at("duplicate_name").get_to(v.duplicate_name);
}

void to_json(nlohmann::json& j, const NodeDuplicate& v) {
    j = {{"id", v.id}, {"name", v.name}, {"duplicate_name", v.duplicate_name}};
}

// ============================================================================
// NodeResolutions
// ============================================================================

void from_json(const nlohmann::json& j, NodeResolutions& v) {
    j.at("entity_resolutions").get_to(v.entity_resolutions);
}

void to_json(nlohmann::json& j, const NodeResolutions& v) {
    j = {{"entity_resolutions", v.entity_resolutions}};
}

// ============================================================================
// EdgeDuplicate
// ============================================================================

void from_json(const nlohmann::json& j, EdgeDuplicate& v) {
    j.at("duplicate_facts").get_to(v.duplicate_facts);
    j.at("contradicted_facts").get_to(v.contradicted_facts);
}

void to_json(nlohmann::json& j, const EdgeDuplicate& v) {
    j = {{"duplicate_facts", v.duplicate_facts},
         {"contradicted_facts", v.contradicted_facts}};
}

// ============================================================================
// EntitySummary
// ============================================================================

void from_json(const nlohmann::json& j, EntitySummary& v) {
    j.at("summary").get_to(v.summary);
}

void to_json(nlohmann::json& j, const EntitySummary& v) {
    j = {{"summary", v.summary}};
}

// ============================================================================
// SummarizedEntity
// ============================================================================

void from_json(const nlohmann::json& j, SummarizedEntity& v) {
    j.at("name").get_to(v.name);
    j.at("summary").get_to(v.summary);
}

void to_json(nlohmann::json& j, const SummarizedEntity& v) {
    j = {{"name", v.name}, {"summary", v.summary}};
}

// ============================================================================
// SummarizedEntities
// ============================================================================

void from_json(const nlohmann::json& j, SummarizedEntities& v) {
    j.at("summaries").get_to(v.summaries);
}

void to_json(nlohmann::json& j, const SummarizedEntities& v) {
    j = {{"summaries", v.summaries}};
}

// ============================================================================
// Summary
// ============================================================================

void from_json(const nlohmann::json& j, Summary& v) {
    j.at("summary").get_to(v.summary);
}

void to_json(nlohmann::json& j, const Summary& v) {
    j = {{"summary", v.summary}};
}

// ============================================================================
// SummaryDescription
// ============================================================================

void from_json(const nlohmann::json& j, SummaryDescription& v) {
    j.at("description").get_to(v.description);
}

void to_json(nlohmann::json& j, const SummaryDescription& v) {
    j = {{"description", v.description}};
}

} // namespace graphiti
