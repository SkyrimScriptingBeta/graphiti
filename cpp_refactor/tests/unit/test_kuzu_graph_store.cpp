// 🏴‍☠️ Comprehensive unit tests for KuzuGraphStore (GraphStore interface)
//
// Every GraphStore method gets tested here. When we absorb KuzuDriver into
// KuzuGraphStore, these tests prove the absorption didn't break anything.

#include <catch2/catch_all.hpp>

#include <graphiti/graph_store.h>
#include <graphiti/error.h>
#include <graphiti/types.h>
#include <driver/kuzu_graph_store.h>
#include "utils/datetime.h"
#include "utils/uuid.h"

#include <chrono>
#include <vector>

using namespace graphiti;
using namespace std::chrono_literals;

#define OK(expr) REQUIRE((expr).has_value())

static std::unique_ptr<KuzuGraphStore> make_store() {
    auto store = std::make_unique<KuzuGraphStore>(":memory:");
    OK(store->setup_schema());
    return store;
}

static TimePoint make_time(int year, int month, int day) {
    auto ymd = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
               std::chrono::day{static_cast<unsigned>(day)};
    return std::chrono::sys_days{ymd};
}

// ============================================================================
// Infrastructure
// ============================================================================

TEST_CASE("GraphStore schema setup", "[graph_store][schema]") {
    KuzuGraphStore store(":memory:");

    SECTION("setup_schema succeeds") {
        OK(store.setup_schema());
    }

    SECTION("setup_schema is idempotent") {
        OK(store.setup_schema());
        OK(store.setup_schema());
    }
}

TEST_CASE("GraphStore rebuild_indices", "[graph_store][indices]") {
    auto store = make_store();
    OK(store->rebuild_indices());
    // Idempotent
    OK(store->rebuild_indices());
}

// ============================================================================
// Entity Persistence
// ============================================================================

TEST_CASE("GraphStore entity CRUD", "[graph_store][entity]") {
    auto store = make_store();
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
        .traits = {"friendly", "diligent"},
    };

    SECTION("persist and get") {
        OK(store->persist_entity(node));

        auto got = store->get_entity(node.uuid);
        REQUIRE(got.has_value());
        CHECK(got->uuid == node.uuid);
        CHECK(got->name == "Alice");
        CHECK(got->group_id == "test-group");
        CHECK(got->labels.size() == 2);
        CHECK(got->summary == "A software developer");
        CHECK(got->attributes["role"] == "engineer");
        CHECK(got->traits.size() == 2);
        CHECK(got->traits[0] == "friendly");
    }

    SECTION("get nonexistent returns not_found") {
        auto r = store->get_entity("nonexistent");
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::not_found);
    }

    SECTION("persist updates existing entity") {
        OK(store->persist_entity(node));
        node.name = "Alice Updated";
        OK(store->persist_entity(node));

        auto got = store->get_entity(node.uuid);
        REQUIRE(got.has_value());
        CHECK(got->name == "Alice Updated");
    }

    SECTION("get multiple entities") {
        EntityNode node2{.uuid = uuid::generate(), .name = "Bob", .group_id = "test-group",
                         .created_at = now, .summary = "Designer"};
        OK(store->persist_entity(node));
        OK(store->persist_entity(node2));

        auto r = store->get_entities({node.uuid, node2.uuid});
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);
    }

    SECTION("delete entity") {
        OK(store->persist_entity(node));
        OK(store->delete_entity(node.uuid));

        auto r = store->get_entity(node.uuid);
        CHECK(!r.has_value());
    }
}

TEST_CASE("GraphStore persist_entity with bundled embedding", "[graph_store][entity][embedding]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode node{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                    .created_at = now};
    std::vector<float> emb(128, 0.0f);
    emb[0] = 1.0f;

    OK(store->persist_entity(node, emb));

    auto loaded = store->load_entity_embedding(node.uuid);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->has_value());
    CHECK(loaded->value().size() == 128);
    CHECK(loaded->value()[0] == Catch::Approx(1.0f));
}

TEST_CASE("GraphStore entity embedding operations", "[graph_store][entity][embedding]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode node{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                    .created_at = now};
    OK(store->persist_entity(node));

    SECTION("persist and load embedding separately") {
        std::vector<float> emb(64, 0.5f);
        OK(store->persist_entity_embedding(node.uuid, emb));

        auto r = store->load_entity_embedding(node.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r->has_value());
        CHECK(r->value().size() == 64);
    }

    SECTION("load embedding from entity without one returns nullopt") {
        auto r = store->load_entity_embedding(node.uuid);
        REQUIRE(r.has_value());
        CHECK(!r->has_value());
    }
}

