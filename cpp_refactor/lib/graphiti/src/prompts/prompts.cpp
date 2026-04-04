#include "prompts.h"
#include "prompt_loader.h"

#include <format>
#include <string_view>

using namespace std::literals;

namespace graphiti::prompts {

static constexpr std::string_view SUMMARY_INSTRUCTIONS = R"(Guidelines:
        1. Output only factual content. Never explain what you're doing, why, or mention limitations/constraints.
        2. Only use the provided messages, entity, and entity context to set attribute values.
        3. Keep the summary concise and to the point. STATE FACTS DIRECTLY IN UNDER 250 CHARACTERS.

        Example summaries:
        BAD: "This is the only activity in the context. The user listened to this song. No other details were provided to include in this summary."
        GOOD: "User played 'Blue Monday' by New Order (electronic genre) on 2024-12-03 at 14:22 UTC."
        BAD: "Based on the messages provided, the user attended a meeting. This summary focuses on that event as it was the main topic discussed."
        GOOD: "User attended Q3 planning meeting with sales team on March 15."
        BAD: "The context shows John ordered pizza. Due to length constraints, other details are omitted from this summary."
        GOOD: "John ordered pepperoni pizza from Mario's at 7:30 PM, delivered to office.")";

static constexpr int MAX_SUMMARY_CHARS = 500;

// ============================================================================
// Entity Extraction
// ============================================================================

// Variables: {0}=entity_types, {1}=previous_episodes_json, {2}=episode_content, {3}=custom_instructions
static constexpr std::string_view EXTRACT_MESSAGE_SYSTEM =
    "You are an AI assistant that extracts entity nodes from conversational messages. "
    "Your primary task is to extract and classify the speaker and other significant entities "
    "mentioned in the conversation.\nDo not escape unicode characters.\n";

static constexpr std::string_view EXTRACT_MESSAGE_USER =
    R"(<ENTITY TYPES>
{0}
</ENTITY TYPES>

<PREVIOUS MESSAGES>
{1}
</PREVIOUS MESSAGES>

<CURRENT MESSAGE>
{2}
</CURRENT MESSAGE>

Instructions:

You are given a conversation context and a CURRENT MESSAGE. Your task is to extract **entity nodes** mentioned **explicitly or implicitly** in the CURRENT MESSAGE.
Pronoun references such as he/she/they or this/that/those should be disambiguated to the names of the reference entities. Only extract distinct entities from the CURRENT MESSAGE. Don't extract pronouns like you, me, he/she/they, we/us as entities.

1. **Speaker Extraction**: Always extract the speaker (the part before the colon `:` in each dialogue line) as the first entity node.
   - If the speaker is mentioned again in the message, treat both mentions as a **single entity**.

2. **Entity Identification**:
   - Extract all significant entities, concepts, or actors that are **explicitly or implicitly** mentioned in the CURRENT MESSAGE.
   - **Exclude** entities mentioned only in the PREVIOUS MESSAGES (they are for context only).

3. **Entity Classification**:
   - Use the descriptions in ENTITY TYPES to classify each extracted entity.
   - Assign the appropriate `entity_type_id` for each one.

4. **Exclusions**:
   - Do NOT extract entities representing relationships or actions.
   - Do NOT extract dates, times, or other temporal information—these will be handled separately.

5. **Formatting**:
   - Be **explicit and unambiguous** in naming entities (e.g., use full names when available).

{3})";

