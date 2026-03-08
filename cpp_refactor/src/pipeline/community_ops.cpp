#include "community_ops.h"

#include "driver/kuzu_driver.h"
#include "utils/uuid.h"

#include <graphiti/embedder.h>
#include <graphiti/llm_client.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <unordered_map>

namespace graphiti::pipeline {

// ============================================================================
// Label propagation
// ============================================================================

std::vector<std::vector<std::string>> label_propagation(
    const std::unordered_map<std::string, std::vector<Neighbor>>& projection
) {
    if (projection.empty()) return {};

    // Initialize: each node gets its own community ID
    std::unordered_map<std::string, int> community_map;
    int id = 0;
    for (auto& [uuid, _] : projection) {
        community_map[uuid] = id++;
    }

    // Asynchronous label propagation: update community_map in-place
    // (each node sees the latest assignments, avoiding oscillation)
    for (int iter = 0; iter < 100; ++iter) {
        bool changed = false;

        for (auto& [uuid, neighbors] : projection) {
            int curr_community = community_map[uuid];

            // Count neighbor communities weighted by edge count
            std::unordered_map<int, int64_t> community_votes;
            for (auto& neighbor : neighbors) {
                auto it = community_map.find(neighbor.node_uuid);
                if (it != community_map.end()) {
                    community_votes[it->second] += neighbor.edge_count;
                }
            }

            // Find top-voted community
            int best_community = curr_community;
            int64_t best_count = 0;
            for (auto& [c, count] : community_votes) {
                if (count > best_count || (count == best_count && c > best_community)) {
                    best_count = count;
                    best_community = c;
                }
            }

            // Only switch if the candidate has more than 1 vote
            int new_community;
            if (best_count > 1) {
                new_community = best_community;
            } else {
                new_community = std::max(best_community, curr_community);
            }

            // In-place update (asynchronous LP)
            if (new_community != curr_community) {
                community_map[uuid] = new_community;
                changed = true;
            }
        }

        if (!changed) break;
    }

    // Group nodes by final community assignment
    std::unordered_map<int, std::vector<std::string>> cluster_map;
    for (auto& [uuid, community] : community_map) {
        cluster_map[community].push_back(uuid);
    }

    std::vector<std::vector<std::string>> clusters;
    clusters.reserve(cluster_map.size());
    for (auto& [_, cluster] : cluster_map) {
        clusters.push_back(std::move(cluster));
    }
    return clusters;
}

// ============================================================================
// Build community from entity cluster via LLM
// ============================================================================

Result<std::pair<CommunityNode, std::vector<CommunityEdge>>> build_community(
    LLMClient& llm,
    EmbedderClient& embedder,
    const std::vector<EntityNode>& cluster,
    std::string_view group_id
) {
    if (cluster.empty()) {
        return std::unexpected(GraphitiError{
            ErrorCode::invalid_config, "Cannot build community from empty cluster"});
    }

    // Collect summaries from cluster entities
    std::string combined_summaries;
    for (auto& entity : cluster) {
        if (!entity.summary.empty()) {
            combined_summaries += std::format("- {}: {}\n", entity.name, entity.summary);
        } else {
            combined_summaries += std::format("- {}\n", entity.name);
        }
    }

    // Generate community summary via LLM
    std::vector<Message> summary_messages = {
        {"system", "You are a helpful assistant that combines summaries."},
        {"user", std::format(
            R"(Synthesize the information from the following entity summaries into a single succinct community summary.

IMPORTANT: Keep the summary concise and to the point. SUMMARY MUST BE LESS THAN 250 CHARACTERS.

Entities:
{})", combined_summaries)}
    };

    std::string summary;
    auto summary_resp = llm.generate_response(summary_messages, std::nullopt, ModelSize::small);
    if (summary_resp.has_value()) {
        if (summary_resp.value().is_string()) {
            summary = summary_resp.value().get<std::string>();
        } else if (summary_resp.value().contains("summary")) {
            summary = summary_resp.value()["summary"].get<std::string>();
        } else if (summary_resp.value().contains("content")) {
            summary = summary_resp.value()["content"].get<std::string>();
        } else {
            summary = summary_resp.value().dump();
        }
    }

    // Generate community name from summary via LLM
    std::vector<Message> name_messages = {
        {"system", "You are a helpful assistant that describes provided contents in a single sentence."},
        {"user", std::format(
            R"(Create a short one sentence description of the summary that explains what kind of information is summarized.
The description must be under 250 characters.

Summary:
{})", summary)}
    };

    std::string name;
    auto name_resp = llm.generate_response(name_messages, std::nullopt, ModelSize::small);
    if (name_resp.has_value()) {
        if (name_resp.value().is_string()) {
            name = name_resp.value().get<std::string>();
        } else if (name_resp.value().contains("description")) {
            name = name_resp.value()["description"].get<std::string>();
        } else if (name_resp.value().contains("content")) {
            name = name_resp.value()["content"].get<std::string>();
        } else {
            name = name_resp.value().dump();
        }
    }

    if (name.empty()) name = "Community";

    auto now = std::chrono::system_clock::now();

    // Generate name embedding
    std::optional<std::vector<float>> name_embedding;
    try {
        name_embedding = embedder.create(name);
    } catch (...) {}

    CommunityNode community{
        .uuid = uuid::generate(),
        .name = name,
        .group_id = std::string(group_id),
        .created_at = now,
        .name_embedding = std::move(name_embedding),
        .summary = summary,
    };

    // Create HAS_MEMBER edges for all entities in cluster
    std::vector<CommunityEdge> edges;
    edges.reserve(cluster.size());
    for (auto& entity : cluster) {
        edges.push_back(CommunityEdge{
            .uuid = uuid::generate(),
            .group_id = std::string(group_id),
            .source_node_uuid = community.uuid,
            .target_node_uuid = entity.uuid,
            .created_at = now,
        });
    }

    return std::make_pair(std::move(community), std::move(edges));
}

// ============================================================================
// Determine entity community
// ============================================================================

Result<std::optional<std::pair<CommunityNode, bool>>> determine_entity_community(
    KuzuDriver& driver,
    std::string_view entity_uuid
) {
    // Check if entity already has a community
    auto existing = driver.get_entity_community(entity_uuid);
    if (!existing.has_value()) return std::unexpected(existing.error());

    if (existing.value().has_value()) {
        return std::optional(std::make_pair(std::move(existing.value().value()), false));
    }

    // Find the mode community among neighbors
    auto neighbors = driver.get_neighbor_communities(entity_uuid);
    if (!neighbors.has_value()) return std::unexpected(neighbors.error());

    if (neighbors.value().empty()) {
        return std::optional<std::pair<CommunityNode, bool>>(std::nullopt);
    }

    // Count community occurrences (mode finding)
    std::unordered_map<std::string, int> counts;
    for (auto& c : neighbors.value()) {
        counts[c.uuid]++;
    }

    // Find most common
    std::string best_uuid;
    int best_count = 0;
    for (auto& [uuid, count] : counts) {
        if (count > best_count) {
            best_count = count;
            best_uuid = uuid;
        }
    }

    // Return the most common neighbor community
    for (auto& c : neighbors.value()) {
        if (c.uuid == best_uuid) {
            return std::optional(std::make_pair(std::move(c), false));
        }
    }

    return std::optional<std::pair<CommunityNode, bool>>(std::nullopt);
}

// ============================================================================
// Update community for an entity
// ============================================================================

Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> update_community(
    KuzuDriver& driver,
    LLMClient& llm,
    EmbedderClient& embedder,
    const EntityNode& entity
) {
    std::vector<CommunityNode> result_nodes;
    std::vector<CommunityEdge> result_edges;

    auto community_result = determine_entity_community(driver, entity.uuid);
    if (!community_result.has_value()) return std::unexpected(community_result.error());

    if (community_result.value().has_value()) {
        auto& [community, is_new] = community_result.value().value();

        if (!is_new) {
            // Entity should join existing community
            auto now = std::chrono::system_clock::now();
            CommunityEdge edge{
                .uuid = uuid::generate(),
                .group_id = entity.group_id,
                .source_node_uuid = community.uuid,
                .target_node_uuid = entity.uuid,
                .created_at = now,
            };
            auto save_result = driver.save_community_edge(edge);
            if (!save_result.has_value()) return std::unexpected(save_result.error());

            result_nodes.push_back(std::move(community));
            result_edges.push_back(std::move(edge));
        }
    }
    // If no community found, entity stays unassigned until the next
    // full community rebuild (build_communities).

    return std::make_pair(std::move(result_nodes), std::move(result_edges));
}

} // namespace graphiti::pipeline
