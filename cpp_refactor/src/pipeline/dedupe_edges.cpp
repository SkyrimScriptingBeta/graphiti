#include "dedupe_edges.h"

#include "driver/kuzu_driver.h"
#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/datetime.h"

#include <graphiti/log.h>

#include <format>
#include <set>

namespace graphiti::pipeline {

Result<DedupeEdgesResult> dedupe_edges(
    LLMClient& llm,
    KuzuDriver& driver,
    const std::vector<EntityEdge>& extracted_edges
) {
    DedupeEdgesResult result;
    std::set<std::string> invalidated_set;

    for (size_t ei = 0; ei < extracted_edges.size(); ++ei) {
        auto& new_edge = extracted_edges[ei];
        log_trace("[graphiti]   dedupe edge %zu/%zu: \"%s\"\n",
                  ei + 1, extracted_edges.size(), new_edge.fact.c_str());

        // Find existing edges between the same source and target nodes
        auto existing_result = driver.get_edges_between_nodes(
            new_edge.source_node_uuid, new_edge.target_node_uuid
        );

        if (!existing_result.has_value() || existing_result.value().empty()) {
            log_trace("[graphiti]     → no existing edges, keeping as new\n");
            result.new_edges.push_back(new_edge);
            continue;
        }

        auto& existing_edges = existing_result.value();

        // Build fact strings with continuous idx numbering
        std::string existing_facts;
        int idx = 0;
        for (auto& e : existing_edges) {
            existing_facts += std::format("idx {}: {}\n", idx++, e.fact);
        }

        // For now, no invalidation candidates from elsewhere
        std::string invalidation_candidates;

        std::string new_fact = std::format("fact: {}", new_edge.fact);

        // Ask LLM to identify duplicates and contradictions
        auto messages = prompts::resolve_edge(
            existing_facts, invalidation_candidates, new_fact
        );

        constexpr int MAX_RETRIES = 2;
        bool edge_handled = false;
        for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
            auto llm_result = llm.generate_response(
                messages, response_schemas::EDGE_DUPLICATE, ModelSize::small
            );

            if (!llm_result.has_value()) {
                // LLM error — keep the edge
                result.new_edges.push_back(new_edge);
                edge_handled = true;
                break;
            }

            try {
                auto dedup = llm_result.value().get<EdgeDuplicate>();

                bool is_duplicate = !dedup.duplicate_facts.empty();
                if (!is_duplicate) {
                    result.new_edges.push_back(new_edge);
                }

                for (int contradicted_idx : dedup.contradicted_facts) {
                    if (contradicted_idx >= 0 && contradicted_idx < static_cast<int>(existing_edges.size())) {
                        invalidated_set.insert(existing_edges[contradicted_idx].uuid);
                    }
                }
                edge_handled = true;
                break; // success
            } catch (const std::exception& e) {
                if (attempt < MAX_RETRIES) {
                    fprintf(stderr, "  [graphiti] edge dedup parse failed (attempt %d/%d), retrying: %s\n",
                            attempt + 1, MAX_RETRIES + 1, e.what());
                    messages.push_back({"user",
                        std::format("The previous response was invalid. Error: {}. "
                                    "Please try again with valid JSON.", e.what())
                    });
                    continue;
                }
            }
        }
        if (!edge_handled) {
            // All retries failed — keep the edge
            result.new_edges.push_back(new_edge);
        }
    }

    result.invalidated_uuids.assign(invalidated_set.begin(), invalidated_set.end());
    return result;
}

} // namespace graphiti::pipeline
