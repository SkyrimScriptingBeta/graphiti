#pragma once

#include <graphiti/llm_client.h>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace graphiti::prompts {

// Helper: serialize data as JSON string for prompt injection (preserves Unicode)
inline std::string to_prompt_json(const nlohmann::json& data) {
    return data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

constexpr std::string_view DO_NOT_ESCAPE_UNICODE = "\nDo not escape unicode characters.\n";

// ============================================================================
// Entity Extraction
// ============================================================================

// Extract entities from a conversational message (dialogue format)
std::vector<Message> extract_message(
    std::string_view entity_types,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    std::string_view custom_instructions = ""
);

// Extract entities from plain text
std::vector<Message> extract_text(
    std::string_view entity_types,
    std::string_view episode_content,
    std::string_view custom_instructions = ""
);

// Extract entities from JSON data (uses source_description for context)
std::vector<Message> extract_json(
    std::string_view entity_types,
    std::string_view source_description,
    std::string_view episode_content,
    std::string_view custom_instructions = ""
);

// ============================================================================
// Edge Extraction
// ============================================================================

// Extract relationship edges between entities
std::vector<Message> extract_edges(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& nodes,
    std::string_view reference_time,
    const nlohmann::json& edge_types = nlohmann::json(),
    std::string_view custom_instructions = ""
);

// ============================================================================
// Node Deduplication
// ============================================================================

// Single node deduplication
std::vector<Message> dedupe_node(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& extracted_node,
    std::string_view entity_type_description,
    const nlohmann::json& existing_nodes
);

// Batch node deduplication
std::vector<Message> dedupe_nodes(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& extracted_nodes,
    const nlohmann::json& existing_nodes
);

// ============================================================================
// Edge Deduplication
// ============================================================================

// Determine if new edge duplicates or contradicts existing edges
std::vector<Message> resolve_edge(
    std::string_view existing_edges,
    std::string_view edge_invalidation_candidates,
    std::string_view new_edge
);

// ============================================================================
// Summarization
// ============================================================================

// Extract summary for a single entity
std::vector<Message> extract_summary(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& node
);

// Batch entity summary extraction
std::vector<Message> extract_summaries_batch(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& entities
);

// Generate a one-sentence description of a summary
std::vector<Message> summary_description(std::string_view summary);

} // namespace graphiti::prompts
