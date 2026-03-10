#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "prompts/prompts.h"

using namespace graphiti;
using namespace graphiti::prompts;
using json = nlohmann::json;

// ============================================================================
// Helper
// ============================================================================

static void check_roles(const std::vector<Message>& msgs) {
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0].role == "system");
    CHECK(msgs[1].role == "user");
}

static void check_unicode_instruction(const std::vector<Message>& msgs) {
    CHECK(msgs[0].content.find("Do not escape unicode") != std::string::npos);
}

// ============================================================================
// extract_message
// ============================================================================

TEST_CASE("extract_message prompt structure", "[prompts][extract]") {
    auto msgs = extract_message(
        R"([{"id": 0, "name": "Person", "description": "A human"}])",
        json::array({json{{"content", "Hello"}}}),
        "Alice: I work at Acme Corp"
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("extracts entity nodes") != std::string::npos);
    CHECK(msgs[1].content.find("<ENTITY TYPES>") != std::string::npos);
    CHECK(msgs[1].content.find("<PREVIOUS MESSAGES>") != std::string::npos);
    CHECK(msgs[1].content.find("<CURRENT MESSAGE>") != std::string::npos);
    CHECK(msgs[1].content.find("Alice: I work at Acme Corp") != std::string::npos);
    CHECK(msgs[1].content.find("Speaker Extraction") != std::string::npos);
    CHECK(msgs[1].content.find("entity_type_id") != std::string::npos);
}

TEST_CASE("extract_message with custom instructions", "[prompts][extract]") {
    auto msgs = extract_message(
        "[]", json::array(), "Test message", "Focus on locations only."
    );
    CHECK(msgs[1].content.find("Focus on locations only.") != std::string::npos);
}

// ============================================================================
// extract_text
// ============================================================================

TEST_CASE("extract_text prompt structure", "[prompts][extract]") {
    auto msgs = extract_text(
        R"([{"id": 0, "name": "Person"}])",
        "Alice works at Acme Corp as an engineer."
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("extracts entity nodes from text") != std::string::npos);
    CHECK(msgs[1].content.find("<TEXT>") != std::string::npos);
    CHECK(msgs[1].content.find("Alice works at Acme Corp") != std::string::npos);
    CHECK(msgs[1].content.find("Avoid creating nodes for relationships") != std::string::npos);
}

// ============================================================================
// extract_json
// ============================================================================

TEST_CASE("extract_json prompt structure", "[prompts][extract]") {
    auto msgs = extract_json(
        R"([{"id": 0, "name": "Event"}])",
        "Spotify play history",
        R"({"user": "alice", "track": "Blue Monday", "artist": "New Order"})"
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("extracts entity nodes from JSON") != std::string::npos);
    CHECK(msgs[1].content.find("<SOURCE DESCRIPTION>") != std::string::npos);
    CHECK(msgs[1].content.find("Spotify play history") != std::string::npos);
    CHECK(msgs[1].content.find("<JSON>") != std::string::npos);
    CHECK(msgs[1].content.find("Blue Monday") != std::string::npos);
    CHECK(msgs[1].content.find("Do NOT extract any properties that contain dates") != std::string::npos);
}

TEST_CASE("extract_json with custom instructions", "[prompts][extract]") {
    auto msgs = extract_json(
        "[]", "API response", "{}", "Focus on user entities only."
    );
    CHECK(msgs[1].content.find("Focus on user entities only.") != std::string::npos);
    CHECK(msgs[1].content.find("<SOURCE DESCRIPTION>") != std::string::npos);
    CHECK(msgs[1].content.find("API response") != std::string::npos);
}

// ============================================================================
// extract_edges
// ============================================================================

TEST_CASE("extract_edges prompt structure", "[prompts][edges]") {
    auto nodes = json::array({
        json{{"name", "Alice"}},
        json{{"name", "Acme Corp"}},
    });

    auto msgs = extract_edges(
        json::array(), "Alice works at Acme Corp", nodes, "2024-06-01T00:00:00Z"
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("fact extractor") != std::string::npos);
    CHECK(msgs[1].content.find("<ENTITIES>") != std::string::npos);
    CHECK(msgs[1].content.find("<REFERENCE_TIME>") != std::string::npos);
    CHECK(msgs[1].content.find("2024-06-01T00:00:00Z") != std::string::npos);
    CHECK(msgs[1].content.find("SCREAMING_SNAKE_CASE") != std::string::npos);
    CHECK(msgs[1].content.find("ISO 8601") != std::string::npos);
}

TEST_CASE("extract_edges with edge types", "[prompts][edges]") {
    auto edge_types = json::array({json{{"name", "WORKS_AT"}}});

    auto msgs = extract_edges(
        json::array(), "test", json::array(), "2024-01-01T00:00:00Z", edge_types
    );

    CHECK(msgs[1].content.find("<FACT_TYPES>") != std::string::npos);
    CHECK(msgs[1].content.find("WORKS_AT") != std::string::npos);
}

