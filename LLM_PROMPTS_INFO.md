# LLM Prompts Reference

Every LLM call Graphiti makes, what the model sees, and where each variable comes from.

All prompts return structured JSON via OpenAI's structured output. The response schemas are defined in `cpp_refactor/src/llm/response_models.h`.

---

# Extract Message (Entity Extraction — Conversational)

Used when `EpisodeType::message`. The richest extraction path — gives the LLM conversation history and speaker parsing rules.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `entity_types` | JSON array of `{entity_type_id, entity_type_name, entity_type_description}` | `TypeDefinitions::entity_types_prompt_json()` or default `[{id:0, name:"Entity"}]` |
| `previous_episodes` | JSON array of `{content: "..."}` for up to 10 prior episodes | `driver.retrieve_episodes(group_id, reference_time, 10, source)` |
| `episode_content` | The raw episode body text, e.g. `"Alice: I work at Acme Corp"` | `episode_body` parameter from `add_episode()` |
| `custom_instructions` | Optional user-provided extraction guidance | `custom_instructions` parameter from `add_episode()` |

**System message:**
```
You are an AI assistant that extracts entity nodes from conversational messages.
Your primary task is to extract and classify the speaker and other significant entities
mentioned in the conversation.
Do not escape unicode characters.
```

**User message:**
```
<ENTITY TYPES>
${JSON array of entity type definitions with id, name, description}
</ENTITY TYPES>

<PREVIOUS MESSAGES>
${JSON array of prior episode contents for context}
</PREVIOUS MESSAGES>

<CURRENT MESSAGE>
${The episode body text, e.g. "Alice: I just started working at Acme Corp."}
</CURRENT MESSAGE>

Instructions:

You are given a conversation context and a CURRENT MESSAGE. Your task is to extract
**entity nodes** mentioned **explicitly or implicitly** in the CURRENT MESSAGE.
Pronoun references such as he/she/they or this/that/those should be disambiguated to
the names of the reference entities. Only extract distinct entities from the CURRENT
MESSAGE. Don't extract pronouns like you, me, he/she/they, we/us as entities.

1. **Speaker Extraction**: Always extract the speaker (the part before the colon `:`
   in each dialogue line) as the first entity node.
   - If the speaker is mentioned again in the message, treat both mentions as a
     **single entity**.

2. **Entity Identification**:
   - Extract all significant entities, concepts, or actors that are **explicitly or
     implicitly** mentioned in the CURRENT MESSAGE.
   - **Exclude** entities mentioned only in the PREVIOUS MESSAGES (they are for
     context only).

3. **Entity Classification**:
   - Use the descriptions in ENTITY TYPES to classify each extracted entity.
   - Assign the appropriate `entity_type_id` for each one.

4. **Exclusions**:
   - Do NOT extract entities representing relationships or actions.
   - Do NOT extract dates, times, or other temporal information--these will be
     handled separately.

5. **Formatting**:
   - Be **explicit and unambiguous** in naming entities (e.g., use full names
     when available).

${Optional custom extraction instructions}
```

**LLM responds with:** `{"extracted_entities": [{"name": "...", "entity_type_id": 0}, ...]}`

---

# Extract Text (Entity Extraction — Prose)

Used when `EpisodeType::text`. Simpler than message — no conversation history, no speaker extraction.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `entity_types` | JSON array of `{entity_type_id, entity_type_name, entity_type_description}` | `TypeDefinitions::entity_types_prompt_json()` or default `[{id:0, name:"Entity"}]` |
| `episode_content` | The raw episode body text | `episode_body` parameter from `add_episode()` |
| `custom_instructions` | Optional user-provided extraction guidance | `custom_instructions` parameter from `add_episode()` |

**System message:**
```
You are an AI assistant that extracts entity nodes from text.
Your primary task is to extract and classify the speaker and other significant entities
mentioned in the provided text.
Do not escape unicode characters.
```

