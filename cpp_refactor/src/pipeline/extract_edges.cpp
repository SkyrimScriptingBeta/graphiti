#include "extract_edges.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <graphiti/log.h>

#include <format>
#include <unordered_map>

namespace graphiti::pipeline {

// Extract edges for a single set of entities (no sharding).
static Result<ExtractedEdges> extract_edges_single(
    LLMClient& llm,
    const nlohmann::json& previous_episodes,
    const std::string& episode_content,
    const nlohmann::json& nodes_json,
    const std::string& reference_time,
    const nlohmann::json& edge_types,
    const std::string& custom_instructions,
    int max_edges
) {
    auto messages = prompts::extract_edges(
        previous_episodes, episode_content,
        nodes_json, reference_time,
        edge_types, custom_instructions
    );

    // Inject max edges cap into the last user message
    if (max_edges > 0 && !messages.empty()) {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == "user") {
                it->content += std::format(
                    "\n\n**IMPORTANT: Extract at most {} edges. After {}, close the JSON array and STOP.**",
                    max_edges, max_edges
                );
                break;
            }
        }
    }

    ExtractedEdges extracted;
    constexpr int MAX_EDGE_RETRIES = 2;
    llm.prompt_name = "extract_edges";
    for (int attempt = 0; attempt <= MAX_EDGE_RETRIES; ++attempt) {
        auto llm_result = llm.generate_response(
            messages, response_schemas::EXTRACTED_EDGES, ModelSize::small
        );
        if (!llm_result.has_value()) {
            return std::unexpected(llm_result.error());
        }

        auto raw_json = llm_result.value();
        try {
            extracted = raw_json.get<ExtractedEdges>();
            return extracted;
        } catch (const std::exception& e) {
            if (attempt < MAX_EDGE_RETRIES) {
                fprintf(stderr, "  [graphiti] edge parse failed (attempt %d/%d), retrying: %s\n",
                        attempt + 1, MAX_EDGE_RETRIES + 1, e.what());
                messages.push_back({
                    "user",
                    std::format(
                        "The previous response had malformed edges. Error: {}. "
                        "Please try again with valid JSON. Every edge must have: "
                        "source_entity_name, target_entity_name, relation_type, fact, valid_at, invalid_at.",
                        e.what()
                    )
                });
                continue;
            }
            return std::unexpected(GraphitiError{
                ErrorCode::llm_parse_error,
                std::format("Failed to parse extracted edges after {} attempts: {} — raw LLM output: {}",
                            MAX_EDGE_RETRIES + 1, e.what(), raw_json.dump())
            });
        }
    }
    return extracted;
}

Result<std::vector<EntityEdge>> extract_edges(
    LLMClient& llm,
    const ExtractEdgesInput& input
) {
    // Build node name -> UUID lookup (case-insensitive)
    // After dedup, node names may be lowercased (e.g. "Agentic" → "agentic")
    // but the edge extraction LLM uses the original casing, so we normalize.
    auto to_lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::unordered_map<std::string, std::string> name_to_uuid;
    for (auto& node : input.nodes) {
        name_to_uuid[to_lower(node.name)] = node.uuid;
    }

    // Build shards — split entities into groups of shard_size
    std::vector<nlohmann::json> shards;
    if (input.shard_size > 0 && static_cast<int>(input.nodes.size()) > input.shard_size) {
        nlohmann::json current_shard = nlohmann::json::array();
        for (auto& node : input.nodes) {
            current_shard.push_back({{"name", node.name}});
            if (static_cast<int>(current_shard.size()) >= input.shard_size) {
                shards.push_back(std::move(current_shard));
                current_shard = nlohmann::json::array();
            }
        }
        if (!current_shard.empty()) {
            shards.push_back(std::move(current_shard));
        }
        log_debug("[graphiti] 🔀 edge extraction: sharding %zu entities into %zu groups of ~%d\n",
                  input.nodes.size(), shards.size(), input.shard_size);
    } else {
        // No sharding — one group with all entities
        nlohmann::json all_nodes = nlohmann::json::array();
        for (auto& node : input.nodes) {
            all_nodes.push_back({{"name", node.name}});
        }
        shards.push_back(std::move(all_nodes));
    }

    // Compute max edges per shard
    // If max_edges is set, use it. Otherwise auto = 1.5x entity count per shard.
    auto compute_max_edges = [&](int entity_count) -> int {
        if (input.max_edges > 0) return input.max_edges;
        return static_cast<int>(entity_count * 1.5);
    };

    // Run edge extraction for each shard SERIALLY and merge results
    ExtractedEdges all_extracted;
    for (size_t i = 0; i < shards.size(); ++i) {
        int shard_entity_count = static_cast<int>(shards[i].size());
        int max_edges = compute_max_edges(shard_entity_count);

        if (shards.size() > 1) {
            log_debug("[graphiti]   shard %zu/%zu (%d entities, max %d edges)\n",
                      i + 1, shards.size(), shard_entity_count, max_edges);
        }

        auto result = extract_edges_single(
            llm, input.previous_episodes, input.episode_content,
            shards[i], input.reference_time,
            input.edge_types, input.custom_instructions,
            max_edges
        );

        if (!result.has_value()) {
            // Log but continue — don't fail the whole extraction for one bad shard
            fprintf(stderr, "  [graphiti] ⚠️ edge shard %zu/%zu failed: %s\n",
                    i + 1, shards.size(), result.error().message.c_str());
            continue;
        }

        for (auto& edge : result->edges) {
            all_extracted.edges.push_back(std::move(edge));
        }
    }

    if (shards.size() > 1) {
        log_debug("[graphiti] 🔀 edge sharding complete: %zu total edges from %zu shards\n",
                  all_extracted.edges.size(), shards.size());
    }

    // Convert to EntityEdge objects
    auto now = std::chrono::system_clock::now();
    std::vector<EntityEdge> edges;

    for (auto& ext : all_extracted.edges) {
        auto src_it = name_to_uuid.find(to_lower(ext.source_entity_name));
        auto tgt_it = name_to_uuid.find(to_lower(ext.target_entity_name));

        // Skip edges with unresolvable entity names
        if (src_it == name_to_uuid.end() || tgt_it == name_to_uuid.end()) {
            log_debug("[graphiti]   ⚠️ dropped edge: \"%s\" → \"%s\" (entity not found)\n",
                      ext.source_entity_name.c_str(), ext.target_entity_name.c_str());
            continue;
        }

        EntityEdge edge;
        edge.uuid = uuid::generate();
        edge.group_id = input.group_id;
        edge.source_node_uuid = src_it->second;
        edge.target_node_uuid = tgt_it->second;
        edge.name = ext.relation_type;
        edge.fact = std::move(ext.fact);
        edge.created_at = now;

        if (ext.valid_at.has_value()) {
            edge.valid_at = datetime::from_iso8601(ext.valid_at.value());
        }
        if (ext.invalid_at.has_value()) {
            edge.invalid_at = datetime::from_iso8601(ext.invalid_at.value());
        }

        edges.push_back(std::move(edge));
    }

    return edges;
}

} // namespace graphiti::pipeline
