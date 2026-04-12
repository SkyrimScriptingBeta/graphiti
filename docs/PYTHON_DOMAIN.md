# Graphiti Python — Domain Entity Map

> A high-level map of every domain entity in the Python Graphiti codebase.
> Focused on types, relationships, and structure — not implementation details.

---

## 🧩 Core Graph Entities

These are the primary domain objects that live in the knowledge graph.

### Nodes

| Type | File | Purpose |
|------|------|---------|
| **`Node`** | `nodes.py` | Abstract base. Fields: `uuid`, `name`, `group_id`, `labels: list[str]`, `created_at` |
| **`EntityNode`** | `nodes.py` | Named knowledge entity ("Kendra", "Adidas"). Has `summary`, `attributes: dict`, `name_embedding`, `agent_ids`, `source_ids`, `source_contexts`, `participant_ids` |
| **`EpisodicNode`** | `nodes.py` | A recorded event/message/data snapshot. Has `content`, `source: EpisodeType`, `source_description`, `valid_at`, `entity_edges: list[str]`, `agent_id` (singular) |
| **`CommunityNode`** | `nodes.py` | Auto-generated cluster of related entities. Has `summary`, `name_embedding`, `agent_ids` |
| **`SagaNode`** | `nodes.py` | Container that chains episodes together. Minimal — just inherits base `Node` fields |

**`EpisodeType`** enum: `message`, `json`, `text`

### Edges

| Type | File | Purpose |
|------|------|---------|
| **`Edge`** | `edges.py` | Abstract base. Fields: `uuid`, `group_id`, `source_node_uuid`, `target_node_uuid`, `created_at` |
| **`EntityEdge`** | `edges.py` | A fact/relationship between two entities. The big one. Has `name`, `fact`, `fact_embedding`, `episodes: list[str]`, `valid_at`, `invalid_at`, `expired_at`, `attributes: dict`, `agent_ids` |
| **`EpisodicEdge`** | `edges.py` | MENTIONS — links an episode to an entity it references. Has `agent_id` (singular) |
| **`CommunityEdge`** | `edges.py` | CONTAINS — links a community to a member entity. Base fields only |
| **`HasEpisodeEdge`** | `edges.py` | Saga → Episode ownership. Base fields only |
| **`NextEpisodeEdge`** | `edges.py` | Episode → Episode sequencing within a saga. Base fields only |

---

## 🔗 Entity Relationships

```
SagaNode ──HAS_EPISODE──► EpisodicNode ──NEXT_EPISODE──► EpisodicNode
                                │
                                │ MENTIONS (EpisodicEdge)
                                ▼
                           EntityNode ◄──RELATES_TO (EntityEdge)──► EntityNode
                                │
                                │ CONTAINS (CommunityEdge)
                                ▼
                          CommunityNode
```

- **Saga → Episode**: `HasEpisodeEdge` (ownership), `NextEpisodeEdge` (ordering)
- **Episode → Entity**: `EpisodicEdge` (MENTIONS) — which entities an episode talks about
- **Entity → Entity**: `EntityEdge` (RELATES_TO) — the core facts/relationships, temporally tracked
- **Community → Entity**: `CommunityEdge` (CONTAINS) — community membership

---

## 🔍 Search Types

### Configuration

| Type | File | Purpose |
|------|------|---------|
| **`SearchConfig`** | `search/search_config.py` | Top-level search configuration. Contains optional configs for each entity type + `limit`, `reranker_min_score` |
| **`EdgeSearchConfig`** | `search/search_config.py` | Search methods + reranker for edges |
| **`NodeSearchConfig`** | `search/search_config.py` | Search methods + reranker for nodes |
| **`EpisodeSearchConfig`** | `search/search_config.py` | Search methods + reranker for episodes |
| **`CommunitySearchConfig`** | `search/search_config.py` | Search methods + reranker for communities |

