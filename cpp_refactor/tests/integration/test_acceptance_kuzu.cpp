// 🏴‍☠️ Acceptance Tests: Replay fixtures → assert raw Kuzu state via Cypher
//
// These tests use ReplayLLMClient + ReplayEmbedder with pre-recorded fixtures.
// No API key needed. No network calls. Deterministic. Fast.

#include <catch2/catch_all.hpp>

#include <graphiti/config.h>
#include <graphiti/graphiti.h>
#include <graphiti/recording_embedder.h>
#include <graphiti/recording_llm_client.h>

#include <main/kuzu.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace graphiti;

// ============================================================================
// Helpers
// ============================================================================

static std::filesystem::path fixture_path(const std::string& name) {
    auto dir = std::filesystem::path(GRAPHITI_SOURCE_DIR) / "tests" / "fixtures" / "recordings";
    return dir / name;
}

// Create a Graphiti instance with replay clients pointed at the named scenario.
// Returns the Graphiti instance — caller can access g.database() for raw queries.
static Graphiti make_replay_graphiti(const std::string& scenario_name) {
    GraphitiConfig config;
    config.db_path = ":memory:";

    auto llm = std::make_unique<ReplayLLMClient>(fixture_path(scenario_name + "_llm.json"));
    auto embedder = std::make_unique<ReplayEmbedder>(fixture_path(scenario_name + "_embedder.json"));

    return Graphiti(std::move(config), std::move(llm), std::move(embedder));
}

// Count rows returned by a Cypher query
static int64_t count_rows(kuzu::main::Connection& conn, const std::string& cypher) {
    auto result = conn.query(cypher);
    REQUIRE(result->isSuccess());
    int64_t count = 0;
    while (result->hasNext()) {
        result->getNext();
        ++count;
    }
    return count;
}

// Collect all string values from a single-column query
static std::vector<std::string> collect_strings(kuzu::main::Connection& conn, const std::string& cypher) {
    auto result = conn.query(cypher);
    REQUIRE(result->isSuccess());
    std::vector<std::string> values;
    while (result->hasNext()) {
        auto tuple = result->getNext();
        auto* val = tuple->getValue(0);
        if (!val->isNull()) {
            values.push_back(val->getValue<std::string>());
        }
    }
    return values;
}

// Check if any string in the vector contains a substring (case-insensitive)
static bool any_contains(const std::vector<std::string>& strings, const std::string& substr) {
    auto lower_substr = substr;
    for (auto& c : lower_substr) c = static_cast<char>(std::tolower(c));
    for (auto& s : strings) {
        auto lower = s;
        for (auto& c : lower) c = static_cast<char>(std::tolower(c));
        if (lower.find(lower_substr) != std::string::npos) return true;
    }
    return false;
}

