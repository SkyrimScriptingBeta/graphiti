# Multi-Agent Attribution (agent_id / agent_ids)

## Overview

`agent_id` is a second, independent filter dimension alongside `group_id`. It tracks which agent produced or contributed to each piece of knowledge in the graph.

- `group_id` = project partition (dedup boundary, database routing)
- `agent_id` = agent attribution (additive metadata, no effect on dedup)

## Data Model

| Element | Field | Type | Semantics |
|---------|-------|------|-----------|
| EpisodicNode | `agent_id` | `str` | Single agent that produced this episode |
| EpisodicEdge | `agent_id` | `str` | Inherited from the episode |
| EntityNode | `agent_ids` | `list[str]` | All agents that contributed to this entity |
| EntityEdge | `agent_ids` | `list[str]` | All agents that contributed to this edge |
| CommunityNode | `agent_ids` | `list[str]` | All agents that contributed to this community |

Episodes have a single agent (one agent per turn). Entities, edges, and communities accumulate agents over time through dedup merges.

## Defaults & Backward Compatibility

- `agent_id` defaults to `''` (empty string)
- `agent_ids` defaults to `[]` (empty list)
- All existing code works unchanged — the fields are invisible unless you use them
- Search with `agent_ids=None` returns everything (no filtering)

## Validation

`agent_id` follows the same regex as `group_id`: `^[a-zA-Z0-9_-]+$`

Empty/None values are allowed (they mean "no agent attribution").

Validation error: `AgentIdValidationError` in `graphiti_core/errors.py`.
Validation function: `validate_agent_id()` in `graphiti_core/helpers.py`.

## API Usage

### Ingestion

```python
await graphiti.add_episode(
    name="conversation turn",
    episode_body="Alice said hello",
    source_description="chat",
    reference_time=datetime.now(UTC),
    group_id="project-alpha",
    agent_id="agent-1",        # <-- new parameter
)
```

The `agent_id` flows through the pipeline:
1. Set on the `EpisodicNode`
2. Propagated to `EpisodicEdge` via `build_episodic_edges()`
3. Propagated to new `EntityNode.agent_ids` via `_create_entity_nodes()`
4. Propagated to new `EntityEdge.agent_ids` via `extract_edges()`

### Dedup Merge

When a new entity or edge resolves to an existing one (dedup match), `agent_ids` accumulate:

```python
# Node dedup (in resolve_extracted_nodes)
resolved_node.agent_ids = list(set(resolved_node.agent_ids + extracted_node.agent_ids))

# Edge dedup (in resolve_extracted_edge, both fast path and LLM path)
resolved_edge.agent_ids = list(set(resolved_edge.agent_ids + extracted_edge.agent_ids))
```

This means if Agent A and Agent B both mention "Alice", the resulting EntityNode for Alice will have `agent_ids=["agent-a", "agent-b"]`.

### Search

```python
# Search everything (default)
results = await graphiti.search("hello", group_ids=["project-alpha"])

# Search only Agent 1's contributions
results = await graphiti.search(
    "hello",
    group_ids=["project-alpha"],
    agent_ids=["agent-1"],       # <-- new parameter
)

# Search multiple agents
results = await graphiti.search(
    "hello",
    group_ids=["project-alpha"],
    agent_ids=["agent-1", "agent-2"],
)

# Advanced search also supports it
results = await graphiti.search_(
    "hello",
    group_ids=["project-alpha"],
    agent_ids=["agent-1"],
)
```

When both `group_ids` and `agent_ids` are provided, results must match both (intersection).

## Kuzu Schema

These columns were added to the Kuzu DDL in `graphiti_core/driver/kuzu_driver.py`:

| Table | Column | Type |
|-------|--------|------|
| `Episodic` (node) | `agent_id` | `STRING` |
| `Entity` (node) | `agent_ids` | `STRING[]` |
| `Community` (node) | `agent_ids` | `STRING[]` |
| `RelatesToNode_` (node) | `agent_ids` | `STRING[]` |
| `MENTIONS` (rel) | `agent_id` | `STRING` |

Not added to: `Saga`, `HAS_MEMBER`, `HAS_EPISODE`, `NEXT_EPISODE`, `RELATES_TO` (structural elements, no agent attribution).

## Kuzu Search Queries

**Entities/Communities/Edges** (have `agent_ids STRING[]` — list overlap check):
```cypher
any(aid IN n.agent_ids WHERE list_contains($agent_ids, aid))
```

**Episodes** (have single `agent_id STRING` — membership check):
```cypher
e.agent_id IN $agent_ids
```

**BFS queries**: Agent filtering is applied to the result set, NOT the traversal constraints. The traversal still uses `group_id` boundaries.

## Scope

This implementation is **Kuzu only**. Other drivers (Neo4j, FalkorDB, Neptune) have the `agent_ids` parameter in their ABC/interface signatures but do not implement filtering logic. The parameter defaults to `None` and is ignored.

## Files Modified

### Validation
- `graphiti_core/errors.py` — `AgentIdValidationError`
- `graphiti_core/helpers.py` — `validate_agent_id()`

### Models
- `graphiti_core/nodes.py` — fields + record parsers + direct save paths
- `graphiti_core/edges.py` — fields + record parsers + direct save paths

### Kuzu Schema & Save
- `graphiti_core/driver/kuzu_driver.py` — DDL
- `graphiti_core/models/nodes/node_db_queries.py` — save/return queries
- `graphiti_core/models/edges/edge_db_queries.py` — save/return queries
- `graphiti_core/driver/kuzu/operations/episode_node_ops.py`
- `graphiti_core/driver/kuzu/operations/entity_node_ops.py`
- `graphiti_core/driver/kuzu/operations/entity_edge_ops.py`
- `graphiti_core/driver/kuzu/operations/episodic_edge_ops.py`
- `graphiti_core/driver/kuzu/operations/community_node_ops.py`

### Pipeline
- `graphiti_core/utils/maintenance/node_operations.py` — extraction + dedup merge
- `graphiti_core/utils/maintenance/edge_operations.py` — extraction + dedup merge

### Search
- `graphiti_core/driver/search_interface/search_interface.py` — base interface
- `graphiti_core/driver/operations/search_ops.py` — ABC
- `graphiti_core/driver/kuzu/operations/search_ops.py` — Kuzu filter queries
- `graphiti_core/search/search_utils.py` — plumbing
- `graphiti_core/search/search.py` — plumbing

### API
- `graphiti_core/graphiti.py` — `add_episode()`, `search()`, `search_()` signatures

## Record Parsing Gotcha

Kuzu can return `None` for columns that haven't been populated yet (e.g., on databases created before this feature). The record parsers use `or` instead of default args to handle this:

```python
agent_id=record.get('agent_id') or ''      # not record.get('agent_id', '')
agent_ids=record.get('agent_ids') or []    # not record.get('agent_ids', [])
```

The `.get(key, default)` pattern returns `None` when the key exists but has a `None` value. The `or` pattern handles both missing keys and explicit `None`.
