# Design: Multi-Agent Attribution for Graphiti

## Problem

We ingest conversation turns from multiple agents working on multiple projects. Each turn has an `agent_name` and a `project`. We need to filter the knowledge graph by both dimensions independently and in combination.

Currently, `group_id` maps to `project`. Agent attribution is lost at ingestion time. There is no way to query "what does Agent X know?" or "what did Agent X contribute to Project Y?" without doubling LLM costs by ingesting every turn under two separate groups.

## Goal

Support two independent filter dimensions — **project** and **agent** — on all knowledge graph data, with the ability to query any combination:

| Query | projects param | agents param |
|-------|---------------|-------------|
| Everything in the entire graph | NULL | NULL |
| Everything for Project X | ["X"] | NULL |
| Everything for Agent A | NULL | ["A"] |
| Agent A in Project X | ["X"] | ["A"] |
| Agent A in Project X or Y | ["X", "Y"] | ["A"] |
| Agents A and B | NULL | ["A", "B"] |
| Agents A and B in Project X | ["X"] | ["A", "B"] |
| Agents A and B in Projects X and Y | ["X", "Y"] | ["A", "B"] |

## Design Decisions

### `group_id` stays as-is (= project)

`group_id` remains the primary partition key, mapped to project. This preserves:
- Existing behavior for all current users
- Deduplication boundaries (entities dedup within same project, not across)
- FalkorDB/Neo4j multi-database semantics (unchanged)
- All existing queries that filter on `group_id`

### `agent_id` is a new, independent dimension

A new `agent_ids: STRING[]` field is added to entity nodes and edges. This is a list because:
- Entity dedup can merge two nodes originally from different agents
- Edge dedup can merge two facts from different agents
- The list accumulates attribution over time

Episodes get a single `agent_id: str` since each episode comes from exactly one agent.

### Dedup behavior

> **CRITICAL: Dedup scoping stays on `group_id` (project), NOT `agent_id`.**
>
> Two agents saying the same fact about the same entity in the same project MUST merge, not create duplicate nodes/edges. The dedup search queries (BM25 + embedding lookup for candidate matches) must continue to filter on `group_id` only. Do NOT add `agent_id` filtering to any dedup query. Agent attribution is purely additive metadata — it never affects whether two things are considered duplicates.

When nodes or edges are deduped:
- The surviving node/edge's `agent_ids` list is extended with the incoming agent_id (deduplicated)
- No change to dedup logic itself — just merge the attribution lists
- The dedup candidate search remains scoped to `group_id` only

### Kuzu array support (verified)

Kuzu supports `STRING[]` columns and provides:
- `list_contains(list, element) -> BOOL` — single element membership
- `list_has_all(list, list) -> BOOL` — subset check
- `any(var IN list WHERE condition)` — overlap predicate (no `list_has_any`, use this instead)

The overlap query pattern:
```cypher
WHERE any(aid IN n.agent_ids WHERE list_contains($agents, aid))
```

---

## Changes Required

### 1. API Changes

#### `add_episode` signature

```python
# BEFORE
async def add_episode(
    self,
    ...
    group_id: str | None = None,
    ...
) -> AddEpisodeResults:

# AFTER
async def add_episode(
    self,
    ...
    group_id: str | None = None,
    agent_id: str | None = None,       # NEW — which agent produced this episode
    ...
) -> AddEpisodeResults:
```

When `agent_id` is None, default to empty string `""` (no agent attribution). Validate with same rules as `group_id` (ASCII alphanumeric + dashes + underscores).

#### `search` signature

```python
# BEFORE
async def search(
    self,
    query: str,
    ...
    group_ids: list[str] | None = None,
    ...
) -> SearchResults:

# AFTER
async def search(
    self,
    query: str,
    ...
    group_ids: list[str] | None = None,
    agent_ids: list[str] | None = None,   # NEW — filter by contributing agents
    ...
) -> SearchResults:
```

