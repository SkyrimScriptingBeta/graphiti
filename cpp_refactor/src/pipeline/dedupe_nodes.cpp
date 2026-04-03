#include "dedupe_nodes.h"

#include "driver/kuzu_driver.h"
#include "llm/response_models.h"
#include "prompts/prompts.h"

#include <graphiti/embedder.h>
#include <graphiti/log.h>
#include <graphiti/callsite_log.h>

#include <algorithm>
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

        log_trace("[graphiti]   dedupe node %zu/%zu: \"%s\"\n",
                  i + 1, result.nodes.size(), node.name.c_str());

        // Skip system nodes — they're infrastructure, not content
        if (node.is_system) {
            log_trace("[graphiti]     → system node, skipping dedup\n");
            continue;
        }

        // Search existing nodes by name (BM25), then filter out system nodes
        // so the LLM never sees Self or other infrastructure nodes as merge candidates
        graphiti::log_callsite("dedupe-nodes-find-candidates");
        auto search_result = driver.search_entity_nodes_bm25(node.name, group_id, 10);
        if (search_result.has_value()) {
            auto& candidates = search_result.value();
            candidates.erase(
                std::remove_if(candidates.begin(), candidates.end(),
                    [](const EntityNode& n) { return n.is_system; }),
                candidates.end());
        }
        if (!search_result.has_value() || search_result.value().empty()) {
            log_trace("[graphiti]     → no candidates, keeping as new\n");
            continue;
        }

        // GRAPHITI_AUTO_MERGE_MATCHING_NODES_BY_NAME=1 — skip LLM for exact name matches
        {
            static int auto_merge = -1;
            if (auto_merge < 0) {
                auto* env = std::getenv("GRAPHITI_AUTO_MERGE_MATCHING_NODES_BY_NAME");
                auto_merge = (env && env[0] == '1') ? 1 : 0;
            }
            if (auto_merge) {
                // Case-insensitive exact name match — merge without LLM
                auto lower = [](std::string s) {
                    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return s;
                };
                auto node_lower = lower(node.name);
                bool found = false;
                for (auto& existing : search_result.value()) {
                    if (lower(existing.name) == node_lower) {
                        log_trace("[graphiti]     → auto-merge (exact name): \"%s\" → \"%s\" (uuid: %s)\n",
                                  node.name.c_str(), existing.name.c_str(), existing.uuid.c_str());
                        result.uuid_map[node.uuid] = existing.uuid;
                        node.uuid = existing.uuid;
                        node.name = existing.name;  // keep the existing name's casing
                        found = true;
                        break;
                    }
                }
                if (found) continue;
            }
        }

        // Build existing nodes JSON for the LLM
        nlohmann::json existing_json = nlohmann::json::array();
        for (auto& existing : search_result.value()) {
            existing_json.push_back({
                {"name", existing.name},
                {"summary", existing.summary},
            });
        }

        // Build extracted node JSON — include summary, labels, and traits for context
        nlohmann::json node_json = {
            {"id", static_cast<int>(i)},
            {"name", node.name},
        };
        if (!node.summary.empty())
            node_json["summary"] = node.summary;
        if (!node.labels.empty())
            node_json["labels"] = node.labels;
        if (!node.traits.empty())
            node_json["traits"] = node.traits;

        // Call LLM for dedup decision
        auto messages = prompts::dedupe_node(
            previous_episodes, episode_content,
            node_json, "", existing_json
        );

        constexpr int MAX_RETRIES = 2;
        llm.prompt_name = "dedupe_node";
        for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
            auto llm_result = llm.generate_response(
                messages, response_schemas::NODE_RESOLUTIONS, ModelSize::small
            );

            if (!llm_result.has_value()) break; // LLM error — keep node as-is

            try {
                auto resolutions = llm_result.value().get<NodeResolutions>();
                if (!resolutions.entity_resolutions.empty()) {
                    auto& res = resolutions.entity_resolutions[0];

                    // Update name to the best version
                    node.name = res.name;

                    if (!res.duplicate_name.empty()) {
                        // Find the existing node UUID by name
                        bool found = false;
                        for (auto& existing : search_result.value()) {
                            if (existing.name == res.duplicate_name) {
                                // Map new UUID to existing UUID
                                log_trace("[graphiti]     → dedup match: \"%s\" → \"%s\" (uuid: %s → %s)\n",
                                          node.name.c_str(), existing.name.c_str(),
                                          node.uuid.c_str(), existing.uuid.c_str());
                                result.uuid_map[node.uuid] = existing.uuid;
                                node.uuid = existing.uuid;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            log_trace("[graphiti]     ⚠️ LLM said duplicate_name=\"%s\" but no BM25 candidate matched! Candidates: ",
                                      res.duplicate_name.c_str());
                            for (auto& existing : search_result.value()) {
                                log_trace("\"%s\" ", existing.name.c_str());
                            }
                            log_trace("\n");
                        }
                    }
                }
                break; // success
            } catch (const std::exception& e) {
                if (attempt < MAX_RETRIES) {
                    fprintf(stderr, "  [graphiti] node dedup parse failed (attempt %d/%d), retrying: %s\n",
                            attempt + 1, MAX_RETRIES + 1, e.what());
                    messages.push_back({"user",
                        std::format("The previous response was invalid. Error: {}. "
                                    "Please try again with valid JSON.", e.what())
                    });
                    continue;
                }
                // Final attempt failed — keep node as-is
            }
        }
    }

    return result;
}

} // namespace graphiti::pipeline