Each `*SearchConfig` has: `search_methods`, `reranker`, `sim_min_score`, `mmr_lambda`, `bfs_max_depth`

### Search Methods (per entity type)

| Entity Type | Available Methods |
|-------------|-------------------|
| Edge | `cosine_similarity`, `bm25`, `breadth_first_search` |
| Node | `cosine_similarity`, `bm25`, `breadth_first_search` |
| Episode | `bm25` only |
| Community | `cosine_similarity`, `bm25` |

### Rerankers (per entity type)

| Entity Type | Available Rerankers |
|-------------|---------------------|
| Edge | `rrf`, `node_distance`, `episode_mentions`, `mmr`, `cross_encoder` |
| Node | `rrf`, `node_distance`, `episode_mentions`, `mmr`, `cross_encoder` |
| Episode | `rrf`, `cross_encoder` |
| Community | `rrf`, `mmr`, `cross_encoder` |

### Filters & Results

| Type | File | Purpose |
|------|------|---------|
| **`SearchFilters`** | `search/search_filters.py` | Filter bag: `node_labels`, `edge_types`, `valid_at`, `invalid_at`, `created_at`, `expired_at`, `edge_uuids`, `property_filters` |
| **`DateFilter`** | `search/search_filters.py` | A date + comparison operator (e.g., `created_at > 2024-01-01`) |
| **`PropertyFilter`** | `search/search_filters.py` | A property name + value + comparison operator |
| **`ComparisonOperator`** | `search/search_filters.py` | Enum: `equals`, `not_equals`, `greater_than`, `less_than`, `greater_than_equal`, `less_than_equal`, `is_null`, `is_not_null` |
| **`SearchResults`** | `search/search_config.py` | Container: `edges`, `nodes`, `episodes`, `communities` — each with parallel `*_reranker_scores` lists |

Pre-built search recipes live in `search/search_config_recipes.py` — 14 named configurations.

---

## 📦 Result Types (from Graphiti class)

| Type | File | Purpose |
|------|------|---------|
| **`AddEpisodeResults`** | `graphiti.py` | Single episode ingestion result: `episode`, `episodic_edges`, `nodes`, `edges`, `communities`, `community_edges` |
| **`AddBulkEpisodeResults`** | `graphiti.py` | Bulk ingestion result: same shape but `episodes` (plural) |
| **`AddTripletResults`** | `graphiti.py` | Manual triplet addition: `nodes`, `edges` |

---

## 🏗️ Infrastructure / Service Types

### LLM Layer

| Type | File | Purpose |
|------|------|---------|
| **`LLMClient`** | `llm_client/client.py` | Abstract base for LLM providers |
| **`LLMConfig`** | `llm_client/config.py` | Config: `api_key`, `model`, `base_url`, `temperature`, `max_tokens`, `small_model` |
| **`ModelSize`** | `llm_client/config.py` | Enum: `small`, `medium` |
| **`TokenUsage`** | `llm_client/token_tracker.py` | Dataclass: `input_tokens`, `output_tokens` |
| **`PromptTokenUsage`** | `llm_client/token_tracker.py` | Per-prompt accumulator: `prompt_name`, `call_count`, totals |
| **`TokenUsageTracker`** | `llm_client/token_tracker.py` | Thread-safe dict of `PromptTokenUsage` |

Concrete clients: `OpenAIClient`, `OpenAIGenericClient`, `AzureOpenAILLMClient`, `AnthropicClient`, `GeminiClient`, `GroqClient`

### Embedder Layer

| Type | File | Purpose |
|------|------|---------|
| **`EmbedderClient`** | `embedder/client.py` | Abstract base |
| **`EmbedderConfig`** | `embedder/client.py` | Config: `embedding_dim` |

Concrete embedders: `OpenAIEmbedder`, `AzureOpenAIEmbedderClient`, `GeminiEmbedder`, `VoyageEmbedder`

### Cross-Encoder / Reranker Layer

