# Every Kuzu Invocation in the Graphiti Application

Every single call to the datastore (KuzuDriver or writer_client) from the application code.
Excludes: the driver implementation itself, tests, and the kuzu_writer_server.

---

## graphiti.cpp — Core Pipeline

### build_indices()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 1 | 158 | `driver.setup_schema()` | None | Checked for error | Creates all node/rel tables if they don't exist | ⚡CALLSITE:build-indices-setup-schema |
| 2 | 160 | `driver.build_fts_indices()` | None | Returned to caller | Drops and recreates FTS indices | ⚡CALLSITE:build-indices-fts |

### initialize_self()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 3 | 178 | `save_entity_node(self_node)` | EntityNode: is_system=true, name="Self", uuid="self" | Discarded | Creates the Self identity anchor node | ⚡CALLSITE:init-self-save-self-node |
| 4 | 198 | `save_entity_node(person_node)` | EntityNode: is_identity=true, name=agent name | Discarded | Creates Person entity representing the agent | ⚡CALLSITE:init-self-save-person-node |
| 5 | 213 | `save_entity_node(role_node)` | EntityNode: agent role entity | Discarded | Creates Role entity for agent | ⚡CALLSITE:init-self-save-role-node |
| 6 | 231 | `save_entity_edge(edge)` | EntityEdge: one of SAME_AS, HAS_ROLE, HAS_ROLE (x3 edges total) | Discarded | Wires Self→Person, Self→Role, Person→Role | ⚡CALLSITE:init-self-save-identity-edges |
| 7 | 253 | `build_fts_indices()` | None | Discarded | Rebuilds FTS so extraction can find Self | ⚡CALLSITE:init-self-rebuild-fts |

### sweep_orphans()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 8 | 265 | `driver.search_entity_nodes_bm25("*", gid, 500)` | Wildcard query, group_id, limit=500 | Stored — find orphaned entities | Gets all entities in group to identify orphans (no edges) | ⚡CALLSITE:sweep-orphans-find-all-entities |

