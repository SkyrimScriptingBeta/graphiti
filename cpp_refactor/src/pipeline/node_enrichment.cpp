#include "node_enrichment.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"

#include <format>

namespace graphiti::pipeline {

VoidResult enrich_node_summaries(
    LLMClient& llm,
    std::vector<EntityNode>& nodes,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content
) {
    if (nodes.empty()) return {};

    // Build entities JSON for batch summary extraction
    nlohmann::json entities_json = nlohmann::json::array();
    for (auto& node : nodes) {
        entities_json.push_back({
            {"name", node.name},
            {"summary", node.summary},
        });
    }

    auto messages = prompts::extract_summaries_batch(
        previous_episodes, episode_content, entities_json
    );

    auto llm_result = llm.generate_response(
        messages, response_schemas::SUMMARIZED_ENTITIES, ModelSize::small
    );

    if (!llm_result.has_value()) {
        return std::unexpected(llm_result.error());
    }

    try {
        auto summaries = llm_result.value().get<SummarizedEntities>();

        // Match summaries back to nodes by name
        for (auto& summary : summaries.summaries) {
            for (auto& node : nodes) {
                if (node.name == summary.name) {
                    node.summary = std::move(summary.summary);
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::llm_parse_error,
            std::format("Failed to parse entity summaries: {}", e.what())
        });
    }

    return {};
}

} // namespace graphiti::pipeline