**User message:**
```
<ENTITY TYPES>
${JSON array of entity type definitions with id, name, description}
</ENTITY TYPES>

<TEXT>
${The episode body text, e.g. "Acme Corp hired Alice as a software engineer."}
</TEXT>

Given the above text, extract entities from the TEXT that are explicitly or implicitly
mentioned. For each entity extracted, also determine its entity type based on the
provided ENTITY TYPES and their descriptions. Indicate the classified entity type by
providing its entity_type_id.

${Optional custom extraction instructions}

Guidelines:
1. Extract significant entities, concepts, or actors mentioned in the conversation.
2. Avoid creating nodes for relationships or actions.
3. Avoid creating nodes for temporal information like dates, times or years (these
   will be added to edges later).
4. Be as explicit as possible in your node names, using full names and avoiding
   abbreviations.
```

**LLM responds with:** `{"extracted_entities": [{"name": "...", "entity_type_id": 0}, ...]}`

---

# Extract JSON (Entity Extraction — Structured Data)

Used when `EpisodeType::json`. The only extraction path that sends `source_description` to the LLM, because raw JSON is meaningless without knowing what it represents.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `entity_types` | JSON array of `{entity_type_id, entity_type_name, entity_type_description}` | `TypeDefinitions::entity_types_prompt_json()` or default `[{id:0, name:"Entity"}]` |
| `source_description` | Human-readable description of what the JSON represents, e.g. `"Spotify play history"` | `source_description` parameter from `add_episode()` → `episode.source_description` |
| `episode_content` | The raw JSON string | `episode_body` parameter from `add_episode()` |
| `custom_instructions` | Optional user-provided extraction guidance | `custom_instructions` parameter from `add_episode()` |

**System message:**
```
You are an AI assistant that extracts entity nodes from JSON.
Your primary task is to extract and classify relevant entities from JSON files
Do not escape unicode characters.
```

**User message:**
```
<ENTITY TYPES>
${JSON array of entity type definitions with id, name, description}
</ENTITY TYPES>

<SOURCE DESCRIPTION>:
${What the JSON represents, e.g. "Spotify play history" or "Slack API webhook payload"}
</SOURCE DESCRIPTION>
<JSON>
${The raw JSON content, e.g. {"user": "alice", "track": "Blue Monday", "artist": "New Order"}}
</JSON>

${Optional custom extraction instructions}

Given the above source description and JSON, extract relevant entities from the
provided JSON. For each entity extracted, also determine its entity type based on
the provided ENTITY TYPES and their descriptions. Indicate the classified entity type
by providing its entity_type_id.

Guidelines:
1. Extract all entities that the JSON represents. This will often be something like
   a "name" or "user" field
2. Extract all entities mentioned in all other properties throughout the JSON structure
3. Do NOT extract any properties that contain dates
```

**LLM responds with:** `{"extracted_entities": [{"name": "...", "entity_type_id": 0}, ...]}`

---

# Extract Edges (Relationship/Fact Extraction)

Runs after entity extraction. Same prompt regardless of `EpisodeType`. Extracts factual relationships between the entities found in the previous step.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `previous_episodes` | JSON array of prior episode contents | `driver.retrieve_episodes()` |
| `episode_content` | The raw episode body text | `episode_body` parameter from `add_episode()` |
| `nodes` | JSON array of `{name: "..."}` for each extracted entity | Output of `extract_nodes()` (after dedup) |
| `reference_time` | ISO 8601 timestamp, e.g. `"2024-06-01T00:00:00Z"` | `reference_time` parameter from `add_episode()`, converted via `datetime::to_iso8601()` |
| `edge_types` | Optional JSON array of custom edge/fact type definitions | `TypeDefinitions::edge_types_prompt_json()` if custom types defined |
| `custom_instructions` | Optional user-provided extraction guidance | `custom_instructions` parameter from `add_episode()` |

**System message:**
```
You are an expert fact extractor that extracts fact triples from text.
1. Extracted fact triples should also be extracted with relevant date information.
2. Treat the CURRENT TIME as the time the CURRENT MESSAGE was sent.
All temporal information should be extracted relative to this time.
Do not escape unicode characters.
```