### add_episode() — Single Episode Ingestion

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 9 | 438 | `driver.retrieve_episodes(gid, reference_time, 10, source)` | group_id, reference_time, last_n=10, source type | Stored → JSON for LLM context | Step 1: get previous episodes for temporal context | ⚡CALLSITE:episode-retrieve-prior-episodes |
| 10 | 472 | `save_episodic_node(episode)` | EpisodicNode: content, source, valid_at, agent_id, source_id, participant_ids | Checked for error | Step 2: persist the episode record before extraction | ⚡CALLSITE:episode-save-episodic-node |
| 11 | 808 | `get_entity_node(node.uuid)` | UUID of dedup-matched node | If exists, merge attribution | Step 9: fetch existing node to merge agent_ids, source_ids, etc. | ⚡CALLSITE:episode-fetch-existing-for-merge |
| 12 | 843 | `save_entity_node(ex)` | Existing node with merged agent_ids, source_ids, source_contexts, participant_ids, traits | Discarded | Step 9: persist merged existing node | ⚡CALLSITE:episode-save-merged-entity |
| 13 | 850 | `save_entity_node(node)` | New EntityNode with all fields | Discarded | Step 9: persist new (non-dedup) nodes | ⚡CALLSITE:episode-save-new-entity |
| 14 | 856 | `save_entity_node_embedding(uuid, embedding)` | UUID, float vector | Discarded | Step 9: store name embedding for cosine search | ⚡CALLSITE:episode-save-entity-embedding |
| 15 | 868 | `save_entity_edge(edge)` | EntityEdge: source_uuid, target_uuid, fact, episodes list | Discarded | Step 9: persist extracted edges | ⚡CALLSITE:episode-save-entity-edge |
| 16 | 873 | `save_entity_edge_embedding(uuid, embedding)` | UUID, float vector | Discarded | Step 9: store fact embedding for cosine search | ⚡CALLSITE:episode-save-edge-embedding |
| 17 | 879 | `driver.get_entity_edge(uuid)` | Edge UUID of contradicted edge | If exists, mark invalid | Step 9: fetch contradicted edges to set expired_at/invalid_at | ⚡CALLSITE:episode-fetch-contradicted-edge |
| 18 | 887 | `save_entity_edge(edge)` | Edge with expired_at and invalid_at set | Discarded | Step 9: persist invalidated/contradicted edges (soft delete) | ⚡CALLSITE:episode-invalidate-contradicted-edge |
| 19 | 908 | `save_episodic_edge(ee)` | EpisodicEdge: episode→entity MENTIONS relationship | Discarded | Step 10: create MENTIONS edges (writer path only) | ⚡CALLSITE:episode-save-mentions-edge |
| 20 | 920 | `build_fts_indices()` | None | Discarded | Step 10b: rebuild FTS after entity inserts | ⚡CALLSITE:episode-rebuild-fts-after-persist |
| 21 | 930 | `get_saga_by_name(name, gid)` | Saga name, group_id | If not found, create new | Step 11: find or create saga | ⚡CALLSITE:episode-find-saga |
| 22 | 941 | `save_saga_node(saga_node)` | New SagaNode | Discarded | Step 11: persist new saga | ⚡CALLSITE:episode-save-saga-node |
| 23 | 952 | `get_last_episode_in_saga(saga_uuid, exclude)` | Saga UUID, current episode UUID to exclude | If valid, chain episodes | Step 11: find previous episode for NEXT_EPISODE edge | ⚡CALLSITE:episode-find-last-in-saga |
| 24 | 962 | `save_next_episode_edge(uuid, prev, current, gid, now)` | Generated UUID, prev episode UUID, current episode UUID, group_id, timestamp | Discarded | Step 11: link episodes in sequence | ⚡CALLSITE:episode-save-next-episode-edge |
| 25 | 971 | `save_has_episode_edge(uuid, saga_uuid, ep_uuid, gid, now)` | Generated UUID, saga UUID, episode UUID, group_id, timestamp | Discarded | Step 11: link saga to episode | ⚡CALLSITE:episode-save-has-episode-edge |
| 26 | 993 | `save_episodic_node(episode)` | EpisodicNode with content cleared | Discarded | Post-step 11: re-save episode with cleared content if raw storage disabled | ⚡CALLSITE:episode-resave-cleared-content |