| Type | File | Purpose |
|------|------|---------|
| **`CrossEncoderClient`** | `cross_encoder/client.py` | Abstract base — `rerank(query, passages)` |

Concrete: `OpenAIRerankerClient`, `GeminiRerankerClient`, `BGERerankerClient`

### Tracing

| Type | File | Purpose |
|------|------|---------|
| **`Tracer`** | `tracer.py` | Abstract base for distributed tracing |
| **`TracerSpan`** | `tracer.py` | Abstract span |
| **`NoOpTracer`** / **`NoOpSpan`** | `tracer.py` | Default no-op implementation |
| **`OpenTelemetryTracer`** / **`OpenTelemetrySpan`** | `tracer.py` | OTel implementation |

### Orchestration

| Type | File | Purpose |
|------|------|---------|
| **`GraphitiClients`** | `graphiti_types.py` | Bundles all service dependencies: `driver`, `llm_client`, `embedder`, `cross_encoder`, `tracer` |

---

## 🔧 Driver / Operations Layer

### Core Abstractions

| Type | File | Purpose |
|------|------|---------|
| **`GraphDriver`** | `driver/driver.py` | Abstract driver base. Owns 11 operation interfaces as properties |
| **`GraphProvider`** | `driver/driver.py` | Enum: `NEO4J`, `FALKORDB`, `KUZU`, `NEPTUNE` |
| **`QueryExecutor`** | `driver/query_executor.py` | Protocol for running queries (decouples ops from driver) |

### 11 Operations ABCs

All in `driver/operations/`:

| ABC | Operates On |
|-----|------------|
| `EntityNodeOperations` | EntityNode CRUD + embedding |
| `EpisodeNodeOperations` | EpisodicNode CRUD + retrieval by time/saga |
| `CommunityNodeOperations` | CommunityNode CRUD + embedding |
| `SagaNodeOperations` | SagaNode CRUD |
| `EntityEdgeOperations` | EntityEdge CRUD + between-nodes + by-node queries |
| `EpisodicEdgeOperations` | EpisodicEdge (MENTIONS) CRUD |
| `CommunityEdgeOperations` | CommunityEdge (CONTAINS) CRUD |
| `HasEpisodeEdgeOperations` | HasEpisodeEdge CRUD |
| `NextEpisodeEdgeOperations` | NextEpisodeEdge CRUD |
| `SearchOperations` | All search methods: fulltext, similarity, BFS, rerankers, filter building |
| `GraphMaintenanceOperations` | Index lifecycle, community clustering, cascade operations |

---

## 🧠 Pipeline / Prompt Types

These are LLM response models — structured output schemas for extraction and deduplication.

| Type | File | Purpose |
|------|------|---------|
| **`Message`** | `prompts/models.py` | `role` + `content` — LLM message |
| **`ExtractedEntity`** | `prompts/extract_nodes.py` | `name`, `entity_type_id` |
| **`ExtractedEntities`** | `prompts/extract_nodes.py` | List wrapper |
| **`EntitySummary`** | `prompts/extract_nodes.py` | `summary` for a single entity |
| **`SummarizedEntity`** | `prompts/extract_nodes.py` | `name` + `summary` |
| **`SummarizedEntities`** | `prompts/extract_nodes.py` | List wrapper |
| **`Edge`** (prompt) | `prompts/extract_edges.py` | `source_entity_name`, `target_entity_name`, `relation_type`, `fact`, `valid_at`, `invalid_at` |
| **`ExtractedEdges`** | `prompts/extract_edges.py` | List wrapper |
| **`NodeDuplicate`** | `prompts/dedupe_nodes.py` | `id`, `name`, `duplicate_name` |
| **`NodeResolutions`** | `prompts/dedupe_nodes.py` | List of `NodeDuplicate` |
| **`EdgeDuplicate`** | `prompts/dedupe_edges.py` | `duplicate_facts: list[int]`, `contradicted_facts: list[int]` |

---

## 📡 API / DTO Types

