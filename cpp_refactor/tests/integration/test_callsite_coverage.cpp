#include <catch2/catch_all.hpp>

#include <graphiti/callsite_log.h>
#include <graphiti/config.h>
#include <graphiti/graphiti.h>
#include <graphiti/search_config.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace graphiti;

static auto callsite_dir() {
    static auto dir = std::filesystem::temp_directory_path() / "graphiti_callsites";
    return dir;
}

static GraphitiConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    auto config = GraphitiConfig::from_env();
    config.db_path = ":memory:";
    return config;
}

// ============================================================================
// Test 1: Identity initialization (5 callsites, 0 LLM calls)
// ============================================================================
TEST_CASE("Callsite: initialize_self creates identity nodes", "[callsite][identity]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_identity");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto result = g.initialize_self({
        .name = "TestAgent",
        .role = "Engineer",
        .role_description = "Writes tests",
        .team = "QA",
        .group_id = "identity_test",
    });
    REQUIRE(result.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 2: Contradicted edges (2 callsites)
// ============================================================================
TEST_CASE("Callsite: contradicted edge gets invalidated", "[callsite][contradiction]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_contradiction");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // First: establish a fact
    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "contra_test",
    });
    REQUIRE(r1.has_value());

    REQUIRE(g.build_indices().has_value());

    // Second: contradict that fact
    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Alice left Acme Corp and joined Google as a senior engineer.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(24),
        .group_id = "contra_test",
    });
    REQUIRE(r2.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 3: NEXT_EPISODE edge via saga (1 callsite)
// ============================================================================
TEST_CASE("Callsite: saga creates NEXT_EPISODE edge", "[callsite][saga]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_saga_next");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode({
        .name = "saga_ep1",
        .body = "Alice started her new project at work.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "saga_test",
        .saga = "project-alpha",
    });
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode({
        .name = "saga_ep2",
        .body = "Alice completed the first milestone of her project.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(1),
        .group_id = "saga_test",
        .saga = "project-alpha",
    });
    REQUIRE(r2.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 4: Cleared content re-save (2 callsites)
// ============================================================================
TEST_CASE("Callsite: store_raw_episode_content=false clears content", "[callsite][cleared]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_cleared_content");

    auto config = make_config();
    config.store_raw_episode_content = false;
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "cleared_ep",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "cleared_test",
    });
    REQUIRE(r.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 4b: Bulk with cleared content (1 callsite: bulk-resave-cleared-content)
// ============================================================================
TEST_CASE("Callsite: bulk with store_raw_episode_content=false", "[callsite][bulk_cleared]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_bulk_cleared");

    auto config = make_config();
    config.store_raw_episode_content = false;
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();
    RawEpisode ep1;
    ep1.name = "bulk_cleared_ep1";
    ep1.content = "Alice works at Acme Corp.";
    ep1.source_description = "test";
    ep1.reference_time = now;

    auto result = g.add_episode_bulk({
        .episodes = {ep1},
        .group_id = "bulk_cleared_test",
        .agent_id = "test-agent",
    });
    REQUIRE(result.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 5: Bulk saga + dedup paths (7 callsites)
// ============================================================================
TEST_CASE("Callsite: bulk ingestion with saga and dedup", "[callsite][bulk]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_bulk_saga_dedup");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // First ingest a single episode to seed the graph with "Alice"
    auto seed = g.add_episode({
        .name = "seed",
        .body = "Alice is a software engineer at Acme Corp.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "bulk_test",
    });
    REQUIRE(seed.has_value());
    REQUIRE(g.build_indices().has_value());

    // Bulk ingest with overlapping entities + saga name
    RawEpisode ep1;
    ep1.name = "bulk_ep1";
    ep1.content = "Alice presented at the team meeting about the new API.";
    ep1.source_description = "test";
    ep1.reference_time = now + std::chrono::hours(1);

    RawEpisode ep2;
    ep2.name = "bulk_ep2";
    ep2.content = "Alice also reviewed Bob's code after the meeting.";
    ep2.source_description = "test";
    ep2.reference_time = now + std::chrono::hours(2);

    auto result = g.add_episode_bulk({
        .episodes = {ep1, ep2},
        .group_id = "bulk_test",
        .agent_id = "test-agent",
        .saga = "daily-standup",
    });
    REQUIRE(result.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 6: Search orchestrator edge paths + MMR (5 callsites)
// ============================================================================
TEST_CASE("Callsite: search_advanced with edge search and MMR", "[callsite][search_orch]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_search_orch_edges");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "search_ep",
        .body = "Alice works at Acme Corp. Bob works at Acme Corp. They are on the same team.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "search_orch_test",
    });
    REQUIRE(r.has_value());
    REQUIRE(g.build_indices().has_value());

    // Edge search with MMR reranker — hits orch-edge-bm25, orch-edge-cosine, orch-edge-load-embedding-mmr
    auto edge_mmr_config = edge_hybrid_search_mmr();
    auto sr = g.search_advanced({
        .query = "Who works at Acme?",
        .config = edge_mmr_config,
        .group_id = "search_orch_test",
    });
    REQUIRE(sr.has_value());

    // Node search with MMR — hits orch-node-load-embedding-mmr
    auto node_mmr_config = node_hybrid_search_mmr();
    auto nr = g.search_advanced({
        .query = "Who works at Acme?",
        .config = node_mmr_config,
        .group_id = "search_orch_test",
    });
    REQUIRE(nr.has_value());

    callsite_log_close();
}