### add_episode_bulk() — Bulk Episode Ingestion

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 27 | 1055 | `save_episodic_node(ep)` | EpisodicNode | Checked for error | Step 1: persist all episodes before extraction | ⚡CALLSITE:bulk-save-episodic-node |
| 28 | 1073 | `driver.retrieve_episodes(gid, ep.valid_at, 10, ep.source)` | group_id, reference_time, last_n=10, source | Stored → JSON context per episode | Step 2: temporal context per episode | ⚡CALLSITE:bulk-retrieve-prior-episodes |
| 29 | 1526 | `get_entity_node(node.uuid)` | Node UUID | If exists, merge attribution | Step 9: fetch existing for dedup merge | ⚡CALLSITE:bulk-fetch-existing-for-merge |
| 30 | 1561 | `save_entity_node(ex)` | Existing node with merged IDs | Discarded | Step 9: persist merged existing | ⚡CALLSITE:bulk-save-merged-entity |
| 31 | 1568 | `save_entity_node(node)` | New EntityNode | Discarded | Step 9: persist new nodes | ⚡CALLSITE:bulk-save-new-entity |
| 32 | 1574 | `save_entity_node_embedding(uuid, embedding)` | UUID, float vector | Discarded | Step 9: store name embeddings | ⚡CALLSITE:bulk-save-entity-embedding |
| 33 | 1582 | `save_entity_edge(edge)` | EntityEdge | Discarded | Step 9: persist edges | ⚡CALLSITE:bulk-save-entity-edge |
| 34 | 1587 | `save_entity_edge_embedding(uuid, embedding)` | UUID, float vector | Discarded | Step 9: store edge embeddings | ⚡CALLSITE:bulk-save-edge-embedding |
| 35 | 1628 | `save_episodic_edge(edge)` | EpisodicEdge MENTIONS | Discarded | Step 10: create MENTIONS edges (writer path) | ⚡CALLSITE:bulk-save-mentions-edge |
| 36 | 1641 | `build_fts_indices()` | None | Discarded | Step 10b: rebuild FTS after bulk inserts | ⚡CALLSITE:bulk-rebuild-fts-after-persist |
| 37 | 1651 | `get_saga_by_name(name, gid)` | Saga name, group_id | Check existence | Step 11: find/create saga | ⚡CALLSITE:bulk-find-saga |
| 38 | 1662 | `save_saga_node(saga_node)` | SagaNode | Discarded | Step 11: persist saga | ⚡CALLSITE:bulk-save-saga-node |
| 39 | 1676 | `get_last_episode_in_saga(saga_uuid)` | Saga UUID (no exclude in bulk) | Chain episodes | Step 11: find last episode in saga | ⚡CALLSITE:bulk-find-last-in-saga |
| 40 | 1687 | `save_next_episode_edge(uuid, prev, current, gid, now)` | UUIDs, group_id, timestamp | Discarded | Step 11: NEXT_EPISODE edge | ⚡CALLSITE:bulk-save-next-episode-edge |
| 41 | 1695 | `save_has_episode_edge(uuid, saga_uuid, ep_uuid, gid, now)` | UUIDs, group_id, timestamp | Discarded | Step 11: HAS_EPISODE edge | ⚡CALLSITE:bulk-save-has-episode-edge |
| 42 | 1711 | `save_episodic_node(ep)` | EpisodicNode with cleared content | Discarded | Post-step 11: clear raw content | ⚡CALLSITE:bulk-resave-cleared-content |

### delete_group()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 43 | 1775 | `driver.clear_data({group_id})` | Vector with single group_id | Returned to caller | Deletes all data in a group | ⚡CALLSITE:delete-group-clear-data |

### retrieve_episodes()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 44 | 1793 | `driver.retrieve_episodes_by_saga(saga, gid, ref_time, last_n)` | Saga name, group_id, reference_time, last_n | Returned to caller | Retrieve episodes filtered by saga | ⚡CALLSITE:retrieve-episodes-by-saga |
| 45 | 1798 | `driver.retrieve_episodes(gid, ref_time, last_n, source)` | group_id, reference_time, last_n, source type | Returned to caller | Retrieve episodes by time/source | ⚡CALLSITE:retrieve-episodes-by-time |

### get_nodes_and_edges_by_episode()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 46 | 1814 | `driver.get_episodic_node(ep_uuid)` | Episode UUID | Added to results | Fetch the episode itself | ⚡CALLSITE:get-by-episode-fetch-episode |
| 47 | 1820 | `driver.get_edge_uuids_by_episode(ep_uuid)` | Episode UUID | UUIDs → fetch edges | Get edge UUIDs referenced by episode | ⚡CALLSITE:get-by-episode-edge-uuids |
| 48 | 1822 | `driver.get_entity_edges(edge_uuids)` | Vector of edge UUIDs | Edges added to results | Fetch actual edge objects | ⚡CALLSITE:get-by-episode-fetch-edges |
| 49 | 1831 | `driver.get_mentioned_entity_uuids(ep_uuid)` | Episode UUID | UUIDs → fetch nodes | Get entity UUIDs mentioned by episode | ⚡CALLSITE:get-by-episode-mentioned-uuids |
| 50 | 1833 | `driver.get_entity_nodes(node_uuids)` | Vector of node UUIDs | Nodes added to results | Fetch actual node objects | ⚡CALLSITE:get-by-episode-fetch-nodes |

