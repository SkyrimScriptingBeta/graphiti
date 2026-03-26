#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti::pipeline {

struct ExtractEdgesInput {
    std::string episode_content;
    nlohmann::json previous_episodes = nlohmann::json::array();
    std::vector<EntityNode> nodes;
    std::string reference_time; // ISO8601
    std::string group_id;
    nlohmann::json edge_types = nlohmann::json();
    std::string custom_instructions;
    int shard_size = 0;  // 0 = no sharding; N = split entities into groups of N
};

// Extract relationship edges between entities via LLM.
// Returns EntityEdges with source/target resolved to node UUIDs.
Result<std::vector<EntityEdge>> extract_edges(
    LLMClient& llm,
    const ExtractEdgesInput& input
);

} // namespace graphiti::pipeline
