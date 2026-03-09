#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <graphiti/type_definitions.h>

using namespace graphiti;
using json = nlohmann::json;

// ============================================================================
// YAML Parsing
// ============================================================================

TEST_CASE("TypeDefinitions: parse entity types from YAML", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"
      last_name: "Last name"
      occupation: "Job or profession"
  Organization:
    description: "A company or institution"
    fields:
      industry: "Industry sector"
)");

    REQUIRE(result.has_value());
    auto& defs = result.value();
    REQUIRE(defs.entity_types.size() == 2);

    CHECK(defs.entity_types[0].name == "Person");
    CHECK(defs.entity_types[0].description == "A human person");
    REQUIRE(defs.entity_types[0].fields.size() == 3);
    CHECK(defs.entity_types[0].fields.at("first_name") == "First name");
    CHECK(defs.entity_types[0].fields.at("last_name") == "Last name");
    CHECK(defs.entity_types[0].fields.at("occupation") == "Job or profession");

    CHECK(defs.entity_types[1].name == "Organization");
    CHECK(defs.entity_types[1].description == "A company or institution");
    REQUIRE(defs.entity_types[1].fields.size() == 1);
    CHECK(defs.entity_types[1].fields.at("industry") == "Industry sector");
}

TEST_CASE("TypeDefinitions: parse edge types from YAML", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string(R"(
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
    auto& defs = result.value();
    REQUIRE(defs.edge_types.size() == 2);

    CHECK(defs.edge_types[0].name == "WORKS_AT");
    CHECK(defs.edge_types[0].description == "Person is employed by an organization");
    CHECK(defs.edge_types[0].source_type == "Person");
    CHECK(defs.edge_types[0].target_type == "Organization");

    CHECK(defs.edge_types[1].name == "KNOWS");
    CHECK(defs.edge_types[1].source_type == "Person");
    CHECK(defs.edge_types[1].target_type == "Person");
}

TEST_CASE("TypeDefinitions: parse excluded_entity_types from YAML", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string(R"(
exclude_entity_types:
  - Entity
)");

    REQUIRE(result.has_value());
    auto& defs = result.value();
    REQUIRE(defs.excluded_entity_types.size() == 1);
    CHECK(defs.excluded_entity_types[0] == "Entity");
}

TEST_CASE("TypeDefinitions: full YAML with all sections", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"
  Organization:
    description: "A company"
    fields:
      industry: "Industry sector"

edge_types:
  WORKS_AT:
    description: "Employment relationship"
    source: Person
    target: Organization

exclude_entity_types:
  - Entity
)");

    REQUIRE(result.has_value());
    auto& defs = result.value();
    CHECK(defs.entity_types.size() == 2);
    CHECK(defs.edge_types.size() == 1);
    CHECK(defs.excluded_entity_types.size() == 1);
}

TEST_CASE("TypeDefinitions: empty YAML produces empty defs", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string("");
    REQUIRE(result.has_value());
    auto& defs = result.value();
    CHECK(defs.entity_types.empty());
    CHECK(defs.edge_types.empty());
    CHECK(defs.excluded_entity_types.empty());
}

TEST_CASE("TypeDefinitions: entity type with no fields", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Location:
    description: "A geographic place"
)");

    REQUIRE(result.has_value());
    REQUIRE(result.value().entity_types.size() == 1);
    CHECK(result.value().entity_types[0].name == "Location");
    CHECK(result.value().entity_types[0].description == "A geographic place");
    CHECK(result.value().entity_types[0].fields.empty());
}

TEST_CASE("TypeDefinitions: malformed YAML returns error", "[type_definitions][yaml]") {
    auto result = TypeDefinitions::from_yaml_string("{{{{ not valid yaml ::::");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::invalid_config);
}

// ============================================================================
// entity_types_prompt_json()
// ============================================================================

