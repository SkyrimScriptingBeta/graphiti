#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {

class GraphStore;
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

// Get community clusters for the given group_ids using label propagation.
// If group_ids is empty, queries all distinct group_ids from the graph.
// Returns clusters of EntityNodes ready for community building.
Result<std::vector<std::vector<EntityNode>>> get_community_clusters(
    GraphStore& store,
    const std::vector<std::string>& group_ids
);

// Remove all community nodes and their edges from the graph.
VoidResult remove_communities(GraphStore& store);

// Build a single community from a cluster of entity nodes using LLM.
// Uses tree-based pairwise summarization for quality on large clusters.
// Returns community node + HAS_MEMBER edges.
Result<std::pair<CommunityNode, std::vector<CommunityEdge>>> build_community(
    LLMClient& llm,
    EmbedderClient& embedder,
    const std::vector<EntityNode>& cluster,
    std::string_view group_id
);

// Full community build orchestration: remove old communities, cluster entities,
// build new communities via LLM, persist everything.
// Returns all created community nodes and edges.
Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> build_communities(
    GraphStore& store,
    LLMClient& llm,
    EmbedderClient& embedder,
    const std::vector<std::string>& group_ids
);

// Determine which community an entity should belong to based on its neighbors.
// Returns (community_node, is_new_community).
// Returns nullopt if entity has no neighbors in any community.
Result<std::optional<std::pair<CommunityNode, bool>>> determine_entity_community(
    GraphStore& store,
    std::string_view entity_uuid
);

// Update community assignment for a single entity after add_episode.
// Creates new community or adds entity to existing neighbor community.
// Also updates the community summary by merging with entity summary via LLM.
Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> update_community(
    GraphStore& store,
    LLMClient& llm,
    EmbedderClient& embedder,
    const EntityNode& entity
);

} // namespace pipeline
} // namespace graphiti
