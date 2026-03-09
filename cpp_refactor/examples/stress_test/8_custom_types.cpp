/*
 * CUSTOM ENTITY/EDGE TYPES STRESS TEST
 *
 * Exercises TypeDefinitions parsing and ingestion:
 * - YAML with special characters in descriptions
 * - Many entity types (10+)
 * - Entity type with many fields (20+)
 * - Edge type referencing undefined entity type
 * - Empty fields map
 * - Unicode in type names and field names
 * - Full ingestion with custom types
 *
 * Requires OPENAI_API_KEY.
 */

#include "shared.h"

#include <graphiti/type_definitions.h>

#include <chrono>
#include <format>

using namespace graphiti;

int main() {
    auto now = std::chrono::system_clock::now();

    // ====================================================================
    stress::separator("YAML: SPECIAL CHARACTERS IN DESCRIPTIONS");
    // ====================================================================
    {
        auto result = TypeDefinitions::from_yaml_string(
            "entity_types:\n"
            "  Person:\n"
            "    description: \"A person with special chars: <>&!\"\n"
            "    fields:\n"
            "      name: \"The person's full name (first & last)\"\n"
        );
        stress::test("special chars parse ok", result.has_value());
        if (result.has_value()) {
            stress::test("description preserved",
                result.value().entity_types[0].description.find("special") != std::string::npos);
        }
    }

    // ====================================================================
    stress::separator("YAML: MANY ENTITY TYPES (12)");
    // ====================================================================
    {
        auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human"
  Organization:
    description: "A company"
  Location:
    description: "A place"
  Event:
    description: "An occurrence"
  Product:
    description: "A thing for sale"
  Technology:
    description: "A tech concept"
  Document:
    description: "A written work"
  Vehicle:
    description: "A mode of transport"
  Animal:
    description: "A living creature"
  Building:
    description: "A structure"
  Concept:
    description: "An abstract idea"
  Food:
    description: "Something edible"
)");
        stress::test("12 entity types parsed", result.has_value());
        if (result.has_value()) {
            stress::test("correct count", result.value().entity_types.size() == 12);
            auto prompt = result.value().entity_types_prompt_json();
            auto j = nlohmann::json::parse(prompt);
            // 12 custom + 1 Entity = 13
            stress::test("prompt has 13 types", j.size() == 13);
        }
    }

    // ====================================================================
    stress::separator("YAML: ENTITY TYPE WITH MANY FIELDS (20)");
    // ====================================================================
    {
        std::string yaml = "entity_types:\n  DetailedPerson:\n    description: \"Very detailed\"\n    fields:\n";
        for (int i = 0; i < 20; ++i) {
            yaml += std::format("      field_{}: \"Description for field {}\"\n", i, i);
        }
        auto result = TypeDefinitions::from_yaml_string(yaml);
        stress::test("20 fields parsed", result.has_value());
        if (result.has_value()) {
            stress::test("field count correct", result.value().entity_types[0].fields.size() == 20);
            auto schema = nlohmann::json::parse(
                result.value().attribute_schema_for("DetailedPerson"));
            stress::test("schema has 20 properties", schema["properties"].size() == 20);
        }
    }

    // ====================================================================
    stress::separator("YAML: EDGE TYPE REFERENCING UNDEFINED ENTITY TYPE");
    // ====================================================================
    {
        auto result = TypeDefinitions::from_yaml_string(R"(
edge_types:
  LIVES_IN:
    description: "Where someone lives"
    source: Alien
    target: Planet
)");
        stress::test("undefined type refs parse ok", result.has_value());
        if (result.has_value()) {
            auto j = result.value().edge_types_prompt_json();
            stress::test("edge prompt generated", j.size() == 1);
            stress::test("source preserved", j[0]["fact_type_signatures"][0][0] == "Alien");
        }
    }

    // ====================================================================
    stress::separator("YAML: ENTITY TYPE WITH EMPTY FIELDS");
    // ====================================================================
    {
        auto result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  SimpleType:
    description: "A type with no custom fields"
)");
        stress::test("empty fields parsed", result.has_value());
        if (result.has_value()) {
            stress::test("no fields", result.value().entity_types[0].fields.empty());
            stress::test("attribute schema minimal",
                result.value().attribute_schema_for("SimpleType") != "{}");
        }
    }

    // ====================================================================
    stress::separator("YAML: UNICODE IN TYPE NAMES AND DESCRIPTIONS");
    // ====================================================================
    {
        auto result = TypeDefinitions::from_yaml_string(
            "entity_types:\n"
            "  \"Personne\":\n"
            "    description: \"Une personne humaine\"\n"
            "    fields:\n"
            "      \"pr\xc3\xa9nom\": \"Le pr\xc3\xa9nom\"\n"
            "      \"nom_de_famille\": \"Le nom de famille\"\n"
        );
        stress::test("unicode YAML parsed", result.has_value());
        if (result.has_value()) {
            stress::test("unicode type name", result.value().entity_types[0].name == "Personne");
            stress::test("unicode field exists",
                result.value().entity_types[0].fields.count("pr\xc3\xa9nom") == 1);
        }
    }

    // ====================================================================
    stress::separator("INGESTION WITH CUSTOM TYPES (PERSON/ORG)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto defs_result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"
      occupation: "Job"
  Organization:
    description: "A company"
    fields:
      industry: "Industry sector"

edge_types:
  WORKS_AT:
    description: "Employment relationship"
    source: Person
    target: Organization
)");
        stress::test("type defs parsed", defs_result.has_value());

        if (defs_result.has_value()) {
            auto& defs = defs_result.value();
            auto result = g.add_episode(
                "ep1",
                "Alice Johnson works as a software engineer at TechCorp, a technology company.",
                "chat", now, EpisodeType::message,
                "typed_group", "", std::nullopt, std::nullopt, std::nullopt,
                false, &defs
            );
            stress::test("typed ingestion succeeds", result.has_value());

            if (result.has_value()) {
                auto& nodes = result.value().nodes;
                std::cout << std::format("    -> {} nodes, {} edges\n",
                    nodes.size(), result.value().edges.size());

                bool found_labeled = false;
                bool found_attrs = false;
                for (auto& node : nodes) {
                    std::cout << std::format("    -> node '{}' labels=[", node.name);
                    for (size_t i = 0; i < node.labels.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << node.labels[i];
                    }
                    std::cout << "]";
                    if (!node.attributes.empty()) {
                        std::cout << " attrs=" << node.attributes.dump();
                        found_attrs = true;
                    }
                    std::cout << "\n";
                    if (!node.labels.empty()) found_labeled = true;
                }

                stress::test("at least one node has labels", found_labeled);
                stress::test("at least one node has attributes", found_attrs);

                for (auto& edge : result.value().edges) {
                    std::cout << std::format("    -> edge '{}': {}\n", edge.name, edge.fact);
                }
            }
        }
    }

    // ====================================================================
    stress::separator("INGESTION WITH EXCLUDED ENTITY TYPE");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto defs_result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"

