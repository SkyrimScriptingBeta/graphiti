# 🔥 Kickoff: Kuzu Driver Assertions

## Your Mission

Write acceptance tests that replay recorded LLM/embedder fixtures into a `:memory:` Kuzu database, then query the raw database with Cypher to verify exact state.

## Step 1: Create the test file

`cpp_refactor/tests/integration/test_acceptance_kuzu.cpp`

Use this pattern for each test:

```cpp
#include <catch2/catch_all.hpp>
#include <graphiti/config.h>
#include <graphiti/graphiti.h>
#include <graphiti/recording_llm_client.h>
#include <graphiti/recording_embedder.h>
#include <main/kuzu.h>  // for raw Connection + query

// Fixture path helper — recordings are in tests/fixtures/recordings/
// Use __FILE__ to build a relative path, or hardcode from project root.

TEST_CASE("Acceptance: add_episode creates correct Kuzu state", "[acceptance]") {
    GraphitiConfig config;
    config.db_path = ":memory:";

    auto replay_llm = std::make_unique<ReplayLLMClient>(fixture_path("add_episode_hello_llm.json"));
    auto replay_embed = std::make_unique<ReplayEmbedder>(fixture_path("add_episode_hello_embedder.json"));

    Graphiti g(std::move(config), std::move(replay_llm), std::move(replay_embed));
    g.build_indices();

    auto result = g.add_episode({
        .name = "hello",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "acceptance_test",
    });
    REQUIRE(result.has_value());

    // === RAW KUZU ASSERTIONS ===
    auto conn = kuzu::main::Connection(g.database());

    // Entity nodes exist
    auto r = conn.query("MATCH (n:Entity) WHERE n.group_id = 'acceptance_test' RETURN n.name");
    // ... assert Alice and Acme exist

    // Edge exists
    auto r2 = conn.query("MATCH (a:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(b:Entity) "
                          "WHERE e.group_id = 'acceptance_test' RETURN e.fact");
    // ... assert the "works at" fact exists

    // Episodic node exists
    auto r3 = conn.query("MATCH (ep:Episodic) WHERE ep.group_id = 'acceptance_test' RETURN ep.name");
    // ... assert episode exists

    // MENTIONS edges exist
    auto r4 = conn.query("MATCH (ep:Episodic)-[:MENTIONS]->(n:Entity) "
                          "WHERE ep.group_id = 'acceptance_test' RETURN n.name");
    // ... assert both entities are mentioned
}
```

## Step 2: What to assert for each scenario

### add_episode_hello
- 2 Entity nodes (Alice, Acme Corp) with correct names, group_id, non-empty summaries
- 1+ RelatesToNode_ edges with a fact about "works at"
- 1 Episodic node with the episode content
- 2 MENTIONS edges (episode → Alice, episode → Acme)
- name_embedding populated on both Entity nodes
- fact_embedding populated on the RelatesToNode_

### multi_episode_dedup
- After 2 episodes, "Acme Corp" should exist ONCE (deduped), not twice
- Both episodes should have MENTIONS edges to the shared Acme node
- "Alice" and "Bob" should be separate entities

### add_episode_search
- Same as add_episode_hello, PLUS verify search returns results
- (Search is already tested via the API — the Kuzu assertion is about the data being findable)

### contradiction
- After episode 1: edge "Alice works at Acme" has NO expired_at
- After episode 2: that edge has expired_at SET, and a new edge "Alice works at Google" exists

### remove_episode
- After add: entities and edges exist
- After remove: episode node gone, MENTIONS edges gone, orphaned entities deleted

### add_triplet
- 2 Entity nodes with exact names
- 1 RelatesToNode_ edge with exact fact
- Embeddings populated on all three

### management_apis
- After add: overview returns nodes and edges
- After delete_group: MATCH (n) WHERE n.group_id = 'X' returns empty

## Step 3: Fixture path resolution

The recordings are at `cpp_refactor/tests/fixtures/recordings/`. You need a helper that resolves paths relative to the source file or project root. Options:

1. Use xmake to copy fixtures to build dir (cleanest)
2. Use `__FILE__` and walk up to find `tests/fixtures/recordings/`
3. Hardcode from known project root (quick and dirty)

Option 2 is probably fine for now:
```cpp
static std::filesystem::path fixture_path(const std::string& name) {
    auto dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / "recordings";
    return dir / name;
}
```

## Step 4: Build target

These tests should build as part of `graphiti_integration_tests` for now (same xmake target). They don't need an API key since they use replay. Tag them `[acceptance]` so they can be run separately.

## Key Kuzu Query Patterns

```cpp
auto conn = kuzu::main::Connection(g.database());

// Count entities in group
auto r = conn.query("MATCH (n:Entity) WHERE n.group_id = $gid RETURN count(n) AS cnt",
                     {{"gid", kuzu::common::Value("my_group")}});

// Get entity by name pattern
auto r = conn.query("MATCH (n:Entity) WHERE n.name CONTAINS 'Alice' AND n.group_id = $gid RETURN n",
                     {{"gid", kuzu::common::Value("my_group")}});

// Check edge between nodes
auto r = conn.query("MATCH (a:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(b:Entity) "
                     "WHERE e.group_id = $gid RETURN e.fact, a.name, b.name",
                     {{"gid", kuzu::common::Value("my_group")}});

// Check expired_at on edge
auto r = conn.query("MATCH (e:RelatesToNode_) WHERE e.group_id = $gid AND e.expired_at <> '' RETURN e.fact",
                     {{"gid", kuzu::common::Value("my_group")}});
```

Note: Kuzu parameterized queries use `kuzu::common::Value`. Check `kuzu_driver.cpp` for examples of how the existing driver constructs parameters.

## Don't Forget

- Read the Ethos first
- `xmake f --qt=C:/qt/6.10.2/msvc2022_64 -m release -p windows -a x64 -c -y` before building
- Run tests individually: `xmake run graphiti_integration_tests "[acceptance]"` or by name
- 60-second timeout per test, kill if stuck
- These tests should be FAST (no network) — if they take more than 2 seconds, something's wrong
- Commit early and often on the `graph-providers` branch

Go cook. 🏴‍☠️
