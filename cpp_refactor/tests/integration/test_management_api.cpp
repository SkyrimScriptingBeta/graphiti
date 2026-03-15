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

// ============================================================================
// retrieve_episodes
// ============================================================================

TEST_CASE("retrieve_episodes: basic retrieval", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Bob lives in Denver.",
        .source_description = "chat",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "test_group",
    });
    REQUIRE(r2.has_value());

    // Retrieve all episodes
    auto far_future = now + std::chrono::hours(24);
    auto episodes = g.retrieve_episodes(far_future, 20, "test_group");
    REQUIRE(episodes.has_value());
    CHECK(episodes.value().size() == 2);
}

TEST_CASE("retrieve_episodes: respects last_n limit", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    for (int i = 0; i < 3; ++i) {
        auto r = g.add_episode({
            .name = "ep" + std::to_string(i),
            .body = "Content " + std::to_string(i),
            .source_description = "chat",
            .reference_time = now + std::chrono::seconds(i * 60),
            .group_id = "test_group",
        });
        REQUIRE(r.has_value());
    }

    auto far_future = now + std::chrono::hours(24);
    auto episodes = g.retrieve_episodes(far_future, 1, "test_group");
    REQUIRE(episodes.has_value());
    CHECK(episodes.value().size() == 1);
}

TEST_CASE("retrieve_episodes: with saga filter", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Add episode with saga
    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Alice started at Acme.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
        .saga = "onboarding",
    });
    REQUIRE(r1.has_value());

    // Add episode without saga
    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Bob went to the store.",
        .source_description = "chat",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "test_group",
    });
    REQUIRE(r2.has_value());

    auto far_future = now + std::chrono::hours(24);

    // Retrieve saga episodes only
    auto saga_eps = g.retrieve_episodes(far_future, 20, "test_group",
                                         std::nullopt, "onboarding");
    REQUIRE(saga_eps.has_value());
    CHECK(saga_eps.value().size() == 1);

    // Retrieve all episodes
    auto all_eps = g.retrieve_episodes(far_future, 20, "test_group");
    REQUIRE(all_eps.has_value());
    CHECK(all_eps.value().size() == 2);
}

// ============================================================================
// get_nodes_and_edges_by_episode
// ============================================================================

TEST_CASE("get_nodes_and_edges_by_episode: returns nodes and edges", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp as an engineer.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r.has_value());

    auto ep_uuid = r.value().episode.uuid;

    auto results = g.get_nodes_and_edges_by_episode({ep_uuid});
    REQUIRE(results.has_value());

    CHECK(!results.value().episodes.empty());
    CHECK(!results.value().nodes.empty());
    INFO("Episodes: " << results.value().episodes.size());
    INFO("Nodes: " << results.value().nodes.size());
    INFO("Edges: " << results.value().edges.size());
}

// ============================================================================
// remove_episode
// ============================================================================

TEST_CASE("remove_episode: deletes episode and orphaned entities", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r.has_value());
    auto ep_uuid = r.value().episode.uuid;

    // Verify episode exists via retrieval
    auto before = g.get_nodes_and_edges_by_episode({ep_uuid});
    REQUIRE(before.has_value());
    CHECK(!before.value().episodes.empty());

    // Remove it
    auto remove_result = g.remove_episode(ep_uuid);
    REQUIRE(remove_result.has_value());

    // Episode should be gone
    auto after = g.get_nodes_and_edges_by_episode({ep_uuid});
    REQUIRE(after.has_value());
    CHECK(after.value().episodes.empty());
}

TEST_CASE("remove_episode: preserves nodes shared with other episodes", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Two episodes mentioning overlapping entities
    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r1.has_value());

    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Alice also works on the AI team at Acme Corp.",
        .source_description = "chat",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "test_group",
    });
    REQUIRE(r2.has_value());

    auto ep1_uuid = r1.value().episode.uuid;
    auto ep2_uuid = r2.value().episode.uuid;

    // Remove first episode
    auto remove_result = g.remove_episode(ep1_uuid);
    REQUIRE(remove_result.has_value());

    // Second episode should still have its data
    auto after = g.get_nodes_and_edges_by_episode({ep2_uuid});
    REQUIRE(after.has_value());
    CHECK(!after.value().episodes.empty());
    // Shared nodes should still exist
    CHECK(!after.value().nodes.empty());
}

// ============================================================================
// add_triplet
// ============================================================================

TEST_CASE("add_triplet: inserts manual triplet", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    EntityNode source;
    source.name = "Alice";
    source.group_id = "test_group";
    source.created_at = now;

    EntityNode target;
    target.name = "Acme Corp";
    target.group_id = "test_group";
    target.created_at = now;

    EntityEdge edge;
    edge.group_id = "test_group";
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme Corp";
    edge.created_at = now;

    auto result = g.add_triplet(std::move(source), std::move(edge), std::move(target));
    REQUIRE(result.has_value());

    CHECK(result.value().nodes.size() == 2);
    CHECK(result.value().edges.size() == 1);

    // Verify searchable
    REQUIRE(g.build_indices().has_value());
    auto search_result = g.search({.query = "Alice Acme", .group_id = "test_group"});
    REQUIRE(search_result.has_value());
    CHECK(!search_result.value().empty());
}

TEST_CASE("add_triplet: deduplicates against existing nodes", "[integration][management]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // First: ingest via normal pipeline to create Alice
    auto r = g.add_episode({
        .name = "ep1",
        .body = "Alice is a software engineer.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r.has_value());
    REQUIRE(!r.value().nodes.empty());

    // Now add a triplet with Alice - should resolve to existing
    EntityNode source;
    source.name = "Alice";
    source.group_id = "test_group";
    source.created_at = now;

    EntityNode target;
    target.name = "TechCorp";
    target.group_id = "test_group";
    target.created_at = now;

    EntityEdge edge;
    edge.group_id = "test_group";
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at TechCorp";
    edge.created_at = now;

    auto result = g.add_triplet(std::move(source), std::move(edge), std::move(target));
    REQUIRE(result.has_value());
    CHECK(result.value().nodes.size() == 2);
}