### remove_episode()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 51 | 1853 | `driver.get_edge_uuids_by_episode(episode_uuid)` | Episode UUID | UUIDs iterated | Get edges to check for deletion | ⚡CALLSITE:remove-episode-get-edge-uuids |
| 52 | 1856 | `driver.get_entity_edge(edge_uuid)` | Edge UUID | Check if first episode matches | Fetch edge to decide if it should be deleted | ⚡CALLSITE:remove-episode-check-edge-origin |
| 53 | 1861 | `driver.delete_entity_edge(edge_uuid)` | Edge UUID | Discarded | Hard delete edge if first created by this episode | ⚡CALLSITE:remove-episode-delete-edge |
| 54 | 1868 | `driver.get_mentioned_entity_uuids(episode_uuid)` | Episode UUID | UUIDs iterated | Get entities to check for orphan deletion | ⚡CALLSITE:remove-episode-get-mentioned-uuids |
| 55 | 1873 | `driver.delete_entity_node(node_uuid)` | Node UUID | Discarded | Hard delete orphaned node | ⚡CALLSITE:remove-episode-delete-orphan-node |
| 56 | 1879 | `driver.delete_episodic_node(episode_uuid)` | Episode UUID | Returned to caller | Hard delete episode (DETACH DELETE removes MENTIONS too) | ⚡CALLSITE:remove-episode-delete-episode |

### add_triplet()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 57 | 1918 | `driver.search_entity_nodes_cosine(embedding, gid, 0.9f, 1)` | Embedding, group_id, min_score=0.9, limit=1 | If match, reuse existing node | Semantic dedup — find existing node by similarity | ⚡CALLSITE:triplet-dedup-cosine-search |
| 58 | 1940 | `save_entity_node(source_node)` | Source EntityNode | Discarded | Persist source node of triplet | ⚡CALLSITE:triplet-save-source-node |
| 59 | 1945 | `save_entity_node_embedding(uuid, embedding)` | UUID, float vector | Discarded | Store source embedding | ⚡CALLSITE:triplet-save-source-embedding |
| 60 | 1953 | `save_entity_node(target_node)` | Target EntityNode | Discarded | Persist target node | ⚡CALLSITE:triplet-save-target-node |
| 61 | 1958 | `save_entity_node_embedding(uuid, embedding)` | UUID, float vector | Discarded | Store target embedding | ⚡CALLSITE:triplet-save-target-embedding |
| 62 | 1967 | `save_entity_edge(edge)` | EntityEdge source→target | Discarded | Persist the edge | ⚡CALLSITE:triplet-save-edge |
| 63 | 1972 | `save_entity_edge_embedding(uuid, embedding)` | UUID, float vector | Discarded | Store edge embedding | ⚡CALLSITE:triplet-save-edge-embedding |

### get_graph_overview()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 64 | 1992 | `driver.get_node_summaries_by_group(gid)` | group_id | Iterated for summary objects | Fetch lightweight node summaries | ⚡CALLSITE:overview-get-node-summaries |
| 65 | 2001 | `driver.get_edge_summaries_by_nodes(uuids, gid)` | Vector of node UUIDs, group_id | Filtered and sorted | Fetch lightweight edge summaries | ⚡CALLSITE:overview-get-edge-summaries |

### search_entity_nodes_bm25()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 66 | 2105 | `driver.search_entity_nodes_bm25(query, gid, limit, filters)` | Query, group_id, limit, filters | Returned to caller | Public wrapper for BM25 node search | ⚡CALLSITE:public-bm25-node-search |

---

## search/search.cpp — Search Orchestration

### hybrid_edge_search()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 67 | 44 | `driver.search_entity_edges_bm25(query, gid, limit, filters)` | Query, group_id, limit, filters | Edges → edge_map | BM25 text search on edge facts | ⚡CALLSITE:hybrid-edge-bm25 |
| 68 | 45 | `driver.search_entity_edges_cosine(embedding, gid, 0.0f, limit, filters)` | Embedding, group_id, min=0.0, limit, filters | Merged with BM25 | Cosine similarity on edge embeddings | ⚡CALLSITE:hybrid-edge-cosine |

