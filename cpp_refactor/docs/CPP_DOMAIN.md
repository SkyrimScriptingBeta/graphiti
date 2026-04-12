# 🗺️ C++ Domain Report

> High-level map of every important domain type, abstraction, and relationship in the Graphiti C++ port.
> Generated 2026-04-12 from the `graph-providers` branch.

---

## Library Architecture

The C++ port is split into **4 static libraries** with a strict dependency chain:

```
graphiti-core          ← Foundation: domain types + abstract interfaces
    ↓
graphiti-kuzu          ← Kuzu graph DB implementation of GraphStore
    ↓
graphiti-kuzu-writer   ← Remote write proxy (WebSocket to centralized daemon)
    ↓
graphiti               ← Orchestrator: LLM, embedder, pipeline, search, config
```

Each library lives under `cpp_refactor/lib/<name>/`.

---

## Core Domain Entities

All defined in `graphiti-core/include/graphiti/types.h`.

### Node Types

| Type | Purpose | Key Fields |
|------|---------|------------|
| **EntityNode** | Semantic entity (Person, Org, Concept…) | uuid, name, group_id, labels, name_embedding, summary, attributes (JSON), traits, is_system, is_identity, agent_ids, source_ids, participant_ids |
| **EpisodicNode** | A message/event carrying knowledge | uuid, name, group_id, source (EpisodeType), content, valid_at, entity_edges (UUIDs), agent_id, source_id, participant_ids |
| **CommunityNode** | Cluster of related entities | uuid, name, group_id, name_embedding, summary, agent_ids, source_ids, participant_ids |
| **SagaNode** | Named temporal sequence container | uuid, name, group_id, created_at |

### Edge Types

| Type | Connects | Purpose | Key Fields |
|------|----------|---------|------------|
| **EntityEdge** | Entity → Entity | Facts/relationships | uuid, name, fact, fact_embedding, episodes, valid_at, invalid_at, expired_at, attributes (JSON), is_system, agent_ids, source_ids, participant_ids |
| **EpisodicEdge** | Episode → Entity | MENTIONS link | uuid, source/target UUIDs, agent_id, source_id, participant_ids |
| **CommunityEdge** | Entity → Community | Membership | uuid, source/target UUIDs |
| **HasEpisodeEdge** | Saga → Episode | Saga membership | uuid, source/target UUIDs |
| **NextEpisodeEdge** | Episode → Episode | Temporal sequencing | uuid, source/target UUIDs |

### Enums

| Enum | Values |
|------|--------|
| **EpisodeType** | `message`, `json`, `text` |
| **ErrorCode** | `ok`, `llm_error`, `llm_parse_error`, `llm_rate_limit`, `db_error`, `embedding_error`, `http_error`, `invalid_config`, `not_found` |
| **ComparisonOp** | `eq`, `neq`, `gt`, `lt`, `gte`, `lte`, `is_null`, `is_not_null` |

### Type Aliases

```cpp
using TimePoint      = std::chrono::system_clock::time_point;
using Result<T>      = std::expected<T, GraphitiError>;
using VoidResult     = std::expected<void, GraphitiError>;
using PropertyValue  = std::variant<std::string, int64_t, double>;
using DateFilterClause = std::vector<std::vector<DateFilter>>;
```

---

## Graph Relationships (Visual)

```
SagaNode ──HasEpisodeEdge──→ EpisodicNode ──NextEpisodeEdge──→ EpisodicNode
                                  │
                            EpisodicEdge (MENTIONS)
                                  │
                                  ▼
                             EntityNode ──EntityEdge──→ EntityNode
                                  │
                            CommunityEdge
                                  │
                                  ▼
                            CommunityNode
```

---

## Abstract Interfaces

### GraphStore (`graphiti-core/include/graphiti/graph_store.h`)

The **repository pattern** abstraction for all persistence and search. ~60 virtual methods covering:

- **Schema**: `setup_schema()`, `rebuild_indices()`
- **Entity CRUD**: `persist_entity()`, `get_entity()`, `delete_entity()`
- **Edge CRUD**: `persist_edge()`, `get_edge()`, `delete_edge()`
- **Episode management**: `persist_episode()`, `retrieve_episodes()`, `retrieve_episodes_by_saga()`
- **Saga management**: `find_saga()`, `persist_saga()`, `link_saga_episode()`
- **Community management**: `persist_community()`, `get_entity_community()`, `get_neighbor_communities()`
- **Search (BM25)**: `search_entities_bm25()`, `search_edges_bm25()`, `search_episodes_bm25()`
- **Search (Cosine)**: `search_entities_cosine()`, `search_edges_cosine()`, `search_communities_cosine()`
- **Search (BFS)**: `search_edges_bfs()`, `search_nodes_bfs()`
- **Analytics**: `get_node_summaries()`, `count_episode_mentions()`, `check_node_adjacency()`