exclude_entity_types:
  - Entity
)");
        stress::test("excluded type defs parsed", defs_result.has_value());

        if (defs_result.has_value()) {
            auto& defs = defs_result.value();
            auto result = g.add_episode(
                "ep1",
                "Alice talked about the Grand Canyon with Bob.",
                "chat", now, EpisodeType::message,
                "excl_group", "", std::nullopt, std::nullopt, std::nullopt,
                false, &defs
            );
            stress::test("excluded-type ingestion succeeds", result.has_value());

            if (result.has_value()) {
                std::cout << std::format("    -> {} nodes (Entity type excluded)\n",
                    result.value().nodes.size());
                for (auto& node : result.value().nodes) {
                    std::cout << std::format("    -> '{}'\n", node.name);
                }
            }
        }
    }

    // ====================================================================
    stress::separator("BULK INGESTION WITH CUSTOM TYPES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto defs_result = TypeDefinitions::from_yaml_string(R"(
entity_types:
  Person:
    description: "A human person"
    fields:
      first_name: "First name"
  Organization:
    description: "A company"

edge_types:
  WORKS_AT:
    description: "Employment"
    source: Person
    target: Organization
)");
        stress::test("bulk type defs parsed", defs_result.has_value());

        if (defs_result.has_value()) {
            auto& defs = defs_result.value();
            std::vector<RawEpisode> episodes = {
                {"ep1", "Alice works at Acme Corp.", "chat", now},
                {"ep2", "Bob works at TechInc.", "chat", now + std::chrono::seconds(60)},
            };

            auto result = g.add_episode_bulk(
                episodes, "bulk_typed", "", std::nullopt, std::nullopt, &defs
            );
            stress::test("bulk typed ingestion succeeds", result.has_value());

            if (result.has_value()) {
                std::cout << std::format("    -> {} episodes, {} nodes, {} edges\n",
                    result.value().episodes.size(),
                    result.value().nodes.size(),
                    result.value().edges.size());
            }
        }
    }

    // ====================================================================
    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