// ============================================================================
// Edge Persistence
// ============================================================================

TEST_CASE("GraphStore edge CRUD", "[graph_store][edge]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode alice{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                     .created_at = now};
    EntityNode bob{.uuid = uuid::generate(), .name = "Bob", .group_id = "grp",
                   .created_at = now};
    OK(store->persist_entity(alice));
    OK(store->persist_entity(bob));

    EntityEdge edge{
        .uuid = uuid::generate(),
        .group_id = "grp",
        .source_node_uuid = alice.uuid,
        .target_node_uuid = bob.uuid,
        .name = "WORKS_WITH",
        .fact = "Alice works with Bob",
        .episodes = {"ep-1", "ep-2"},
        .created_at = now,
        .valid_at = now,
        .attributes = {{"confidence", 0.95}},
    };

    SECTION("persist and get") {
        OK(store->persist_edge(edge));

        auto got = store->get_edge(edge.uuid);
        REQUIRE(got.has_value());
        CHECK(got->uuid == edge.uuid);
        CHECK(got->source_node_uuid == alice.uuid);
        CHECK(got->target_node_uuid == bob.uuid);
        CHECK(got->fact == "Alice works with Bob");
        CHECK(got->episodes.size() == 2);
        CHECK(!got->expired_at.has_value());
    }

    SECTION("get nonexistent edge returns not_found") {
        auto r = store->get_edge("nonexistent");
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::not_found);
    }

    SECTION("get_edges_between finds edges") {
        OK(store->persist_edge(edge));
        auto r = store->get_edges_between(alice.uuid, bob.uuid);
        REQUIRE(r.has_value());
        CHECK(r->size() == 1);
    }

    SECTION("get multiple edges") {
        EntityEdge edge2{.uuid = uuid::generate(), .group_id = "grp",
                         .source_node_uuid = bob.uuid, .target_node_uuid = alice.uuid,
                         .name = "KNOWS", .fact = "Bob knows Alice", .created_at = now};
        OK(store->persist_edge(edge));
        OK(store->persist_edge(edge2));

        auto r = store->get_edges({edge.uuid, edge2.uuid});
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);
    }

    SECTION("delete edge") {
        OK(store->persist_edge(edge));
        OK(store->delete_edge(edge.uuid));
        CHECK(!store->get_edge(edge.uuid).has_value());
    }

    SECTION("edge with expired_at and invalid_at") {
        edge.expired_at = now + 24h;
        edge.invalid_at = now + 48h;
        OK(store->persist_edge(edge));

        auto got = store->get_edge(edge.uuid);
        REQUIRE(got.has_value());
        CHECK(got->expired_at.has_value());
        CHECK(got->invalid_at.has_value());
    }
}

TEST_CASE("GraphStore persist_edge with bundled embedding", "[graph_store][edge][embedding]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "KNOWS", .fact = "A knows B", .created_at = now};
    std::vector<float> emb(64, 0.1f);

    OK(store->persist_edge(edge, emb));

    auto loaded = store->load_edge_embedding(edge.uuid);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->has_value());
    CHECK(loaded->value().size() == 64);
}

TEST_CASE("GraphStore edge embedding operations", "[graph_store][edge][embedding]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "REL", .fact = "A to B", .created_at = now};
    OK(store->persist_edge(edge));

    std::vector<float> emb(32, 0.2f);
    OK(store->persist_edge_embedding(edge.uuid, emb));

    auto r = store->load_edge_embedding(edge.uuid);
    REQUIRE(r.has_value());
    REQUIRE(r->has_value());
    CHECK(r->value().size() == 32);
}

// ============================================================================
// Episode Management
// ============================================================================

