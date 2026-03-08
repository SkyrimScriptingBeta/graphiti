#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {

class KuzuDriver;
class LLMClient;
class EmbedderClient;

namespace pipeline {

// Neighbor info for label propagation
struct Neighbor {
    std::string node_uuid;
    int64_t edge_count;
};

// Label propagation clustering: assigns nodes to communities based on
// weighted majority vote from neighbors.
// Input: projection map of node_uuid -> list of neighbors.
// Returns: list of clusters (each cluster is a list of node UUIDs).
std::vector<std::vector<std::string>> label_propagation(
    const std::unordered_map<std::string, std::vector<Neighbor>>& projection
);

// Build a single community from a cluster of entity nodes using LLM.
// Returns community node + HAS_MEMBER edges.
Result<std::pair<CommunityNode, std::vector<CommunityEdge>>> build_community(
    LLMClient& llm,
    EmbedderClient& embedder,
    const std::vector<EntityNode>& cluster,
    std::string_view group_id
);

// Determine which community an entity should belong to based on its neighbors.
// Returns (community_node, is_new_community).
// Returns nullopt if entity has no neighbors in any community.
Result<std::optional<std::pair<CommunityNode, bool>>> determine_entity_community(
    KuzuDriver& driver,
    std::string_view entity_uuid
);

// Update community assignment for a single entity after add_episode.
// Creates new community or adds entity to existing neighbor community.
Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> update_community(
    KuzuDriver& driver,
    LLMClient& llm,
    EmbedderClient& embedder,
    const EntityNode& entity
);

} // namespace pipeline
} // namespace graphiti
