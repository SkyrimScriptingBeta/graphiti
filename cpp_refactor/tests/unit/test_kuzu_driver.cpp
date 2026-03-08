#include <catch2/catch_all.hpp>

#include "driver/kuzu_driver.h"
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <algorithm>
#include <chrono>
#include <vector>

// Shorthand: assert a VoidResult succeeded (avoids [[nodiscard]] warnings)
#define OK(expr) REQUIRE((expr).has_value())

using namespace graphiti;
using namespace std::chrono_literals;

// Helper to create a driver with in-memory database and schema
static KuzuDriver make_driver() {
    KuzuDriver driver(":memory:");
    auto r = driver.setup_schema();
    REQUIRE(r.has_value());
    return driver;
}

static TimePoint make_time(int year, int month, int day) {
    auto ymd = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
               std::chrono::day{static_cast<unsigned>(day)};
    return std::chrono::sys_days{ymd};
}

// ============================================================================
// Schema
// ============================================================================

TEST_CASE("KuzuDriver schema setup", "[kuzu][schema]") {
    KuzuDriver driver(":memory:");

    SECTION("setup_schema succeeds") {
        auto r = driver.setup_schema();
        REQUIRE(r.has_value());
    }

    SECTION("setup_schema is idempotent") {
        auto r1 = driver.setup_schema();
        REQUIRE(r1.has_value());
        auto r2 = driver.setup_schema();
        REQUIRE(r2.has_value());
    }
}

// ============================================================================
// Entity Node CRUD
// ============================================================================

TEST_CASE("KuzuDriver entity node CRUD", "[kuzu][entity]") {
    auto driver = make_driver();

    auto now = datetime::utc_now();
    EntityNode node{
        .uuid = uuid::generate(),
        .name = "Alice",
        .group_id = "test-group",
        .labels = {"person", "developer"},
        .created_at = now,
        .name_embedding = std::nullopt,
        .summary = "A software developer",
        .attributes = {{"role", "engineer"}},
    };

    SECTION("save and get") {
        auto save_r = driver.save_entity_node(node);
        REQUIRE(save_r.has_value());

        auto get_r = driver.get_entity_node(node.uuid);
        REQUIRE(get_r.has_value());

        auto& got = get_r.value();
        CHECK(got.uuid == node.uuid);
        CHECK(got.name == "Alice");
        CHECK(got.group_id == "test-group");
        CHECK(got.labels.size() == 2);
        CHECK(got.summary == "A software developer");
        CHECK(got.attributes["role"] == "engineer");
        CHECK(!got.name_embedding.has_value());
    }

    SECTION("get nonexistent returns not_found") {
        auto r = driver.get_entity_node("nonexistent-uuid");
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::not_found);
    }

    SECTION("save updates existing node") {
        OK(driver.save_entity_node(node));
        node.name = "Alice Updated";
        node.summary = "Updated summary";
        OK(driver.save_entity_node(node));

        auto got = driver.get_entity_node(node.uuid);
        REQUIRE(got.has_value());
        CHECK(got->name == "Alice Updated");
        CHECK(got->summary == "Updated summary");
    }

    SECTION("get multiple nodes") {
        EntityNode node2{
            .uuid = uuid::generate(),
            .name = "Bob",
            .group_id = "test-group",
            .labels = {"person"},
            .created_at = now,
            .summary = "A designer",
        };

        OK(driver.save_entity_node(node));
        OK(driver.save_entity_node(node2));

        auto r = driver.get_entity_nodes({node.uuid, node2.uuid});
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);
    }

    SECTION("delete node") {
        OK(driver.save_entity_node(node));
        auto del_r = driver.delete_entity_node(node.uuid);
        REQUIRE(del_r.has_value());

        auto get_r = driver.get_entity_node(node.uuid);
        CHECK(!get_r.has_value());
    }
}

// ============================================================================
// Episodic Node CRUD
// ============================================================================

