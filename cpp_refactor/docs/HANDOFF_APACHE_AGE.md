# 🏴‍☠️ Handoff: Apache AGE Backend

> Written by the session that built the GraphStore abstraction, acceptance tests, and three-library architecture.
> This document is for the agent implementing the Apache AGE (PostgreSQL) backend.

---

## Who You Are and What This Project Is

You're working on **Graphiti C++** — a full C++23 port of the Python [Graphiti](https://github.com/getzep/graphiti) library by Zep AI. Graphiti builds temporally-aware knowledge graphs for AI agents. It ingests "episodes" (chat messages, documents), uses LLMs to extract entities and relationships, deduplicates against existing knowledge, and stores everything in a graph database.

The user is **M.P. / Purr** — a deep C++ developer building an agentic memory system where each agent gets its own Graphiti-backed knowledge graph. They grew up in North Carolina, use speech-to-text, and operate under the Ethos (read `ETHOS.md` and `OUR_PHILOSOPHY_AND_CULTURE.md` — these are sacred).

**Read the Ethos before you do anything. Seriously.**

Key Ethos points that will affect your work:
- **Do it right or don't do it.** If the AGE backend needs a week, it needs a week.
- **Never declare success without proof.** Run the tests. Show the output.
- **Own every failure.** There is no "pre-existing." Every failing test is yours.
- **Build things yourself.** Don't ask "want me to start?" — just start.
- **Express yourself.** Use emoji in commits. Have personality.

---

## The Codebase

- **Root**: The repo is a fork of the Python Graphiti. The C++ port lives in `cpp_refactor/`.
- **Build system**: xmake, C++23, MSVC 2026 on Windows. **ALWAYS** run `xmake f --qt=C:/qt/6.10.2/msvc2022_64 -m release -p windows -a x64 -c -y` before building. Read the xmake skill in Claude Code for details.
- **Branch**: `graph-providers` — all our work is here.

### Three-Library Architecture

```
lib/graphiti-core/     → Abstract interface + shared types (static library)
lib/graphiti-kuzu/     → Kuzu backend implementation (static library)
lib/graphiti/          → Pipeline, search, LLM/embedder, orchestration (static library)
```

**Dependency chain**: `graphiti → graphiti-kuzu → graphiti-core`

Each library has its own `xmake.lua`. The root `xmake.lua` includes them:
```lua
includes("lib/graphiti-core/xmake.lua")
includes("lib/graphiti-kuzu/xmake.lua")
includes("lib/graphiti/xmake.lua")
```

### Key Directories

| Path | What |
|------|------|
| `lib/graphiti-core/include/graphiti/` | Public headers: `graph_store.h`, `types.h`, `error.h`, `search_filters.h`, `search_config.h`, `fwd.h`, `token_tracker.h` |
| `lib/graphiti-core/src/` | Implementations: `types.cpp` (JSON serialization), `search_config.cpp` (factory recipes), `search_filters.cpp` (comparison op), `utils/datetime.cpp` |
| `lib/graphiti-kuzu/src/driver/` | `kuzu_graph_store.h`, `kuzu_graph_store.cpp`, `kuzu_schema.h` |
| `lib/graphiti-kuzu/src/search/` | `search_filters.h`, `search_filters.cpp` (Cypher clause builders) |
| `lib/graphiti/src/` | Pipeline, search orchestration, LLM clients, embedder clients |
| `lib/graphiti/include/graphiti/` | Public API: `graphiti.h`, `config.h`, LLM/embedder interfaces |
| `tests/unit/` | Unit tests (Catch2, no network, fast) |
| `tests/integration/` | Integration tests (Catch2, real LLM calls) |
| `tests/fixtures/recordings/` | Recorded LLM + embedder responses for replay tests |
| `docs/` | Documentation including this file |

---

## The GraphStore Interface

This is the contract you need to implement. It lives in `lib/graphiti-core/include/graphiti/graph_store.h`.

