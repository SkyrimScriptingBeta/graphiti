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

// ============================================================================
// Community Node CRUD
// ============================================================================

TEST_CASE("KuzuDriver community node CRUD", "[kuzu][community]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    SECTION("save and get community") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Tech Workers",
                        .group_id = "grp", .created_at = now,
                        .summary = "People in tech"};
        OK(driver.save_community_node(c));

        auto r = driver.get_community_node(c.uuid);
        REQUIRE(r.has_value());
        CHECK(r.value().name == "Tech Workers");
        CHECK(r.value().summary == "People in tech");
    }

    SECTION("save community with embedding") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Artists",
                        .group_id = "grp", .created_at = now,
                        .name_embedding = std::vector<float>{0.1f, 0.2f, 0.3f},
                        .summary = "Creative people"};
        OK(driver.save_community_node(c));

        auto r = driver.get_community_node(c.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().name_embedding.has_value());
        CHECK(r.value().name_embedding->size() == 3);
    }

    SECTION("save_community_node_embedding") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Engineers",
                        .group_id = "grp", .created_at = now};
        OK(driver.save_community_node(c));

        std::vector<float> emb{0.5f, 0.6f, 0.7f};
        OK(driver.save_community_node_embedding(c.uuid, emb));

        auto r = driver.get_community_node(c.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().name_embedding.has_value());
        CHECK(r.value().name_embedding->size() == 3);
    }

    SECTION("delete community") {
        CommunityNode c{.uuid = uuid::generate(), .name = "ToDelete",
                        .group_id = "grp", .created_at = now};
        OK(driver.save_community_node(c));
        OK(driver.delete_community_node(c.uuid));

        auto r = driver.get_community_node(c.uuid);
        CHECK_FALSE(r.has_value());
    }
}

// ============================================================================
// Community Edge (HAS_MEMBER) CRUD
// ============================================================================

TEST_CASE("KuzuDriver HAS_MEMBER edge", "[kuzu][community][has_member]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    CommunityNode community{.uuid = uuid::generate(), .name = "Dev Team",
                            .group_id = "grp", .created_at = now};
    OK(driver.save_community_node(community));

    EntityNode entity{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                      .created_at = now};
    OK(driver.save_entity_node(entity));

    SECTION("save HAS_MEMBER to entity") {
        CommunityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                           .source_node_uuid = community.uuid,
                           .target_node_uuid = entity.uuid,
                           .created_at = now};
        auto r = driver.save_community_edge(edge);
        REQUIRE(r.has_value());
    }

    SECTION("save HAS_MEMBER to community (sub-community)") {
        CommunityNode sub{.uuid = uuid::generate(), .name = "Sub Team",
                          .group_id = "grp", .created_at = now};
        OK(driver.save_community_node(sub));

        CommunityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                           .source_node_uuid = community.uuid,
                           .target_node_uuid = sub.uuid,
                           .created_at = now};
        auto r = driver.save_community_edge(edge);
        REQUIRE(r.has_value());
    }

    SECTION("delete HAS_MEMBER edge") {
        CommunityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                           .source_node_uuid = community.uuid,
                           .target_node_uuid = entity.uuid,
                           .created_at = now};
        OK(driver.save_community_edge(edge));
        auto r = driver.delete_community_edge(edge.uuid);
        REQUIRE(r.has_value());
    }

    SECTION("get_entity_community finds community for entity") {
        CommunityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                           .source_node_uuid = community.uuid,
                           .target_node_uuid = entity.uuid,
                           .created_at = now};
        OK(driver.save_community_edge(edge));

        auto r = driver.get_entity_community(entity.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().has_value());
        CHECK(r.value()->uuid == community.uuid);
    }

    SECTION("get_entity_community returns nullopt for unassigned entity") {
        auto r = driver.get_entity_community(entity.uuid);
        REQUIRE(r.has_value());
        CHECK_FALSE(r.value().has_value());
    }
}

// ============================================================================
// Community neighbor queries
// ============================================================================