### hybrid_node_search()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 69 | 145 | `driver.search_entity_nodes_bm25(query, gid, limit, filters)` | Query, group_id, limit, filters | Nodes → node_map | BM25 text search on nodes | ⚡CALLSITE:hybrid-node-bm25 |
| 70 | 146 | `driver.search_entity_nodes_cosine(embedding, gid, 0.0f, limit, filters)` | Embedding, group_id, min=0.0, limit, filters | Merged with BM25 | Cosine similarity on node embeddings | ⚡CALLSITE:hybrid-node-cosine |

### episode_search()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 71 | 211 | `driver.search_episodes_bm25(query, gid, limit)` | Query, group_id, limit | Rank-based scores assigned | BM25 on episode content | ⚡CALLSITE:episode-search-bm25 |

### search_orchestrator() — Edge Search

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 72 | 275 | `driver.search_entity_edges_bm25(query, gid, limit, filters)` | Query, group_id, limit, filters | → edge_map | Conditional BM25 edge search | ⚡CALLSITE:orch-edge-bm25 |
| 73 | 286 | `driver.search_entity_edges_cosine(embedding, gid, sim_min_score, limit, filters)` | Embedding, group_id, config min_score, limit, filters | → edge_map | Conditional cosine edge search | ⚡CALLSITE:orch-edge-cosine |
| 74 | 349 | `driver.load_entity_edge_embedding(uuid)` | Edge UUID (per edge in loop) | Embeddings → MMR reranker | Load cached embeddings for MMR diversity reranking | ⚡CALLSITE:orch-edge-load-embedding-mmr |

### search_orchestrator() — Node Search

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 75 | 437 | `driver.search_entity_nodes_bm25(query, gid, limit, filters)` | Query, group_id, limit, filters | → node_map | Conditional BM25 node search | ⚡CALLSITE:orch-node-bm25 |
| 76 | 448 | `driver.search_entity_nodes_cosine(embedding, gid, sim_min_score, limit, filters)` | Embedding, group_id, config min_score, limit, filters | → node_map | Conditional cosine node search | ⚡CALLSITE:orch-node-cosine |
| 77 | 509 | `driver.load_entity_node_embedding(uuid)` | Node UUID (per node in loop) | Embeddings → MMR reranker | Load cached embeddings for MMR diversity reranking | ⚡CALLSITE:orch-node-load-embedding-mmr |

### search_orchestrator() — Community Search

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 78 | 598 | `driver.search_communities_bm25(query, gid, limit)` | Query, group_id, limit | → community_map | BM25 on community summaries | ⚡CALLSITE:orch-community-bm25 |
| 79 | 609 | `driver.search_communities_cosine(embedding, gid, sim_min_score, limit)` | Embedding, group_id, config min_score, limit | → community_map | Cosine on community embeddings | ⚡CALLSITE:orch-community-cosine |

### search_orchestrator() — Node Resolution

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 80 | 651 | `driver.get_entity_nodes(missing_uuids)` | Vector of node UUIDs not in results | Appended with score=0.0 | Fetch nodes referenced by edges but not in search results | ⚡CALLSITE:orch-resolve-missing-nodes |

---

## search/bfs_search.cpp — Graph Traversal

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 81 | 15 | `driver.search_entity_edges_bfs(origins, gid, max_depth, limit, filters)` | Origin UUIDs, group_id, max_depth, limit, filters | Returned to caller | BFS edge traversal from origin nodes | ⚡CALLSITE:bfs-edge-traversal |
| 82 | 26 | `driver.search_entity_nodes_bfs(origins, gid, max_depth, limit, filters)` | Origin UUIDs, group_id, max_depth, limit, filters | Returned to caller | BFS node traversal from origin nodes | ⚡CALLSITE:bfs-node-traversal |

---