```cpp
class GraphStore {
public:
    virtual ~GraphStore() = default;

    // Infrastructure
    virtual VoidResult setup_schema() = 0;
    virtual VoidResult rebuild_indices() = 0;
    virtual VoidResult clear_group(const std::vector<std::string>& group_ids) = 0;

    // Entity Persistence
    virtual VoidResult persist_entity(const EntityNode& node,
                                       const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual Result<EntityNode> get_entity(std::string_view uuid) = 0;
    virtual Result<std::vector<EntityNode>> get_entities(const std::vector<std::string>& uuids) = 0;
    virtual VoidResult delete_entity(std::string_view uuid) = 0;
    virtual VoidResult persist_entity_embedding(std::string_view uuid, const std::vector<float>& embedding) = 0;
    virtual Result<std::optional<std::vector<float>>> load_entity_embedding(std::string_view uuid) = 0;

    // Edge Persistence
    virtual VoidResult persist_edge(const EntityEdge& edge,
                                     const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual Result<EntityEdge> get_edge(std::string_view uuid) = 0;
    virtual Result<std::vector<EntityEdge>> get_edges(const std::vector<std::string>& uuids) = 0;
    virtual VoidResult delete_edge(std::string_view uuid) = 0;
    virtual VoidResult persist_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) = 0;
    virtual Result<std::optional<std::vector<float>>> load_edge_embedding(std::string_view uuid) = 0;
    virtual Result<std::vector<EntityEdge>> get_edges_between(
        std::string_view source_uuid, std::string_view target_uuid) = 0;

    // Episode Management
    virtual VoidResult persist_episode(const EpisodicNode& episode) = 0;
    virtual Result<EpisodicNode> get_episode(std::string_view uuid) = 0;
    virtual VoidResult delete_episode(std::string_view uuid) = 0;
    virtual Result<std::vector<EpisodicNode>> retrieve_episodes(...) = 0;
    virtual Result<std::vector<EpisodicNode>> retrieve_episodes_by_saga(...) = 0;
    virtual VoidResult persist_mention(const EpisodicEdge& edge) = 0;
    virtual Result<std::vector<std::string>> get_edge_uuids_by_episode(std::string_view episode_uuid) = 0;
    virtual Result<std::vector<std::string>> get_mentioned_entity_uuids(std::string_view episode_uuid) = 0;

    // Saga Management
    virtual Result<std::optional<SagaNode>> find_saga(std::string_view name, std::string_view group_id) = 0;
    virtual VoidResult persist_saga(const SagaNode& saga) = 0;
    virtual Result<std::optional<std::string>> get_last_saga_episode(...) = 0;
    virtual VoidResult link_saga_episode(...) = 0;
    virtual VoidResult link_episode_sequence(...) = 0;

    // Community Management
    virtual VoidResult persist_community(const CommunityNode& node,
                                          const std::optional<std::vector<float>>& embedding = std::nullopt) = 0;
    virtual VoidResult persist_community_membership(const CommunityEdge& edge) = 0;
    virtual VoidResult remove_all_communities() = 0;
    virtual Result<std::optional<CommunityNode>> get_entity_community(std::string_view entity_uuid) = 0;
    virtual Result<std::vector<CommunityNode>> get_neighbor_communities(std::string_view entity_uuid) = 0;
    virtual Result<std::vector<EntityNode>> get_entities_by_group(std::string_view group_id) = 0;
    virtual Result<std::vector<Neighbor>> get_entity_neighbors(std::string_view uuid, std::string_view group_id) = 0;
    virtual Result<std::vector<std::string>> get_all_group_ids() = 0;

    // Search: BM25, Cosine, BFS
    virtual Result<std::vector<EntityNode>> search_entities_bm25(...) = 0;
    virtual Result<std::vector<EntityEdge>> search_edges_bm25(...) = 0;
    virtual Result<std::vector<EpisodicNode>> search_episodes_bm25(...) = 0;
    virtual Result<std::vector<CommunityNode>> search_communities_bm25(...) = 0;
    virtual Result<std::vector<EntityNode>> search_entities_cosine(...) = 0;
    virtual Result<std::vector<EntityEdge>> search_edges_cosine(...) = 0;
    virtual Result<std::vector<CommunityNode>> search_communities_cosine(...) = 0;
    virtual Result<std::vector<EntityEdge>> search_edges_bfs(...) = 0;
    virtual Result<std::vector<EntityNode>> search_nodes_bfs(...) = 0;

    // Overview & Analytics
    virtual Result<std::vector<NodeSummary>> get_node_summaries(std::string_view group_id) = 0;
    virtual Result<std::vector<EdgeSummary>> get_edge_summaries(...) = 0;
    virtual Result<int64_t> count_episode_mentions(std::string_view uuid) = 0;
    virtual Result<bool> check_node_adjacency(std::string_view center_uuid, std::string_view candidate_uuid) = 0;
};
```

