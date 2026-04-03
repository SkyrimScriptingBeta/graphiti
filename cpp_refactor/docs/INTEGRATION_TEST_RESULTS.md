# Integration Test Results — Individual Isolation Run

Run date: 2026-04-03
Branch: `graph-providers`
LLM: `gpt-4.1-mini` via `api.openai.com`
Embedder: `text-embedding-3-small` (1024 dim)
Timeout: 60 seconds per test

## Test Results

| # | Test Name | Result |
|---|-----------|--------|
| 1 | OpenAI Embedder: single embedding | ✅ PASS |
| 2 | OpenAI Embedder: batch embedding | ✅ PASS |
| 3 | OpenAI Embedder: dimension truncation | ✅ PASS |
| 4 | OpenAI LLM: simple JSON response | ✅ PASS |
| 5 | OpenAI LLM: structured output with schema | ✅ PASS |
| 6 | OpenAI LLM: small model | ✅ PASS |
| 7 | OpenAI LLM: bad API key returns error | ✅ PASS |
| 8 | Graphiti: full add_episode + search | ✅ PASS |
| 9 | Graphiti: multi-episode ingestion | ✅ PASS |
| 10 | retrieve_episodes: basic retrieval | ✅ PASS |
| 11 | retrieve_episodes: respects last_n limit | ✅ PASS |
| 12 | retrieve_episodes: with saga filter | ✅ PASS |
| 13 | get_nodes_and_edges_by_episode: returns nodes and edges | ✅ PASS |
| 14 | remove_episode: deletes episode and orphaned entities | ✅ PASS |
| 15 | remove_episode: preserves nodes shared with other episodes | ✅ PASS |
| 16 | add_triplet: inserts manual triplet | ✅ PASS |
| 17 | add_triplet: deduplicates against existing nodes | ✅ PASS |
| 18 | token_tracker: tracks usage after add_episode | ✅ PASS |
| 19 | token_tracker: accumulates across multiple episodes | ✅ PASS |
| 20 | source_id and participant_ids: set on episode | ✅ PASS |
| 21 | source_id and participant_ids: propagated to entities | ✅ PASS |
| 22 | source_id and participant_ids: propagated to edges | ✅ PASS |
| 23 | source_id and participant_ids: search filter by source_ids | ✅ PASS |
| 24 | source_id and participant_ids: search filter by participant_ids | ✅ PASS |
| 25 | source_id and participant_ids: dedup merge | ✅ PASS |
| 26 | source_id and participant_ids: bulk episode per-episode override | ✅ PASS |
| 27 | Custom types: entities get labels from type_defs | ✅ PASS |
| 28 | Custom types: attributes populated for typed entities | ✅ PASS |
| 29 | Custom types: edge types guide extraction | ✅ PASS |
| 30 | Custom types: excluded Entity type filters nodes | ✅ PASS |
| 31 | Custom types: search by label works with custom types | ✅ PASS |
| 32 | Multi-agent dedup merges agent_ids on shared entities | ❌ FAIL |
| 33 | Communities: build_communities after ingest | ✅ PASS |
| 34 | Communities: community search via search_advanced | ✅ PASS |
| 35 | Communities: build_communities on empty graph | ✅ PASS |
| 36 | Communities: rebuild communities replaces old ones | ✅ PASS |
| 37 | Communities: update_community during add_episode | ✅ PASS |

**Summary: 36 passed, 1 failed, 0 timed out**

### Failed Test Details

**Multi-agent dedup merges agent_ids on shared entities** — `agent_ids` not persisting to Kuzu. The dedup correctly identifies the shared entity (same UUID from both episodes), but after persisting, the `agent_ids` field comes back empty. This is the same bug seen in the unit tests (`test_agent_attribution.cpp`). Root cause is in the Kuzu driver's handling of `agent_ids` array property.

---

## Callsite Coverage

**64 of 100 callsites covered** by existing integration tests.

### Covered (64)

