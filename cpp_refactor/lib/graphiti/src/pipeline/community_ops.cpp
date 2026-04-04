#include "community_ops.h"

#include <graphiti/graph_store.h>
#include "llm/response_models.h"
#include "utils/uuid.h"

#include <graphiti/embedder.h>
#include <graphiti/llm_client.h>
#include <graphiti/callsite_log.h>

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
// Helpers: LLM summarization
// ============================================================================

namespace {

// Extract a string value from an LLM JSON response, trying common key names.
std::string extract_string_from_response(const nlohmann::json& resp) {
    if (resp.is_string()) return resp.get<std::string>();

    // Try common keys the model might use
    for (auto& key : {"summary", "description", "content", "text", "result",
                       "name", "output", "response"}) {
        if (resp.contains(key) && resp[key].is_string()) {
            return resp[key].get<std::string>();
        }
    }

    // Last resort: take the first string value from the object
    if (resp.is_object()) {
        for (auto& [k, v] : resp.items()) {
            if (v.is_string()) return v.get<std::string>();
        }
    }

    return {};
}

// Summarize a pair of summaries into one (< 250 chars).
// Matches Python's summarize_pair().
std::string summarize_pair(LLMClient& llm, std::string_view left, std::string_view right) {
    std::vector<Message> messages = {
        {"system", "You are a helpful assistant that combines summaries."},
        {"user", std::format(
            R"(Synthesize the information from the following two summaries into a single succinct summary.

IMPORTANT: Keep the summary concise and to the point. SUMMARIES MUST BE LESS THAN 250 CHARACTERS.

Summaries:
[{{"summary": "{}"}}, {{"summary": "{}"}}])", left, right)}
    };

    constexpr int MAX_RETRIES = 2;
    llm.prompt_name = "summarize_pair";
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        auto resp = llm.generate_response(messages, response_schemas::SUMMARY, ModelSize::small);
        if (resp.has_value()) {
            auto result = extract_string_from_response(resp.value());
            if (!result.empty()) return result;
        }
        if (attempt < MAX_RETRIES) {
            fprintf(stderr, "  [graphiti] community summary failed (attempt %d/%d), retrying\n",
                    attempt + 1, MAX_RETRIES + 1);
            messages.push_back({"user", "The previous response was invalid. Please try again with valid JSON."});
        }
    }
    // Fallback: concatenate
    return std::format("{} {}", left, right);
}

// Generate a short one-sentence description of a summary.
// Matches Python's generate_summary_description().
std::string generate_summary_description(LLMClient& llm, std::string_view summary) {
    std::vector<Message> messages = {
        {"system", "You are a helpful assistant that describes provided contents in a single sentence."},
        {"user", std::format(
            R"(Create a short one sentence description of the summary that explains what kind of information is summarized.
Summaries must be under 250 characters.

Summary:
{})", summary)}
    };

    constexpr int MAX_RETRIES = 2;
    llm.prompt_name = "generate_summary_description";
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        auto resp = llm.generate_response(messages, response_schemas::SUMMARY_DESCRIPTION, ModelSize::small);
        if (resp.has_value()) {
            auto result = extract_string_from_response(resp.value());
            if (!result.empty()) return result;
        }
        if (attempt < MAX_RETRIES) {
            fprintf(stderr, "  [graphiti] community description failed (attempt %d/%d), retrying\n",
                    attempt + 1, MAX_RETRIES + 1);
            messages.push_back({"user", "The previous response was invalid. Please try again with valid JSON."});
        }
    }
    return "Community";
}

} // anonymous namespace

// ============================================================================
// Get community clusters via label propagation
// ============================================================================

