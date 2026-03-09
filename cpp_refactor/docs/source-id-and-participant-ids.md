# source_id and participant_ids — Who Said It, Who Was There

## The Three Attribution Dimensions

Every episode (and the entities/edges extracted from it) now carries three attribution dimensions:

| Field | Type | Meaning | Example |
|-------|------|---------|---------|
| `agent_id` | `string` | **Who recorded this** — the AI agent ingesting the memory | `"my-assistant"` |
| `source_id` | `string` | **Who said it** — the person/entity who produced the content | `"alice"` |
| `participant_ids` | `string[]` | **Who was there** — everyone present in the conversation | `["alice", "bob"]` |

### Why Three?

Consider a Slack channel where your assistant is watching:

- **agent_id = "my-assistant"** — the assistant is recording everything
- **source_id = "alice"** — Alice sent this particular message
- **participant_ids = ["alice", "bob", "carol"]** — Alice, Bob, and Carol are in the channel

This lets you later ask: "What did Alice say?" (filter by source_id), "What was discussed when Bob was present?" (filter by participant_ids), or "What did my assistant record?" (filter by agent_id).

## How to Submit Data

### Single Episode

```cpp
auto result = g.add_episode(
    "ep1",                          // name
    "Alice mentioned project Alpha.", // content
    "slack message",                // source_description
    now,                            // reference_time
    EpisodeType::message,           // source type
    "my-project",                   // group_id
    "my-assistant",                 // agent_id
    "alice",                        // source_id
    {"alice", "bob"}                // participant_ids
);
```

### Bulk Episodes

Each `RawEpisode` can specify its own `source_id` and `participant_ids`:

```cpp
std::vector<RawEpisode> episodes = {
    {
        .name = "msg1",
        .content = "Alice: Hey Bob, let's use Kuzu!",
        .source_description = "slack",
        .reference_time = now,
        .source_id = "alice",
        .participant_ids = {"alice", "bob"},
    },
    {
        .name = "msg2",
        .content = "Bob: Sounds good, I'll set it up.",
        .source_description = "slack",
        .reference_time = now + std::chrono::seconds(30),
        .source_id = "bob",
        .participant_ids = {"alice", "bob"},
    },
};

auto result = g.add_episode_bulk(
    episodes,
    "my-project",     // group_id
    "my-assistant"    // agent_id (applies to all)
    // source_id and participant_ids come from each RawEpisode
);
```

You can also set batch-level defaults that apply when a RawEpisode doesn't specify its own:

```cpp
auto result = g.add_episode_bulk(
    episodes,
    "my-project",       // group_id
    "my-assistant",     // agent_id
    "default-source",   // source_id (fallback if RawEpisode.source_id is empty)
    {"everyone"},       // participant_ids (fallback if RawEpisode.participant_ids is empty)
);
```

## How It's Stored in Kuzu

### Episodic Node (the episode itself)

```
Episodic table:
  agent_id STRING DEFAULT ''         -- singular: who recorded
  source_id STRING DEFAULT ''        -- singular: who said it
  participant_ids STRING[] DEFAULT [] -- plural: who was present
```

### Entity Node (extracted entities like "Alice", "Acme Corp")

```
Entity table:
  agent_ids STRING[] DEFAULT []       -- accumulated from all episodes mentioning this entity
  source_ids STRING[] DEFAULT []      -- accumulated from all episodes mentioning this entity
  participant_ids STRING[] DEFAULT [] -- accumulated from all episodes mentioning this entity
```

### RelatesToNode_ (edge facts like "Alice works at Acme")

```
RelatesToNode_ table:
  agent_ids STRING[] DEFAULT []
  source_ids STRING[] DEFAULT []
  participant_ids STRING[] DEFAULT []
```

### MENTIONS Edges (Episodic -> Entity)

```
MENTIONS rel table:
  agent_id STRING DEFAULT ''
  source_id STRING DEFAULT ''
  participant_ids STRING[] DEFAULT []
```

### Community Node

```
Community table:
  agent_ids STRING[] DEFAULT []
  source_ids STRING[] DEFAULT []
  participant_ids STRING[] DEFAULT []
```

### Key Design: Singular vs Plural

- **Episodes** store singular `source_id` and `agent_id` — each episode has exactly one source and one recorder
- **Entities and edges** accumulate plural `source_ids`, `agent_ids`, `participant_ids` — because the same entity can be mentioned by different sources across different conversations

When entity deduplication merges a new mention with an existing entity, the new source_id and participant_ids are appended to the existing arrays (no duplicates).

## Searching with Filters

```cpp
SearchFilters filters;

// Only facts mentioned by Alice
filters.source_ids = {"alice"};

// Only facts from conversations Bob was in
filters.participant_ids = {"bob"};

// Combine with agent filter
filters.agent_ids = {"my-assistant"};

auto results = g.search("project Alpha", "my-project", 10, filters);
```

Each filter generates a Kuzu Cypher overlap query:
```cypher
any(sid IN e.source_ids WHERE list_contains(['alice'], sid))
AND any(pid IN e.participant_ids WHERE list_contains(['bob'], pid))
```

All filters are ANDed together.