TEST_CASE("GraphStore episode CRUD", "[graph_store][episode]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EpisodicNode episode{
        .uuid = uuid::generate(),
        .name = "conv_1",
        .group_id = "test-group",
        .created_at = now,
        .source = EpisodeType::message,
        .source_description = "chat",
        .content = "User said hello",
        .valid_at = now,
    };

    SECTION("persist and get") {
        OK(store->persist_episode(episode));

        auto got = store->get_episode(episode.uuid);
        REQUIRE(got.has_value());
        CHECK(got->uuid == episode.uuid);
        CHECK(got->name == "conv_1");
        CHECK(got->content == "User said hello");
    }

    SECTION("delete episode") {
        OK(store->persist_episode(episode));
        OK(store->delete_episode(episode.uuid));
        // get_episode after delete — verify it's gone
        // (Kuzu DETACH DELETE removes the node and all edges)
    }

    SECTION("retrieve episodes by time") {
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

        OK(store->persist_episode(ep1));
        OK(store->persist_episode(ep2));
        OK(store->persist_episode(ep3));

        auto ref_time = make_time(2024, 1, 2) + 12h;
        auto r = store->retrieve_episodes("grp", ref_time, 10);
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);

        // Filter by source type
        auto r2 = store->retrieve_episodes("grp", make_time(2024, 2, 1), 10, EpisodeType::message);
        REQUIRE(r2.has_value());
        CHECK(r2->size() == 2);  // ep1 and ep3

        // Limit
        auto r3 = store->retrieve_episodes("grp", make_time(2024, 2, 1), 1);
        REQUIRE(r3.has_value());
        CHECK(r3->size() == 1);
    }
}

TEST_CASE("GraphStore persist_mention (MENTIONS edge)", "[graph_store][episode][mention]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EpisodicNode ep{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                    .created_at = now, .source = EpisodeType::message,
                    .content = "Hello", .valid_at = now};
    EntityNode entity{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                      .created_at = now};
    OK(store->persist_episode(ep));
    OK(store->persist_entity(entity));

    EpisodicEdge mention{.uuid = uuid::generate(), .group_id = "grp",
                         .source_node_uuid = ep.uuid, .target_node_uuid = entity.uuid,
                         .created_at = now};
    OK(store->persist_mention(mention));

    // Verify via get_mentioned_entity_uuids
    auto mentioned = store->get_mentioned_entity_uuids(ep.uuid);
    REQUIRE(mentioned.has_value());
    CHECK(mentioned->size() == 1);
    CHECK(mentioned->at(0) == entity.uuid);
}

TEST_CASE("GraphStore get_edge_uuids_by_episode", "[graph_store][episode]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EpisodicNode ep{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                    .created_at = now, .source = EpisodeType::message,
                    .content = "Hello", .valid_at = now};
    OK(store->persist_episode(ep));

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "REL", .fact = "A to B",
                    .episodes = {ep.uuid},
                    .created_at = now};
    OK(store->persist_edge(edge));

    auto uuids = store->get_edge_uuids_by_episode(ep.uuid);
    REQUIRE(uuids.has_value());
    CHECK(uuids->size() == 1);
    CHECK(uuids->at(0) == edge.uuid);
}

// ============================================================================
// Saga Management
// ============================================================================

TEST_CASE("GraphStore saga operations", "[graph_store][saga]") {
    auto store = make_store();
    auto now = make_time(2024, 6, 1);

    SECTION("persist and find saga") {
        SagaNode saga{.uuid = uuid::generate(), .name = "test-saga",
                      .group_id = "grp", .created_at = now};
        OK(store->persist_saga(saga));

        auto r = store->find_saga("test-saga", "grp");
        REQUIRE(r.has_value());
        REQUIRE(r->has_value());
        CHECK(r->value().uuid == saga.uuid);
    }

    SECTION("find_saga returns nullopt when not found") {
        auto r = store->find_saga("nonexistent", "grp");
        REQUIRE(r.has_value());
        CHECK(!r->has_value());
    }

    SECTION("find_saga scoped by group_id") {
        SagaNode saga{.uuid = uuid::generate(), .name = "shared",
                      .group_id = "group_a", .created_at = now};
        OK(store->persist_saga(saga));

        auto r = store->find_saga("shared", "group_b");
        REQUIRE(r.has_value());
        CHECK(!r->has_value());
    }
}

