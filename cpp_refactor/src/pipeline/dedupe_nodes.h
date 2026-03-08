#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {
class KuzuDriver;
class EmbedderClient;
} // namespace graphiti

namespace graphiti::pipeline {

struct DedupeNodesResult {
    std::vector<EntityNode> nodes; // Deduplicated nodes (may reference existing UUIDs)
    std::unordered_map<std::string, std::string> uuid_map; // old UUID -> canonical UUID
};

// Deduplicate extracted nodes against existing graph entities.
// For each node, searches graph for potential matches, asks LLM to compare.
// Returns deduplicated nodes and a UUID mapping for updating edge references.
Result<DedupeNodesResult> dedupe_nodes(
    LLMClient& llm,
    KuzuDriver& driver,
    EmbedderClient& embedder,
    const std::vector<EntityNode>& extracted_nodes,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    std::string_view group_id
);

} // namespace graphiti::pipeline