**User message:**
```
<PREVIOUS_MESSAGES>
${JSON array of prior episode contents}
</PREVIOUS_MESSAGES>

<CURRENT_MESSAGE>
${The episode body text}
</CURRENT_MESSAGE>

<ENTITIES>
${JSON array of entity names extracted in the previous step}
</ENTITIES>

<REFERENCE_TIME>
${ISO 8601 timestamp for resolving relative dates like "last week"}
</REFERENCE_TIME>

${If custom edge types are defined:}
<FACT_TYPES>
${JSON array of edge type definitions}
</FACT_TYPES>

# TASK
Extract all factual relationships between the given ENTITIES based on the CURRENT MESSAGE.
Only extract facts that:
- involve two DISTINCT ENTITIES from the ENTITIES list,
- are clearly stated or unambiguously implied in the CURRENT MESSAGE,
    and can be represented as edges in a knowledge graph.
- Facts should include entity names rather than pronouns whenever possible.

You may use information from the PREVIOUS MESSAGES only to disambiguate references
or support continuity.

${Optional custom extraction instructions}

# EXTRACTION RULES

1. **Entity Name Validation**: `source_entity_name` and `target_entity_name` must
   use only the `name` values from the ENTITIES list provided above.
   - **CRITICAL**: Using names not in the list will cause the edge to be rejected
2. Each fact must involve two **distinct** entities.
3. Do not emit duplicate or semantically redundant facts.
4. The `fact` should closely paraphrase the original source sentence(s). Do not
   verbatim quote the original text.
5. Use `REFERENCE_TIME` to resolve vague or relative temporal expressions
   (e.g., "last week").
6. Do **not** hallucinate or infer temporal bounds from unrelated events.

# RELATION TYPE RULES

- If FACT_TYPES are provided and the relationship matches one of the types
  (considering the entity type signature), use that fact_type_name as the
  `relation_type`.
- Otherwise, derive a `relation_type` from the relationship predicate in
  SCREAMING_SNAKE_CASE (e.g., WORKS_AT, LIVES_IN, IS_FRIENDS_WITH).

# DATETIME RULES

- Use ISO 8601 with "Z" suffix (UTC) (e.g., 2025-04-30T00:00:00Z).
- If the fact is ongoing (present tense), set `valid_at` to REFERENCE_TIME.
- If a change/termination is expressed, set `invalid_at` to the relevant timestamp.
- Leave both fields `null` if no explicit or resolvable time is stated.
- If only a date is mentioned (no time), assume 00:00:00.
- If only a year is mentioned, use January 1st at 00:00:00.
```

**LLM responds with:**
```json
{"edges": [{"source_entity_name": "...", "target_entity_name": "...", "relation_type": "WORKS_AT", "fact": "...", "valid_at": "...", "invalid_at": null}, ...]}
```

---

# Dedupe Node (Single Node Deduplication)

After extracting entities, each one is checked against existing graph entities to see if it's a duplicate. Called once per extracted node.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `previous_episodes` | JSON array of prior episode contents | `driver.retrieve_episodes()` |
| `episode_content` | The raw episode body text | `episode_body` parameter from `add_episode()` |
| `extracted_node` | JSON `{id: N, name: "..."}` for the new entity | Built from the `extract_nodes()` output |
| `entity_type_description` | Description of the entity type (currently empty string) | Passed as `""` in current implementation |
| `existing_nodes` | JSON array of `{name, summary}` for candidate matches | `driver.search_entity_nodes_bm25(node.name, group_id, 10)` |

**System message:**
```
You are a helpful assistant that determines whether or not a NEW ENTITY
is a duplicate of any EXISTING ENTITIES.
Do not escape unicode characters.
```