TEST_CASE("KuzuDriver community neighbor queries", "[kuzu][community][neighbors]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    // Build a small graph: A -[RELATES_TO]-> B, both in "grp"
    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(driver.save_entity_node(a));
    OK(driver.save_entity_node(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "KNOWS", .fact = "A knows B", .created_at = now};
    OK(driver.save_entity_edge(edge));

    SECTION("get_entity_neighbors returns neighbors") {
        auto r = driver.get_entity_neighbors(a.uuid, "grp");
        REQUIRE(r.has_value());
        // Undirected traversal through RelatesToNode_ can include self-reference.
        // Check that B is among the neighbors.
        REQUIRE(r.value().size() >= 1);
        bool found_b = false;
        for (auto& n : r.value()) {
            if (n.node_uuid == b.uuid) {
                found_b = true;
                CHECK(n.edge_count >= 1);
            }
        }
        CHECK(found_b);
    }

    SECTION("get_neighbor_communities when neighbor has a community") {
        CommunityNode community{.uuid = uuid::generate(), .name = "B's community",
                                .group_id = "grp", .created_at = now};
        OK(driver.save_community_node(community));
        CommunityEdge ce{.uuid = uuid::generate(), .group_id = "grp",
                         .source_node_uuid = community.uuid,
                         .target_node_uuid = b.uuid,
                         .created_at = now};
        OK(driver.save_community_edge(ce));

        auto r = driver.get_neighbor_communities(a.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() >= 1);
        CHECK(r.value()[0].uuid == community.uuid);
    }
}

// ============================================================================
// Saga Node CRUD
// ============================================================================

TEST_CASE("KuzuDriver saga node CRUD", "[kuzu][saga]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    SECTION("save and get saga") {
        SagaNode saga{.uuid = uuid::generate(), .name = "test-saga",
                      .group_id = "grp", .created_at = now};
        OK(driver.save_saga_node(saga));

        auto r = driver.get_saga_node(saga.uuid);
        REQUIRE(r.has_value());
        CHECK(r.value().name == "test-saga");
        CHECK(r.value().group_id == "grp");
    }

    SECTION("get saga by name") {
        SagaNode saga{.uuid = uuid::generate(), .name = "my-saga",
                      .group_id = "grp", .created_at = now};
        OK(driver.save_saga_node(saga));

        auto r = driver.get_saga_by_name("my-saga", "grp");
        REQUIRE(r.has_value());
        REQUIRE(r.value().has_value());
        CHECK(r.value()->uuid == saga.uuid);
    }

    SECTION("get saga by name returns nullopt when not found") {
        auto r = driver.get_saga_by_name("nonexistent", "grp");
        REQUIRE(r.has_value());
        CHECK_FALSE(r.value().has_value());
    }

    SECTION("get saga by name scoped by group_id") {
        SagaNode saga{.uuid = uuid::generate(), .name = "shared-name",
                      .group_id = "group_a", .created_at = now};
        OK(driver.save_saga_node(saga));

        auto r = driver.get_saga_by_name("shared-name", "group_b");
        REQUIRE(r.has_value());
        CHECK_FALSE(r.value().has_value());
    }

    SECTION("delete saga") {
        SagaNode saga{.uuid = uuid::generate(), .name = "to-delete",
                      .group_id = "grp", .created_at = now};
        OK(driver.save_saga_node(saga));

        auto r = driver.delete_saga_node(saga.uuid);
        REQUIRE(r.has_value());

        auto r2 = driver.get_saga_node(saga.uuid);
        CHECK_FALSE(r2.has_value());
    }

    SECTION("save saga is idempotent (MERGE)") {
        SagaNode saga{.uuid = uuid::generate(), .name = "saga-v1",
                      .group_id = "grp", .created_at = now};
        OK(driver.save_saga_node(saga));

        saga.name = "saga-v2";
        OK(driver.save_saga_node(saga));

        auto r = driver.get_saga_node(saga.uuid);
        REQUIRE(r.has_value());
        CHECK(r.value().name == "saga-v2");
    }
}

// ============================================================================
// HAS_EPISODE and NEXT_EPISODE Edge Operations
// ============================================================================

