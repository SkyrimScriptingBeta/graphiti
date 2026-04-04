#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "llm/response_models.h"

using namespace graphiti;
using json = nlohmann::json;

// ============================================================================
// ExtractedEntities
// ============================================================================

TEST_CASE("ExtractedEntities parsing", "[response_models][entities]") {
    SECTION("parse typical LLM response") {
        auto j = json::parse(R"({
            "extracted_entities": [
                {"name": "Alice", "entity_type_id": 0},
                {"name": "Acme Corp", "entity_type_id": 1},
                {"name": "Rocky Mountains", "entity_type_id": 2}
            ]
        })");

        auto result = j.get<ExtractedEntities>();
        REQUIRE(result.extracted_entities.size() == 3);
        CHECK(result.extracted_entities[0].name == "Alice");
        CHECK(result.extracted_entities[0].entity_type_id == 0);
        CHECK(result.extracted_entities[1].name == "Acme Corp");
        CHECK(result.extracted_entities[1].entity_type_id == 1);
        CHECK(result.extracted_entities[2].name == "Rocky Mountains");
    }

    SECTION("empty entities list") {
        auto j = json::parse(R"({"extracted_entities": []})");
        auto result = j.get<ExtractedEntities>();
        CHECK(result.extracted_entities.empty());
    }

    SECTION("round-trip") {
        ExtractedEntities original{
            .extracted_entities = {{"Alice", 0}, {"Bob", 1}}};
        auto j = json(original);
        auto parsed = j.get<ExtractedEntities>();
        CHECK(parsed.extracted_entities.size() == 2);
        CHECK(parsed.extracted_entities[0].name == "Alice");
        CHECK(parsed.extracted_entities[1].name == "Bob");
    }
}

// ============================================================================
// ExtractedEdges
// ============================================================================

TEST_CASE("ExtractedEdges parsing", "[response_models][edges]") {
    SECTION("parse with all fields") {
        auto j = json::parse(R"({
            "edges": [
                {
                    "source_entity_name": "Alice",
                    "target_entity_name": "Acme Corp",
                    "relation_type": "WORKS_AT",
                    "fact": "Alice works at Acme Corp as a software engineer",
                    "valid_at": "2024-01-15T00:00:00Z",
                    "invalid_at": null
                }
            ]
        })");

        auto result = j.get<ExtractedEdges>();
        REQUIRE(result.edges.size() == 1);
        auto& e = result.edges[0];
        CHECK(e.source_entity_name == "Alice");
        CHECK(e.target_entity_name == "Acme Corp");
        CHECK(e.relation_type == "WORKS_AT");
        CHECK(e.fact == "Alice works at Acme Corp as a software engineer");
        CHECK(e.valid_at.has_value());
        CHECK(e.valid_at.value() == "2024-01-15T00:00:00Z");
        CHECK(!e.invalid_at.has_value());
    }

    SECTION("parse with optional fields missing") {
        auto j = json::parse(R"({
            "edges": [{
                "source_entity_name": "A",
                "target_entity_name": "B",
                "relation_type": "KNOWS",
                "fact": "A knows B"
            }]
        })");

        auto result = j.get<ExtractedEdges>();
        REQUIRE(result.edges.size() == 1);
        CHECK(!result.edges[0].valid_at.has_value());
        CHECK(!result.edges[0].invalid_at.has_value());
    }

    SECTION("parse multiple edges") {
        auto j = json::parse(R"({
            "edges": [
                {"source_entity_name": "A", "target_entity_name": "B", "relation_type": "KNOWS", "fact": "A knows B"},
                {"source_entity_name": "B", "target_entity_name": "C", "relation_type": "LIKES", "fact": "B likes C"}
            ]
        })");

        auto result = j.get<ExtractedEdges>();
        CHECK(result.edges.size() == 2);
    }

    SECTION("round-trip preserves null timestamps") {
        ExtractedEdge edge{
            .source_entity_name = "X",
            .target_entity_name = "Y",
            .relation_type = "REL",
            .fact = "X relates to Y",
            .valid_at = "2024-06-01T12:00:00Z",
            .invalid_at = std::nullopt,
        };
        auto j = json(ExtractedEdges{.edges = {edge}});
        auto parsed = j.get<ExtractedEdges>();
        CHECK(parsed.edges[0].valid_at.value() == "2024-06-01T12:00:00Z");
        CHECK(!parsed.edges[0].invalid_at.has_value());
    }
}