- [x] build-indices-setup-schema
- [x] build-indices-fts
- [x] episode-retrieve-prior-episodes
- [x] episode-save-episodic-node
- [x] episode-fetch-existing-for-merge
- [x] episode-save-merged-entity
- [x] episode-save-new-entity
- [x] episode-save-entity-embedding
- [x] episode-save-entity-edge
- [x] episode-save-edge-embedding
- [x] episode-find-saga
- [x] episode-save-saga-node
- [x] episode-find-last-in-saga
- [x] episode-save-has-episode-edge
- [x] episode-rebuild-fts-after-persist
- [x] episodic-edges-save-mentions
- [x] dedupe-nodes-find-candidates
- [x] dedupe-edges-find-existing
- [x] bulk-save-episodic-node
- [x] bulk-retrieve-prior-episodes
- [x] bulk-save-new-entity
- [x] bulk-save-entity-embedding
- [x] bulk-save-entity-edge
- [x] bulk-save-edge-embedding
- [x] bulk-rebuild-fts-after-persist
- [x] retrieve-episodes-by-saga
- [x] retrieve-episodes-by-time
- [x] get-by-episode-fetch-episode
- [x] get-by-episode-edge-uuids
- [x] get-by-episode-fetch-edges
- [x] get-by-episode-mentioned-uuids
- [x] get-by-episode-fetch-nodes
- [x] remove-episode-get-edge-uuids
- [x] remove-episode-check-edge-origin
- [x] remove-episode-delete-edge
- [x] remove-episode-get-mentioned-uuids
- [x] remove-episode-delete-orphan-node
- [x] remove-episode-delete-episode
- [x] triplet-dedup-cosine-search
- [x] triplet-save-source-node
- [x] triplet-save-source-embedding
- [x] triplet-save-target-node
- [x] triplet-save-target-embedding
- [x] triplet-save-edge
- [x] triplet-save-edge-embedding
- [x] hybrid-edge-bm25
- [x] hybrid-edge-cosine
- [x] orch-node-bm25
- [x] orch-node-cosine
- [x] orch-community-bm25
- [x] orch-community-cosine
- [x] bfs-edge-traversal
- [x] community-get-entities-for-clustering
- [x] community-get-entity-neighbors
- [x] community-resolve-cluster-nodes
- [x] community-remove-all
- [x] community-save-node
- [x] community-save-node-embedding
- [x] community-save-member-edge
- [x] community-check-existing-membership
- [x] community-find-neighbor-communities
- [x] community-update-add-member-edge
- [x] community-update-save-node
- [x] community-update-save-embedding

### Uncovered (36)

- [ ] init-self-save-self-node
- [ ] init-self-save-person-node
- [ ] init-self-save-role-node
- [ ] init-self-save-identity-edges
- [ ] init-self-rebuild-fts
- [ ] sweep-orphans-find-all-entities
- [ ] episode-fetch-contradicted-edge
- [ ] episode-invalidate-contradicted-edge
- [ ] episode-save-mentions-edge (writer path in graphiti.cpp — pipeline path IS covered)
- [ ] episode-save-next-episode-edge
- [ ] episode-resave-cleared-content
- [ ] episode-search-bm25
- [ ] bulk-fetch-existing-for-merge
- [ ] bulk-save-merged-entity
- [ ] bulk-save-mentions-edge (writer path)
- [ ] bulk-find-saga
- [ ] bulk-save-saga-node
- [ ] bulk-find-last-in-saga
- [ ] bulk-save-next-episode-edge
- [ ] bulk-save-has-episode-edge
- [ ] bulk-resave-cleared-content
- [ ] delete-group-clear-data
- [ ] overview-get-node-summaries
- [ ] overview-get-edge-summaries
- [ ] public-bm25-node-search
- [ ] hybrid-node-bm25
- [ ] hybrid-node-cosine
- [ ] orch-edge-bm25
- [ ] orch-edge-cosine
- [ ] orch-edge-load-embedding-mmr
- [ ] orch-node-load-embedding-mmr
- [ ] orch-resolve-missing-nodes
- [ ] bfs-node-traversal
- [ ] rerank-episode-mention-count
- [ ] rerank-node-adjacency-check
- [ ] community-get-all-groups

### Coverage Gaps by Category

| Category | Gap | Why |
|----------|-----|-----|
| **Self/Identity init** (5) | No test exercises `initialize_self()` | Tests don't configure agent identity |
| **Contradicted edges** (2) | No test ingests conflicting facts | Need multi-episode with fact contradiction |
| **NEXT_EPISODE edge** (2) | saga name provided but saga chaining not tested | Need 2+ episodes with same saga |
| **Writer-path mentions** (2) | Writer client path not tested | Only fires when `kuzu_writer_uri` is set |
| **Cleared content re-save** (2) | `store_raw_episode_content=false` not tested | Need config override |
| **Bulk saga/dedup paths** (7) | Bulk test doesn't trigger dedup or saga paths | Need bulk with overlapping entities + saga |
| **Search orchestrator edges** (5) | Edge-specific search paths not hit | Need `search_advanced` with edge config |
| **Search misc** (5) | MMR, BFS nodes, episode search, rerankers | Need specific search config recipes |
| **Management** (4) | delete_group, overview, public BM25, sweep | Need dedicated tests |
| **Community get_all_groups** (1) | Tests always specify group_id | Need test with empty group list |
