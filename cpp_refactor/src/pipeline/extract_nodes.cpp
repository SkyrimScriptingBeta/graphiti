#include "extract_nodes.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/uuid.h"

#include <format>

namespace graphiti::pipeline {

Result<std::vector<EntityNode>> extract_nodes(
    LLMClient& llm,
    const ExtractNodesInput& input
) {
    // Build prompt based on episode type
    std::vector<Message> messages;
    if (input.episode_type == EpisodeType::message) {
        messages = prompts::extract_message(
            input.entity_types, input.previous_episodes,
            input.episode_content, input.custom_instructions
        );
    } else {
        messages = prompts::extract_text(
            input.entity_types, input.episode_content, input.custom_instructions
        );
    }

    // Call LLM
    auto llm_result = llm.generate_response(
        messages, response_schemas::EXTRACTED_ENTITIES, ModelSize::small
    );
    if (!llm_result.has_value()) {
        return std::unexpected(llm_result.error());
    }

    // Parse response
    ExtractedEntities extracted;
    try {
        extracted = llm_result.value().get<ExtractedEntities>();
    } catch (const std::exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::llm_parse_error,
            std::format("Failed to parse extracted entities: {}", e.what())
        });
    }

    // Convert to EntityNode objects
    auto now = std::chrono::system_clock::now();
    std::vector<EntityNode> nodes;
    nodes.reserve(extracted.extracted_entities.size());

    for (auto& entity : extracted.extracted_entities) {
        EntityNode node;
        node.uuid = uuid::generate();
        node.name = std::move(entity.name);
        node.group_id = input.group_id;
        node.created_at = now;
        nodes.push_back(std::move(node));
    }

    return nodes;
}

} // namespace graphiti::pipeline
