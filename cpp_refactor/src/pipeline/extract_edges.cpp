#include "extract_edges.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <format>
#include <unordered_map>

namespace graphiti::pipeline {

Result<std::vector<EntityEdge>> extract_edges(
    LLMClient& llm,
    const ExtractEdgesInput& input
) {
    // Build node name -> UUID lookup
    std::unordered_map<std::string, std::string> name_to_uuid;
    nlohmann::json nodes_json = nlohmann::json::array();
    for (auto& node : input.nodes) {
        name_to_uuid[node.name] = node.uuid;
        nodes_json.push_back({{"name", node.name}});
    }

    // Build prompt
    auto messages = prompts::extract_edges(
        input.previous_episodes, input.episode_content,
        nodes_json, input.reference_time,
        input.edge_types, input.custom_instructions
    );

    // Call LLM
    auto llm_result = llm.generate_response(
        messages, response_schemas::EXTRACTED_EDGES, ModelSize::small
    );
    if (!llm_result.has_value()) {
        return std::unexpected(llm_result.error());
    }

    // Parse response
    ExtractedEdges extracted;
    try {
        extracted = llm_result.value().get<ExtractedEdges>();
    } catch (const std::exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::llm_parse_error,
            std::format("Failed to parse extracted edges: {}", e.what())
        });
    }

    // Convert to EntityEdge objects
    auto now = std::chrono::system_clock::now();
    std::vector<EntityEdge> edges;

    for (auto& ext : extracted.edges) {
        // Resolve source and target to UUIDs
        auto src_it = name_to_uuid.find(ext.source_entity_name);
        auto tgt_it = name_to_uuid.find(ext.target_entity_name);

        // Skip edges with unresolvable entity names
        if (src_it == name_to_uuid.end() || tgt_it == name_to_uuid.end()) {
            continue;
        }

        EntityEdge edge;
        edge.uuid = uuid::generate();
        edge.group_id = input.group_id;
        edge.source_node_uuid = src_it->second;
        edge.target_node_uuid = tgt_it->second;
        edge.name = ext.relation_type;
        edge.fact = std::move(ext.fact);
        edge.created_at = now;

        // Parse optional timestamps
        if (ext.valid_at.has_value()) {
            edge.valid_at = datetime::from_iso8601(ext.valid_at.value());
        }
        if (ext.invalid_at.has_value()) {
            edge.invalid_at = datetime::from_iso8601(ext.invalid_at.value());
        }

        edges.push_back(std::move(edge));
    }

    return edges;
}

} // namespace graphiti::pipeline
