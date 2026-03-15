# Add & Search API

All ingestion and search methods take a single options struct. Use C++20 designated initializers — skip any field that uses its default.

```cpp
#include <graphiti/graphiti.h>
```

## add_episode

Ingest a single episode. The LLM extracts entities and edges, deduplicates against the existing graph, and persists everything to Kuzu.

```cpp
struct AddEpisodeOptions {
    std::string name;                  // episode name
    std::string body;                  // the content to ingest
    std::string source_description;    // describes where this came from
    TimePoint   reference_time;        // when this happened
    EpisodeType source          = EpisodeType::message;  // message | json | text
    std::string group_id;              // partition key (default: "default")
    std::string agent_id;              // who recorded this
    std::string source_id;             // who said it
    std::string source_context;        // where it happened (e.g. "slack_general")
    std::vector<std::string>    participant_ids;          // who was present
    std::optional<std::string>  custom_instructions;      // extra LLM instructions
    std::optional<std::string>  saga;                     // saga name to link into
    std::optional<std::string>  saga_previous_episode_uuid; // explicit chain ordering
    bool                        update_communities = false;  // re-run community detection
    const TypeDefinitions*      type_defs = nullptr;         // custom entity/edge types
};

Result<AddEpisodeResult> add_episode(AddEpisodeOptions opts);
```

`AddEpisodeResult` contains the created episode, episodic edges (MENTIONS), extracted entity nodes, and extracted entity edges.

### Examples

Minimal:

```cpp
auto result = g.add_episode({
    .name = "chat_1",
    .body = "Alice just started working at Acme Corp as an engineer.",
    .source_description = "conversation",
    .reference_time = std::chrono::system_clock::now(),
});
```

With attribution:

```cpp
auto result = g.add_episode({
    .name = "slack_msg",
    .body = "Alice told Bob about the new Kuzu database.",
    .source_description = "slack message",
    .reference_time = now,
    .group_id = "team_chat",
    .agent_id = "recorder-agent",
    .source_id = "alice",
    .source_context = "slack_general",
    .participant_ids = {"alice", "bob"},
});
```

With saga and community updates:

```cpp
auto result = g.add_episode({
    .name = "onboarding_day1",
    .body = "Alice completed orientation and met the team.",
    .source_description = "HR system",
    .reference_time = now,
    .group_id = "hr",
    .saga = "alice_onboarding",
    .update_communities = true,
});
```

With custom entity types from YAML:

```cpp
auto defs = TypeDefinitions::from_yaml_file("types.yaml").value();
auto result = g.add_episode({
    .name = "ep1",
    .body = "Alice Smith works as an engineer at Acme Corp.",
    .source_description = "chat",
    .reference_time = now,
    .group_id = "typed_group",
    .type_defs = &defs,
});
```

## search

Simple hybrid search over edges (facts). Combines cosine similarity + BM25 with RRF reranking. Returns entity edges ranked by relevance.

```cpp
struct SearchOptions {
    std::string query;           // natural language query
    std::string group_id;        // partition key (default: "default")
    int         num_results = 10;
    std::optional<SearchFilters> filters;
};

Result<std::vector<EntityEdge>> search(SearchOptions opts);
```

Each `EntityEdge` has `source_node_uuid`, `target_node_uuid`, `name`, `fact`, temporal fields (`valid_at`, `invalid_at`, `created_at`, `expired_at`), and attribution arrays.

### Examples

Minimal:

```cpp
auto edges = g.search({.query = "Where does Alice work?"});
```

With group and filters:

```cpp
SearchFilters filters;
filters.source_ids = {"alice"};
filters.participant_ids = {"bob"};

auto edges = g.search({
    .query = "database recommendations",
    .group_id = "team_chat",
    .filters = filters,
});
```

With agent attribution filter:

```cpp
SearchFilters filters;
filters.agent_ids = {"scout-agent"};

auto edges = g.search({
    .query = "market trends",
    .group_id = "research",
    .num_results = 20,
    .filters = filters,
});
```

## search_advanced

Full-control search returning edges, nodes, episodes, and communities — each with scores. You pick the search methods, rerankers, and limits via `SearchConfig`.

```cpp
struct SearchAdvancedOptions {
    std::string  query;
    SearchConfig config;          // controls methods, rerankers, limits
    std::string  group_id;
    std::optional<SearchFilters>            filters;
    std::optional<std::string>              center_node_uuid;     // for BFS
    std::optional<std::vector<std::string>> bfs_origin_node_uuids; // BFS start points
};

Result<SearchResults> search_advanced(SearchAdvancedOptions opts);
```

`SearchResults` contains `edges` + `edge_scores`, `nodes` + `node_scores`, `episodes` + `episode_scores`, `communities` + `community_scores`.

### Pre-built recipes

14 recipe functions return ready-made `SearchConfig`s:

| Recipe | What it searches |
|--------|-----------------|
| `edge_hybrid_search_rrf()` | Edges only, RRF reranker |
| `edge_hybrid_search_mmr()` | Edges only, MMR diversity |
| `edge_hybrid_search_node_distance()` | Edges only, node distance reranker |
| `edge_hybrid_search_episode_mentions()` | Edges only, episode frequency reranker |
| `edge_hybrid_search_cross_encoder()` | Edges only, cross-encoder reranker |
| `node_hybrid_search_rrf()` | Nodes only, RRF |
| `node_hybrid_search_mmr()` | Nodes only, MMR |
| `node_hybrid_search_node_distance()` | Nodes only, node distance |
| `node_hybrid_search_episode_mentions()` | Nodes only, episode frequency |
| `community_hybrid_search_rrf()` | Communities only, RRF |
| `community_hybrid_search_mmr()` | Communities only, MMR |
| `combined_hybrid_search_rrf()` | Edges + nodes + episodes, RRF |
| `combined_hybrid_search_mmr()` | Edges + nodes + episodes, MMR |
| `combined_hybrid_search_cross_encoder()` | Edges + nodes + episodes, cross-encoder |

