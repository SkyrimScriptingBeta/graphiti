#include "response_models.h"

namespace graphiti {

// ============================================================================
// ExtractedEntity
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEntity& v) {
    if (j.contains("name") && !j["name"].is_null())
        j["name"].get_to(v.name);
    if (j.contains("entity_type_id") && !j["entity_type_id"].is_null())
        j["entity_type_id"].get_to(v.entity_type_id);
    if (j.contains("traits") && j["traits"].is_array()) {
        for (auto& t : j["traits"]) {
            if (t.is_string()) v.traits.push_back(t.get<std::string>());
        }
    }
    if (v.name.empty()) throw std::runtime_error("entity name is empty or null");
}

void to_json(nlohmann::json& j, const ExtractedEntity& v) {
    j = {{"name", v.name}, {"entity_type_id", v.entity_type_id}};
    if (!v.traits.empty()) j["traits"] = v.traits;
}

// ============================================================================
// ExtractedEntities
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEntities& v) {
    for (auto& entity_json : j.at("extracted_entities")) {
        try {
            v.extracted_entities.push_back(entity_json.get<ExtractedEntity>());
        } catch (const std::exception& e) {
            fprintf(stderr, "  [graphiti] skipping malformed entity: %s\n", e.what());
        }
    }
}

void to_json(nlohmann::json& j, const ExtractedEntities& v) {
    j = {{"extracted_entities", v.extracted_entities}};
}

// ============================================================================
// ExtractedEdge
// ============================================================================

void from_json(const nlohmann::json& j, ExtractedEdge& v) {
    auto safe_str = [&](const char* key) -> std::string {
        if (j.contains(key) && !j[key].is_null()) return j[key].get<std::string>();
        return "";
    };
    v.source_entity_name = safe_str("source_entity_name");
    v.target_entity_name = safe_str("target_entity_name");
    v.relation_type      = safe_str("relation_type");
    v.fact               = safe_str("fact");
    if (j.contains("valid_at") && !j["valid_at"].is_null())
        v.valid_at = j["valid_at"].get<std::string>();
    if (j.contains("invalid_at") && !j["invalid_at"].is_null())
        v.invalid_at = j["invalid_at"].get<std::string>();
    if (v.source_entity_name.empty() || v.target_entity_name.empty())
        throw std::runtime_error("edge missing source or target entity name");
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
    // Parse edges individually — skip malformed ones instead of losing all edges
    for (auto& edge_json : j.at("edges")) {
        try {
            v.edges.push_back(edge_json.get<ExtractedEdge>());
        } catch (const std::exception& e) {
            // Log and skip — one bad edge shouldn't kill 30 good ones
            fprintf(stderr, "  [graphiti] skipping malformed edge: %s\n", e.what());
        }
    }
}

void to_json(nlohmann::json& j, const ExtractedEdges& v) {
    j = {{"edges", v.edges}};
}

// ============================================================================
// NodeDuplicate
// ============================================================================

void from_json(const nlohmann::json& j, NodeDuplicate& v) {
    if (j.contains("id") && !j["id"].is_null()) j["id"].get_to(v.id);
    if (j.contains("name") && !j["name"].is_null()) j["name"].get_to(v.name);
    if (j.contains("duplicate_name") && !j["duplicate_name"].is_null())
        j["duplicate_name"].get_to(v.duplicate_name);
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
    if (j.contains("summary") && !j["summary"].is_null())
        j["summary"].get_to(v.summary);
}

void to_json(nlohmann::json& j, const EntitySummary& v) {
    j = {{"summary", v.summary}};
}

// ============================================================================
// SummarizedEntity
// ============================================================================

void from_json(const nlohmann::json& j, SummarizedEntity& v) {
    if (j.contains("name") && !j["name"].is_null()) j["name"].get_to(v.name);
    if (j.contains("summary") && !j["summary"].is_null()) j["summary"].get_to(v.summary);
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