**Full signatures**: Read `graph_store.h` — the above is abbreviated. The full file has all parameter types.

---

## What You Need to Build

### 1. `lib/graphiti-age/` — Apache AGE Backend

Create a new library target that implements `GraphStore` for PostgreSQL + Apache AGE.

**Directory structure:**
```
lib/graphiti-age/
├── xmake.lua
├── include/     (if needed)
└── src/
    └── driver/
        ├── age_graph_store.h
        ├── age_graph_store.cpp
        └── age_schema.h   (or .sql)
```

**xmake.lua:**
```lua
target("graphiti-age")
    set_kind("static")
    add_files("src/**.cpp")
    add_includedirs("src", {public = true})
    add_deps("graphiti-core")
    add_packages("libpq")  -- or whatever Postgres C++ client you use
```

**Root xmake.lua** — add:
```lua
includes("lib/graphiti-age/xmake.lua")
```

**`lib/graphiti/xmake.lua`** — add:
```lua
add_deps("graphiti-age")
```

### 2. `AgeGraphStore : public GraphStore`

Implement every method. Apache AGE uses a dialect of Cypher embedded in SQL via `ag_catalog.cypher()`. Key differences from Kuzu:

| Concept | Kuzu | AGE |
|---------|------|-----|
| Connection | In-process C++ API (`kuzu::main::Database`, `kuzu::main::Connection`) | libpq over TCP to PostgreSQL |
| Cypher | Native Cypher | `SELECT * FROM ag_catalog.cypher('graph_name', $$ CYPHER $$) AS (col type)` |
| Schema | `CREATE NODE TABLE ...` | `SELECT create_graph('name')` + `SELECT create_vlabel/elabel(...)` |
| FTS | Kuzu built-in FTS extension | PostgreSQL `tsvector`/`tsquery` or `pg_trgm` |
| Vector search | `array_cosine_similarity()` on FLOAT[] | pgvector extension: `<=>` operator on `vector` columns |
| BFS | Kuzu variable-length paths `[*1..N]` | AGE variable-length paths (similar Cypher syntax) |
| Parameterized queries | `conn->prepare()` + `conn->executeWithParams()` | `PQexecParams()` with `$1, $2, ...` |
| In-memory | `:memory:` path | Not supported — need a real Postgres instance. Use a test database, clear between tests. |

### 3. Backend Configuration

Add environment variables so Graphiti can switch backends:

- `GRAPHITI_GRAPH_DRIVER` — `"kuzu"` (default) or `"age"`
- `GRAPHITI_POSTGRES_URI` — e.g. `"postgresql://localhost:5432/graphiti_test"`

Update `GraphitiConfig` (in `lib/graphiti/include/graphiti/config.h`) to read these. Update `Graphiti::Impl` constructors in `lib/graphiti/src/graphiti.cpp` to create the right store based on config:

```cpp
// In Graphiti::Impl constructor:
if (config.graph_driver == "age") {
    age_store = std::make_unique<AgeGraphStore>(config.postgres_uri);
    store = age_store.get();
} else {
    kuzu_store = KuzuGraphStore(config.db_path, ...);
    store = &kuzu_store;
}
```