TEST_CASE("KuzuDriver episodic node CRUD", "[kuzu][episodic]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    EpisodicNode episode{
        .uuid = uuid::generate(),
        .name = "conversation_1",
        .group_id = "test-group",
        .created_at = now,
        .source = EpisodeType::message,
        .source_description = "chat conversation",
        .content = "User said hello and introduced themselves",
        .valid_at = now,
        .entity_edges = {},
    };

    SECTION("save and get") {
        auto save_r = driver.save_episodic_node(episode);
        REQUIRE(save_r.has_value());

        auto get_r = driver.get_episodic_node(episode.uuid);
        REQUIRE(get_r.has_value());

        auto& got = get_r.value();
        CHECK(got.uuid == episode.uuid);
        CHECK(got.name == "conversation_1");
        CHECK(got.group_id == "test-group");
        CHECK(got.source == EpisodeType::message);
        CHECK(got.source_description == "chat conversation");
        CHECK(got.content == "User said hello and introduced themselves");
    }

    SECTION("retrieve episodes by group and time") {
        // Create multiple episodes at different times
        auto t1 = make_time(2024, 1, 1);
        auto t2 = make_time(2024, 1, 2);
        auto t3 = make_time(2024, 1, 3);

        EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                         .created_at = t1, .source = EpisodeType::message,
                         .content = "First", .valid_at = t1};
        EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                         .created_at = t2, .source = EpisodeType::text,
                         .content = "Second", .valid_at = t2};
        EpisodicNode ep3{.uuid = uuid::generate(), .name = "ep3", .group_id = "grp",
                         .created_at = t3, .source = EpisodeType::message,
                         .content = "Third", .valid_at = t3};

        OK(driver.save_episodic_node(ep1));
        OK(driver.save_episodic_node(ep2));
        OK(driver.save_episodic_node(ep3));

        // Retrieve all before t3 (should get ep1 and ep2)
        auto ref_time = make_time(2024, 1, 2) + 12h;
        auto r = driver.retrieve_episodes("grp", ref_time, 10);
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);

        // Retrieve with source filter
        auto r2 = driver.retrieve_episodes("grp", make_time(2024, 2, 1), 10,
                                           EpisodeType::message);
        REQUIRE(r2.has_value());
        CHECK(r2->size() == 2); // ep1 and ep3 are message type

        // Retrieve with limit
        auto r3 = driver.retrieve_episodes("grp", make_time(2024, 2, 1), 1);
        REQUIRE(r3.has_value());
        CHECK(r3->size() == 1);
    }
}

// ============================================================================
// Entity Edge CRUD (RelatesToNode_ pattern)
// ============================================================================

TEST_CASE("KuzuDriver entity edge CRUD", "[kuzu][edge]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    // Create two entity nodes first
    EntityNode alice{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                     .created_at = now, .summary = "Person A"};
    EntityNode bob{.uuid = uuid::generate(), .name = "Bob", .group_id = "grp",
                   .created_at = now, .summary = "Person B"};

    OK(driver.save_entity_node(alice));
    OK(driver.save_entity_node(bob));

    EntityEdge edge{
        .uuid = uuid::generate(),
        .group_id = "grp",
        .source_node_uuid = alice.uuid,
        .target_node_uuid = bob.uuid,
        .name = "WORKS_WITH",
        .fact = "Alice works with Bob at Acme Corp",
        .fact_embedding = std::nullopt,
        .episodes = {"ep-1", "ep-2"},
        .created_at = now,
        .expired_at = std::nullopt,
        .valid_at = now,
        .invalid_at = std::nullopt,
        .attributes = {{"confidence", 0.95}},
    };

    SECTION("save and get") {
        auto save_r = driver.save_entity_edge(edge);
        REQUIRE(save_r.has_value());

        auto get_r = driver.get_entity_edge(edge.uuid);
        REQUIRE(get_r.has_value());

        auto& got = get_r.value();
        CHECK(got.uuid == edge.uuid);
        CHECK(got.source_node_uuid == alice.uuid);
        CHECK(got.target_node_uuid == bob.uuid);
        CHECK(got.name == "WORKS_WITH");
        CHECK(got.fact == "Alice works with Bob at Acme Corp");
        CHECK(got.episodes.size() == 2);
        CHECK(got.valid_at.has_value());
        CHECK(!got.expired_at.has_value());
    }

    SECTION("get nonexistent edge returns not_found") {
        auto r = driver.get_entity_edge("nonexistent");
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::not_found);
    }

    SECTION("get edges between nodes") {
        OK(driver.save_entity_edge(edge));

        auto r = driver.get_edges_between_nodes(alice.uuid, bob.uuid);
        REQUIRE(r.has_value());
        CHECK(r->size() == 1);
        CHECK(r->at(0).uuid == edge.uuid);
    }

    SECTION("get edges by node") {
        OK(driver.save_entity_edge(edge));

        auto r = driver.get_edges_by_node(alice.uuid);
        REQUIRE(r.has_value());
        CHECK(r->size() == 1);
    }

    SECTION("get multiple edges") {
        // Create a second edge
        EntityEdge edge2{
            .uuid = uuid::generate(),
            .group_id = "grp",
            .source_node_uuid = bob.uuid,
            .target_node_uuid = alice.uuid,
            .name = "KNOWS",
            .fact = "Bob knows Alice",
            .created_at = now,
        };

        OK(driver.save_entity_edge(edge));
        OK(driver.save_entity_edge(edge2));

        auto r = driver.get_entity_edges({edge.uuid, edge2.uuid});
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);
    }

    SECTION("delete edge") {
        OK(driver.save_entity_edge(edge));
        auto del_r = driver.delete_entity_edge(edge.uuid);
        REQUIRE(del_r.has_value());

        auto get_r = driver.get_entity_edge(edge.uuid);
        CHECK(!get_r.has_value());
    }

    SECTION("edge with all optional timestamps") {
        edge.expired_at = now + 24h;
        edge.invalid_at = now + 48h;

        OK(driver.save_entity_edge(edge));
        auto got = driver.get_entity_edge(edge.uuid);
        REQUIRE(got.has_value());
        CHECK(got->expired_at.has_value());
        CHECK(got->invalid_at.has_value());
    }
}