// ============================================================================
// NodeResolutions
// ============================================================================

TEST_CASE("NodeResolutions parsing", "[response_models][dedupe]") {
    SECTION("parse with duplicates found") {
        auto j = json::parse(R"({
            "entity_resolutions": [
                {"id": 0, "name": "Alice Johnson", "duplicate_name": "Alice"},
                {"id": 1, "name": "Acme Corporation", "duplicate_name": ""},
                {"id": 2, "name": "Bob Smith", "duplicate_name": "Bob"}
            ]
        })");

        auto result = j.get<NodeResolutions>();
        REQUIRE(result.entity_resolutions.size() == 3);
        CHECK(result.entity_resolutions[0].id == 0);
        CHECK(result.entity_resolutions[0].name == "Alice Johnson");
        CHECK(result.entity_resolutions[0].duplicate_name == "Alice");
        CHECK(result.entity_resolutions[1].duplicate_name.empty());
    }

    SECTION("round-trip") {
        NodeResolutions original{
            .entity_resolutions = {{0, "A", ""}, {1, "B", "B_existing"}}};
        auto j = json(original);
        auto parsed = j.get<NodeResolutions>();
        CHECK(parsed.entity_resolutions.size() == 2);
        CHECK(parsed.entity_resolutions[1].duplicate_name == "B_existing");
    }
}

// ============================================================================
// EdgeDuplicate
// ============================================================================

TEST_CASE("EdgeDuplicate parsing", "[response_models][dedupe]") {
    SECTION("parse with duplicates and contradictions") {
        auto j = json::parse(R"({
            "duplicate_facts": [3, 5],
            "contradicted_facts": [1, 7]
        })");

        auto result = j.get<EdgeDuplicate>();
        CHECK(result.duplicate_facts == std::vector<int>{3, 5});
        CHECK(result.contradicted_facts == std::vector<int>{1, 7});
    }

    SECTION("parse with empty lists") {
        auto j = json::parse(R"({
            "duplicate_facts": [],
            "contradicted_facts": []
        })");

        auto result = j.get<EdgeDuplicate>();
        CHECK(result.duplicate_facts.empty());
        CHECK(result.contradicted_facts.empty());
    }
}

// ============================================================================
// EntitySummary
// ============================================================================

TEST_CASE("EntitySummary parsing", "[response_models][summary]") {
    auto j = json::parse(R"({"summary": "Alice is a software engineer at Acme Corp"})");
    auto result = j.get<EntitySummary>();
    CHECK(result.summary == "Alice is a software engineer at Acme Corp");
}

// ============================================================================
// SummarizedEntities
// ============================================================================

TEST_CASE("SummarizedEntities parsing", "[response_models][summary]") {
    SECTION("parse with multiple summaries") {
        auto j = json::parse(R"({
            "summaries": [
                {"name": "Alice", "summary": "Software engineer at Acme"},
                {"name": "Bob", "summary": "Product manager at Acme"}
            ]
        })");

        auto result = j.get<SummarizedEntities>();
        REQUIRE(result.summaries.size() == 2);
        CHECK(result.summaries[0].name == "Alice");
        CHECK(result.summaries[0].summary == "Software engineer at Acme");
        CHECK(result.summaries[1].name == "Bob");
    }
}

// ============================================================================
// Summary + SummaryDescription
// ============================================================================

TEST_CASE("Summary parsing", "[response_models][summary]") {
    auto j = json::parse(R"({"summary": "A community of tech workers"})");
    auto result = j.get<Summary>();
    CHECK(result.summary == "A community of tech workers");
}

