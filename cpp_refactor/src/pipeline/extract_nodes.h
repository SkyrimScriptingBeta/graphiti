#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/type_definitions.h>
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
    std::string source_description; // Used by EpisodeType::json to tell the LLM what the JSON represents
    const TypeDefinitions* type_defs = nullptr;
};

// Extract entity nodes from episode content via LLM.
// Returns EntityNodes with generated UUIDs but no embeddings yet.
// When type_defs is set, resolves entity_type_id to labels and filters excluded types.
Result<std::vector<EntityNode>> extract_nodes(
    LLMClient& llm,
    const ExtractNodesInput& input
);

// Extract custom attributes for entities via LLM based on TypeDefinitions.
// For each node with a custom type that has fields defined, calls the LLM
// to extract attribute values from the episode content.
VoidResult extract_entity_attributes(
    LLMClient& llm,
    std::vector<EntityNode>& nodes,
    const TypeDefinitions& type_defs,
    std::string_view episode_content
);

} // namespace graphiti::pipeline