// ============================================================================
// Acceptance: add_episode hello
// ============================================================================
TEST_CASE("Acceptance: add_episode creates correct Kuzu state", "[acceptance]") {
    auto g = make_replay_graphiti("add_episode_hello");
    REQUIRE(g.build_indices().has_value());

    auto result = g.add_episode({
        .name = "hello",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_test",
    });
    REQUIRE(result.has_value());

    auto conn = kuzu::main::Connection(&g.database());

    // --- Entity nodes ---
    auto entity_names = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_test' RETURN n.name");
    REQUIRE(entity_names.size() >= 2);
    CHECK(any_contains(entity_names, "Alice"));
    CHECK(any_contains(entity_names, "Acme"));

    // --- Entity summaries ---
    // Note: Some entities may have empty summaries after a single episode.
    // The LLM generates summaries in batch; short episodes may leave some blank.
    auto name_summary_pairs = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_test' RETURN n.name + ': ' + n.summary");
    int non_empty_summaries = 0;
    for (auto& ns : name_summary_pairs) {
        INFO("Entity: " << ns);
        auto colon_pos = ns.find(": ");
        if (colon_pos != std::string::npos && ns.size() > colon_pos + 2) {
            ++non_empty_summaries;
        }
    }
    CHECK(non_empty_summaries >= 1);

    // --- RelatesToNode_ edge with a fact ---
    auto facts = collect_strings(conn,
        "MATCH (a:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(b:Entity) "
        "WHERE e.group_id = 'rec_test' RETURN e.fact");
    for (auto& f : facts) {
        INFO("Edge fact: '" << f << "'");
    }
    REQUIRE(!facts.empty());
    // Fact should mention Alice, Acme, or the relationship between them
    bool has_relevant_fact = any_contains(facts, "alice") || any_contains(facts, "acme") ||
                             any_contains(facts, "work") || any_contains(facts, "engineer");
    CHECK(has_relevant_fact);

    // --- Episodic node ---
    auto ep_count = count_rows(conn,
        "MATCH (ep:Episodic) WHERE ep.group_id = 'rec_test' RETURN ep");
    CHECK(ep_count >= 1);

    // --- MENTIONS edges (episode → entities) ---
    auto mentioned = collect_strings(conn,
        "MATCH (ep:Episodic)-[:MENTIONS]->(n:Entity) "
        "WHERE ep.group_id = 'rec_test' RETURN n.name");
    REQUIRE(mentioned.size() >= 2);
    CHECK(any_contains(mentioned, "Alice"));
    CHECK(any_contains(mentioned, "Acme"));

    // --- name_embedding populated on Entity nodes ---
    auto embedding_count = count_rows(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_test' "
        "AND n.name_embedding IS NOT NULL AND size(n.name_embedding) > 0 "
        "RETURN n");
    CHECK(embedding_count >= 2);

    // --- fact_embedding populated on RelatesToNode_ ---
    auto fact_embed_count = count_rows(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_test' "
        "AND e.fact_embedding IS NOT NULL AND size(e.fact_embedding) > 0 "
        "RETURN e");
    CHECK(fact_embed_count >= 1);
}

// ============================================================================
// Acceptance: multi-episode dedup
// ============================================================================
TEST_CASE("Acceptance: multi-episode dedup merges shared entities", "[acceptance]") {
    auto g = make_replay_graphiti("multi_episode_dedup");
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Episode 1: "Alice works at Acme Corp."
    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "rec_dedup",
    });
    REQUIRE(r1.has_value());
    REQUIRE(g.build_indices().has_value());

    // Episode 2: "Bob also works at Acme Corp with Alice."
    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Bob also works at Acme Corp with Alice.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(1),
        .group_id = "rec_dedup",
    });
    REQUIRE(r2.has_value());

    auto conn = kuzu::main::Connection(&g.database());

    // --- Acme should exist exactly ONCE (deduped) ---
    auto acme_names = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_dedup' "
        "AND lower(n.name) CONTAINS 'acme' RETURN n.name");
    CHECK(acme_names.size() == 1);

    // --- Alice and Bob should be separate entities ---
    auto all_names = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_dedup' RETURN n.name");
    CHECK(any_contains(all_names, "Alice"));
    CHECK(any_contains(all_names, "Bob"));

    // --- Both episodes exist ---
    auto ep_count = count_rows(conn,
        "MATCH (ep:Episodic) WHERE ep.group_id = 'rec_dedup' RETURN ep");
    CHECK(ep_count == 2);

    // --- Both episodes have MENTIONS edges to entities ---
    auto mentions_count = count_rows(conn,
        "MATCH (ep:Episodic)-[:MENTIONS]->(n:Entity) "
        "WHERE ep.group_id = 'rec_dedup' RETURN ep.name, n.name");
    CHECK(mentions_count >= 4);  // 2 episodes × 2+ entities each
}

// ============================================================================
// Acceptance: add_episode + search
// ============================================================================
TEST_CASE("Acceptance: add_episode then search returns results", "[acceptance]") {
    auto g = make_replay_graphiti("add_episode_search");
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "search_ep",
        .body = "Alice works at Acme Corp as a software engineer in Denver.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_search",
    });
    REQUIRE(r.has_value());
    REQUIRE(g.build_indices().has_value());

    // --- Entities created ---
    auto conn = kuzu::main::Connection(&g.database());
    auto entity_names = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_search' RETURN n.name");
    REQUIRE(entity_names.size() >= 2);
    CHECK(any_contains(entity_names, "Alice"));

    // --- Search finds results ---
    auto sr = g.search({.query = "Where does Alice work?", .group_id = "rec_search"});
    REQUIRE(sr.has_value());
    CHECK(!sr.value().empty());

    // --- At least one search result mentions Alice or Acme ---
    bool found_relevant = false;
    for (auto& edge : sr.value()) {
        auto fact_lower = edge.fact;
        for (auto& c : fact_lower) c = static_cast<char>(std::tolower(c));
        if (fact_lower.find("alice") != std::string::npos ||
            fact_lower.find("acme") != std::string::npos) {
            found_relevant = true;
            break;
        }
    }
    CHECK(found_relevant);
}

