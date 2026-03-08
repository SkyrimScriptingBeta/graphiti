#include "dedupe_edges.h"

#include "driver/kuzu_driver.h"
#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/datetime.h"

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

    for (auto& new_edge : extracted_edges) {
        // Find existing edges between the same source and target nodes
        auto existing_result = driver.get_edges_between_nodes(
            new_edge.source_node_uuid, new_edge.target_node_uuid
        );

        if (!existing_result.has_value() || existing_result.value().empty()) {
            // No existing edges, this is new
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

        auto llm_result = llm.generate_response(
            messages, response_schemas::EDGE_DUPLICATE, ModelSize::small
        );

        if (!llm_result.has_value()) {
            // On LLM error, keep the edge (err on the side of adding)
            result.new_edges.push_back(new_edge);
            continue;
        }

        try {
            auto dedup = llm_result.value().get<EdgeDuplicate>();

            // Check if the new edge is a duplicate of any existing edge
            bool is_duplicate = !dedup.duplicate_facts.empty();

            if (!is_duplicate) {
                result.new_edges.push_back(new_edge);
            }

            // Mark contradicted edges for invalidation
            for (int contradicted_idx : dedup.contradicted_facts) {
                if (contradicted_idx >= 0 && contradicted_idx < static_cast<int>(existing_edges.size())) {
                    invalidated_set.insert(existing_edges[contradicted_idx].uuid);
                }
            }
        } catch (...) {
            // Parse error — keep the edge
            result.new_edges.push_back(new_edge);
        }
    }

    result.invalidated_uuids.assign(invalidated_set.begin(), invalidated_set.end());
    return result;
}

} // namespace graphiti::pipeline