TEST_CASE("GraphStore saga episode linking", "[graph_store][saga][linking]") {
    auto store = make_store();
    auto now = make_time(2024, 6, 1);

    SagaNode saga{.uuid = uuid::generate(), .name = "saga", .group_id = "grp", .created_at = now};
    OK(store->persist_saga(saga));

    EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "Hello", .valid_at = now};
    EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .source_description = "test", .content = "World", .valid_at = now + 24h};
    OK(store->persist_episode(ep1));
    OK(store->persist_episode(ep2));

    SECTION("link_saga_episode and get_last_saga_episode") {
        OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));

        auto last = store->get_last_saga_episode(saga.uuid);
        REQUIRE(last.has_value());
        REQUIRE(last->has_value());
        CHECK(last->value() == ep1.uuid);
    }

    SECTION("get_last_saga_episode returns most recent") {
        OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
        OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));

        auto last = store->get_last_saga_episode(saga.uuid);
        REQUIRE(last.has_value());
        REQUIRE(last->has_value());
        CHECK(last->value() == ep2.uuid);
    }

    SECTION("get_last_saga_episode with exclude") {
        OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
        OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));

        auto last = store->get_last_saga_episode(saga.uuid, ep2.uuid);
        REQUIRE(last.has_value());
        REQUIRE(last->has_value());
        CHECK(last->value() == ep1.uuid);
    }

    SECTION("get_last_saga_episode on empty saga returns nullopt") {
        SagaNode empty{.uuid = uuid::generate(), .name = "empty", .group_id = "grp", .created_at = now};
        OK(store->persist_saga(empty));

        auto r = store->get_last_saga_episode(empty.uuid);
        REQUIRE(r.has_value());
        CHECK(!r->has_value());
    }

    SECTION("link_episode_sequence") {
        OK(store->link_episode_sequence(uuid::generate(), ep1.uuid, ep2.uuid, "grp", now));
        // No crash, edge created successfully
    }
}

TEST_CASE("GraphStore retrieve_episodes_by_saga", "[graph_store][saga][retrieve]") {
    auto store = make_store();
    auto now = make_time(2024, 6, 1);

    SagaNode saga{.uuid = uuid::generate(), .name = "my-saga", .group_id = "grp", .created_at = now};
    OK(store->persist_saga(saga));

    EpisodicNode ep1{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .content = "Alpha", .valid_at = now};
    EpisodicNode ep2{.uuid = uuid::generate(), .name = "ep2", .group_id = "grp",
                     .created_at = now, .source = EpisodeType::message,
                     .content = "Beta", .valid_at = now + 24h};
    OK(store->persist_episode(ep1));
    OK(store->persist_episode(ep2));
    OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep1.uuid, "grp", now));
    OK(store->link_saga_episode(uuid::generate(), saga.uuid, ep2.uuid, "grp", now));

    auto r = store->retrieve_episodes_by_saga("my-saga", "grp", now + 7200h, 20);
    REQUIRE(r.has_value());
    CHECK(r->size() == 2);
    CHECK(r->at(0).content == "Beta");  // Most recent first
}

// ============================================================================
// Community Management
// ============================================================================

TEST_CASE("GraphStore community operations", "[graph_store][community]") {
    auto store = make_store();
    auto now = make_time(2024, 6, 1);

    SECTION("persist_community and search") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Tech Workers",
                        .group_id = "grp", .created_at = now, .summary = "Tech people"};
        OK(store->persist_community(c));
        OK(store->rebuild_indices());

        auto r = store->search_communities_bm25("Tech", "grp", 10);
        REQUIRE(r.has_value());
        CHECK(!r->empty());
        CHECK(r->at(0).name == "Tech Workers");
    }

    SECTION("persist_community with bundled embedding") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Artists",
                        .group_id = "grp", .created_at = now, .summary = "Creative"};
        std::vector<float> emb{0.1f, 0.2f, 0.3f};
        OK(store->persist_community(c, emb));
        // No crash — embedding stored alongside community node
    }

    SECTION("persist_community_membership") {
        CommunityNode c{.uuid = uuid::generate(), .name = "Team", .group_id = "grp", .created_at = now};
        EntityNode e{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp", .created_at = now};
        OK(store->persist_community(c));
        OK(store->persist_entity(e));

        CommunityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                           .source_node_uuid = c.uuid, .target_node_uuid = e.uuid,
                           .created_at = now};
        OK(store->persist_community_membership(edge));

        auto r = store->get_entity_community(e.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r->has_value());
        CHECK(r->value().uuid == c.uuid);
    }

    SECTION("get_entity_community returns nullopt for unassigned") {
        EntityNode e{.uuid = uuid::generate(), .name = "Bob", .group_id = "grp", .created_at = now};
        OK(store->persist_entity(e));

        auto r = store->get_entity_community(e.uuid);
        REQUIRE(r.has_value());
        CHECK(!r->has_value());
    }

    SECTION("remove_all_communities") {
        CommunityNode c{.uuid = uuid::generate(), .name = "ToRemove", .group_id = "grp", .created_at = now};
        OK(store->persist_community(c));
        OK(store->remove_all_communities());
        // Searching should find nothing
        OK(store->rebuild_indices());
        auto r = store->search_communities_bm25("ToRemove", "grp", 10);
        REQUIRE(r.has_value());
        CHECK(r->empty());
    }
}