## search/rerankers.cpp — Search Reranking

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 83 | 31 | `driver.count_episode_mentions(uuid)` | Node/edge UUID (per item in loop) | Count → score | Episode frequency scoring for reranking | ⚡CALLSITE:rerank-episode-mention-count |
| 84 | 79 | `driver.check_node_adjacency(center_uuid, uuid)` | Center node UUID, candidate UUID (per item) | Bool → distance score | Node distance reranking — connected=1.0, center=10.0, else=0.0 | ⚡CALLSITE:rerank-node-adjacency-check |

---

## pipeline/community_ops.cpp — Community Detection

### get_community_clusters()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 85 | 195 | `driver.get_all_group_ids()` | None | All group IDs | Get groups when none specified | ⚡CALLSITE:community-get-all-groups |
| 86 | 204 | `driver.get_entity_nodes_by_group(gid)` | group_id | Nodes → label propagation | Fetch all entities in group for clustering | ⚡CALLSITE:community-get-entities-for-clustering |
| 87 | 214 | `driver.get_entity_neighbors(node.uuid, gid)` | Entity UUID, group_id | Neighbors → weighted projection | Build adjacency for label propagation | ⚡CALLSITE:community-get-entity-neighbors |
| 88 | 229 | `driver.get_entity_nodes(uuid_cluster)` | Vector of UUIDs from cluster | Resolved EntityNodes | Convert UUID clusters back to full objects | ⚡CALLSITE:community-resolve-cluster-nodes |

### remove_communities()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 89 | 245 | `driver.remove_all_communities()` | None | Checked for error | Delete all communities before rebuild | ⚡CALLSITE:community-remove-all |

### build_communities()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 90 | 368 | `driver.save_community_node(node)` | CommunityNode with UUID, name, summary | Checked for error | Persist new community | ⚡CALLSITE:community-save-node |
| 91 | 372 | `driver.save_community_node_embedding(uuid, embedding)` | UUID, float vector | Discarded | Store community name embedding | ⚡CALLSITE:community-save-node-embedding |
| 92 | 376 | `driver.save_community_edge(edge)` | CommunityEdge HAS_MEMBER (per member in loop) | Discarded | Link community to member entities | ⚡CALLSITE:community-save-member-edge |

### determine_entity_community()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 93 | 397 | `driver.get_entity_community(entity_uuid)` | Entity UUID | If exists, return early | Check if entity already in a community | ⚡CALLSITE:community-check-existing-membership |
| 94 | 405 | `driver.get_neighbor_communities(entity_uuid)` | Entity UUID | Communities → mode finding | Find most common community among neighbors | ⚡CALLSITE:community-find-neighbor-communities |

### update_community()

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 95 | 478 | `driver.save_community_edge(edge)` | CommunityEdge HAS_MEMBER | Checked for error | Add entity to community (if new) | ⚡CALLSITE:community-update-add-member-edge |
| 96 | 491 | `driver.save_community_node(community)` | Updated CommunityNode | Checked for error | Persist community with merged summary | ⚡CALLSITE:community-update-save-node |
| 97 | 495 | `driver.save_community_node_embedding(uuid, embedding)` | UUID, regenerated embedding | Discarded | Update embedding after name change | ⚡CALLSITE:community-update-save-embedding |

---

## pipeline/dedupe_nodes.cpp — Node Deduplication

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 98 | 44 | `driver.search_entity_nodes_bm25(name, gid, 10)` | Node name as query, group_id, limit=10 | Candidates filtered (exclude system nodes) | Find existing nodes with similar names for dedup | ⚡CALLSITE:dedupe-nodes-find-candidates |

---

## pipeline/dedupe_edges.cpp — Edge Deduplication

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 99 | 29 | `driver.get_edges_between_nodes(source_uuid, target_uuid)` | Source node UUID, target node UUID | If empty → keep new; if exists → LLM dedup | Find existing edges between same node pair | ⚡CALLSITE:dedupe-edges-find-existing |

