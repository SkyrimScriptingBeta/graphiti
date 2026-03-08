#include "dedupe_nodes.h"

#include "driver/kuzu_driver.h"
#include "llm/response_models.h"
#include "prompts/prompts.h"

#include <graphiti/embedder.h>

#include <format>

namespace graphiti::pipeline {

Result<DedupeNodesResult> dedupe_nodes(
    LLMClient& llm,
    KuzuDriver& driver,
    EmbedderClient& embedder,
    const std::vector<EntityNode>& extracted_nodes,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    std::string_view group_id
) {
    DedupeNodesResult result;
    result.nodes = extracted_nodes; // Start with copies

    if (extracted_nodes.empty()) return result;

    // For each extracted node, search for potential duplicates in the graph
    for (size_t i = 0; i < result.nodes.size(); ++i) {
        auto& node = result.nodes[i];

        // Search existing nodes by name (BM25)
        auto search_result = driver.search_entity_nodes_bm25(node.name, group_id, 10);
        if (!search_result.has_value() || search_result.value().empty()) {
            // No candidates found, node is new
            continue;
        }

        // Build existing nodes JSON for the LLM
        nlohmann::json existing_json = nlohmann::json::array();
        for (auto& existing : search_result.value()) {
            existing_json.push_back({
                {"name", existing.name},
                {"summary", existing.summary},
            });
        }

        // Build extracted node JSON
        nlohmann::json node_json = {
            {"id", static_cast<int>(i)},
            {"name", node.name},
        };

        // Call LLM for dedup decision
        auto messages = prompts::dedupe_node(
            previous_episodes, episode_content,
            node_json, "", existing_json
        );

        auto llm_result = llm.generate_response(
            messages, response_schemas::NODE_RESOLUTIONS, ModelSize::small
        );

        if (!llm_result.has_value()) continue; // On error, keep the node as-is

        try {
            auto resolutions = llm_result.value().get<NodeResolutions>();
            if (!resolutions.entity_resolutions.empty()) {
                auto& res = resolutions.entity_resolutions[0];

                // Update name to the best version
                node.name = res.name;

                if (!res.duplicate_name.empty()) {
                    // Find the existing node UUID by name
                    for (auto& existing : search_result.value()) {
                        if (existing.name == res.duplicate_name) {
                            // Map new UUID to existing UUID
                            result.uuid_map[node.uuid] = existing.uuid;
                            node.uuid = existing.uuid;
                            break;
                        }
                    }
                }
            }
        } catch (...) {
            // Parse error — keep node as-is
        }
    }

    return result;
}

} // namespace graphiti::pipeline
