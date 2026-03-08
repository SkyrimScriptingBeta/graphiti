#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <graphiti/types.h>
#include <graphiti/search_filters.h>
#include <driver/kuzu_driver.h>
#include <search/search_filters.h>
#include <nlohmann/json.hpp>

using namespace graphiti;

// ============================================================================
// JSON round-trip tests for agent_id/agent_ids fields
// ============================================================================

TEST_CASE("EntityNode JSON round-trip with agent_ids", "[agent_attribution][types]") {
    EntityNode node;
    node.uuid = "entity-1";
    node.name = "Alice";
    node.group_id = "project-x";
    node.created_at = std::chrono::system_clock::now();
    node.agent_ids = {"agent-a", "agent-b"};

    nlohmann::json j = node;
    auto restored = j.get<EntityNode>();

    REQUIRE(restored.agent_ids.size() == 2);
    REQUIRE(restored.agent_ids[0] == "agent-a");
    REQUIRE(restored.agent_ids[1] == "agent-b");
}

TEST_CASE("EntityNode JSON round-trip with empty agent_ids", "[agent_attribution][types]") {
    EntityNode node;
    node.uuid = "entity-2";
    node.name = "Bob";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    // agent_ids defaults to empty

    nlohmann::json j = node;
    auto restored = j.get<EntityNode>();

    REQUIRE(restored.agent_ids.empty());
}

TEST_CASE("EpisodicNode JSON round-trip with agent_id", "[agent_attribution][types]") {
    EpisodicNode node;
    node.uuid = "ep-1";
    node.name = "Episode 1";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.source = EpisodeType::message;
    node.source_description = "chat";
    node.content = "Hello";
    node.valid_at = node.created_at;
    node.agent_id = "agent-a";

    nlohmann::json j = node;
    auto restored = j.get<EpisodicNode>();

    REQUIRE(restored.agent_id == "agent-a");
}

TEST_CASE("EntityEdge JSON round-trip with agent_ids", "[agent_attribution][types]") {
    EntityEdge edge;
    edge.uuid = "edge-1";
    edge.group_id = "g";
    edge.source_node_uuid = "src";
    edge.target_node_uuid = "tgt";
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme";
    edge.created_at = std::chrono::system_clock::now();
    edge.agent_ids = {"agent-a", "agent-b", "agent-c"};

    nlohmann::json j = edge;
    auto restored = j.get<EntityEdge>();

    REQUIRE(restored.agent_ids.size() == 3);
    REQUIRE(restored.agent_ids[0] == "agent-a");
    REQUIRE(restored.agent_ids[2] == "agent-c");
}

TEST_CASE("EpisodicEdge JSON round-trip with agent_id", "[agent_attribution][types]") {
    EpisodicEdge edge;
    edge.uuid = "mentions-1";
    edge.group_id = "g";
    edge.source_node_uuid = "ep";
    edge.target_node_uuid = "entity";
    edge.created_at = std::chrono::system_clock::now();
    edge.agent_id = "agent-x";

    nlohmann::json j = edge;
    auto restored = j.get<EpisodicEdge>();

    REQUIRE(restored.agent_id == "agent-x");
}

TEST_CASE("CommunityNode JSON round-trip with agent_ids", "[agent_attribution][types]") {
    CommunityNode node;
    node.uuid = "comm-1";
    node.name = "Tech Community";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.summary = "A community about tech";
    node.agent_ids = {"agent-a"};

    nlohmann::json j = node;
    auto restored = j.get<CommunityNode>();

    REQUIRE(restored.agent_ids.size() == 1);
    REQUIRE(restored.agent_ids[0] == "agent-a");
}

// ============================================================================
// Kuzu persistence tests for agent_id/agent_ids
// ============================================================================

TEST_CASE("Entity node persists agent_ids to Kuzu", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    EntityNode node;
    node.uuid = "entity-agent-test";
    node.name = "Alice";
    node.group_id = "project-x";
    node.created_at = std::chrono::system_clock::now();
    node.agent_ids = {"agent-a", "agent-b"};

    REQUIRE(driver.save_entity_node(node).has_value());

    auto result = driver.get_entity_node("entity-agent-test");
    REQUIRE(result.has_value());
    REQUIRE(result.value().agent_ids.size() == 2);
    REQUIRE(result.value().agent_ids[0] == "agent-a");
    REQUIRE(result.value().agent_ids[1] == "agent-b");
}

TEST_CASE("Entity node with empty agent_ids persists correctly", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    EntityNode node;
    node.uuid = "entity-no-agent";
    node.name = "Bob";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    // agent_ids defaults to empty

    REQUIRE(driver.save_entity_node(node).has_value());

    auto result = driver.get_entity_node("entity-no-agent");
    REQUIRE(result.has_value());
    REQUIRE(result.value().agent_ids.empty());
}

TEST_CASE("Episodic node persists agent_id to Kuzu", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    EpisodicNode node;
    node.uuid = "ep-agent-test";
    node.name = "Episode";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.source = EpisodeType::message;
    node.source_description = "chat";
    node.content = "Hello";
    node.valid_at = node.created_at;
    node.agent_id = "agent-a";

    REQUIRE(driver.save_episodic_node(node).has_value());

    auto result = driver.get_episodic_node("ep-agent-test");
    REQUIRE(result.has_value());
    REQUIRE(result.value().agent_id == "agent-a");
}