std::vector<Message> extract_message(
    std::string_view entity_types,
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    std::string_view custom_instructions
) {
    auto sys_override = load_prompt_override("extract_message", "system");
    auto user_override = load_prompt_override("extract_message", "user");

    std::string sys = std::vformat(
        sys_override.value_or(std::string(EXTRACT_MESSAGE_SYSTEM)),
        std::make_format_args(entity_types)
    );

    auto prev_json = to_prompt_json(previous_episodes);
    std::string user = std::vformat(
        user_override.value_or(std::string(EXTRACT_MESSAGE_USER)),
        std::make_format_args(entity_types, prev_json, episode_content, custom_instructions)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// Variables: {0}=entity_types, {1}=episode_content, {2}=custom_instructions
std::vector<Message> extract_text(
    std::string_view entity_types,
    std::string_view episode_content,
    std::string_view custom_instructions
) {
    std::string sys = resolve_prompt("extract_text", "system",
        "You are an AI assistant that extracts entity nodes from text. "
        "Your primary task is to extract and classify the speaker and other significant entities "
        "mentioned in the provided text.\nDo not escape unicode characters.\n");

    std::string user = std::vformat(
        resolve_prompt("extract_text", "user",
        R"(<ENTITY TYPES>
{0}
</ENTITY TYPES>

<TEXT>
{1}
</TEXT>

Given the above text, extract entities from the TEXT that are explicitly or implicitly mentioned.
For each entity extracted, also determine its entity type based on the provided ENTITY TYPES and their descriptions.
Indicate the classified entity type by providing its entity_type_id.

{2}

Guidelines:
1. Extract significant entities, concepts, or actors mentioned in the conversation.
2. Avoid creating nodes for relationships or actions.
3. Avoid creating nodes for temporal information like dates, times or years (these will be added to edges later).
4. Be as explicit as possible in your node names, using full names and avoiding abbreviations.)"),
        std::make_format_args(entity_types, episode_content, custom_instructions)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// Variables: {0}=entity_types, {1}=source_description, {2}=episode_content, {3}=custom_instructions
std::vector<Message> extract_json(
    std::string_view entity_types,
    std::string_view source_description,
    std::string_view episode_content,
    std::string_view custom_instructions
) {
    std::string sys = resolve_prompt("extract_json", "system",
        "You are an AI assistant that extracts entity nodes from JSON. "
        "Your primary task is to extract and classify relevant entities from JSON files.\n"
        "Do not escape unicode characters.\n");

    std::string user = std::vformat(
        resolve_prompt("extract_json", "user",
        R"(<ENTITY TYPES>
{0}
</ENTITY TYPES>

<SOURCE DESCRIPTION>:
{1}
</SOURCE DESCRIPTION>
<JSON>
{2}
</JSON>

{3}

Given the above source description and JSON, extract relevant entities from the provided JSON.
For each entity extracted, also determine its entity type based on the provided ENTITY TYPES and their descriptions.
Indicate the classified entity type by providing its entity_type_id.

Guidelines:
1. Extract all entities that the JSON represents. This will often be something like a "name" or "user" field
2. Extract all entities mentioned in all other properties throughout the JSON structure
3. Do NOT extract any properties that contain dates)"),
        std::make_format_args(entity_types, source_description, episode_content, custom_instructions)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// ============================================================================
// Edge Extraction
// ============================================================================

std::vector<Message> extract_edges(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& nodes,
    std::string_view reference_time,
    const nlohmann::json& edge_types,
    std::string_view custom_instructions
) {
    std::string sys = resolve_prompt("extract_edges", "system",
        "You are an expert fact extractor that extracts fact triples from text. "
        "1. Extracted fact triples should also be extracted with relevant date information. "
        "2. Treat the CURRENT TIME as the time the CURRENT MESSAGE was sent. "
        "All temporal information should be extracted relative to this time.\n"
        "Do not escape unicode characters.\n");

    // Check for full user override — if present, it replaces the entire multi-part user template
    // Variables: {0}=previous_episodes, {1}=episode_content, {2}=entities, {3}=reference_time,
    //            {4}=edge_types (may be empty), {5}=custom_instructions
    auto user_override = load_prompt_override("extract_edges", "user");
    if (user_override) {
        auto prev_json = to_prompt_json(previous_episodes);
        auto nodes_json = to_prompt_json(nodes);
        auto etypes = (!edge_types.is_null() && !edge_types.empty()) ? to_prompt_json(edge_types) : std::string{};
        std::string user = std::vformat(*user_override,
            std::make_format_args(prev_json, episode_content, nodes_json, reference_time, etypes, custom_instructions));
        return {{"system", std::move(sys)}, {"user", std::move(user)}};
    }

    // Default: build user message in parts (compiled-in prompts)
    std::string user = std::format(
        R"(<PREVIOUS_MESSAGES>
{0}
</PREVIOUS_MESSAGES>

<CURRENT_MESSAGE>
{1}
</CURRENT_MESSAGE>

<ENTITIES>
{2}
</ENTITIES>

<REFERENCE_TIME>
{3}  # ISO 8601 (UTC); used to resolve relative time mentions
</REFERENCE_TIME>
)",
        to_prompt_json(previous_episodes),
        episode_content,
        to_prompt_json(nodes),
        reference_time
    );

    if (!edge_types.is_null() && !edge_types.empty()) {
        user += std::format("\n<FACT_TYPES>\n{}\n</FACT_TYPES>\n", to_prompt_json(edge_types));
    }

    user += std::format(
        R"(# TASK
Extract all factual relationships between the given ENTITIES based on the CURRENT MESSAGE.
Be thorough — extract every relationship between entities, including roles, responsibilities, dependencies, ownership, and process flows. Aim to capture all meaningful connections, not just the most obvious ones.
Only extract facts that:
- involve two DISTINCT ENTITIES from the ENTITIES list,
- are clearly stated or unambiguously implied in the CURRENT MESSAGE,
    and can be represented as edges in a knowledge graph.
- Facts should include entity names rather than pronouns whenever possible.

You may use information from the PREVIOUS MESSAGES only to disambiguate references or support continuity.


{0}

# EXTRACTION RULES

1. **Entity Name Validation**: `source_entity_name` and `target_entity_name` must use only the `name` values from the ENTITIES list provided above.
   - **CRITICAL**: Using names not in the list will cause the edge to be rejected
2. Each fact must involve two **distinct** entities.
3. Do not emit duplicate or semantically redundant facts.
4. The `fact` should closely paraphrase the original source sentence(s). Do not verbatim quote the original text.
5. Use `REFERENCE_TIME` to resolve vague or relative temporal expressions (e.g., "last week").
6. Do **not** hallucinate or infer temporal bounds from unrelated events.

# RELATION TYPE RULES

- If FACT_TYPES are provided and the relationship matches one of the types (considering the entity type signature), use that fact_type_name as the `relation_type`.
- Otherwise, prefer one of these standard relation types when they fit:
  HAS_ROLE, MEMBER_OF, LEADS, SAME_AS, IS_A, WORKS_WITH, DEVELOPS, DEPENDS_ON, IMPLEMENTS, BUILT_WITH, RESPONSIBLE_FOR, WRITES, REVIEWS, VALIDATES, USES, ATTACHES, REQUIRES, TRANSITIONS_TO, BLOCKS, UNBLOCKS, KICKED_OFF_BY, DISCOVERED, LEARNED_FROM, CONTRADICTS, SUPERSEDES, CAUSED_BY, FOLLOWS, VIOLATES, CITES, INTRODUCED_IN, REMOVED_IN, REPLACED_BY, CHANGED_FROM, PRODUCES
- If none of the above fit, derive a `relation_type` in SCREAMING_SNAKE_CASE.

# DATETIME RULES

- Use ISO 8601 with "Z" suffix (UTC) (e.g., 2025-04-30T00:00:00Z).
- If the fact is ongoing (present tense), set `valid_at` to REFERENCE_TIME.
- If a change/termination is expressed, set `invalid_at` to the relevant timestamp.
- Leave both fields `null` if no explicit or resolvable time is stated.
- If only a date is mentioned (no time), assume 00:00:00.
- If only a year is mentioned, use January 1st at 00:00:00.)",
        custom_instructions
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// ============================================================================
// Node Deduplication
// ============================================================================

// Variables: {0}=previous_episodes, {1}=episode_content, {2}=extracted_node, {3}=entity_type_description, {4}=existing_nodes
std::vector<Message> dedupe_node(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& extracted_node,
    std::string_view entity_type_description,
    const nlohmann::json& existing_nodes
) {
    std::string sys = resolve_prompt("dedupe_node", "system",
        "You are a helpful assistant that determines whether or not a NEW ENTITY "
        "is a duplicate of any EXISTING ENTITIES.\nDo not escape unicode characters.\n");

    auto prev_json = to_prompt_json(previous_episodes);
    auto node_json = to_prompt_json(extracted_node);
    auto existing_json = to_prompt_json(existing_nodes);
    auto user_override = load_prompt_override("dedupe_node", "user");
    std::string user;
    if (user_override) {
        user = std::vformat(*user_override,
            std::make_format_args(prev_json, episode_content, node_json, entity_type_description, existing_json));
    } else {
        user = std::vformat(
            R"(<PREVIOUS MESSAGES>
{0}
</PREVIOUS MESSAGES>
<CURRENT MESSAGE>
{1}
</CURRENT MESSAGE>
<NEW ENTITY>
{2}
</NEW ENTITY>
<ENTITY TYPE DESCRIPTION>
{3}
</ENTITY TYPE DESCRIPTION>

<EXISTING ENTITIES>
{4}
</EXISTING ENTITIES>

Given the above EXISTING ENTITIES and their attributes, MESSAGE, and PREVIOUS MESSAGES; Determine if the NEW ENTITY extracted from the conversation
is a duplicate entity of one of the EXISTING ENTITIES.

Entities should only be considered duplicates if they refer to the *same real-world object or concept*.
Semantic Equivalence: if a descriptive label in existing_entities clearly refers to a named entity in context, treat them as duplicates.

Do NOT mark entities as duplicates if:
- They are related but distinct.
- They have similar names or purposes but refer to separate instances or concepts.

 TASK:
 1. Compare the NEW ENTITY against each entity in EXISTING ENTITIES.
 2. If it refers to the same real-world object or concept, identify the matching entity by name.

Respond with a JSON object containing an "entity_resolutions" array with a single entry:
{{
    "entity_resolutions": [
        {{
            "id": integer id from NEW ENTITY,
            "name": the best full name for the entity,
            "duplicate_name": the name of the matching entity from EXISTING ENTITIES, or empty string if none
        }}
    ]
}}

Only use names that appear in EXISTING ENTITIES, and return empty string when unsure.)",
            std::make_format_args(prev_json, episode_content, node_json, entity_type_description, existing_json));
    }

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// Variables: {0}=previous_episodes, {1}=episode_content, {2}=extracted_nodes, {3}=existing_nodes, {4}=count, {5}=count-1
std::vector<Message> dedupe_nodes(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& extracted_nodes,
    const nlohmann::json& existing_nodes
) {
    auto count = extracted_nodes.size();
    auto count_minus_1 = count > 0 ? count - 1 : (size_t)0;

    std::string sys = resolve_prompt("dedupe_nodes", "system",
        "You are a helpful assistant that determines whether or not ENTITIES extracted "
        "from a conversation are duplicates of existing entities.\nDo not escape unicode characters.\n");

    auto prev_json = to_prompt_json(previous_episodes);
    auto nodes_json = to_prompt_json(extracted_nodes);
    auto existing_json = to_prompt_json(existing_nodes);
    auto user_override = load_prompt_override("dedupe_nodes", "user");
    std::string user;
    if (user_override) {
        user = std::vformat(*user_override,
            std::make_format_args(prev_json, episode_content, nodes_json, existing_json, count, count_minus_1));
    } else {
        user = std::vformat(
            R"(<PREVIOUS MESSAGES>
{0}
</PREVIOUS MESSAGES>
<CURRENT MESSAGE>
{1}
</CURRENT MESSAGE>


Each of the following ENTITIES were extracted from the CURRENT MESSAGE.
Each entity in ENTITIES is represented as a JSON object with the following structure:
{{
    id: integer id of the entity,
    name: "name of the entity",
    entity_type: ["Entity", "<optional additional label>", ...],
    entity_type_description: "Description of what the entity type represents"
}}

<ENTITIES>
{2}
</ENTITIES>

<EXISTING ENTITIES>
{3}
</EXISTING ENTITIES>

Each entry in EXISTING ENTITIES is an object with the following structure:
{{
    name: "name of the candidate entity",
    entity_types: ["Entity", "<optional additional label>", ...],
    ...<additional attributes such as summaries or metadata>
}}

For each of the above ENTITIES, determine if the entity is a duplicate of any of the EXISTING ENTITIES.

Entities should only be considered duplicates if they refer to the *same real-world object or concept*.

Do NOT mark entities as duplicates if:
- They are related but distinct.
- They have similar names or purposes but refer to separate instances or concepts.

Task:
ENTITIES contains {4} entities with IDs 0 through {5}.
Your response MUST include EXACTLY {4} resolutions with IDs 0 through {5}. Do not skip or add IDs.

For every entity, return an object with the following keys:
{{
    "id": integer id from ENTITIES,
    "name": the best full name for the entity (preserve the original name unless a duplicate has a more complete name),
    "duplicate_name": the name of the EXISTING ENTITY that is the best duplicate match, or empty string if there is no duplicate
}}

- Only use names that appear in EXISTING ENTITIES.
- Use empty string if there is no duplicate.
- Never fabricate entity names.)",
            std::make_format_args(prev_json, episode_content, nodes_json, existing_json, count, count_minus_1));
    }

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// ============================================================================
// Edge Deduplication
// ============================================================================

// Variables: {0}=existing_edges, {1}=edge_invalidation_candidates, {2}=new_edge
std::vector<Message> resolve_edge(
    std::string_view existing_edges,
    std::string_view edge_invalidation_candidates,
    std::string_view new_edge
) {
    std::string sys = resolve_prompt("resolve_edge", "system",
        "You are a helpful assistant that de-duplicates facts from fact lists and determines "
        "which existing facts are contradicted by the new fact.\nDo not escape unicode characters.\n");

    std::string user = std::vformat(
        resolve_prompt("resolve_edge", "user",
        R"(Task:
You will receive TWO lists of facts with CONTINUOUS idx numbering across both lists.
EXISTING FACTS are indexed first, followed by FACT INVALIDATION CANDIDATES.

1. DUPLICATE DETECTION:
   - If the NEW FACT represents identical factual information as any fact in EXISTING FACTS, return those idx values in duplicate_facts.
   - Facts with similar information that contain key differences should NOT be marked as duplicates.
   - If no duplicates, return an empty list for duplicate_facts.

2. CONTRADICTION DETECTION:
   - Determine which facts the NEW FACT contradicts from either list.
   - A fact from EXISTING FACTS can be both a duplicate AND contradicted (e.g., semantically the same but the new fact updates/supersedes it).
   - Return all contradicted idx values in contradicted_facts.
   - If no contradictions, return an empty list for contradicted_facts.

IMPORTANT:
- duplicate_facts: ONLY idx values from EXISTING FACTS (cannot include FACT INVALIDATION CANDIDATES)
- contradicted_facts: idx values from EITHER list (EXISTING FACTS or FACT INVALIDATION CANDIDATES)
- The idx values are continuous across both lists (INVALIDATION CANDIDATES start where EXISTING FACTS end)

Guidelines:
1. Some facts may be very similar but will have key differences, particularly around numeric values.
   Do not mark these as duplicates.

<EXISTING FACTS>
{0}
</EXISTING FACTS>

<FACT INVALIDATION CANDIDATES>
{1}
</FACT INVALIDATION CANDIDATES>

<NEW FACT>
{2}
</NEW FACT>)"),
        std::make_format_args(existing_edges, edge_invalidation_candidates, new_edge)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// ============================================================================
// Summarization
// ============================================================================

// Variables: {0}=max_chars, {1}=summary_instructions, {2}=previous_episodes, {3}=episode_content, {4}=node
std::vector<Message> extract_summary(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& node
) {
    std::string sys = resolve_prompt("extract_summary", "system",
        "You are a helpful assistant that extracts entity summaries from the provided text.\n"
        "Do not escape unicode characters.\n");

    auto prev_json = to_prompt_json(previous_episodes);
    auto node_json = to_prompt_json(node);
    std::string user = std::vformat(
        resolve_prompt("extract_summary", "user",
        R"(Given the MESSAGES and the ENTITY, update the summary that combines relevant information about the entity
from the messages and relevant information from the existing summary. Summary must be under {0} characters.

{1}

<MESSAGES>
{2}
{3}
</MESSAGES>

<ENTITY>
{4}
</ENTITY>)"),
        std::make_format_args(MAX_SUMMARY_CHARS, SUMMARY_INSTRUCTIONS, prev_json, episode_content, node_json)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// Variables: {0}=max_chars, {1}=summary_instructions, {2}=previous_episodes, {3}=episode_content, {4}=entities
std::vector<Message> extract_summaries_batch(
    const nlohmann::json& previous_episodes,
    std::string_view episode_content,
    const nlohmann::json& entities
) {
    std::string sys = resolve_prompt("extract_summaries_batch", "system",
        "You are a helpful assistant that generates concise entity summaries from provided context.\n"
        "Do not escape unicode characters.\n");

    auto prev_json = to_prompt_json(previous_episodes);
    auto entities_json = to_prompt_json(entities);
    std::string user = std::vformat(
        resolve_prompt("extract_summaries_batch", "user",
        R"(Given the MESSAGES and a list of ENTITIES, generate an updated summary for each entity that needs one.
Each summary must be under {0} characters.

{1}

<MESSAGES>
{2}
{3}
</MESSAGES>

<ENTITIES>
{4}
</ENTITIES>

For each entity, combine relevant information from the MESSAGES with any existing summary content.
Only return summaries for entities that have meaningful information to summarize.
If an entity has no relevant information in the messages and no existing summary, you may skip it.)"),
        std::make_format_args(MAX_SUMMARY_CHARS, SUMMARY_INSTRUCTIONS, prev_json, episode_content, entities_json)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

// Variables: {0}=summary
std::vector<Message> summary_description(std::string_view summary) {
    std::string sys = resolve_prompt("summary_description", "system",
        "You are a helpful assistant that describes provided contents in a single sentence.\n"
        "Do not escape unicode characters.\n");

    std::string user = std::vformat(
        resolve_prompt("summary_description", "user",
        R"(Create a short one sentence description of the summary that explains what kind of information is summarized.
Summaries must be under 250 characters.

Summary:
{0})"),
        std::make_format_args(summary)
    );

    return {{"system", std::move(sys)}, {"user", std::move(user)}};
}

} // namespace graphiti::prompts