---

## pipeline/episodic_edges.cpp — Episodic Relationships

| # | Line | Method | Parameters | Return Usage | Why | ID |
|---|------|--------|------------|-------------|-----|----|
| 100 | 27 | `driver.save_episodic_edge(edge)` | EpisodicEdge MENTIONS (per entity in loop) | Checked for error | Create MENTIONS edge from episode to each entity | ⚡CALLSITE:episodic-edges-save-mentions |

---

## Summary

**Total application-level Kuzu invocations: 100**

### By Category

| Category | Count | Description |
|----------|-------|-------------|
| **Entity Node writes** | 14 | save_entity_node, save_entity_node_embedding, delete_entity_node |
| **Entity Edge writes** | 10 | save_entity_edge, save_entity_edge_embedding, delete_entity_edge |
| **Episodic Node writes** | 5 | save_episodic_node |
| **Episodic Edge writes** | 3 | save_episodic_edge |
| **Community writes** | 8 | save_community_node, save_community_node_embedding, save_community_edge, remove_all |
| **Saga writes** | 6 | save_saga_node, save_has_episode_edge, save_next_episode_edge |
| **Schema/Index** | 5 | setup_schema, build_fts_indices |
| **Entity reads** | 8 | get_entity_node, get_entity_nodes, get_entity_edge, get_edge_uuids, get_mentioned_uuids |
| **Search (BM25)** | 9 | search_entity_nodes_bm25, search_entity_edges_bm25, search_episodes_bm25, search_communities_bm25 |
| **Search (cosine)** | 6 | search_entity_nodes_cosine, search_entity_edges_cosine, search_communities_cosine |
| **Search (BFS)** | 2 | search_entity_edges_bfs, search_entity_nodes_bfs |
| **Search (other)** | 4 | load embeddings for MMR reranking |
| **Reranker queries** | 2 | count_episode_mentions, check_node_adjacency |
| **Community reads** | 6 | get_entity_nodes_by_group, get_entity_neighbors, get_entity_community, get_neighbor_communities, get_all_group_ids |
| **Episode reads** | 4 | retrieve_episodes, retrieve_episodes_by_saga |
| **Overview reads** | 2 | get_node_summaries_by_group, get_edge_summaries_by_nodes |
| **Dedup queries** | 2 | search_entity_nodes_bm25 (for dedup), get_edges_between_nodes |
| **Maintenance** | 2 | clear_data, remove_all_communities |
| **Triplet dedup** | 2 | search_entity_nodes_cosine (for triplet dedup) |

### By Caller

| Caller | Invocations |
|--------|-------------|
| `graphiti.cpp::add_episode()` | 18 |
| `graphiti.cpp::add_episode_bulk()` | 16 |
| `search/search.cpp::search_orchestrator()` | 12 |
| `pipeline/community_ops.cpp` (all functions) | 13 |
| `graphiti.cpp::add_triplet()` | 7 |
| `graphiti.cpp::remove_episode()` | 6 |
| `graphiti.cpp::initialize_self()` | 5 |
| `graphiti.cpp::get_nodes_and_edges_by_episode()` | 5 |
| `search/search.cpp::hybrid_edge_search()` | 2 |
| `search/search.cpp::hybrid_node_search()` | 2 |
| `search/bfs_search.cpp` | 2 |
| `search/rerankers.cpp` | 2 |
| `graphiti.cpp::build_indices()` | 2 |
| `graphiti.cpp::get_graph_overview()` | 2 |
| `graphiti.cpp::retrieve_episodes()` | 2 |
| `pipeline/dedupe_nodes.cpp` | 1 |
| `pipeline/dedupe_edges.cpp` | 1 |
| `pipeline/episodic_edges.cpp` | 1 |
| `graphiti.cpp::sweep_orphans()` | 1 |
| `graphiti.cpp::search_entity_nodes_bm25()` | 1 |
| `graphiti.cpp::delete_group()` | 1 |