// ============================================================================
// Acceptance: contradiction (edge invalidation)
// ============================================================================
TEST_CASE("Acceptance: contradiction invalidates old edge", "[acceptance]") {
    auto g = make_replay_graphiti("contradiction");
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Episode 1: Alice works at Acme
    auto r1 = g.add_episode({
        .name = "fact",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "rec_contra",
    });
    REQUIRE(r1.has_value());
    REQUIRE(g.build_indices().has_value());

    auto conn = kuzu::main::Connection(&g.database());

    // Before contradiction: edge exists with NO expired_at
    auto pre_facts = collect_strings(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_contra' "
        "AND e.expired_at IS NULL RETURN e.fact");
    REQUIRE(!pre_facts.empty());

    // Episode 2: Alice left Acme, joined Google
    auto r2 = g.add_episode({
        .name = "update",
        .body = "Alice left Acme Corp and joined Google as a senior engineer.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(24),
        .group_id = "rec_contra",
    });
    REQUIRE(r2.has_value());

    // After contradiction: at least one edge should have expired_at SET
    auto expired_facts = collect_strings(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_contra' "
        "AND e.expired_at IS NOT NULL RETURN e.fact");
    CHECK(!expired_facts.empty());

    // And there should be a new edge that is NOT expired (the Google one)
    auto active_facts = collect_strings(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_contra' "
        "AND e.expired_at IS NULL RETURN e.fact");
    CHECK(!active_facts.empty());

    // The active facts should mention Google or the new role
    bool has_new_fact = false;
    for (auto& f : active_facts) {
        auto lower = f;
        for (auto& c : lower) c = static_cast<char>(std::tolower(c));
        if (lower.find("google") != std::string::npos ||
            lower.find("senior") != std::string::npos ||
            lower.find("left") != std::string::npos ||
            lower.find("joined") != std::string::npos) {
            has_new_fact = true;
            break;
        }
    }
    CHECK(has_new_fact);
}

// ============================================================================
// Acceptance: remove_episode
// ============================================================================
TEST_CASE("Acceptance: remove_episode cleans up Kuzu state", "[acceptance]") {
    auto g = make_replay_graphiti("remove_episode");
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "to_remove",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_remove",
    });
    REQUIRE(r.has_value());

    auto conn = kuzu::main::Connection(&g.database());

    // Before removal: episode, entities, and MENTIONS exist
    auto ep_before = count_rows(conn,
        "MATCH (ep:Episodic) WHERE ep.group_id = 'rec_remove' RETURN ep");
    CHECK(ep_before >= 1);

    auto mentions_before = count_rows(conn,
        "MATCH (ep:Episodic)-[:MENTIONS]->(n:Entity) "
        "WHERE ep.group_id = 'rec_remove' RETURN n");
    CHECK(mentions_before >= 1);

    // Remove the episode
    auto episode_uuid = r.value().episode.uuid;
    auto del = g.remove_episode(episode_uuid);
    REQUIRE(del.has_value());

    // After removal: episode node gone
    auto ep_after = count_rows(conn,
        "MATCH (ep:Episodic) WHERE ep.uuid = '" + episode_uuid + "' RETURN ep");
    CHECK(ep_after == 0);

    // After removal: MENTIONS edges from that episode gone
    auto mentions_after = count_rows(conn,
        "MATCH (ep:Episodic)-[:MENTIONS]->(n:Entity) "
        "WHERE ep.uuid = '" + episode_uuid + "' RETURN n");
    CHECK(mentions_after == 0);
}