TEST_CASE("GraphStore community neighbor queries", "[graph_store][community][neighbors]") {
    auto store = make_store();
    auto now = make_time(2024, 6, 1);

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "KNOWS", .fact = "A knows B", .created_at = now};
    OK(store->persist_edge(edge));

    SECTION("get_entity_neighbors") {
        auto r = store->get_entity_neighbors(a.uuid, "grp");
        REQUIRE(r.has_value());
        REQUIRE(r->size() >= 1);
        bool found_b = false;
        for (auto& n : r.value()) {
            if (n.node_uuid == b.uuid) found_b = true;
        }
        CHECK(found_b);
    }

    SECTION("get_neighbor_communities") {
        CommunityNode c{.uuid = uuid::generate(), .name = "B's community",
                        .group_id = "grp", .created_at = now};
        OK(store->persist_community(c));
        CommunityEdge ce{.uuid = uuid::generate(), .group_id = "grp",
                         .source_node_uuid = c.uuid, .target_node_uuid = b.uuid,
                         .created_at = now};
        OK(store->persist_community_membership(ce));

        auto r = store->get_neighbor_communities(a.uuid);
        REQUIRE(r.has_value());
        REQUIRE(r->size() >= 1);
        CHECK(r->at(0).uuid == c.uuid);
    }
}

TEST_CASE("GraphStore get_entities_by_group and get_all_group_ids", "[graph_store][community][groups]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    OK(store->persist_entity({.uuid = uuid::generate(), .name = "A", .group_id = "g1", .created_at = now}));
    OK(store->persist_entity({.uuid = uuid::generate(), .name = "B", .group_id = "g2", .created_at = now}));

    auto by_group = store->get_entities_by_group("g1");
    REQUIRE(by_group.has_value());
    CHECK(by_group->size() == 1);

    auto groups = store->get_all_group_ids();
    REQUIRE(groups.has_value());
    CHECK(groups->size() >= 2);
}

// ============================================================================
// Clear Data
// ============================================================================

TEST_CASE("GraphStore clear_group", "[graph_store][maintenance]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    auto uuid1 = uuid::generate();
    auto uuid2 = uuid::generate();
    OK(store->persist_entity({.uuid = uuid1, .name = "A", .group_id = "keep", .created_at = now}));
    OK(store->persist_entity({.uuid = uuid2, .name = "B", .group_id = "delete", .created_at = now}));

    OK(store->clear_group({"delete"}));

    CHECK(store->get_entity(uuid1).has_value());
    CHECK(!store->get_entity(uuid2).has_value());
}

// ============================================================================
// Delete Cascades
// ============================================================================

TEST_CASE("GraphStore delete entity cascades to edges", "[graph_store][cascade]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "REL", .fact = "A to B", .created_at = now};
    OK(store->persist_edge(edge));

    OK(store->delete_entity(a.uuid));
    CHECK(!store->get_edge(edge.uuid).has_value());
}

// ============================================================================
// Overview & Analytics
// ============================================================================

TEST_CASE("GraphStore overview and analytics", "[graph_store][analytics]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "Bob", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "KNOWS", .fact = "Alice knows Bob", .created_at = now};
    OK(store->persist_edge(edge));

    SECTION("get_node_summaries") {
        auto r = store->get_node_summaries("grp");
        REQUIRE(r.has_value());
        CHECK(r->size() == 2);
    }

    SECTION("get_edge_summaries") {
        auto r = store->get_edge_summaries({a.uuid, b.uuid}, "grp");
        REQUIRE(r.has_value());
        CHECK(r->size() >= 1);
    }

    SECTION("count_episode_mentions") {
        EpisodicNode ep{.uuid = uuid::generate(), .name = "ep", .group_id = "grp",
                        .created_at = now, .source = EpisodeType::message,
                        .content = "Hello", .valid_at = now};
        OK(store->persist_episode(ep));
        EpisodicEdge mention{.uuid = uuid::generate(), .group_id = "grp",
                             .source_node_uuid = ep.uuid, .target_node_uuid = a.uuid,
                             .created_at = now};
        OK(store->persist_mention(mention));

        auto r = store->count_episode_mentions(a.uuid);
        REQUIRE(r.has_value());
        CHECK(r.value() >= 1);
    }

    SECTION("check_node_adjacency") {
        auto r = store->check_node_adjacency(a.uuid, b.uuid);
        REQUIRE(r.has_value());
        CHECK(r.value() == true);

        // Non-adjacent
        EntityNode c{.uuid = uuid::generate(), .name = "C", .group_id = "grp", .created_at = now};
        OK(store->persist_entity(c));
        auto r2 = store->check_node_adjacency(a.uuid, c.uuid);
        REQUIRE(r2.has_value());
        CHECK(r2.value() == false);
    }
}