Result<std::vector<std::vector<EntityNode>>> get_community_clusters(
    GraphStore& store,
    const std::vector<std::string>& group_ids
) {
    // Resolve group_ids: if empty, query all distinct group_ids
    std::vector<std::string> resolved_groups = group_ids;
    if (resolved_groups.empty()) {
        graphiti::log_callsite("community-get-all-groups");
        auto all_groups = store.get_all_group_ids();
        if (!all_groups.has_value()) return std::unexpected(all_groups.error());
        resolved_groups = std::move(all_groups.value());
    }

    std::vector<std::vector<EntityNode>> community_clusters;

    for (auto& gid : resolved_groups) {
        // Get all entities in this group
        graphiti::log_callsite("community-get-entities-for-clustering");
        auto nodes_result = store.get_entities_by_group(gid);
        if (!nodes_result.has_value()) return std::unexpected(nodes_result.error());
        auto& nodes = nodes_result.value();

        if (nodes.empty()) continue;

        // Build weighted projection: node_uuid -> neighbors
        std::unordered_map<std::string, std::vector<Neighbor>> projection;

        for (auto& node : nodes) {
            graphiti::log_callsite("community-get-entity-neighbors");
            auto neighbors_result = store.get_entity_neighbors(node.uuid, gid);
            if (!neighbors_result.has_value()) return std::unexpected(neighbors_result.error());

            std::vector<Neighbor> pipeline_neighbors;
            for (auto& n : neighbors_result.value()) {
                pipeline_neighbors.push_back({n.node_uuid, n.edge_count});
            }
            projection[node.uuid] = std::move(pipeline_neighbors);
        }

        // Run label propagation to get UUID clusters
        auto cluster_uuids = label_propagation(projection);

        // Resolve UUID clusters back to EntityNode objects
        for (auto& uuid_cluster : cluster_uuids) {
            graphiti::log_callsite("community-resolve-cluster-nodes");
            auto entity_result = store.get_entities(uuid_cluster);
            if (!entity_result.has_value()) return std::unexpected(entity_result.error());
            if (!entity_result.value().empty()) {
                community_clusters.push_back(std::move(entity_result.value()));
            }
        }
    }

    return community_clusters;
}

// ============================================================================
// Remove all communities
// ============================================================================

VoidResult remove_communities(GraphStore& store) {
    graphiti::log_callsite("community-remove-all");
    return store.remove_all_communities();
}

// ============================================================================
// Build community from entity cluster via LLM (tree-based summarization)
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
    std::vector<std::string> summaries;
    summaries.reserve(cluster.size());
    for (auto& entity : cluster) {
        summaries.push_back(entity.summary.empty() ? entity.name : entity.summary);
    }

    // Tree-based pairwise summarization (matches Python's build_community)
    while (summaries.size() > 1) {
        std::string odd_one_out;
        bool has_odd = false;

        if (summaries.size() % 2 == 1) {
            odd_one_out = std::move(summaries.back());
            summaries.pop_back();
            has_odd = true;
        }

        auto half = summaries.size() / 2;
        std::vector<std::string> new_summaries;
        new_summaries.reserve(half + (has_odd ? 1 : 0));

        for (size_t i = 0; i < half; ++i) {
            new_summaries.push_back(
                summarize_pair(llm, summaries[i], summaries[half + i])
            );
        }

        if (has_odd) {
            new_summaries.push_back(std::move(odd_one_out));
        }

        summaries = std::move(new_summaries);
    }

    std::string summary = summaries[0];

    // Generate community name from summary via LLM
    std::string name = generate_summary_description(llm, summary);
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
// Build all communities (full orchestration)
// ============================================================================

Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> build_communities(
    GraphStore& store,
    LLMClient& llm,
    EmbedderClient& embedder,
    const std::vector<std::string>& group_ids
) {
    // Step 1: Remove old communities
    auto remove_result = remove_communities(store);
    if (!remove_result.has_value()) return std::unexpected(remove_result.error());

    // Step 2: Get community clusters via label propagation
    auto clusters_result = get_community_clusters(store, group_ids);
    if (!clusters_result.has_value()) return std::unexpected(clusters_result.error());

    std::vector<CommunityNode> all_nodes;
    std::vector<CommunityEdge> all_edges;

    // Step 3: Build each community via LLM
    for (auto& cluster : clusters_result.value()) {
        if (cluster.empty()) continue;

        auto community_result = build_community(llm, embedder, cluster, cluster[0].group_id);
        if (!community_result.has_value()) continue; // Skip failed clusters

        auto& [node, edges] = community_result.value();

        // Step 4: Persist community node (with optional embedding) and edges
        graphiti::log_callsite("community-save-node");
        auto save_node = store.persist_community(node, node.name_embedding);
        if (!save_node.has_value()) continue;

        for (auto& edge : edges) {
            graphiti::log_callsite("community-save-member-edge");
            (void)store.persist_community_membership(edge);
        }

        all_nodes.push_back(std::move(node));
        all_edges.insert(all_edges.end(),
            std::make_move_iterator(edges.begin()),
            std::make_move_iterator(edges.end()));
    }

    return std::make_pair(std::move(all_nodes), std::move(all_edges));
}

