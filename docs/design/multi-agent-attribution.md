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

---

## Implementor's Guide to the Existing Test Suite

This section was added after the initial design review to help the implementing agent navigate the Python test suite.

### Test suite structure

```
tests/
  helpers_test.py                        # Shared fixtures: graph_driver, mock_embedder, assertion helpers
  test_graphiti_mock.py                  # ~24 tests — DB-level CRUD + search (real DB, mocked LLM)
  test_add_triplet.py                    # ~10 tests — add_triplet (real DB, mocked LLM)
  test_graphiti_int.py                   # 1 integration test (live LLM + live DB)
  test_entity_exclusion_int.py           # add_episode with exclusion (live LLM + live DB)
  test_edge_int.py / test_node_int.py    # Node/edge integration (live DB, mocked LLM)
  utils/
    maintenance/
      test_node_operations.py            # Node dedup — fully mocked, no DB, no LLM
      test_edge_operations.py            # Edge dedup — fully mocked
      test_entity_extraction.py          # extract_nodes, summarization — fully mocked
      test_bulk_utils.py                 # Bulk dedup logic — fully mocked
    search/
      search_utils_test.py              # hybrid_node_search — mocked driver
  llm_client/ embedder/ cross_encoder/  # Provider-specific tests
```

### Two tiers that matter for this work

**Tier 1 — Pure mocks, no infra needed (run anywhere):**
- `tests/utils/maintenance/test_node_operations.py` — dedup logic. **This is where you add the "dedup merges agent_ids" test.**
- `tests/utils/maintenance/test_edge_operations.py` — edge dedup. Same.
- `tests/utils/maintenance/test_entity_extraction.py` — extraction. Good reference for mocking `LLMClient`.

**Tier 2 — Real DB (Kuzu in-memory), mocked LLM:**
- `test_graphiti_mock.py` — DB round-trip for nodes/edges/search. **This is where you add the "agent_ids persists through save/load" test.**
- `test_add_triplet.py` — Good pattern reference for mocking LLM at `generate_response` level.

### How to run tests

```bash
# Establish baseline — run all non-integration tests against Kuzu only
DISABLE_NEO4J=1 DISABLE_FALKORDB=1 pytest tests/ -k "not _int" -m "not integration" --disable-warnings

# Run just the dedup tests (your most important ones)
pytest tests/utils/maintenance/test_node_operations.py tests/utils/maintenance/test_edge_operations.py -v

# Run DB-level tests against Kuzu in-memory
DISABLE_NEO4J=1 DISABLE_FALKORDB=1 pytest tests/test_graphiti_mock.py -v
```

Kuzu uses `:memory:` — no server, no Docker, no setup. Just `DISABLE_NEO4J=1 DISABLE_FALKORDB=1`.

### Patterns to copy

**For pure-unit dedup tests** (testing that agent_ids merge correctly during node dedup):
Copy the mock setup from `tests/utils/maintenance/test_node_operations.py`. It uses `AsyncMock(spec=LLMClient)` and `Mock(spec=GraphDriver)` — no real calls. The dedup functions take a `GraphitiClients` object which you construct with `GraphitiClients.model_construct(...)`.

**For DB round-trip tests** (testing that agent_ids persist to Kuzu and come back):
Copy the fixture signature from `test_add_triplet.py` or `test_graphiti_mock.py`. The `graph_driver` fixture from `helpers_test.py` auto-parametrizes across enabled backends.

```python
# Example: add to test_graphiti_mock.py
@pytest.mark.asyncio
async def test_entity_node_persists_agent_ids(graph_driver):
    node = EntityNode(
        name='Alice', group_id='project_x',
        agent_ids=['agent-a', 'agent-b'],  # NEW FIELD
    )
    await node.save(graph_driver)
    retrieved = await EntityNode.get_by_uuid(graph_driver, node.uuid)
    assert set(retrieved.agent_ids) == {'agent-a', 'agent-b'}
```

**For dedup-merges-attribution tests:**
```python
# Example: add to test_node_operations.py
# After dedup resolves two nodes as duplicates, the surviving node
# should have agent_ids from BOTH the existing and incoming node.
# Mock the LLM to return "these are duplicates" and verify the merge.
```

### The gap: no mocked add_episode test

The full `add_episode` pipeline is only tested in `_int` files that require a live `OPENAI_API_KEY`. There is no existing test that mocks the LLM and runs the full pipeline. If you want to test the end-to-end flow of `agent_id` threading through `add_episode` without an API key, you'll need to mock `LLMClient.generate_response` to return canned extraction/dedup responses. `test_add_triplet.py` is the closest existing pattern for this.

Alternatively: if you have an `OPENAI_API_KEY` available, the simplest full-pipeline test is to add a case to `test_graphiti_int.py` that passes `agent_id='test-agent'` to `add_episode` and asserts on the returned `AddEpisodeResults`.

### Recommended test strategy

1. **Before any changes**: Run the baseline (`pytest tests/ -k "not _int" -m "not integration"`) and confirm green.
2. **Add model tests**: Verify `agent_id`/`agent_ids` fields serialize and deserialize correctly on all affected types.
3. **Add dedup tests**: Verify `agent_ids` accumulates during node and edge dedup (pure mock tier).
4. **Add DB round-trip tests**: Verify Kuzu stores and retrieves `STRING[]` columns correctly (Kuzu in-memory tier).
5. **Add search filter test**: Verify `agent_ids` filtering works in at least one search function (DB tier with mocked LLM).
6. **After all changes**: Run the full baseline again to catch regressions.
7. **Optional**: If API key is available, add one integration test that ingests 2 episodes from different agents and searches with agent filtering.