### REST Server DTOs (`server/graph_service/dto/`)

| Type | Purpose |
|------|---------|
| **`AddMessagesRequest`** | `group_id` + `messages: list[Message]` |
| **`AddEntityNodeRequest`** | `uuid`, `group_id`, `name`, `summary` |
| **`SearchQuery`** | `group_ids`, `query`, `max_facts` |
| **`GetMemoryRequest`** | `group_id`, `max_facts`, `center_node_uuid`, `messages` |
| **`FactResult`** | `uuid`, `name`, `fact`, `valid_at`, `invalid_at`, `created_at`, `expired_at` |
| **`Result`** | Generic `message` + `success` response |

### Bulk Ingestion

| Type | File | Purpose |
|------|------|---------|
| **`RawEpisode`** | `utils/bulk_utils.py` | `name`, `uuid`, `content`, `source_description`, `source: EpisodeType`, `reference_time` |
| **`UnionFind`** | `utils/bulk_utils.py` | Data structure for cross-episode entity deduplication |

---

## ⚠️ Error Types (`errors.py`)

All inherit from **`GraphitiError`** (which inherits `Exception`):

- `EdgeNotFoundError`, `EdgesNotFoundError`
- `GroupsEdgesNotFoundError`, `GroupsNodesNotFoundError`
- `NodeNotFoundError`
- `SearchRerankerError`
- `EntityTypeValidationError`
- `GroupIdValidationError`, `AgentIdValidationError`

---

## 🌊 The Big Picture

```
                              ┌─────────────┐
                              │   Graphiti   │  ← main orchestrator
                              │  (class)     │
                              └──────┬───────┘
                                     │
                    ┌────────────────┼────────────────┐
                    │                │                │
              ┌─────▼─────┐  ┌──────▼──────┐  ┌─────▼──────┐
              │ GraphDriver│  │  LLMClient  │  │  Embedder  │
              │ (abstract) │  │  (abstract) │  │  (abstract)│
              └─────┬──────┘  └─────────────┘  └────────────┘
                    │
         11 Operations ABCs
          (CRUD + Search)
                    │
    ┌───────────────┼───────────────┐
    │               │               │
┌───▼───┐     ┌─────▼─────┐   ┌────▼────┐
│ Nodes │     │   Edges   │   │ Search  │
│       │     │           │   │         │
│Entity │     │EntityEdge │   │Filters  │
│Episode│◄────│EpisodicEdge│   │Config   │
│Community│   │CommunityEdge│  │Results  │
│Saga   │     │HasEpisode │   │Recipes  │
└───────┘     │NextEpisode│   └─────────┘
              └───────────┘
```

### Pipeline Flow (what happens when you call `add_episode`)

```
Raw Content
    │
    ▼
Extract Entities (LLM) → ExtractedEntities
    │
    ▼
Resolve/Dedupe Nodes (LLM + graph search) → EntityNode[]
    │
    ▼
Extract Edges (LLM) → EntityEdge[]
    │
    ▼
Resolve/Dedupe Edges (LLM + graph search) → resolved + invalidated edges
    │
    ▼
Build Episodic Edges (MENTIONS links)
    │
    ▼
Update Communities (label propagation + LLM summarization)
    │
    ▼
Save everything → AddEpisodeResults
```

---

## 🏷️ Attribution Dimensions

Four orthogonal attribution axes, tracked across the graph:

| Dimension | Singular (on Episode) | Accumulated (on Entity/Edge/Community) | Purpose |
|-----------|-----------------------|----------------------------------------|---------|
| `agent_id` | ✅ | `agent_ids: list` | Who recorded this |
| `source_id` | ✅ | `source_ids: list` | Who said it |
| `source_context` | ✅ | `source_contexts: list` | Where it happened (channel, doc, etc.) |
| `participant_ids` | ✅ (list) | `participant_ids: list` | Who was present |

All four are filterable via `SearchFilters`.