// ============================================================================
// Episodic Edge (MENTIONS)
// ============================================================================

TEST_CASE("KuzuDriver episodic edge (MENTIONS)", "[kuzu][mentions]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    // Create episode and entity
    EpisodicNode episode{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                         .created_at = now, .source = EpisodeType::message,
                         .content = "Hello", .valid_at = now};
    EntityNode entity{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                      .created_at = now};

    OK(driver.save_episodic_node(episode));
    OK(driver.save_entity_node(entity));

    EpisodicEdge mention{
        .uuid = uuid::generate(),
        .group_id = "grp",
        .source_node_uuid = episode.uuid,
        .target_node_uuid = entity.uuid,
        .created_at = now,
    };

    SECTION("save MENTIONS edge") {
        auto r = driver.save_episodic_edge(mention);
        REQUIRE(r.has_value());
    }
}

// ============================================================================
// Embedding Operations
// ============================================================================

TEST_CASE("KuzuDriver embedding operations", "[kuzu][embedding]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    SECTION("entity node embedding save and load") {
        EntityNode node{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                        .created_at = now};
        OK(driver.save_entity_node(node));

        std::vector<float> embedding(128, 0.0f);
        embedding[0] = 1.0f;
        embedding[1] = 0.5f;

        auto save_r = driver.save_entity_node_embedding(node.uuid, embedding);
        REQUIRE(save_r.has_value());

        auto load_r = driver.load_entity_node_embedding(node.uuid);
        REQUIRE(load_r.has_value());
        REQUIRE(load_r->has_value());
        CHECK(load_r->value().size() == 128);
        CHECK(load_r->value()[0] == Catch::Approx(1.0f));
        CHECK(load_r->value()[1] == Catch::Approx(0.5f));
    }

    SECTION("entity edge embedding save and load") {
        EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp",
                     .created_at = now};
        EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp",
                     .created_at = now};
        OK(driver.save_entity_node(a));
        OK(driver.save_entity_node(b));

        EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                        .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                        .name = "KNOWS", .fact = "A knows B", .created_at = now};
        OK(driver.save_entity_edge(edge));

        std::vector<float> embedding(64, 0.1f);
        auto save_r = driver.save_entity_edge_embedding(edge.uuid, embedding);
        REQUIRE(save_r.has_value());

        auto load_r = driver.load_entity_edge_embedding(edge.uuid);
        REQUIRE(load_r.has_value());
        REQUIRE(load_r->has_value());
        CHECK(load_r->value().size() == 64);
    }

    SECTION("load embedding from node without embedding returns nullopt") {
        EntityNode node{.uuid = uuid::generate(), .name = "Empty", .group_id = "grp",
                        .created_at = now};
        OK(driver.save_entity_node(node));

        auto r = driver.load_entity_node_embedding(node.uuid);
        REQUIRE(r.has_value());
        CHECK(!r->has_value()); // nullopt
    }
}

// ============================================================================
// Clear Data
// ============================================================================

TEST_CASE("KuzuDriver clear data", "[kuzu][maintenance]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    SECTION("clear all data") {
        OK(driver.save_entity_node(
            {.uuid = uuid::generate(), .name = "A", .group_id = "g1", .created_at = now}));
        OK(driver.save_entity_node(
            {.uuid = uuid::generate(), .name = "B", .group_id = "g2", .created_at = now}));

        auto r = driver.clear_data({});
        REQUIRE(r.has_value());

        // Both should be gone
        auto nodes = driver.get_entity_nodes({});
        REQUIRE(nodes.has_value());
        CHECK(nodes->empty());
    }

    SECTION("clear by group_id") {
        auto uuid1 = uuid::generate();
        auto uuid2 = uuid::generate();
        OK(driver.save_entity_node(
            {.uuid = uuid1, .name = "A", .group_id = "keep", .created_at = now}));
        OK(driver.save_entity_node(
            {.uuid = uuid2, .name = "B", .group_id = "delete", .created_at = now}));

        auto r = driver.clear_data({"delete"});
        REQUIRE(r.has_value());

        auto kept = driver.get_entity_node(uuid1);
        CHECK(kept.has_value());

        auto deleted = driver.get_entity_node(uuid2);
        CHECK(!deleted.has_value());
    }
}

// ============================================================================
// Delete cascades (entity node with edges)
// ============================================================================

TEST_CASE("KuzuDriver delete entity with edges", "[kuzu][cascade]") {
    auto driver = make_driver();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp",
                 .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp",
                 .created_at = now};
    OK(driver.save_entity_node(a));
    OK(driver.save_entity_node(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "REL", .fact = "A relates to B", .created_at = now};
    OK(driver.save_entity_edge(edge));

    // Deleting node A should also clean up the RelatesToNode_
    auto r = driver.delete_entity_node(a.uuid);
    REQUIRE(r.has_value());

    auto edge_r = driver.get_entity_edge(edge.uuid);
    CHECK(!edge_r.has_value());
}