// ============================================================================
// Determine entity community
// ============================================================================

Result<std::optional<std::pair<CommunityNode, bool>>> determine_entity_community(
    GraphStore& store,
    std::string_view entity_uuid
) {
    // Check if entity already has a community
    graphiti::log_callsite("community-check-existing-membership");
    auto existing = store.get_entity_community(entity_uuid);
    if (!existing.has_value()) return std::unexpected(existing.error());

    if (existing.value().has_value()) {
        return std::optional(std::make_pair(std::move(existing.value().value()), false));
    }

    // Find the mode community among neighbors
    graphiti::log_callsite("community-find-neighbor-communities");
    auto neighbors = store.get_neighbor_communities(entity_uuid);
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

    // Return the most common neighbor community (is_new=true: entity needs to be added)
    for (auto& c : neighbors.value()) {
        if (c.uuid == best_uuid) {
            return std::optional(std::make_pair(std::move(c), true));
        }
    }

    return std::optional<std::pair<CommunityNode, bool>>(std::nullopt);
}

// ============================================================================
// Update community for an entity
// ============================================================================

Result<std::pair<std::vector<CommunityNode>, std::vector<CommunityEdge>>> update_community(
    GraphStore& store,
    LLMClient& llm,
    EmbedderClient& embedder,
    const EntityNode& entity
) {
    std::vector<CommunityNode> result_nodes;
    std::vector<CommunityEdge> result_edges;

    auto community_result = determine_entity_community(store, entity.uuid);
    if (!community_result.has_value()) return std::unexpected(community_result.error());

    if (!community_result.value().has_value()) {
        // No community found — entity stays unassigned until next full rebuild
        return std::make_pair(std::move(result_nodes), std::move(result_edges));
    }

    auto& [community, is_new] = community_result.value().value();

    // Update community summary by merging entity summary with community summary
    std::string new_summary = summarize_pair(llm, entity.summary, community.summary);
    std::string new_name = generate_summary_description(llm, new_summary);

    community.summary = new_summary;
    community.name = new_name;

    // If entity is new to this community, create HAS_MEMBER edge
    if (is_new) {
        auto now = std::chrono::system_clock::now();
        CommunityEdge edge{
            .uuid = uuid::generate(),
            .group_id = entity.group_id,
            .source_node_uuid = community.uuid,
            .target_node_uuid = entity.uuid,
            .created_at = now,
        };
        graphiti::log_callsite("community-update-add-member-edge");
        auto save_edge = store.persist_community_membership(edge);
        if (!save_edge.has_value()) return std::unexpected(save_edge.error());
        result_edges.push_back(std::move(edge));
    }

    // Regenerate name embedding
    std::optional<std::vector<float>> name_embedding;
    try {
        name_embedding = embedder.create(community.name);
    } catch (...) {}
    community.name_embedding = std::move(name_embedding);

    // Save updated community (with optional embedding)
    graphiti::log_callsite("community-update-save-node");
    auto save_node = store.persist_community(community, community.name_embedding);
    if (!save_node.has_value()) return std::unexpected(save_node.error());

    result_nodes.push_back(std::move(community));

    return std::make_pair(std::move(result_nodes), std::move(result_edges));
}

} // namespace graphiti::pipeline
