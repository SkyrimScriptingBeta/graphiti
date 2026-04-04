#pragma once

#include <graphiti/error.h>
#include <graphiti/llm_client.h>
#include <graphiti/types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace graphiti::pipeline {

// Generate or update summaries for a batch of nodes via LLM.
VoidResult enrich_node_summaries(
    LLMClient& llm,
    std::vector<EntityNode>& nodes,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content
);

} // namespace graphiti::pipeline
