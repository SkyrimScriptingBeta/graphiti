#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>

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

TEST_CASE("Graphiti: full add_episode + search", "[integration][e2e]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    // Ingest a conversation
    auto result = g.add_episode(
        "conversation_1",
        "Alice: I just started working at Acme Corp as a software engineer.",
        "chat conversation",
        std::chrono::system_clock::now(),
        EpisodeType::message,
        "test_group"
    );

    REQUIRE(result.has_value());
    auto& ep = result.value();

    // Should have extracted at least Alice and Acme Corp
    CHECK(!ep.nodes.empty());
    CHECK(!ep.edges.empty());
    CHECK(!ep.episode.uuid.empty());

    INFO("Extracted " << ep.nodes.size() << " nodes and " << ep.edges.size() << " edges");

    bool found_alice = false;
    bool found_acme = false;
    for (auto& node : ep.nodes) {
        if (node.name.find("Alice") != std::string::npos) found_alice = true;
        if (node.name.find("Acme") != std::string::npos) found_acme = true;
    }
    CHECK(found_alice);
    CHECK(found_acme);

    // Search for relevant facts
    auto search_result = g.search("Where does Alice work?", "test_group");
    REQUIRE(search_result.has_value());

    INFO("Search returned " << search_result.value().size() << " edges");
    CHECK(!search_result.value().empty());

    // The top result should mention Alice and Acme
    if (!search_result.value().empty()) {
        auto& top = search_result.value()[0];
        bool mentions_relevant = top.fact.find("Alice") != std::string::npos
                              || top.fact.find("Acme") != std::string::npos
                              || top.fact.find("work") != std::string::npos;
        CHECK(mentions_relevant);
    }
}

TEST_CASE("Graphiti: multi-episode ingestion", "[integration][e2e]") {
    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto now = std::chrono::system_clock::now();

    // Episode 1: Alice at Acme
    auto r1 = g.add_episode(
        "ep1", "Alice: I work at Acme Corp in Denver.",
        "chat", now, EpisodeType::message, "test"
    );
    REQUIRE(r1.has_value());

    // Episode 2: Bob at Acme
    auto r2 = g.add_episode(
        "ep2", "Bob: I also work at Acme Corp. Alice and I are on the same team.",
        "chat", now + std::chrono::seconds(60), EpisodeType::message, "test"
    );
    REQUIRE(r2.has_value());

    // Search should find facts about Acme
    auto results = g.search("Who works at Acme Corp?", "test");
    REQUIRE(results.has_value());
    CHECK(!results.value().empty());
}
