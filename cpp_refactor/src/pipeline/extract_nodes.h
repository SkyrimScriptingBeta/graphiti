#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace graphiti::pipeline {

struct ExtractNodesInput {
    std::string episode_content;
    EpisodeType episode_type = EpisodeType::message;
    std::string entity_types; // JSON string of entity type definitions
    nlohmann::json previous_episodes = nlohmann::json::array();
    std::string group_id;
    std::string custom_instructions;
};

// Extract entity nodes from episode content via LLM.
// Returns EntityNodes with generated UUIDs but no embeddings yet.
Result<std::vector<EntityNode>> extract_nodes(
    LLMClient& llm,
    const ExtractNodesInput& input
);

} // namespace graphiti::pipeline