TEST_CASE("TypeDefinitions: entity_types_prompt_json includes Entity by default", "[type_definitions][prompt]") {
    TypeDefinitions defs;
    defs.entity_types = {
        {"Person", "A human person", {{"name", "Name"}}},
        {"Organization", "A company", {}}
    };

    auto prompt = defs.entity_types_prompt_json();
    auto j = json::parse(prompt);
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 3);

    CHECK(j[0]["entity_type_id"] == 0);
    CHECK(j[0]["entity_type_name"] == "Entity");
    CHECK(j[0]["entity_type_description"] == "A general entity");

    CHECK(j[1]["entity_type_id"] == 1);
    CHECK(j[1]["entity_type_name"] == "Person");
    CHECK(j[1]["entity_type_description"] == "A human person");

    CHECK(j[2]["entity_type_id"] == 2);
    CHECK(j[2]["entity_type_name"] == "Organization");
    CHECK(j[2]["entity_type_description"] == "A company");
}

TEST_CASE("TypeDefinitions: entity_types_prompt_json excludes Entity when excluded", "[type_definitions][prompt]") {
    TypeDefinitions defs;
    defs.entity_types = {{"Person", "A human person", {}}};
    defs.excluded_entity_types = {"Entity"};

    auto prompt = defs.entity_types_prompt_json();
    auto j = json::parse(prompt);
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 1);

    CHECK(j[0]["entity_type_id"] == 1);
    CHECK(j[0]["entity_type_name"] == "Person");
}

TEST_CASE("TypeDefinitions: entity_types_prompt_json with no custom types", "[type_definitions][prompt]") {
    TypeDefinitions defs;

    auto prompt = defs.entity_types_prompt_json();
    auto j = json::parse(prompt);
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 1);
    CHECK(j[0]["entity_type_name"] == "Entity");
}

TEST_CASE("TypeDefinitions: excluded custom type skipped in prompt", "[type_definitions][prompt]") {
    TypeDefinitions defs;
    defs.entity_types = {
        {"Person", "A person", {}},
        {"Internal", "Should be hidden", {}}
    };
    defs.excluded_entity_types = {"Internal"};

    auto prompt = defs.entity_types_prompt_json();
    auto j = json::parse(prompt);
    REQUIRE(j.size() == 2); // Entity + Person (Internal excluded)
    CHECK(j[0]["entity_type_name"] == "Entity");
    CHECK(j[1]["entity_type_name"] == "Person");
}

// ============================================================================
// edge_types_prompt_json()
// ============================================================================

TEST_CASE("TypeDefinitions: edge_types_prompt_json builds fact_type format", "[type_definitions][prompt]") {
    TypeDefinitions defs;
    defs.edge_types = {
        {"WORKS_AT", "Employment", "Person", "Organization"},
        {"KNOWS", "Acquaintance", "Person", "Person"}
    };

    auto j = defs.edge_types_prompt_json();
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 2);

    CHECK(j[0]["fact_type_name"] == "WORKS_AT");
    CHECK(j[0]["fact_type_description"] == "Employment");
    REQUIRE(j[0]["fact_type_signatures"].is_array());
    REQUIRE(j[0]["fact_type_signatures"].size() == 1);
    CHECK(j[0]["fact_type_signatures"][0][0] == "Person");
    CHECK(j[0]["fact_type_signatures"][0][1] == "Organization");

    CHECK(j[1]["fact_type_name"] == "KNOWS");
    CHECK(j[1]["fact_type_signatures"][0][0] == "Person");
    CHECK(j[1]["fact_type_signatures"][0][1] == "Person");
}

TEST_CASE("TypeDefinitions: edge_types_prompt_json empty when no edge types", "[type_definitions][prompt]") {
    TypeDefinitions defs;
    auto j = defs.edge_types_prompt_json();
    REQUIRE(j.is_array());
    CHECK(j.empty());
}

// ============================================================================
// attribute_schema_for()
// ============================================================================

