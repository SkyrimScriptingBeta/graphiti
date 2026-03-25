#include "node_enrichment.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"

#include <graphiti/log.h>

#include <format>

namespace graphiti::pipeline {

VoidResult enrich_node_summaries(
    LLMClient& llm,
    std::vector<EntityNode>& nodes,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content
) {
    if (nodes.empty()) return {};

    log_trace("[graphiti]   enriching summaries for %zu nodes:", nodes.size());
    for (auto& n : nodes) log_trace(" \"%s\"", n.name.c_str());
    log_trace("\n");

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

    constexpr int MAX_RETRIES = 2;
    llm.prompt_name = "extract_summaries_batch";
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
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
            break;  // success
        } catch (const std::exception& e) {
            if (attempt < MAX_RETRIES) {
                fprintf(stderr, "  [graphiti] summary parse failed (attempt %d/%d), retrying: %s\n",
                        attempt + 1, MAX_RETRIES + 1, e.what());
                messages.push_back({"user",
                    std::format("The previous response was invalid. Error: {}. "
                                "Please try again with valid JSON.", e.what())
                });
                continue;
            }
            return std::unexpected(GraphitiError{
                ErrorCode::llm_parse_error,
                std::format("Failed to parse entity summaries after {} attempts: {}", MAX_RETRIES + 1, e.what())
            });
        }
    }

    return {};
}

} // namespace graphiti::pipeline