TEST_CASE("extract_edges without edge types", "[prompts][edges]") {
    auto msgs = extract_edges(
        json::array(), "test", json::array(), "2024-01-01T00:00:00Z"
    );

    CHECK(msgs[1].content.find("<FACT_TYPES>") == std::string::npos);
}

// ============================================================================
// dedupe_node
// ============================================================================

TEST_CASE("dedupe_node prompt structure", "[prompts][dedupe]") {
    auto msgs = dedupe_node(
        json::array(),
        "Alice mentioned Bob",
        json{{"id", 0}, {"name", "Bob"}},
        "A person",
        json::array({json{{"name", "Robert Smith"}, {"summary", "Known as Bob"}}})
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("determines whether") != std::string::npos);
    CHECK(msgs[1].content.find("<NEW ENTITY>") != std::string::npos);
    CHECK(msgs[1].content.find("<EXISTING ENTITIES>") != std::string::npos);
    CHECK(msgs[1].content.find("entity_resolutions") != std::string::npos);
    CHECK(msgs[1].content.find("duplicate_name") != std::string::npos);
}

// ============================================================================
// dedupe_nodes
// ============================================================================

TEST_CASE("dedupe_nodes prompt structure", "[prompts][dedupe]") {
    auto extracted = json::array({
        json{{"id", 0}, {"name", "Alice"}},
        json{{"id", 1}, {"name", "Acme"}},
    });

    auto msgs = dedupe_nodes(
        json::array(),
        "Alice at Acme",
        extracted,
        json::array({json{{"name", "Alice Johnson"}}})
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[1].content.find("<ENTITIES>") != std::string::npos);
    CHECK(msgs[1].content.find("<EXISTING ENTITIES>") != std::string::npos);
    // Should inject the count (2) and max index (1)
    CHECK(msgs[1].content.find("2 entities") != std::string::npos);
    CHECK(msgs[1].content.find("IDs 0 through 1") != std::string::npos);
}

// ============================================================================
// resolve_edge
// ============================================================================

TEST_CASE("resolve_edge prompt structure", "[prompts][dedupe]") {
    auto msgs = resolve_edge(
        "idx 0: Alice works at Acme Corp",
        "idx 1: Alice left Acme Corp",
        "Alice joined Google"
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("de-duplicates facts") != std::string::npos);
    CHECK(msgs[1].content.find("<EXISTING FACTS>") != std::string::npos);
    CHECK(msgs[1].content.find("<FACT INVALIDATION CANDIDATES>") != std::string::npos);
    CHECK(msgs[1].content.find("<NEW FACT>") != std::string::npos);
    CHECK(msgs[1].content.find("duplicate_facts") != std::string::npos);
    CHECK(msgs[1].content.find("contradicted_facts") != std::string::npos);
}

// ============================================================================
// extract_summary
// ============================================================================

TEST_CASE("extract_summary prompt structure", "[prompts][summary]") {
    auto msgs = extract_summary(
        json::array(),
        "Alice is a software engineer at Acme",
        json{{"name", "Alice"}, {"summary", ""}}
    );

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("extracts entity summaries") != std::string::npos);
    CHECK(msgs[1].content.find("<MESSAGES>") != std::string::npos);
    CHECK(msgs[1].content.find("<ENTITY>") != std::string::npos);
    CHECK(msgs[1].content.find("500") != std::string::npos); // MAX_SUMMARY_CHARS
    CHECK(msgs[1].content.find("STATE FACTS DIRECTLY") != std::string::npos);
}

// ============================================================================
// extract_summaries_batch
// ============================================================================

TEST_CASE("extract_summaries_batch prompt structure", "[prompts][summary]") {
    auto entities = json::array({
        json{{"name", "Alice"}, {"summary", ""}},
        json{{"name", "Acme Corp"}, {"summary", "A company"}},
    });

    auto msgs = extract_summaries_batch(json::array(), "Alice works at Acme", entities);

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("generates concise entity summaries") != std::string::npos);
    CHECK(msgs[1].content.find("<ENTITIES>") != std::string::npos);
    CHECK(msgs[1].content.find("skip it") != std::string::npos);
}

// ============================================================================
// summary_description
// ============================================================================

TEST_CASE("summary_description prompt structure", "[prompts][summary]") {
    auto msgs = summary_description("Alice is a software engineer specializing in AI");

    check_roles(msgs);
    check_unicode_instruction(msgs);

    CHECK(msgs[0].content.find("describes provided contents") != std::string::npos);
    CHECK(msgs[1].content.find("one sentence description") != std::string::npos);
    CHECK(msgs[1].content.find("250 characters") != std::string::npos);
    CHECK(msgs[1].content.find("Alice is a software engineer") != std::string::npos);
}