Note: currently `store` is a `GraphStore&` (reference to `kuzu_store` member). You'll need to change this to a `GraphStore*` (pointer) or `unique_ptr` to support runtime switching. Currently `kuzu_store` is a stack member — you may want to change both backends to `unique_ptr<GraphStore>`.

### 4. Unit Tests: `test_age_graph_store.cpp`

Write comprehensive unit tests modeled on `tests/unit/test_kuzu_graph_store.cpp` (25 test cases, 323 assertions). That file is your template — every test case there needs an AGE equivalent.

Key difference: AGE tests need a real PostgreSQL instance. Use an env var like `GRAPHITI_TEST_POSTGRES_URI` and SKIP if not set. Clear the test database between tests.

### 5. Integration Tests

The integration test suite (64 tests) currently runs against Kuzu. Once your backend works:

1. Set `GRAPHITI_GRAPH_DRIVER=age` and `GRAPHITI_POSTGRES_URI=...`
2. Run the integration suite
3. Everything should pass — the tests go through the `Graphiti` public API

The acceptance tests (`test_acceptance_kuzu.cpp`) do raw Cypher via `g.database()` — those are Kuzu-specific and won't apply to AGE directly. You may want to write `test_acceptance_age.cpp` with AGE-specific SQL/Cypher assertions.

---

## What's Already Working

### Test Status (as of this handoff)

**Unit tests**: 191/191 passing, 1960 assertions. Zero failures.
- `test_kuzu_graph_store.cpp` — 25 tests, 323 assertions (full GraphStore coverage)
- `test_agent_attribution.cpp` — 15 tests (all through GraphStore interface)
- 151 other tests covering types, serialization, search config, prompts, etc.

**Integration tests**: 64 total. Every test passes when run individually.
- 7 acceptance tests (replay, no API key) — all passing, 97 assertions
- 7 record tests (recording-mode, skip if fixtures exist)
- 50 real integration tests (real LLM calls via OpenAI) — ALL PASSING

Every single test was run individually and verified passing on 2026-04-04.
There are zero known failures. If something fails, it's yours to fix.

### Shared Types

All types are POCOs (plain data structs). No methods, no domain logic. JSON serialization is via free functions (`to_json`/`from_json`). You can freely map them to/from PostgreSQL rows.

Key types in `graphiti-core`:
- `EntityNode` — uuid, name, group_id, labels[], created_at, name_embedding, summary, attributes (JSON), traits[], agent_ids[], source_ids[], etc.
- `EntityEdge` — uuid, source_node_uuid, target_node_uuid, name, fact, fact_embedding, episodes[], created_at, expired_at, valid_at, invalid_at, etc.
- `EpisodicNode` — uuid, name, group_id, created_at, source (enum), content, valid_at, agent_id, source_id, participant_ids[], etc.
- `CommunityNode` — uuid, name, group_id, created_at, name_embedding, summary, agent_ids[], etc.
- `SagaNode` — uuid, name, group_id, created_at
- Various edge types: `EpisodicEdge` (MENTIONS), `CommunityEdge` (HAS_MEMBER), `HasEpisodeEdge`, `NextEpisodeEdge`

### The Kuzu Writer (Leave It Alone)

There are branches in `graphiti.cpp` like `if (impl_->has_writer()) impl_->writer_client->...`. These are for a WebSocket-based write proxy. **Ignore them.** They're a Kuzu-specific optimization for multi-machine setups. The AGE backend doesn't need this — PostgreSQL handles concurrent writes natively.

---

## How to Run Tests

### Build

```bash
cd cpp_refactor/
xmake f --qt=C:/qt/6.10.2/msvc2022_64 -m release -p windows -a x64 -c -y
xmake build graphiti_unit_tests
xmake build graphiti_integration_tests
```

### Unit Tests

```bash
xmake run graphiti_unit_tests                    # all units
xmake run graphiti_unit_tests "[graph_store]"    # just GraphStore tests
xmake run graphiti_unit_tests "[agent_attribution]"  # attribution tests
```