Nested helper types:
- `GraphStore::Neighbor` — node_uuid + edge_count
- `GraphStore::NodeSummary` — uuid, name, labels, attribution vectors
- `GraphStore::EdgeSummary` — uuid, name, source/target UUIDs, attribution vectors

### LLMClient (`graphiti/include/graphiti/llm_client.h`)

Abstract interface for any LLM provider. Single key method:

```cpp
virtual Result<nlohmann::json> generate_response(
    const std::vector<Message>&,
    std::optional<std::string_view> json_schema,
    ModelSize
) = 0;
```

Carries a `TokenTracker`, `prompt_name`, `max_tokens_override`, and `on_attempt` callback.

### EmbedderClient (`graphiti/include/graphiti/embedder.h`)

Abstract interface for embedding providers:

```cpp
virtual std::vector<float> create(std::string_view input) = 0;
virtual std::vector<std::vector<float>> create_batch(const std::vector<std::string>&) = 0;
```

### GraphitiLogger (`graphiti/include/graphiti/logger.h`)

Abstract observability interface:

- `on_llm_call_start()` / `on_llm_call_end()` — LLM call lifecycle
- `on_embedding_call()` — embedder calls
- `on_pipeline_step()` — pipeline stage completion

Info structs: `LLMCallInfo`, `EmbeddingCallInfo`, `PipelineStepInfo`

---

## Implementations

### GraphStore Implementations

| Class | File | Pattern |
|-------|------|---------|
| **KuzuGraphStore** | `graphiti-kuzu/src/driver/kuzu_graph_store.h` | In-process Kuzu DB; owns or shares `kuzu::main::Database` |
| **KuzuWriterGraphStore** | `graphiti-kuzu-writer/src/kuzu_writer_graph_store.h` | Inherits KuzuGraphStore; reads locally, writes remotely via WebSocket JSON-RPC |

### LLMClient Implementations

| Class | File | Purpose |
|-------|------|---------|
| **OpenAIClient** | `graphiti/include/graphiti/openai_client.h` | Production OpenAI API (supports base_url override for OpenRouter etc.) |
| **CallbackLLMClient** | `graphiti/include/graphiti/callback_llm_client.h` | User-injected callback function |
| **RecordingLLMClient** | `graphiti/include/graphiti/recording_llm_client.h` | Wraps any LLMClient, records calls to JSON |
| **ReplayLLMClient** | `graphiti/include/graphiti/recording_llm_client.h` | Replays recorded responses (deterministic testing) |
| **LoggingLLMClient** | `graphiti/src/llm/logging_llm_client.h` | Decorator: wraps any LLMClient, logs to GraphitiLogger |

### EmbedderClient Implementations

| Class | File | Purpose |
|-------|------|---------|
| **OpenAIEmbedder** | `graphiti/src/embedder/openai_embedder.h` | Production OpenAI embeddings API |
| **CallbackEmbedder** | `graphiti/include/graphiti/callback_embedder.h` | User-injected callback |
| **RecordingEmbedder** | `graphiti/include/graphiti/recording_embedder.h` | Records embedding calls to JSON |
| **ReplayEmbedder** | `graphiti/include/graphiti/recording_embedder.h` | Replays recorded embeddings |
| **LoggingEmbedder** | `graphiti/src/embedder/logging_embedder.h` | Decorator: logs to GraphitiLogger |

### Logger Implementation

| Class | File | Purpose |
|-------|------|---------|
| **SqliteGraphitiLogger** | `graphiti/include/graphiti/sqlite_logger.h` | Thread-safe SQLite persistence of all logs |

---

## Search Infrastructure

### Search Configuration (`graphiti-core/include/graphiti/search_config.h`)

```
SearchConfig
├── EdgeSearchConfig     (methods: cosine_similarity, bm25, bfs; reranker; sim_min_score; mmr_lambda; bfs_max_depth)
├── NodeSearchConfig     (same shape as edge)
├── EpisodeSearchConfig  (bm25 only; reranker)
├── CommunitySearchConfig (cosine_similarity, bm25; reranker; sim_min_score; mmr_lambda)
├── limit
└── reranker_min_score
```