TEST_CASE("TypeDefinitions: attribute_schema_for builds correct schema", "[type_definitions][schema]") {
    TypeDefinitions defs;
    defs.entity_types = {
        {"Person", "A person", {{"first_name", "First name"}, {"last_name", "Last name"}}}
    };

    auto schema_str = defs.attribute_schema_for("Person");
    auto schema = json::parse(schema_str);

    CHECK(schema["type"] == "object");
    REQUIRE(schema["properties"].is_object());
    REQUIRE(schema["properties"].size() == 2);

    CHECK(schema["properties"]["first_name"]["description"] == "First name");
    CHECK(schema["properties"]["first_name"]["type"] == json::array({"string", "null"}));

    CHECK(schema["properties"]["last_name"]["description"] == "Last name");
}

TEST_CASE("TypeDefinitions: attribute_schema_for unknown type returns empty", "[type_definitions][schema]") {
    TypeDefinitions defs;
    CHECK(defs.attribute_schema_for("Unknown") == "{}");
}

TEST_CASE("TypeDefinitions: attribute_schema_for type with no fields", "[type_definitions][schema]") {
    TypeDefinitions defs;
    defs.entity_types = {{"Location", "A place", {}}};

    auto schema_str = defs.attribute_schema_for("Location");
    auto schema = json::parse(schema_str);
    CHECK(schema["type"] == "object");
    CHECK(schema["properties"].is_object());
    CHECK(schema["properties"].empty());
}

// ============================================================================
// resolve_type_name()
// ============================================================================

TEST_CASE("TypeDefinitions: resolve_type_name", "[type_definitions][resolve]") {
    TypeDefinitions defs;
    defs.entity_types = {
        {"Person", "A person", {}},
        {"Organization", "A company", {}}
    };

    CHECK(defs.resolve_type_name(0) == "Entity");
    CHECK(defs.resolve_type_name(1) == "Person");
    CHECK(defs.resolve_type_name(2) == "Organization");
    CHECK(defs.resolve_type_name(99) == "Entity"); // out of range fallback
    CHECK(defs.resolve_type_name(-1) == "Entity"); // negative fallback
}

// ============================================================================
// is_excluded()
// ============================================================================

TEST_CASE("TypeDefinitions: is_excluded", "[type_definitions][exclude]") {
    TypeDefinitions defs;
    defs.excluded_entity_types = {"Entity", "Internal"};

    CHECK(defs.is_excluded("Entity"));
    CHECK(defs.is_excluded("Internal"));
    CHECK(!defs.is_excluded("Person"));
    CHECK(!defs.is_excluded(""));
}

// ============================================================================
// Round-trip: YAML -> prompt JSON
// ============================================================================

TEST_CASE("TypeDefinitions: YAML round-trip to prompt JSON", "[type_definitions][roundtrip]") {
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
      organization_type: "Type of organization"

edge_types:
  WORKS_AT:
    description: "Person is employed by an organization"
    source: Person
    target: Organization
  KNOWS:
    description: "Two people know each other"
    source: Person
    target: Person

exclude_entity_types:
  - Entity
)");

    REQUIRE(result.has_value());
    auto& defs = result.value();

    // Entity types prompt should NOT include Entity (excluded)
    auto entity_prompt = json::parse(defs.entity_types_prompt_json());
    REQUIRE(entity_prompt.size() == 2);
    CHECK(entity_prompt[0]["entity_type_name"] == "Person");
    CHECK(entity_prompt[1]["entity_type_name"] == "Organization");

    // Edge types prompt
    auto edge_prompt = defs.edge_types_prompt_json();
    REQUIRE(edge_prompt.size() == 2);
    CHECK(edge_prompt[0]["fact_type_name"] == "WORKS_AT");
    CHECK(edge_prompt[1]["fact_type_name"] == "KNOWS");

    // Attribute schema
    auto person_schema = json::parse(defs.attribute_schema_for("Person"));
    CHECK(person_schema["properties"].size() == 3);
    CHECK(person_schema["properties"]["first_name"]["description"] == "First name of the person");

    auto org_schema = json::parse(defs.attribute_schema_for("Organization"));
    CHECK(org_schema["properties"].size() == 2);
}