// ============================================================================
// Acceptance: add_triplet
// ============================================================================
TEST_CASE("Acceptance: add_triplet creates correct Kuzu state", "[acceptance]") {
    auto g = make_replay_graphiti("add_triplet");
    REQUIRE(g.build_indices().has_value());

    EntityNode source;
    source.name = "Alice";
    source.group_id = "rec_triplet";
    source.created_at = std::chrono::system_clock::now();

    EntityNode target;
    target.name = "Acme Corp";
    target.group_id = "rec_triplet";
    target.created_at = std::chrono::system_clock::now();

    EntityEdge edge;
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme Corp";
    edge.group_id = "rec_triplet";
    edge.created_at = std::chrono::system_clock::now();

    auto r = g.add_triplet(source, edge, target);
    REQUIRE(r.has_value());
    CHECK(r.value().nodes.size() == 2);
    CHECK(r.value().edges.size() == 1);

    auto conn = kuzu::main::Connection(&g.database());

    // --- 2 Entity nodes ---
    auto entity_names = collect_strings(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_triplet' RETURN n.name");
    REQUIRE(entity_names.size() == 2);
    CHECK(any_contains(entity_names, "Alice"));
    CHECK(any_contains(entity_names, "Acme"));

    // --- 1 RelatesToNode_ edge ---
    auto facts = collect_strings(conn,
        "MATCH (a:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(b:Entity) "
        "WHERE e.group_id = 'rec_triplet' RETURN e.fact");
    REQUIRE(facts.size() == 1);
    CHECK(any_contains(facts, "work"));

    // --- name_embedding populated on both entities ---
    auto name_embed_count = count_rows(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_triplet' "
        "AND n.name_embedding IS NOT NULL AND size(n.name_embedding) > 0 "
        "RETURN n");
    CHECK(name_embed_count == 2);

    // --- fact_embedding populated on edge ---
    auto fact_embed_count = count_rows(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_triplet' "
        "AND e.fact_embedding IS NOT NULL AND size(e.fact_embedding) > 0 "
        "RETURN e");
    CHECK(fact_embed_count == 1);
}

// ============================================================================
// Acceptance: management APIs
// ============================================================================
TEST_CASE("Acceptance: management APIs overview, BM25, delete_group", "[acceptance]") {
    auto g = make_replay_graphiti("management_apis");
    REQUIRE(g.build_indices().has_value());

    auto r = g.add_episode({
        .name = "mgmt",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_mgmt",
    });
    REQUIRE(r.has_value());
    REQUIRE(g.build_indices().has_value());

    auto conn = kuzu::main::Connection(&g.database());

    // --- Verify data exists before management operations ---
    auto entity_count_before = count_rows(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_mgmt' RETURN n");
    REQUIRE(entity_count_before >= 2);

    // --- get_graph_overview returns nodes and edges ---
    auto overview = g.get_graph_overview("rec_mgmt");
    REQUIRE(overview.has_value());
    CHECK((!overview.value().nodes.empty() || !overview.value().edges.empty()));

    // --- BM25 search finds Alice ---
    auto bm25 = g.search_entity_nodes_bm25("Alice", "rec_mgmt");
    REQUIRE(bm25.has_value());
    REQUIRE(!bm25.value().empty());
    CHECK(any_contains({bm25.value()[0].name}, "Alice"));

    // --- delete_group clears everything ---
    auto del = g.delete_group("rec_mgmt");
    REQUIRE(del.has_value());

    // Entities gone
    auto entity_count_after = count_rows(conn,
        "MATCH (n:Entity) WHERE n.group_id = 'rec_mgmt' RETURN n");
    CHECK(entity_count_after == 0);

    // Edges gone
    auto edge_count_after = count_rows(conn,
        "MATCH (e:RelatesToNode_) WHERE e.group_id = 'rec_mgmt' RETURN e");
    CHECK(edge_count_after == 0);

    // Episodes gone
    auto ep_count_after = count_rows(conn,
        "MATCH (ep:Episodic) WHERE ep.group_id = 'rec_mgmt' RETURN ep");
    CHECK(ep_count_after == 0);
}