**User message:**
```
<PREVIOUS MESSAGES>
${JSON array of prior episode contents}
</PREVIOUS MESSAGES>
<CURRENT MESSAGE>
${The episode body text}
</CURRENT MESSAGE>
<NEW ENTITY>
${JSON object: {id: 0, name: "Alice"}}
</NEW ENTITY>
<ENTITY TYPE DESCRIPTION>
${Description of the entity type, currently empty}
</ENTITY TYPE DESCRIPTION>

<EXISTING ENTITIES>
${JSON array of candidate matches: [{name: "Alice Smith", summary: "..."}, ...]}
</EXISTING ENTITIES>

Given the above EXISTING ENTITIES and their attributes, MESSAGE, and PREVIOUS MESSAGES;
Determine if the NEW ENTITY extracted from the conversation is a duplicate entity of
one of the EXISTING ENTITIES.

Entities should only be considered duplicates if they refer to the *same real-world
object or concept*.
Semantic Equivalence: if a descriptive label in existing_entities clearly refers to
a named entity in context, treat them as duplicates.

Do NOT mark entities as duplicates if:
- They are related but distinct.
- They have similar names or purposes but refer to separate instances or concepts.

TASK:
1. Compare the NEW ENTITY against each entity in EXISTING ENTITIES.
2. If it refers to the same real-world object or concept, identify the matching
   entity by name.

Only use names that appear in EXISTING ENTITIES, and return empty string when unsure.
```

**LLM responds with:**
```json
{"entity_resolutions": [{"id": 0, "name": "Alice Smith", "duplicate_name": "Alice Smith"}]}
```
(`duplicate_name` is empty string if no match)

---

# Resolve Edge (Edge Deduplication / Contradiction)

After extracting edges, each new edge is compared against existing edges between the same two nodes. The LLM determines if the new fact is a duplicate or contradicts existing facts.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `existing_edges` | Numbered list of existing facts, e.g. `"idx 0: Alice works at Acme\n"` | `driver.get_edges_between_nodes(source_uuid, target_uuid)` |
| `edge_invalidation_candidates` | Additional facts that might be invalidated (currently empty) | Currently `""` in implementation |
| `new_edge` | The new fact, e.g. `"fact: Alice left Acme Corp"` | Built from the `extract_edges()` output |

**System message:**
```
You are a helpful assistant that de-duplicates facts from fact lists and determines
which existing facts are contradicted by the new fact.
Do not escape unicode characters.
```

**User message:**
```
Task:
You will receive TWO lists of facts with CONTINUOUS idx numbering across both lists.
EXISTING FACTS are indexed first, followed by FACT INVALIDATION CANDIDATES.

1. DUPLICATE DETECTION:
   - If the NEW FACT represents identical factual information as any fact in
     EXISTING FACTS, return those idx values in duplicate_facts.
   - Facts with similar information that contain key differences should NOT be
     marked as duplicates.
   - If no duplicates, return an empty list for duplicate_facts.

2. CONTRADICTION DETECTION:
   - Determine which facts the NEW FACT contradicts from either list.
   - A fact from EXISTING FACTS can be both a duplicate AND contradicted (e.g.,
     semantically the same but the new fact updates/supersedes it).
   - Return all contradicted idx values in contradicted_facts.
   - If no contradictions, return an empty list for contradicted_facts.

IMPORTANT:
- duplicate_facts: ONLY idx values from EXISTING FACTS (cannot include FACT
  INVALIDATION CANDIDATES)
- contradicted_facts: idx values from EITHER list (EXISTING FACTS or FACT
  INVALIDATION CANDIDATES)
- The idx values are continuous across both lists (INVALIDATION CANDIDATES start
  where EXISTING FACTS end)

Guidelines:
1. Some facts may be very similar but will have key differences, particularly around
   numeric values. Do not mark these as duplicates.

<EXISTING FACTS>
${Numbered list: "idx 0: Alice works at Acme Corp\nidx 1: Alice is a software engineer\n"}
</EXISTING FACTS>

<FACT INVALIDATION CANDIDATES>
${Additional facts to check for contradictions, currently empty}
</FACT INVALIDATION CANDIDATES>

<NEW FACT>
${The new fact to compare: "fact: Alice left Acme Corp in January 2025"}
</NEW FACT>
```

**LLM responds with:** `{"duplicate_facts": [], "contradicted_facts": [0]}`

---

# Extract Summaries (Batch Entity Summarization)

After extraction and dedup, generates or updates a summary for each entity based on what was learned from the episode.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `previous_episodes` | JSON array of prior episode contents | `driver.retrieve_episodes()` |
| `episode_content` | The raw episode body text | `episode_body` parameter from `add_episode()` |
| `entities` | JSON array of `{name, summary}` for entities needing summaries | Built from the deduplicated `EntityNode` objects |

