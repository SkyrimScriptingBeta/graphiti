#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>

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

TEST_CASE("token_tracker: tracks usage after add_episode", "[integration][token_tracking]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    // Before ingestion: no tokens used
    auto before = g.token_tracker().get_total_usage();
    CHECK(before.total_tokens() == 0);
    CHECK(g.token_tracker().get_total_calls() == 0);

    auto now = std::chrono::system_clock::now();
    auto r = g.add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp as an engineer.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r.has_value());

    // After ingestion: tokens should be recorded
    auto after = g.token_tracker().get_total_usage();
    CHECK(after.input_tokens > 0);
    CHECK(after.output_tokens > 0);
    CHECK(after.total_tokens() > 0);
    CHECK(g.token_tracker().get_total_calls() > 0);

    INFO("Total input tokens: " << after.input_tokens);
    INFO("Total output tokens: " << after.output_tokens);
    INFO("Total LLM calls: " << g.token_tracker().get_total_calls());

    // Should have usage broken down by prompt
    auto usage = g.token_tracker().get_usage();
    CHECK(!usage.empty());

    for (auto& [name, entry] : usage) {
        INFO("Prompt: " << name << " calls=" << entry.call_count
             << " in=" << entry.total_input_tokens
             << " out=" << entry.total_output_tokens);
        CHECK(entry.call_count > 0);
        CHECK(entry.total_input_tokens > 0);
    }
}

TEST_CASE("token_tracker: accumulates across multiple episodes", "[integration][token_tracking]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Bob is a teacher in Portland.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "test_group",
    });
    REQUIRE(r1.has_value());

    auto after_first = g.token_tracker().get_total_usage();
    auto calls_first = g.token_tracker().get_total_calls();

    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Carol is a doctor in Seattle.",
        .source_description = "chat",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "test_group",
    });
    REQUIRE(r2.has_value());

    auto after_second = g.token_tracker().get_total_usage();
    auto calls_second = g.token_tracker().get_total_calls();

    // Second episode should add more tokens
    CHECK(after_second.total_tokens() > after_first.total_tokens());
    CHECK(calls_second > calls_first);

    INFO("After ep1: " << after_first.total_tokens() << " tokens, " << calls_first << " calls");
    INFO("After ep2: " << after_second.total_tokens() << " tokens, " << calls_second << " calls");
}