**Search methods**: `cosine_similarity`, `bm25`, `bfs`
**Rerankers**: `rrf`, `node_distance`, `episode_mentions`, `mmr`, `cross_encoder`

Pre-built recipes: `edge_hybrid_search_rrf()`, `node_hybrid_search_mmr()`, `combined_hybrid_search_rrf()`, etc.

### Search Filters (`graphiti-core/include/graphiti/search_filters.h`)

- Node label / edge type filters
- Temporal filters: `valid_at`, `invalid_at`, `created_at`, `expired_at` (AND/OR clause structure)
- Property filters (generic key-value with ComparisonOp)
- Attribution: `agent_ids`, `source_ids`, `source_contexts`, `participant_ids`

### SearchResults (`graphiti-core/include/graphiti/search_config.h`)

```
SearchResults
├── edges    + edge_scores
├── nodes    + node_scores
├── episodes + episode_scores
└── communities + community_scores
```

### Search Orchestration (`graphiti/src/search/`)

| Component | File | Purpose |
|-----------|------|---------|
| `hybrid_edge_search()` | search.h | BM25 + cosine + optional BFS, merged with RRF |
| `episode_search()` | search.h | BM25 fulltext on episode content |
| `search_orchestrator()` | search.h | Full advanced search across all entity types |
| `edge_bfs_search()` / `node_bfs_search()` | bfs_search.h | Graph traversal search |
| `episode_mentions_reranker()` | rerankers.h | Score by mention frequency |
| `node_distance_reranker()` | rerankers.h | Score by 1-hop adjacency |
| `maximal_marginal_relevance()` | rerankers.h | Balance relevance + diversity |
| `cross_encoder_rerank()` | rerankers.h | LLM-based passage scoring |
| `rrf()` | search_utils.h | Reciprocal Rank Fusion |

---

## Pipeline Stages (`graphiti/src/pipeline/`)

The ingestion pipeline runs in order:

```
Episode Content
    ↓
1. extract_nodes()        → ExtractedEntity structs → EntityNode[]
    ↓
2. extract_edges()        → ExtractedEdge structs → EntityEdge[]
    ↓
3. dedupe_nodes()         → DedupeNodesResult (nodes + uuid_map for remapping)
    ↓
4. dedupe_edges()         → DedupeEdgesResult (new_edges + invalidated_uuids)
    ↓
5. node_enrichment()      → Updated summaries via LLM
    ↓
6. episodic_edges()       → MENTIONS edges (EpisodicEdge)
    ↓
7. community_ops()        → Label propagation clustering → CommunityNode + CommunityEdge
```

### LLM Response Models (`graphiti/src/llm/response_models.h`)

Pipeline intermediate types parsed from LLM JSON output:

| Type | Purpose |
|------|---------|
| **ExtractedEntity** | name, entity_type_id, traits |
| **ExtractedEntities** | vector of ExtractedEntity |
| **ExtractedEdge** | source_entity_name, target_entity_name, relation_type, fact, valid_at, invalid_at |
| **ExtractedEdges** | vector of ExtractedEdge |
| **NodeDuplicate** | id, name, duplicate_name |
| **NodeResolutions** | vector of NodeDuplicate |
| **EdgeDuplicate** | duplicate_facts, contradicted_facts (index vectors) |
| **EntitySummary** | summary string |
| **SummarizedEntity** | name + summary |
| **SummarizedEntities** | vector of SummarizedEntity |

---

## Configuration Types

### GraphitiConfig (`graphiti/include/graphiti/config.h`)

```
GraphitiConfig
├── db_path                    (default ":memory:")
├── default_group_id           (optional)
├── llm: LLMConfig
│   ├── api_key, model, small_model
│   ├── base_url, temperature, frequency/repetition_penalty
│   ├── max_tokens, max_output_tokens, truncation_multiplier
│   └── edge_shard_size, max_edges, extra_tokens_per_entity, entity_scaling_threshold
├── embedder: EmbedderConfig
│   ├── api_key, model, base_url
│   └── embedding_dim
├── store_raw_episode_content  (bool)
├── read_only                  (bool)
├── onnx_model_path            (optional, local embeddings)
├── max_parallel_extractions   (for bulk ops)
├── kuzu_writer_uri            (WebSocket URI for remote writer)
└── kuzu_writer_target_db      (target DB on writer server)
```

`GraphitiConfig::from_env()` loads from environment variables.

### TypeDefinitions (`graphiti/include/graphiti/type_definitions.h`)

User-defined custom entity/edge types loaded from YAML:

```
TypeDefinitions
├── entity_types: vector<EntityTypeDef>
│   └── EntityTypeDef { name, description, fields: map<string,string> }
├── edge_types: vector<EdgeTypeDef>
│   └── EdgeTypeDef { name, description, source_type, target_type }
└── excluded_entity_types: vector<string>
```

Methods: `from_yaml_file()`, `from_yaml_string()`, `entity_types_prompt_json()`, `edge_types_prompt_json()`

---

## The Orchestrator: Graphiti Class (`graphiti/include/graphiti/graphiti.h`)

Thread-safe (mutex-serialized). The main entry point tying everything together.

### Constructors

```cpp
Graphiti(GraphitiConfig)                                              // own Kuzu, default OpenAI
Graphiti(GraphitiConfig, unique_ptr<LLMClient>, unique_ptr<EmbedderClient>)  // own Kuzu, custom providers
Graphiti(GraphitiConfig, kuzu::main::Database& shared_db)             // shared Kuzu, default OpenAI
Graphiti(GraphitiConfig, kuzu::main::Database&, LLMClient*, EmbedderClient*) // shared Kuzu, custom providers
```

### Key Methods

| Method | Returns | Purpose |
|--------|---------|---------|
| `build_indices()` | VoidResult | Rebuild FTS indices |
| `add_episode(AddEpisodeOptions)` | Result\<AddEpisodeResult\> | Single episode ingestion (full pipeline) |
| `add_episode_bulk(AddEpisodeBulkOptions)` | Result\<AddBulkEpisodeResults\> | Batch ingestion with cross-episode dedup |
| `search(SearchOptions)` | Result\<SearchResults\> | Simple hybrid search |
| `search_advanced(SearchAdvancedOptions)` | Result\<SearchResults\> | Advanced multi-method search |
| `get_graph_analytics()` | JSON | Graph stats and overview |
| `add_logger(GraphitiLogger*)` | void | Register observability logger |

### Nested Types

- **AgentIdentity** — name, role, role_description, team, group_id
- **AddTripletResult** — nodes + edges from a single extraction
- **AddEpisodeResult** — episode + episodic_edges + nodes + edges
- **AddBulkEpisodeResults** — episodes + nodes + edges
- **RawEpisode** — minimal episode data for bulk ingestion

---

## Utility Types

| Type | File | Purpose |
|------|------|---------|
| **TokenUsage** | token_tracker.h | input_tokens + output_tokens |
| **PromptTokenUsage** | token_tracker.h | Per-prompt-name token accounting |
| **TokenTracker** | token_tracker.h | Accumulator across LLM calls |
| **GraphitiError** | error.h | ErrorCode + message |
| **Message** | llm_client.h | role + content (LLM conversation message) |
| **DateFilter** | search_filters.h | date + ComparisonOp |
| **PropertyFilter** | search_filters.h | property_name + PropertyValue + ComparisonOp |
| **HttpClient** | http/http_client.h | cpp-httplib wrapper for API calls |
| **KuzuWriterClient** | remote/kuzu_writer_client.h | JSON-RPC WebSocket client for remote writes |
| **UnionFind** | utils/union_find.h | Duplicate clustering for bulk dedup |

---

## Design Patterns Summary

| Pattern | Where |
|---------|-------|
| **Repository** | `GraphStore` abstracts all persistence behind domain operations |
| **Decorator** | Recording/Logging/Replay wrappers for LLMClient and EmbedderClient |
| **Callback Injection** | `CallbackLLMClient`, `CallbackEmbedder` — plug custom logic without subclassing |
| **Pipeline** | Extract → Dedup → Enrich → Link → Community — each stage is a separate module |
| **Strategy** | Pluggable search methods, rerankers, LLM/embedding providers |
| **Result/Expected** | `std::expected<T, GraphitiError>` throughout — no exceptions |

---

## Key Differences from Python

- **Kuzu instead of Neo4j/FalkorDB** — embedded graph DB, no external server needed
- **GraphStore abstraction** — Python talks directly to Neo4j; C++ has a clean repository interface ready for multiple backends (Apache AGE/Postgres planned)
- **KuzuWriterGraphStore** — distributed write pattern via WebSocket daemon (no Python equivalent)
- **Recording/Replay pattern** — deterministic testing without live LLM calls
- **No MCP server or HTTP server** — the C++ port is a library, not a service (yet)
- **YAML-based TypeDefinitions** — custom entity/edge types loaded from YAML files
- **C++23 std::expected** — error handling without exceptions