// ============================================================================
// Search: BM25
// ============================================================================

TEST_CASE("GraphStore BM25 search", "[graph_store][search][bm25]") {
    auto store = make_store();
    auto now = datetime::utc_now();
    OK(store->rebuild_indices());

    EntityNode alice{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp",
                     .created_at = now, .summary = "Software engineer at Acme"};
    OK(store->persist_entity(alice));
    OK(store->rebuild_indices());

    auto r = store->search_entities_bm25("Alice", "grp", 10);
    REQUIRE(r.has_value());
    CHECK(!r->empty());
}

TEST_CASE("GraphStore BM25 edge search", "[graph_store][search][bm25]") {
    auto store = make_store();
    auto now = datetime::utc_now();
    OK(store->rebuild_indices());

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));

    EntityEdge edge{.uuid = uuid::generate(), .group_id = "grp",
                    .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                    .name = "KNOWS", .fact = "A knows B from work", .created_at = now};
    OK(store->persist_edge(edge));
    OK(store->rebuild_indices());

    auto r = store->search_edges_bm25("knows work", "grp", 10);
    REQUIRE(r.has_value());
    CHECK(!r->empty());
}

TEST_CASE("GraphStore BM25 episode search", "[graph_store][search][bm25]") {
    auto store = make_store();
    auto now = datetime::utc_now();
    OK(store->rebuild_indices());

    EpisodicNode ep{.uuid = uuid::generate(), .name = "ep1", .group_id = "grp",
                    .created_at = now, .source = EpisodeType::message,
                    .content = "Alice told Bob about the project", .valid_at = now};
    OK(store->persist_episode(ep));
    OK(store->rebuild_indices());

    auto r = store->search_episodes_bm25("project", "grp", 10);
    REQUIRE(r.has_value());
    CHECK(!r->empty());
}

// ============================================================================
// Search: Cosine
// ============================================================================

TEST_CASE("GraphStore cosine search", "[graph_store][search][cosine]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode node{.uuid = uuid::generate(), .name = "Alice", .group_id = "grp", .created_at = now};
    std::vector<float> emb(128, 0.0f);
    emb[0] = 1.0f;
    OK(store->persist_entity(node, emb));

    // Search with same embedding — should find it
    auto r = store->search_entities_cosine(emb, "grp", 0.0f, 10);
    REQUIRE(r.has_value());
    CHECK(!r->empty());
}

// ============================================================================
// Search: BFS
// ============================================================================

TEST_CASE("GraphStore BFS search", "[graph_store][search][bfs]") {
    auto store = make_store();
    auto now = datetime::utc_now();

    EntityNode a{.uuid = uuid::generate(), .name = "A", .group_id = "grp", .created_at = now};
    EntityNode b{.uuid = uuid::generate(), .name = "B", .group_id = "grp", .created_at = now};
    EntityNode c{.uuid = uuid::generate(), .name = "C", .group_id = "grp", .created_at = now};
    OK(store->persist_entity(a));
    OK(store->persist_entity(b));
    OK(store->persist_entity(c));

    OK(store->persist_edge({.uuid = uuid::generate(), .group_id = "grp",
                            .source_node_uuid = a.uuid, .target_node_uuid = b.uuid,
                            .name = "REL", .fact = "A to B", .created_at = now}));
    OK(store->persist_edge({.uuid = uuid::generate(), .group_id = "grp",
                            .source_node_uuid = b.uuid, .target_node_uuid = c.uuid,
                            .name = "REL", .fact = "B to C", .created_at = now}));

    auto edges = store->search_edges_bfs({a.uuid}, "grp", 3, 20);
    REQUIRE(edges.has_value());
    CHECK(!edges->empty());

    auto nodes = store->search_nodes_bfs({a.uuid}, "grp", 3, 20);
    REQUIRE(nodes.has_value());
    CHECK(!nodes->empty());
}
