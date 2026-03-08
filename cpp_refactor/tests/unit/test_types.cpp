#include <catch2/catch_all.hpp>
#include <graphiti/types.h>
#include <nlohmann/json.hpp>

using namespace graphiti;

TEST_CASE("EpisodeType string conversion", "[types]") {
    REQUIRE(to_string(EpisodeType::message) == "message");
    REQUIRE(to_string(EpisodeType::json) == "json");
    REQUIRE(to_string(EpisodeType::text) == "text");

    REQUIRE(episode_type_from_string("message") == EpisodeType::message);
    REQUIRE(episode_type_from_string("json") == EpisodeType::json);
    REQUIRE(episode_type_from_string("text") == EpisodeType::text);
    REQUIRE(episode_type_from_string("unknown") == EpisodeType::message);
}

TEST_CASE("EntityNode JSON round-trip", "[types]") {
    EntityNode node;
    node.uuid = "test-uuid-123";
    node.name = "Alice";
    node.group_id = "group-1";
    node.labels = {"Person", "Employee"};
    node.created_at = std::chrono::system_clock::now();
    node.summary = "Alice is a software engineer";
    node.attributes = {{"age", 30}, {"role", "engineer"}};
    node.name_embedding = std::vector<float>{0.1f, 0.2f, 0.3f};

    nlohmann::json j = node;
    auto restored = j.get<EntityNode>();

    REQUIRE(restored.uuid == node.uuid);
    REQUIRE(restored.name == node.name);
    REQUIRE(restored.group_id == node.group_id);
    REQUIRE(restored.labels == node.labels);
    REQUIRE(restored.summary == node.summary);
    REQUIRE(restored.attributes == node.attributes);
    REQUIRE(restored.name_embedding.has_value());
    REQUIRE(restored.name_embedding->size() == 3);
    REQUIRE(restored.name_embedding->at(0) == Catch::Approx(0.1f));
}

TEST_CASE("EntityNode JSON round-trip without embedding", "[types]") {
    EntityNode node;
    node.uuid = "no-embed";
    node.name = "Bob";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.name_embedding = std::nullopt;

    nlohmann::json j = node;
    auto restored = j.get<EntityNode>();

    REQUIRE(restored.uuid == "no-embed");
    REQUIRE_FALSE(restored.name_embedding.has_value());
}

TEST_CASE("EpisodicNode JSON round-trip", "[types]") {
    EpisodicNode node;
    node.uuid = "ep-uuid";
    node.name = "episode_1";
    node.group_id = "group-1";
    node.created_at = std::chrono::system_clock::now();
    node.source = EpisodeType::message;
    node.source_description = "chat conversation";
    node.content = "Alice: I love hiking";
    node.valid_at = node.created_at;
    node.entity_edges = {"edge-1", "edge-2"};

    nlohmann::json j = node;
    auto restored = j.get<EpisodicNode>();

    REQUIRE(restored.uuid == node.uuid);
    REQUIRE(restored.content == node.content);
    REQUIRE(restored.source == EpisodeType::message);
    REQUIRE(restored.entity_edges.size() == 2);
}

TEST_CASE("EntityEdge JSON round-trip", "[types]") {
    EntityEdge edge;
    edge.uuid = "edge-uuid";
    edge.group_id = "group-1";
    edge.source_node_uuid = "src-uuid";
    edge.target_node_uuid = "tgt-uuid";
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme Corp";
    edge.fact_embedding = std::vector<float>{0.5f, 0.6f};
    edge.episodes = {"ep-1"};
    edge.created_at = std::chrono::system_clock::now();
    edge.valid_at = edge.created_at;
    edge.invalid_at = std::nullopt;
    edge.expired_at = std::nullopt;
    edge.attributes = {{"confidence", 0.95}};

    nlohmann::json j = edge;
    auto restored = j.get<EntityEdge>();

    REQUIRE(restored.uuid == edge.uuid);
    REQUIRE(restored.name == "WORKS_AT");
    REQUIRE(restored.fact == "Alice works at Acme Corp");
    REQUIRE(restored.fact_embedding.has_value());
    REQUIRE(restored.valid_at.has_value());
    REQUIRE_FALSE(restored.invalid_at.has_value());
    REQUIRE_FALSE(restored.expired_at.has_value());
}

TEST_CASE("EpisodicEdge JSON round-trip", "[types]") {
    EpisodicEdge edge;
    edge.uuid = "ee-uuid";
    edge.group_id = "group-1";
    edge.source_node_uuid = "ep-uuid";
    edge.target_node_uuid = "entity-uuid";
    edge.created_at = std::chrono::system_clock::now();

    nlohmann::json j = edge;
    auto restored = j.get<EpisodicEdge>();

    REQUIRE(restored.uuid == edge.uuid);
    REQUIRE(restored.source_node_uuid == "ep-uuid");
    REQUIRE(restored.target_node_uuid == "entity-uuid");
}

TEST_CASE("SagaNode JSON round-trip", "[types]") {
    SagaNode node;
    node.uuid = "saga-uuid";
    node.name = "My Saga";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();

    nlohmann::json j = node;
    auto restored = j.get<SagaNode>();

    REQUIRE(restored.uuid == "saga-uuid");
    REQUIRE(restored.name == "My Saga");
}

TEST_CASE("CommunityNode JSON round-trip", "[types]") {
    CommunityNode node;
    node.uuid = "comm-uuid";
    node.name = "Tech Workers";
    node.group_id = "g";
    node.created_at = std::chrono::system_clock::now();
    node.summary = "A community of tech workers";
    node.name_embedding = std::vector<float>{0.1f, 0.2f};

    nlohmann::json j = node;
    auto restored = j.get<CommunityNode>();

    REQUIRE(restored.uuid == "comm-uuid");
    REQUIRE(restored.summary == "A community of tech workers");
    REQUIRE(restored.name_embedding.has_value());
}