**System message:**
```
You are a helpful assistant that generates concise entity summaries from provided context.
Do not escape unicode characters.
```

**User message:**
```
Given the MESSAGES and a list of ENTITIES, generate an updated summary for each entity
that needs one. Each summary must be under 500 characters.

Guidelines:
    1. Output only factual content. Never explain what you're doing, why, or mention
       limitations/constraints.
    2. Only use the provided messages, entity, and entity context to set attribute values.
    3. Keep the summary concise and to the point. STATE FACTS DIRECTLY IN UNDER 250
       CHARACTERS.

    Example summaries:
    BAD: "This is the only activity in the context. The user listened to this song.
          No other details were provided to include in this summary."
    GOOD: "User played 'Blue Monday' by New Order (electronic genre) on 2024-12-03
           at 14:22 UTC."
    BAD: "Based on the messages provided, the user attended a meeting. This summary
          focuses on that event as it was the main topic discussed."
    GOOD: "User attended Q3 planning meeting with sales team on March 15."
    BAD: "The context shows John ordered pizza. Due to length constraints, other
          details are omitted from this summary."
    GOOD: "John ordered pepperoni pizza from Mario's at 7:30 PM, delivered to office."

<MESSAGES>
${JSON array of prior episode contents}
${The current episode body text}
</MESSAGES>

<ENTITIES>
${JSON array: [{name: "Alice", summary: "existing summary or empty"}, ...]}
</ENTITIES>

For each entity, combine relevant information from the MESSAGES with any existing
summary content. Only return summaries for entities that have meaningful information
to summarize. If an entity has no relevant information in the messages and no existing
summary, you may skip it.
```

**LLM responds with:** `{"summaries": [{"name": "Alice", "summary": "Software engineer at Acme Corp since 2024."}, ...]}`

---

# Community Summarization

Used during `build_communities()` and `update_communities()`. Summarizes clusters of related entities using tree-based pairwise reduction.

## Summarize Pair

Combines two summaries into one (<250 chars). Called repeatedly in a binary-tree pattern until one summary remains per community.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `left` | First summary text | Entity summary from community cluster |
| `right` | Second summary text | Entity summary from community cluster |

**System message:**
```
You are a helpful assistant that combines summaries.
```

**User message:**
```
Synthesize the information from the following two summaries into a single succinct summary.

IMPORTANT: Keep the summary concise and to the point. SUMMARIES MUST BE LESS THAN 250 CHARACTERS.

Respond with a JSON object with a single key "summary" containing your summary.

Summaries:
[{"summary": "${first entity/community summary}"}, {"summary": "${second entity/community summary}"}]
```

**LLM responds with:** `{"summary": "..."}`

## Summary Description

Generates a one-sentence label for a community based on its summary.

| Variable | What it contains | Where it comes from |
|---|---|---|
| `summary` | The community's combined summary text | Output of pairwise summarization |

**System message:**
```
You are a helpful assistant that describes provided contents in a single sentence.
Do not escape unicode characters.
```

**User message:**
```
Create a short one sentence description of the summary that explains what kind of
information is summarized. The description must be under 250 characters.

Respond with a JSON object with a single key "description" containing your description.

Summary:
${The community summary text}
```

**LLM responds with:** `{"description": "..."}`

---

# Pipeline Order

When you call `add_episode()`, these LLM calls happen in this order:

1. **Extract Entities** — `extract_message`, `extract_text`, or `extract_json` (based on `EpisodeType`)
2. **Dedupe Nodes** — `dedupe_node` for each extracted entity vs existing graph
3. **Extract Edges** — `extract_edges` to find relationships between entities
4. **Dedupe Edges** — `resolve_edge` for each new edge vs existing edges
5. **Enrich Summaries** — `extract_summaries_batch` to update entity summaries
6. **Extract Attributes** — (if custom types with fields) per-entity attribute extraction
7. **Community Update** — (if `update_communities=true`) `summarize_pair` + `summary_description`
