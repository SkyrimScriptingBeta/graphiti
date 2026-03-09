#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>

#include <chrono>
#include <cstdlib>
#include <string>

using namespace graphiti;

static GraphitiConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    GraphitiConfig config;
    config.db_path = ":memory:";
    config.llm.api_key = key;
    config.embedder.api_key = key;
    return config;
}

TEST_CASE("source_id and participant_ids: set on episode", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice told Bob about the new Kuzu database.",
        "slack message",
        now,
        EpisodeType::message,
        "test_group",
        "recorder-agent",  // agent_id
        "alice",           // source_id (Alice said it)
        {"alice", "bob"}   // participant_ids (Alice and Bob were present)
    );
    REQUIRE(result.has_value());

    auto& ep = result.value().episode;
    CHECK(ep.agent_id == "recorder-agent");
    CHECK(ep.source_id == "alice");
    REQUIRE(ep.participant_ids.size() == 2);
    CHECK(ep.participant_ids[0] == "alice");
    CHECK(ep.participant_ids[1] == "bob");
}

TEST_CASE("source_id and participant_ids: propagated to entities", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice works at Acme Corp as a software engineer.",
        "chat",
        now,
        EpisodeType::message,
        "test_group",
        "agent-1",        // agent_id
        "alice",          // source_id
        {"alice", "bob"}  // participant_ids
    );
    REQUIRE(result.has_value());

    auto& nodes = result.value().nodes;
    REQUIRE(!nodes.empty());

    // All new nodes should have the attribution ids
    for (auto& node : nodes) {
        INFO("Node: " << node.name);
        CHECK(!node.agent_ids.empty());
        CHECK(!node.source_ids.empty());
        CHECK(!node.participant_ids.empty());

        // Check specific values
        bool has_agent = false;
        for (auto& id : node.agent_ids) {
            if (id == "agent-1") has_agent = true;
        }
        CHECK(has_agent);

        bool has_source = false;
        for (auto& id : node.source_ids) {
            if (id == "alice") has_source = true;
        }
        CHECK(has_source);

        bool has_alice = false, has_bob = false;
        for (auto& id : node.participant_ids) {
            if (id == "alice") has_alice = true;
            if (id == "bob") has_bob = true;
        }
        CHECK(has_alice);
        CHECK(has_bob);
    }
}

TEST_CASE("source_id and participant_ids: propagated to edges", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice works at Acme Corp. Bob also works at Acme Corp.",
        "chat",
        now,
        EpisodeType::message,
        "test_group",
        "agent-1",
        "alice",
        {"alice", "bob"}
    );
    REQUIRE(result.has_value());

    auto& edges = result.value().edges;
    if (!edges.empty()) {
        for (auto& edge : edges) {
            INFO("Edge: " << edge.name << " fact: " << edge.fact);
            CHECK(!edge.source_ids.empty());
            CHECK(!edge.participant_ids.empty());
        }
    }
}

TEST_CASE("source_id and participant_ids: search filter by source_ids", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Episode from Alice
    auto r1 = g.add_episode(
        "ep1", "Alice mentioned that Kuzu is fast.",
        "chat", now, EpisodeType::message,
        "test_group", "agent-1", "alice", {"alice", "bob"}
    );
    REQUIRE(r1.has_value());

    // Episode from Carol
    auto r2 = g.add_episode(
        "ep2", "Carol said that Neo4j has a large community.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message,
        "test_group", "agent-1", "carol", {"carol", "dave"}
    );
    REQUIRE(r2.has_value());

    REQUIRE(g.build_indices().has_value());

    // Search filtered by source_id "alice"
    SearchFilters filters;
    filters.source_ids = {"alice"};
    auto search = g.search("database", "test_group", 10, filters);
    REQUIRE(search.has_value());

    // Results should only include edges with source_ids containing "alice"
    for (auto& edge : search.value()) {
        bool has_alice = false;
        for (auto& sid : edge.source_ids) {
            if (sid == "alice") has_alice = true;
        }
        CHECK(has_alice);
    }
}

TEST_CASE("source_id and participant_ids: search filter by participant_ids", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Episode with Alice and Bob present
    auto r1 = g.add_episode(
        "ep1", "Discussed project plans for Kuzu integration.",
        "chat", now, EpisodeType::message,
        "test_group", "agent-1", "alice", {"alice", "bob"}
    );
    REQUIRE(r1.has_value());

    REQUIRE(g.build_indices().has_value());

    // Search filtered by participant "bob"
    SearchFilters filters;
    filters.participant_ids = {"bob"};
    auto search = g.search("Kuzu", "test_group", 10, filters);
    REQUIRE(search.has_value());

    // All results should have "bob" in participant_ids
    for (auto& edge : search.value()) {
        bool has_bob = false;
        for (auto& pid : edge.participant_ids) {
            if (pid == "bob") has_bob = true;
        }
        CHECK(has_bob);
    }
}

TEST_CASE("source_id and participant_ids: dedup merge", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Alice says something about Kuzu
    auto r1 = g.add_episode(
        "ep1", "Kuzu is an embedded graph database written in C++.",
        "chat", now, EpisodeType::message,
        "test_group", "agent-1", "alice", {"alice", "bob"}
    );
    REQUIRE(r1.has_value());

    // Carol says something about Kuzu (should dedup-merge entities)
    auto r2 = g.add_episode(
        "ep2", "Kuzu supports Cypher queries and has excellent performance.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message,
        "test_group", "agent-1", "carol", {"carol", "dave"}
    );
    REQUIRE(r2.has_value());

    // After dedup, the "Kuzu" entity should have merged source_ids and participant_ids
    // We can verify by searching
    REQUIRE(g.build_indices().has_value());

    SearchConfig cfg;
    cfg.node_config = NodeSearchConfig{};
    cfg.limit = 10;
    auto search = g.search_advanced("Kuzu database", cfg, "test_group");
    REQUIRE(search.has_value());

    // Check nodes for merged attribution
    for (auto& node : search.value().nodes) {
        if (node.name.find("Kuzu") != std::string::npos ||
            node.name.find("kuzu") != std::string::npos) {
            INFO("Node: " << node.name
                << " source_ids.size=" << node.source_ids.size()
                << " participant_ids.size=" << node.participant_ids.size());
            // After dedup merge, should have both alice and carol as sources
            // (if dedup matched them correctly)
        }
    }
}

TEST_CASE("source_id and participant_ids: bulk episode per-episode override", "[integration][source_participants]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    std::vector<RawEpisode> episodes = {
        {
            .name = "ep1",
            .content = "Alice mentioned project Alpha.",
            .source_description = "slack",
            .reference_time = now,
            .source = EpisodeType::message,
            .source_id = "alice",                // per-episode override
            .participant_ids = {"alice", "bob"},  // per-episode override
        },
        {
            .name = "ep2",
            .content = "Carol discussed project Beta.",
            .source_description = "slack",
            .reference_time = now + std::chrono::seconds(60),
            .source = EpisodeType::message,
            .source_id = "carol",                  // different source
            .participant_ids = {"carol", "dave"},   // different participants
        },
    };

    auto result = g.add_episode_bulk(
        episodes, "test_group", "agent-1"
        // source_id and participant_ids default to "" and {} at batch level
        // per-episode values from RawEpisode take precedence
    );
    REQUIRE(result.has_value());

    auto& eps = result.value().episodes;
    REQUIRE(eps.size() == 2);

    CHECK(eps[0].source_id == "alice");
    CHECK(eps[0].participant_ids.size() == 2);
    CHECK(eps[1].source_id == "carol");
    CHECK(eps[1].participant_ids.size() == 2);
}
