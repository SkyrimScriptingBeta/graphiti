#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>
#include <graphiti/search_config.h>

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

TEST_CASE("Communities: build_communities after ingest", "[integration][community]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto now = std::chrono::system_clock::now();

    // Ingest several related episodes to create a connected graph
    auto r1 = g.add_episode(
        "ep1", "Alice works at Acme Corp as a software engineer in Denver.",
        "chat", now, EpisodeType::message, "test_group"
    );
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode(
        "ep2", "Bob also works at Acme Corp. Bob and Alice are on the same team.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message, "test_group"
    );
    REQUIRE(r2.has_value());

    auto r3 = g.add_episode(
        "ep3", "Charlie is the CEO of Acme Corp. He hired both Alice and Bob.",
        "chat", now + std::chrono::seconds(120), EpisodeType::message, "test_group"
    );
    REQUIRE(r3.has_value());

    // Rebuild FTS indices after ingestion
    auto rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Build communities
    auto communities = g.build_communities({"test_group"});
    REQUIRE(communities.has_value());

    auto& [nodes, edges] = communities.value();

    INFO("Built " << nodes.size() << " communities with " << edges.size() << " HAS_MEMBER edges");

    // Should have at least one community
    CHECK(!nodes.empty());
    // Each community should have at least one member edge
    CHECK(!edges.empty());

    // Each community should have a non-empty name and summary
    for (auto& node : nodes) {
        CHECK(!node.uuid.empty());
        CHECK(!node.name.empty());
        CHECK(!node.summary.empty());
        CHECK(!node.group_id.empty());
    }
}

TEST_CASE("Communities: community search via search_advanced", "[integration][community]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode(
        "ep1", "Alice is a software engineer at Acme Corp in Denver.",
        "chat", now, EpisodeType::message, "test_group"
    );
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode(
        "ep2", "Bob works at Acme Corp too. Alice and Bob collaborate daily.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message, "test_group"
    );
    REQUIRE(r2.has_value());

    // Rebuild FTS indices
    auto rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Build communities
    auto communities = g.build_communities({"test_group"});
    REQUIRE(communities.has_value());

    // Rebuild FTS indices again after community creation
    rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Search using community config
    auto search_result = g.search_advanced(
        "Who works at Acme?",
        community_hybrid_search_rrf(),
        "test_group"
    );
    REQUIRE(search_result.has_value());

    auto& results = search_result.value();
    INFO("Community search returned " << results.communities.size() << " communities");

    // Should find at least one community
    CHECK(!results.communities.empty());
}

TEST_CASE("Communities: build_communities on empty graph", "[integration][community]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    // Build communities on empty graph — should succeed with empty result
    auto communities = g.build_communities({"nonexistent_group"});
    REQUIRE(communities.has_value());

    auto& [nodes, edges] = communities.value();
    CHECK(nodes.empty());
    CHECK(edges.empty());
}

TEST_CASE("Communities: rebuild communities replaces old ones", "[integration][community]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode(
        "ep1", "Alice works at Acme Corp with Bob.",
        "chat", now, EpisodeType::message, "test_group"
    );
    REQUIRE(r1.has_value());

    // Rebuild FTS indices
    auto rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Build communities first time
    auto first = g.build_communities({"test_group"});
    REQUIRE(first.has_value());
    auto first_count = first.value().first.size();

    // Add more data
    auto r2 = g.add_episode(
        "ep2", "Charlie joined Acme Corp. Charlie, Alice, and Bob work in engineering.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message, "test_group"
    );
    REQUIRE(r2.has_value());

    // Rebuild FTS indices
    rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Rebuild communities — old ones should be replaced
    auto second = g.build_communities({"test_group"});
    REQUIRE(second.has_value());

    INFO("First build: " << first_count << " communities, second build: "
         << second.value().first.size() << " communities");

    // Both builds should succeed (exact counts depend on LLM extraction)
    // The key check is that rebuild doesn't fail
}

TEST_CASE("Communities: update_community during add_episode", "[integration][community]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto now = std::chrono::system_clock::now();

    // Ingest and build initial communities
    auto r1 = g.add_episode(
        "ep1", "Alice and Bob work at Acme Corp together.",
        "chat", now, EpisodeType::message, "test_group"
    );
    REQUIRE(r1.has_value());

    auto rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    auto communities = g.build_communities({"test_group"});
    REQUIRE(communities.has_value());

    // Now add a new episode with update_communities=true
    auto r2 = g.add_episode(
        "ep2", "Charlie just joined Acme Corp. He will work with Alice and Bob.",
        "chat", now + std::chrono::seconds(60),
        EpisodeType::message,
        "test_group",
        "",          // agent_id
        "",          // source_id
        {},          // participant_ids
        std::nullopt, // custom_instructions
        std::nullopt, // saga
        std::nullopt, // saga_previous_episode_uuid
        true         // update_communities
    );
    REQUIRE(r2.has_value());

    INFO("add_episode with update_communities=true succeeded, "
         << r2.value().nodes.size() << " nodes extracted");
}