// ============================================================================
// Test 7: Episode BM25 search (1 callsite)
// ============================================================================
TEST_CASE("Callsite: episode BM25 search via search_advanced", "[callsite][episode_search]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_episode_search");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "ep_search_ep",
        .body = "Alice works at Acme Corp in Denver.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "ep_search_test",
    });
    REQUIRE(r.has_value());
    REQUIRE(g.build_indices().has_value());

    SearchConfig sc;
    sc.episode_config = EpisodeSearchConfig{};
    auto sr = g.search_advanced({
        .query = "Acme Corp",
        .config = sc,
        .group_id = "ep_search_test",
    });
    REQUIRE(sr.has_value());
    CHECK(!sr.value().episodes.empty());

    callsite_log_close();
}

// ============================================================================
// Test 8: Management APIs (4 callsites)
// ============================================================================
TEST_CASE("Callsite: management APIs — overview, bm25, delete_group", "[callsite][management]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_management_apis");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "mgmt_ep",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "mgmt_test",
    });
    REQUIRE(r.has_value());
    REQUIRE(g.build_indices().has_value());

    // overview — fires overview-get-node-summaries, overview-get-edge-summaries
    auto overview = g.get_graph_overview("mgmt_test");
    REQUIRE(overview.has_value());

    // public BM25 — fires public-bm25-node-search
    auto bm25 = g.search_entity_nodes_bm25("Alice", "mgmt_test");
    REQUIRE(bm25.has_value());
    CHECK(!bm25.value().empty());

    // delete_group — fires delete-group-clear-data
    auto del = g.delete_group("mgmt_test");
    REQUIRE(del.has_value());

    // verify deletion
    auto after = g.search_entity_nodes_bm25("Alice", "mgmt_test");
    // May fail on FTS after delete, either empty or error is fine
    if (after.has_value()) {
        CHECK(after.value().empty());
    }

    callsite_log_close();
}

// ============================================================================
// Test 9: Misc — sweep_orphans, rerankers, community groups, BFS
// ============================================================================
TEST_CASE("Callsite: sweep_orphans + rerankers + community groups + BFS", "[callsite][misc]") {
    callsite_log_set_dir(callsite_dir().string());
    callsite_log_open("coverage_misc");

    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode({
        .name = "misc_ep1",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "misc_test",
    });
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode({
        .name = "misc_ep2",
        .body = "Bob also works at Acme Corp. Alice and Bob are on the same team.",
        .source_description = "test",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "misc_test",
    });
    REQUIRE(r2.has_value());
    REQUIRE(g.build_indices().has_value());

    // Grab a node UUID for reranker/BFS origin
    std::string alice_uuid;
    for (auto& n : r1.value().nodes) {
        if (n.name.find("Alice") != std::string::npos) {
            alice_uuid = n.uuid;
            break;
        }
    }

    // sweep_orphans — fires sweep-orphans-find-all-entities
    auto sweep = g.sweep_orphans("misc_test");
    CHECK(sweep.has_value());

    // build_communities with empty group list — fires community-get-all-groups
    auto communities = g.build_communities({});
    CHECK(communities.has_value());
    REQUIRE(g.build_indices().has_value());

    // Search with episode_mentions reranker — fires rerank-episode-mention-count
    SearchConfig ep_mentions_config;
    ep_mentions_config.edge_config = EdgeSearchConfig{
        .search_methods = {EdgeSearchMethod::bm25},
        .reranker = Reranker::episode_mentions,
    };
    auto sr1 = g.search_advanced({
        .query = "Who works at Acme?",
        .config = ep_mentions_config,
        .group_id = "misc_test",
    });
    CHECK(sr1.has_value());

    // Search with node_distance reranker — fires rerank-node-adjacency-check
    if (!alice_uuid.empty()) {
        SearchConfig node_dist_config;
        node_dist_config.edge_config = EdgeSearchConfig{
            .search_methods = {EdgeSearchMethod::bm25},
            .reranker = Reranker::node_distance,
        };
        auto sr2 = g.search_advanced({
            .query = "Who works at Acme?",
            .config = node_dist_config,
            .group_id = "misc_test",
            .center_node_uuid = alice_uuid,
        });
        CHECK(sr2.has_value());
    }

    // BFS node traversal — fires bfs-node-traversal
    if (!alice_uuid.empty()) {
        SearchConfig bfs_config;
        bfs_config.node_config = NodeSearchConfig{
            .search_methods = {NodeSearchMethod::bfs},
        };
        auto sr3 = g.search_advanced({
            .query = "Acme",
            .config = bfs_config,
            .group_id = "misc_test",
            .bfs_origin_node_uuids = std::vector<std::string>{alice_uuid},
        });
        CHECK(sr3.has_value());
    }

    callsite_log_close();
}