TEST_CASE("KuzuDriver HAS_EPISODE edge", "[kuzu][saga][has_episode]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    SagaNode saga{.uuid = uuid::generate(), .name = "ep-saga",
                  .group_id = "grp", .created_at = now};
    OK(driver.save_saga_node(saga));

    EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Hello",
                     .valid_at = now};
    OK(driver.save_episodic_node(ep1));

    SECTION("save HAS_EPISODE edge") {
        auto edge_uuid = uuid::generate();
        auto r = driver.save_has_episode_edge(edge_uuid, saga.uuid, ep1.uuid, "grp", now);
        REQUIRE(r.has_value());
    }

    SECTION("get_last_episode_in_saga returns episode") {
        auto edge_uuid = uuid::generate();
        OK(driver.save_has_episode_edge(edge_uuid, saga.uuid, ep1.uuid, "grp", now));

        auto r = driver.get_last_episode_in_saga(saga.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().has_value());
        CHECK(r.value().value() == ep1.uuid);
    }

    SECTION("get_last_episode_in_saga returns nullopt for empty saga") {
        SagaNode empty{.uuid = uuid::generate(), .name = "empty-saga",
                       .group_id = "grp", .created_at = now};
        OK(driver.save_saga_node(empty));

        auto r = driver.get_last_episode_in_saga(empty.uuid);
        REQUIRE(r.has_value());
        CHECK_FALSE(r.value().has_value());
    }

    SECTION("get_last_episode_in_saga with exclude") {
        EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                         .created_at = now, .source = EpisodeType::message,
                         .source_description = "test", .content = "World",
                         .valid_at = now + 24h};
        OK(driver.save_episodic_node(ep2));
        OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
        OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));

        // Exclude ep2 (the most recent), should get ep1
        auto r = driver.get_last_episode_in_saga(saga.uuid, ep2.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().has_value());
        CHECK(r.value().value() == ep1.uuid);
    }

    SECTION("get_last_episode_in_saga returns most recent by valid_at") {
        EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                         .created_at = now, .source = EpisodeType::message,
                         .source_description = "test", .content = "World",
                         .valid_at = now + 24h};
        OK(driver.save_episodic_node(ep2));
        OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
        OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));

        auto r = driver.get_last_episode_in_saga(saga.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r.value().has_value());
        CHECK(r.value().value() == ep2.uuid);
    }
}

TEST_CASE("KuzuDriver NEXT_EPISODE edge", "[kuzu][saga][next_episode]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "First",
                     .valid_at = now};
    EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Second",
                     .valid_at = now + 24h};
    OK(driver.save_episodic_node(ep1));
    OK(driver.save_episodic_node(ep2));

    SECTION("save NEXT_EPISODE edge") {
        auto edge_uuid = uuid::generate();
        auto r = driver.save_next_episode_edge(edge_uuid, ep1.uuid, ep2.uuid, "grp", now);
        REQUIRE(r.has_value());
    }
}

// ============================================================================
// Saga-aware Episode Retrieval
// ============================================================================

TEST_CASE("KuzuDriver retrieve_episodes_by_saga", "[kuzu][saga][retrieve]") {
    auto driver = make_driver();
    auto now = make_time(2024, 6, 1);

    SagaNode saga{.uuid = uuid::generate(), .name = "my-saga",
                  .group_id = "grp", .created_at = now};
    OK(driver.save_saga_node(saga));

    EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Alpha",
                     .valid_at = now};
    EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Beta",
                     .valid_at = now + 24h};
    EpisodicNode ep3{.uuid = uuid::generate(), .name = "ep3", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Gamma",
                     .valid_at = now + 48h};
    OK(driver.save_episodic_node(ep1));
    OK(driver.save_episodic_node(ep2));
    OK(driver.save_episodic_node(ep3));

    OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
    OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));
    OK(driver.save_has_episode_edge(uuid::generate(), saga.uuid, ep3.uuid, "grp", now));

    SECTION("retrieves all episodes in saga") {
        auto far_future = now + 7200h;
        auto r = driver.retrieve_episodes_by_saga("my-saga", "grp", far_future, 20);
        REQUIRE(r.has_value());
        CHECK(r.value().size() == 3);
    }

    SECTION("filters by reference_time") {
        // Only ep1 is valid at or before now
        auto r = driver.retrieve_episodes_by_saga("my-saga", "grp", now, 20);
        REQUIRE(r.has_value());
        CHECK(r.value().size() == 1);
        CHECK(r.value()[0].content == "Alpha");
    }

    SECTION("respects limit") {
        auto far_future = now + 7200h;
        auto r = driver.retrieve_episodes_by_saga("my-saga", "grp", far_future, 2);
        REQUIRE(r.has_value());
        CHECK(r.value().size() == 2);
    }

    SECTION("returns empty for nonexistent saga") {
        auto r = driver.retrieve_episodes_by_saga("no-such-saga", "grp", now + 7200h, 20);
        REQUIRE(r.has_value());
        CHECK(r.value().empty());
    }

    SECTION("returns ordered by valid_at DESC") {
        auto far_future = now + 7200h;
        auto r = driver.retrieve_episodes_by_saga("my-saga", "grp", far_future, 20);
        REQUIRE(r.has_value());
        REQUIRE(r.value().size() == 3);
        // Most recent first
        CHECK(r.value()[0].content == "Gamma");
        CHECK(r.value()[1].content == "Beta");
        CHECK(r.value()[2].content == "Alpha");
    }
}