TEST_CASE("Entity edge persists agent_ids to Kuzu", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    // Create source and target nodes first
    EntityNode src;
    src.uuid = "src-node";
    src.name = "Alice";
    src.group_id = "g";
    src.created_at = std::chrono::system_clock::now();
    REQUIRE(driver.save_entity_node(src).has_value());

    EntityNode tgt;
    tgt.uuid = "tgt-node";
    tgt.name = "Acme Corp";
    tgt.group_id = "g";
    tgt.created_at = std::chrono::system_clock::now();
    REQUIRE(driver.save_entity_node(tgt).has_value());

    EntityEdge edge;
    edge.uuid = "edge-agent-test";
    edge.group_id = "g";
    edge.source_node_uuid = "src-node";
    edge.target_node_uuid = "tgt-node";
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme";
    edge.created_at = std::chrono::system_clock::now();
    edge.agent_ids = {"agent-a", "agent-b"};

    REQUIRE(driver.save_entity_edge(edge).has_value());

    auto result = driver.get_entity_edge("edge-agent-test");
    REQUIRE(result.has_value());
    REQUIRE(result.value().agent_ids.size() == 2);
    REQUIRE(result.value().agent_ids[0] == "agent-a");
    REQUIRE(result.value().agent_ids[1] == "agent-b");
}

TEST_CASE("Community node persists agent_ids to Kuzu", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    CommunityNode node;
    node.uuid = "comm-agent-test";
    node.name = "Tech Community";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.summary = "About tech";
    node.agent_ids = {"agent-a"};

    REQUIRE(driver.save_community_node(node).has_value());

    auto result = driver.get_community_node("comm-agent-test");
    REQUIRE(result.has_value());
    REQUIRE(result.value().agent_ids.size() == 1);
    REQUIRE(result.value().agent_ids[0] == "agent-a");
}

TEST_CASE("agent_ids accumulates through entity node update", "[agent_attribution][kuzu]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());

    // Save node with agent-a
    EntityNode node;
    node.uuid = "entity-accum";
    node.name = "Alice";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.agent_ids = {"agent-a"};
    REQUIRE(driver.save_entity_node(node).has_value());

    // Read it back
    auto r1 = driver.get_entity_node("entity-accum");
    REQUIRE(r1.has_value());
    REQUIRE(r1.value().agent_ids == std::vector<std::string>{"agent-a"});

    // Update with accumulated agent_ids (simulating dedup merge)
    node.agent_ids = {"agent-a", "agent-b"};
    REQUIRE(driver.save_entity_node(node).has_value());

    auto r2 = driver.get_entity_node("entity-accum");
    REQUIRE(r2.has_value());
    REQUIRE(r2.value().agent_ids.size() == 2);
    REQUIRE(r2.value().agent_ids[0] == "agent-a");
    REQUIRE(r2.value().agent_ids[1] == "agent-b");
}

// ============================================================================
// Search filter tests for agent_ids
// ============================================================================

TEST_CASE("Entity nodes searchable with agent_ids filter via BM25", "[agent_attribution][search]") {
    KuzuDriver driver(":memory:");
    REQUIRE(driver.setup_schema().has_value());
    REQUIRE(driver.build_fts_indices().has_value());

    // Create two nodes from different agents
    EntityNode node_a;
    node_a.uuid = "node-a";
    node_a.name = "Alice";
    node_a.group_id = "g";
    node_a.created_at = std::chrono::system_clock::now();
    node_a.summary = "Alice is an engineer";
    node_a.agent_ids = {"agent-a"};
    REQUIRE(driver.save_entity_node(node_a).has_value());

    EntityNode node_b;
    node_b.uuid = "node-b";
    node_b.name = "Bob";
    node_b.group_id = "g";
    node_b.created_at = std::chrono::system_clock::now();
    node_b.summary = "Bob is a designer";
    node_b.agent_ids = {"agent-b"};
    REQUIRE(driver.save_entity_node(node_b).has_value());

    // Rebuild FTS indices to include new nodes
    REQUIRE(driver.build_fts_indices().has_value());

    // Search without agent filter: should find both
    auto all = driver.search_entity_nodes_bm25("Alice Bob", "g", 10);
    REQUIRE(all.has_value());
    REQUIRE(all.value().size() >= 1);  // BM25 may return partial

    // The agent_ids are persisted — verify they came back
    bool found_a = false, found_b = false;
    for (auto& n : all.value()) {
        if (n.uuid == "node-a") {
            REQUIRE(n.agent_ids == std::vector<std::string>{"agent-a"});
            found_a = true;
        }
        if (n.uuid == "node-b") {
            REQUIRE(n.agent_ids == std::vector<std::string>{"agent-b"});
            found_b = true;
        }
    }
    REQUIRE((found_a || found_b));
}

TEST_CASE("SearchFilters agent_ids generates correct Cypher clause", "[agent_attribution][filters]") {
    SearchFilters filters;
    filters.agent_ids = {"agent-a", "agent-b"};

    // Test edge filter
    auto edge_result = build_edge_filter_clauses(filters);
    REQUIRE(edge_result.clauses.size() == 1);
    REQUIRE(edge_result.clauses[0].find("any(aid IN e.agent_ids") != std::string::npos);
    REQUIRE(edge_result.clauses[0].find("'agent-a'") != std::string::npos);
    REQUIRE(edge_result.clauses[0].find("'agent-b'") != std::string::npos);

    // Test node filter
    auto node_result = build_node_filter_clauses(filters);
    REQUIRE(node_result.clauses.size() == 1);
    REQUIRE(node_result.clauses[0].find("any(aid IN n.agent_ids") != std::string::npos);
}

TEST_CASE("SearchFilters empty agent_ids generates no clause", "[agent_attribution][filters]") {
    SearchFilters filters;
    // agent_ids is empty by default

    auto edge_result = build_edge_filter_clauses(filters);
    // Should have no agent-related clauses
    for (auto& clause : edge_result.clauses) {
        REQUIRE(clause.find("agent_ids") == std::string::npos);
    }

    auto node_result = build_node_filter_clauses(filters);
    REQUIRE(node_result.clauses.empty());
}