When `agent_ids` is None, no agent filtering is applied (returns results from all agents).

### 2. Schema Changes (Kuzu)

File: `graphiti_core/driver/kuzu_driver.py`

Add `agent_id STRING` or `agent_ids STRING[]` to every table that currently has `group_id`:

| Table | Type | Change |
|-------|------|--------|
| `Episodic` | NODE | Add `agent_id STRING` (single — one agent per episode) |
| `Entity` | NODE | Add `agent_ids STRING[]` (list — accumulates through dedup) |
| `Community` | NODE | Add `agent_ids STRING[]` (inherits from member entities) |
| `RelatesToNode_` | NODE | Add `agent_ids STRING[]` (list — accumulates through dedup) |
| `MENTIONS` | REL | Add `agent_id STRING` (single — from the episode's agent) |
| `HAS_MEMBER` | REL | No change needed (community membership is structural) |
| `Saga` | NODE | No change needed (sagas are structural containers) |
| `HAS_EPISODE` | REL | No change needed (structural) |
| `NEXT_EPISODE` | REL | No change needed (structural) |

**Migration**: Existing databases need an `ALTER TABLE` to add the new columns with default values:
```cypher
ALTER TABLE Episodic ADD agent_id STRING DEFAULT '';
ALTER TABLE Entity ADD agent_ids STRING[] DEFAULT [];
ALTER TABLE Community ADD agent_ids STRING[] DEFAULT [];
ALTER TABLE RelatesToNode_ ADD agent_ids STRING[] DEFAULT [];
ALTER TABLE MENTIONS ADD agent_id STRING DEFAULT '';
```

### 3. Model Changes

#### `graphiti_core/nodes.py`

`EpisodicNode`: Add `agent_id: str = ''`
`EntityNode`: Add `agent_ids: list[str] = []`
`CommunityNode`: Add `agent_ids: list[str] = []`

#### `graphiti_core/edges.py`

`EntityEdge`: Add `agent_ids: list[str] = []` (this maps to `RelatesToNode_`)
`EpisodicEdge`: Add `agent_id: str = ''` (MENTIONS edge)

### 4. Save/Load Changes

Every save params dict and record parser needs updating:

#### Save params (write path)

Files in `graphiti_core/driver/kuzu/operations/`:

| File | Change |
|------|--------|
| `episode_node_ops.py` | Add `'agent_id': node.agent_id` to save params |
| `entity_node_ops.py` | Add `'agent_ids': node.agent_ids` to save params |
| `entity_edge_ops.py` | Add `'agent_ids': edge.agent_ids` to save params |
| `episodic_edge_ops.py` | Add `'agent_id': edge.agent_id` to save params |
| `community_node_ops.py` | Add `'agent_ids': node.agent_ids` to save params |

#### Record parsers (read path)

Files in `graphiti_core/nodes.py` and `graphiti_core/edges.py`:

Every `from_record` / deserialization site needs to read the new field from DB records. Look for existing `group_id` deserialization patterns and add the corresponding `agent_id`/`agent_ids` field beside it.

### 5. Pipeline Changes

#### `graphiti_core/graphiti.py` — `add_episode` flow

1. Accept `agent_id` parameter, validate it, default to `""`
2. When creating `EpisodicNode`: set `agent_id = agent_id`
3. When creating `EpisodicEdge` (MENTIONS): set `agent_id = agent_id`
4. Pass `agent_id` through to extraction and dedup pipeline functions

#### `graphiti_core/utils/maintenance/node_operations.py` — Node dedup

When deduplicating nodes:
- If the existing node's `agent_ids` doesn't contain the current `agent_id`, append it
- Use `list(set(existing.agent_ids + [agent_id]))` to merge and deduplicate

#### `graphiti_core/utils/maintenance/edge_operations.py` — Edge dedup

Same pattern as node dedup:
- Surviving edge accumulates `agent_ids` from the incoming edge
- `list(set(existing.agent_ids + new_edge.agent_ids))` to merge

#### Entity extraction (`extract_nodes`, `extract_edges`)

New entities and edges created during extraction should be initialized with `agent_ids = [agent_id]` where `agent_id` comes from the current episode.

### 6. Search Changes

#### Search query construction

Every search function that currently appends a `group_id` filter needs an optional `agent_ids` filter too.

**Kuzu-specific** (`graphiti_core/driver/kuzu/operations/search_ops.py`):

For each of the ~8 search methods that currently do:
```python
if group_ids is not None:
    group_filter_query += '\nAND e.group_id IN $group_ids'
    filter_params['group_ids'] = group_ids
```

Add:
```python
if agent_ids is not None:
    group_filter_query += '\nAND any(aid IN e.agent_ids WHERE list_contains($agent_ids, aid))'
    filter_params['agent_ids'] = agent_ids
```

Note: For `Episodic` table queries, use `e.agent_id IN $agent_ids` (single value, not list overlap).

**Non-Kuzu drivers** (`search_utils.py`, Neo4j/FalkorDB ops):

Same pattern — add optional `agent_ids` parameter and append WHERE clause. For Neo4j/FalkorDB, the syntax would be different (e.g., `ANY(aid IN e.agent_ids WHERE aid IN $agent_ids)`).

#### Search function signatures

All search utility functions that accept `group_ids` should also accept `agent_ids`:

- `edge_fulltext_search`
- `edge_similarity_search`
- `edge_bfs_search`
- `node_fulltext_search`
- `node_similarity_search`
- `node_bfs_search`
- `episode_fulltext_search`
- `community_fulltext_search`
- `community_similarity_search`
- `get_relevant_nodes`

#### BFS join conditions

Currently: `WHERE n.group_id = origin.group_id`

This should stay as-is. BFS traversal respects project boundaries. Agent filtering is applied to the final result set, not during traversal. (You want to find graph-connected facts within a project, then filter by agent attribution.)

### 7. Validation

File: `graphiti_core/helpers.py`

Add `validate_agent_id()` with same rules as `validate_group_id()`:
```python
def validate_agent_id(agent_id: str | None) -> bool:
    if not agent_id:
        return True
    if not re.match(r'^[a-zA-Z0-9_-]+$', agent_id):
        raise AgentIdValidationError(agent_id)
    return True
```

### 8. MCP Server / REST API

If the MCP server or REST API exposes `add_episode` and `search`, their DTOs need the new parameters:

- `add_episode` endpoint: accept optional `agent_id` field
- `search` endpoint: accept optional `agent_ids` field

---

## What This Does NOT Change

- **Dedup boundaries**: Still scoped to `group_id` (project). Entities from different projects remain separate.
- **Saga behavior**: Sagas remain structural containers, unaffected by agent attribution.
- **Community clustering**: Runs per `group_id`. Communities could optionally inherit `agent_ids` from their member entities, but this is not required for the core use case.
- **FalkorDB/Neo4j multi-database**: The `group_id` → database mapping is unchanged. Agent attribution is purely a column-level filter.
- **Existing users**: `agent_id` defaults to `""` / `[]`. All existing queries continue to work. Agent filtering is opt-in.

---

## Testing Plan

1. **Unit tests**: Verify `agent_ids` round-trips through save/load for all node and edge types
2. **Dedup tests**: Verify agent_ids accumulate correctly when nodes/edges are deduped
3. **Search tests**:
   - Search with `agent_ids=None` returns everything (backward compatible)
   - Search with `agent_ids=["A"]` returns only Agent A's contributions
   - Search with `agent_ids=["A", "B"]` returns contributions from either
   - Search with both `group_ids` and `agent_ids` returns the intersection
4. **Integration test**: Ingest episodes from 2 agents in 2 projects, verify all 8 query combinations from the goal table
