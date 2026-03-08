#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/types.h>

#include <string>
#include <vector>

namespace graphiti {
class KuzuDriver;
} // namespace graphiti

namespace graphiti::pipeline {

struct DedupeEdgesResult {
    std::vector<EntityEdge> new_edges;          // Edges to save (non-duplicates)
    std::vector<std::string> invalidated_uuids; // Existing edge UUIDs that are contradicted
};

// For each new edge, check existing edges between the same nodes.
// Ask LLM to identify duplicates and contradictions.
// Returns non-duplicate edges and a list of edges to invalidate.
Result<DedupeEdgesResult> dedupe_edges(
    LLMClient& llm,
    KuzuDriver& driver,
    const std::vector<EntityEdge>& extracted_edges
);

} // namespace graphiti::pipeline
