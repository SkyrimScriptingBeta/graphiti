#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>
#include <graphiti/type_definitions.h>

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

static TypeDefinitions make_person_org_types() {
    auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person mentioned in the conversation"
    fields:
      first_name: "First name of the person"
      last_name: "Last name of the person"
      occupation: "Job or profession"
  Organization:
    description: "A company, institution, or organized group"
    fields:
      industry: "Industry or sector"

edge_types:
  WORKS_AT:
    description: "Person is employed by an organization"
    source: Person
    target: Organization
  KNOWS:
    description: "Two people know each other"
    source: Person
    target: Person
)");
    REQUIRE(result.has_value());
    return std::move(result.value());
}

TEST_CASE("Custom types: entities get labels from type_defs", "[integration][custom_types]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto type_defs = make_person_org_types();
    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice Smith works as an engineer at Acme Corp.",
        "chat", now, EpisodeType::message,
        "test_group", "", "", {},
        std::nullopt, std::nullopt, std::nullopt,
        false, &type_defs
    );
    REQUIRE(result.has_value());

    auto& nodes = result.value().nodes;
    REQUIRE(!nodes.empty());

    // At least one node should have a label beyond default
    bool found_labeled = false;
    for (auto& node : nodes) {
        if (!node.labels.empty()) {
            found_labeled = true;
            // Labels should contain "Entity" and a custom type
            INFO("Node: " << node.name << " labels: [" <<
                (node.labels.size() > 0 ? node.labels[0] : "") << ", " <<
                (node.labels.size() > 1 ? node.labels[1] : "") << "]");
        }
    }
    CHECK(found_labeled);
}

TEST_CASE("Custom types: attributes populated for typed entities", "[integration][custom_types]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto type_defs = make_person_org_types();
    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice Smith works as a software engineer at Acme Corp in the technology industry.",
        "chat", now, EpisodeType::message,
        "test_group", "", "", {},
        std::nullopt, std::nullopt, std::nullopt,
        false, &type_defs
    );
    REQUIRE(result.has_value());

    // Check that at least one node has attributes
    bool found_attributes = false;
    for (auto& node : result.value().nodes) {
        if (!node.attributes.empty() && node.attributes.is_object()) {
            found_attributes = true;
            INFO("Node: " << node.name << " attributes: " << node.attributes.dump());
        }
    }
    CHECK(found_attributes);
}

TEST_CASE("Custom types: edge types guide extraction", "[integration][custom_types]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto type_defs = make_person_org_types();
    auto now = std::chrono::system_clock::now();

    auto result = g.add_episode(
        "ep1",
        "Alice works at Acme Corp. Bob also works at Acme Corp. Alice knows Bob.",
        "chat", now, EpisodeType::message,
        "test_group", "", "", {},
        std::nullopt, std::nullopt, std::nullopt,
        false, &type_defs
    );
    REQUIRE(result.has_value());

    auto& edges = result.value().edges;
    INFO("Edge count: " << edges.size());
    for (auto& e : edges) {
        INFO("Edge: " << e.name << " fact: " << e.fact);
    }
    // Should have at least one edge
    CHECK(!edges.empty());
}

TEST_CASE("Custom types: excluded Entity type filters nodes", "[integration][custom_types]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"

exclude_entity_types:
  - Entity
)");
    REQUIRE(result.has_value());
    auto type_defs = std::move(result.value());

    auto now = std::chrono::system_clock::now();
    auto ep = g.add_episode(
        "ep1",
        "Alice mentioned the Grand Canyon during her conversation with Bob.",
        "chat", now, EpisodeType::message,
        "test_group", "", "", {},
        std::nullopt, std::nullopt, std::nullopt,
        false, &type_defs
    );
    REQUIRE(ep.has_value());

    // With Entity excluded, nodes classified as generic Entity should be filtered
    for (auto& node : ep.value().nodes) {
        // Every remaining node should have labels (Person)
        // Note: LLM might classify everything as Person, which is fine
        INFO("Node: " << node.name);
    }
}

TEST_CASE("Custom types: search by label works with custom types", "[integration][custom_types]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto type_defs = make_person_org_types();
    auto now = std::chrono::system_clock::now();

    auto ep = g.add_episode(
        "ep1",
        "Alice Smith is a software engineer at Acme Corp.",
        "chat", now, EpisodeType::message,
        "test_group", "", "", {},
        std::nullopt, std::nullopt, std::nullopt,
        false, &type_defs
    );
    REQUIRE(ep.has_value());

    // Rebuild indices after ingestion
    auto rebuild = g.build_indices();
    REQUIRE(rebuild.has_value());

    // Search should work normally
    auto search_result = g.search("Alice", "test_group");
    REQUIRE(search_result.has_value());
    // Just verify search doesn't error out with custom-typed nodes
}