### Examples

With a recipe:

```cpp
auto results = g.search_advanced({
    .query = "Who works at Acme?",
    .config = combined_hybrid_search_rrf(),
    .group_id = "team",
});

for (size_t i = 0; i < results->edges.size(); ++i)
    fmt::println("  edge: {} (score: {})", results->edges[i].fact, results->edge_scores[i]);
for (size_t i = 0; i < results->communities.size(); ++i)
    fmt::println("  community: {} (score: {})", results->communities[i].summary, results->community_scores[i]);
```

With custom config:

```cpp
SearchConfig cfg;
cfg.edge_config = EdgeSearchConfig{
    .search_methods = {EdgeSearchMethod::cosine_similarity, EdgeSearchMethod::bm25},
    .reranker = Reranker::mmr,
    .mmr_lambda = 0.7f,
};
cfg.node_config = NodeSearchConfig{
    .search_methods = {NodeSearchMethod::cosine_similarity},
    .reranker = Reranker::rrf,
};
cfg.limit = 20;

auto results = g.search_advanced({
    .query = "Alice",
    .config = cfg,
    .group_id = "team",
});
```

## add_episode_bulk

Batch ingestion. Processes all episodes together with cross-deduplication within the batch. Skips edge invalidation and community updates for speed — call `build_communities()` afterward if needed.

```cpp
struct AddEpisodeBulkOptions {
    std::vector<RawEpisode> episodes;  // the batch
    std::string group_id;              // batch-level default
    std::string agent_id;              // batch-level default
    std::string source_id;             // batch-level default (per-episode overrides win)
    std::string source_context;        // batch-level default (per-episode overrides win)
    std::vector<std::string>   participant_ids;    // batch-level default (per-episode overrides win)
    std::optional<std::string> custom_instructions;
    std::optional<std::string> saga;               // link all episodes into this saga
    const TypeDefinitions*     type_defs = nullptr;
};

Result<AddBulkEpisodeResults> add_episode_bulk(AddEpisodeBulkOptions opts);
```

`RawEpisode` is:

```cpp
struct RawEpisode {
    std::string name;
    std::string content;
    std::string source_description;
    TimePoint   reference_time;
    EpisodeType source = EpisodeType::message;
    std::optional<std::string>  uuid;              // auto-generated if empty
    std::string source_id;                          // per-episode override
    std::string source_context;                     // per-episode override
    std::vector<std::string>    participant_ids;    // per-episode override
};
```

Per-episode `source_id`, `source_context`, and `participant_ids` override the batch-level defaults when non-empty.

### Examples

Basic batch:

```cpp
auto now = std::chrono::system_clock::now();

std::vector<RawEpisode> episodes = {
    {.name = "ep1", .content = "Alice started at Acme.", .source_description = "chat",
     .reference_time = now},
    {.name = "ep2", .content = "Bob joined Alice's team.", .source_description = "chat",
     .reference_time = now + std::chrono::seconds(60)},
};

auto result = g.add_episode_bulk({
    .episodes = episodes,
    .group_id = "onboarding",
});
```

Mixed-source batch with saga:

```cpp
std::vector<RawEpisode> episodes = {
    {.name = "slack_1", .content = "Sprint planning went well.",
     .source_description = "slack", .reference_time = monday,
     .source_id = "alice", .source_context = "slack_engineering"},
    {.name = "email_1", .content = "Client approved the design.",
     .source_description = "email", .reference_time = tuesday,
     .source_id = "bob", .source_context = "email_clients"},
};

auto result = g.add_episode_bulk({
    .episodes = episodes,
    .group_id = "project_x",
    .agent_id = "project-tracker",
    .saga = "sprint_42",
});
```

## Error handling

Every method returns `std::expected`. No exceptions cross the API:

```cpp
auto result = g.add_episode({...});
if (result) {
    auto& r = result.value();
    // r.episode, r.nodes, r.edges
} else {
    auto& err = result.error();
    // err.code: ErrorCode::{llm_error, db_error, embedding_error, ...}
    // err.message: human-readable string
}
```

## SearchFilters reference

```cpp
struct SearchFilters {
    std::vector<std::string> node_labels;      // filter by entity label
    std::vector<std::string> edge_types;       // filter by edge name/type
    std::optional<DateFilterClause> valid_at;   // temporal: when fact was true
    std::optional<DateFilterClause> invalid_at; // temporal: when fact stopped being true
    std::optional<DateFilterClause> created_at; // temporal: when edge was created
    std::optional<DateFilterClause> expired_at; // temporal: when edge was expired
    std::vector<std::string> edge_uuids;       // restrict to specific edges
    std::vector<PropertyFilter> property_filters;
    std::vector<std::string> agent_ids;        // who recorded it
    std::vector<std::string> source_ids;       // who said it
    std::vector<std::string> source_contexts;  // where it happened
    std::vector<std::string> participant_ids;  // who was present
};
```

All filter arrays use OR semantics — an item matches if it overlaps with any value in the filter.