TEST_CASE("SummaryDescription parsing", "[response_models][summary]") {
    auto j = json::parse(R"({"description": "Group of engineers working on AI projects"})");
    auto result = j.get<SummaryDescription>();
    CHECK(result.description == "Group of engineers working on AI projects");
}

// ============================================================================
// JSON Schema Validation
// ============================================================================

TEST_CASE("Few-shot examples contain valid JSON objects", "[response_models][schema]") {
    // Extract and verify JSON objects embedded in the few-shot example strings.
    // Each example block contains "Example N:" lines followed by JSON.
    auto extract_json_lines = [](std::string_view examples) -> std::vector<nlohmann::json> {
        std::vector<nlohmann::json> results;
        std::string line;
        for (size_t i = 0; i < examples.size(); ++i) {
            if (examples[i] == '\n' || i == examples.size() - 1) {
                if (i == examples.size() - 1 && examples[i] != '\n') line += examples[i];
                if (!line.empty() && line[0] == '{') {
                    results.push_back(json::parse(line));
                }
                line.clear();
            } else {
                line += examples[i];
            }
        }
        return results;
    };

    auto entities = extract_json_lines(response_schemas::EXTRACTED_ENTITIES);
    CHECK(entities.size() == 2);
    for (auto& j : entities) CHECK(j.contains("extracted_entities"));

    auto edges = extract_json_lines(response_schemas::EXTRACTED_EDGES);
    CHECK(edges.size() == 2);
    for (auto& j : edges) CHECK(j.contains("edges"));

    auto resolutions = extract_json_lines(response_schemas::NODE_RESOLUTIONS);
    CHECK(resolutions.size() == 2);
    for (auto& j : resolutions) CHECK(j.contains("entity_resolutions"));

    auto dupes = extract_json_lines(response_schemas::EDGE_DUPLICATE);
    CHECK(dupes.size() == 2);
    for (auto& j : dupes) {
        CHECK(j.contains("duplicate_facts"));
        CHECK(j.contains("contradicted_facts"));
    }

    auto summaries_single = extract_json_lines(response_schemas::ENTITY_SUMMARY);
    CHECK(summaries_single.size() == 2);
    for (auto& j : summaries_single) CHECK(j.contains("summary"));

    auto summaries_batch = extract_json_lines(response_schemas::SUMMARIZED_ENTITIES);
    CHECK(summaries_batch.size() == 2);
    for (auto& j : summaries_batch) CHECK(j.contains("summaries"));

    auto summary = extract_json_lines(response_schemas::SUMMARY);
    CHECK(summary.size() == 2);
    for (auto& j : summary) CHECK(j.contains("summary"));

    auto desc = extract_json_lines(response_schemas::SUMMARY_DESCRIPTION);
    CHECK(desc.size() == 2);
    for (auto& j : desc) CHECK(j.contains("description"));
}

// ============================================================================
// Malformed JSON handling
// ============================================================================

TEST_CASE("Response models handle missing required fields", "[response_models][error]") {
    SECTION("ExtractedEntity missing name") {
        auto j = json::parse(R"({"entity_type_id": 0})");
        CHECK_THROWS(j.get<ExtractedEntity>());
    }

    SECTION("ExtractedEdge missing fact defaults to empty string") {
        auto j = json::parse(R"({
            "source_entity_name": "A",
            "target_entity_name": "B",
            "relation_type": "KNOWS"
        })");
        // fact is optional — missing fact → empty string, not an exception
        auto edge = j.get<ExtractedEdge>();
        CHECK(edge.fact.empty());
        CHECK(edge.source_entity_name == "A");
        CHECK(edge.target_entity_name == "B");
    }

    SECTION("EdgeDuplicate missing contradicted_facts") {
        auto j = json::parse(R"({"duplicate_facts": [1]})");
        CHECK_THROWS(j.get<EdgeDuplicate>());
    }
}