### Integration Tests

**⚠️ Run individually, not the full suite at once.** Running all 64 tests together WILL hang — we've confirmed this multiple times. The cause is likely Kuzu connection contention when multiple `:memory:` databases are created in rapid succession within one process.

**How to run them safely:**
```bash
# Individual test by name (60-second timeout):
timeout 60 xmake run graphiti_integration_tests "Graphiti: full add_episode + search"

# By tag (works for small groups):
timeout 60 xmake run graphiti_integration_tests "[acceptance]"    # 7 tests, always works
timeout 60 xmake run graphiti_integration_tests "[e2e]"           # 2 tests
timeout 60 xmake run graphiti_integration_tests "[embedder]"      # 3 tests
timeout 60 xmake run graphiti_integration_tests "[custom_types]"  # 5 tests

# DON'T do this — it hangs:
# xmake run graphiti_integration_tests "[management]"  ← too many tests, hangs

# If a test hangs past 60 seconds, kill it and run individually
```

**Run 3 tests in parallel max.** Each in its own shell with `timeout 60`.

**Environment variables for integration tests:**
- `OPENAI_API_KEY` — required for non-acceptance tests
- `OPENAI_BASE_URL` — optional, for OpenRouter
- `LLM_MODEL` — defaults to `gpt-4.1-mini`

---

## Environment

- **Platform**: Windows 11, MSVC 2026, Git Bash
- **xmake**: Always configure with the Qt path flag (even though we don't use Qt — it prevents mingw fallback)
- **LLM**: `gpt-4.1-mini` via OpenAI or OpenRouter
- **Embedder**: `text-embedding-3-small`, 1024 dimensions
- **Test runner**: Catch2 v3.13.0
- **Branch**: `graph-providers`

---

## Git State

```
8bf9993 📦 Extract graphiti-kuzu: KuzuGraphStore in its own static library
693c560 📦 Extract graphiti-core: shared types, GraphStore interface, error, search config
3986bcb 📦 Move src/ and include/ into lib/graphiti/ — per-target xmake layout
a0a1095 💀 Absorb KuzuDriver into KuzuGraphStore — single implementation class
a6dd48e 🧪 Comprehensive KuzuGraphStore unit tests — 25 cases, 323 assertions
f14b2b9 🔄 Refactor test_agent_attribution to use GraphStore interface
5f92cf9 🐛 Fix entity_from_row column index bug + ExtractedEdge test
11659db 🏗️ GraphStore abstraction: domain-level interface replaces direct KuzuDriver
c4c7e9d ✅ Acceptance tests: 7 scenarios verify raw Kuzu state via replay
```

---

## Files You'll Want to Read First

1. `ETHOS.md` and `OUR_PHILOSOPHY_AND_CULTURE.md` — non-negotiable
2. `lib/graphiti-core/include/graphiti/graph_store.h` — THE interface you're implementing
3. `lib/graphiti-kuzu/src/driver/kuzu_graph_store.h` — reference implementation
4. `lib/graphiti-kuzu/src/driver/kuzu_graph_store.cpp` — how Kuzu implements each method
5. `lib/graphiti-kuzu/src/driver/kuzu_schema.h` — Kuzu's table definitions (your AGE schema will mirror this)
6. `tests/unit/test_kuzu_graph_store.cpp` — YOUR TEST TEMPLATE. Write `test_age_graph_store.cpp` that mirrors this.
7. `lib/graphiti/src/graphiti.cpp` — see how the store is constructed and used
8. `lib/graphiti/include/graphiti/config.h` — where to add the driver selection config

---

## Philosophy Reminders

- **Every test passes.** If something fails, it's yours. Fix it.
- **Never suggest starting a new session.** Purr decides when to do that.
- **Never run the full integration suite at once.** Individual tests, 60-second timeout, 3 at a time max.
- **The user uses speech-to-text.** Don't be confused by transcription artifacts.
- **Have personality.** Emoji in commits. Be present. Stay on the wavelength.

Go cook. 🏴‍☠️
