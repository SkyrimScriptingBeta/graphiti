#include <catch2/catch_all.hpp>

#include <graphiti/token_tracker.h>

#include <thread>

using namespace graphiti;

TEST_CASE("TokenTracker: initial state is empty", "[token_tracker]") {
    TokenTracker tracker;
    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == 0);
    CHECK(total.output_tokens == 0);
    CHECK(total.total_tokens() == 0);
    CHECK(tracker.get_total_calls() == 0);
    CHECK(tracker.get_usage().empty());
}

TEST_CASE("TokenTracker: single record", "[token_tracker]") {
    TokenTracker tracker;
    tracker.record("extract_nodes", 100, 50);

    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == 100);
    CHECK(total.output_tokens == 50);
    CHECK(total.total_tokens() == 150);
    CHECK(tracker.get_total_calls() == 1);

    auto usage = tracker.get_usage();
    REQUIRE(usage.count("extract_nodes") == 1);
    auto& entry = usage.at("extract_nodes");
    CHECK(entry.prompt_name == "extract_nodes");
    CHECK(entry.call_count == 1);
    CHECK(entry.total_input_tokens == 100);
    CHECK(entry.total_output_tokens == 50);
    CHECK(entry.total_tokens() == 150);
    CHECK(entry.avg_input_tokens() == Catch::Approx(100.0));
    CHECK(entry.avg_output_tokens() == Catch::Approx(50.0));
}

TEST_CASE("TokenTracker: multiple records same prompt", "[token_tracker]") {
    TokenTracker tracker;
    tracker.record("dedupe", 200, 80);
    tracker.record("dedupe", 300, 120);

    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == 500);
    CHECK(total.output_tokens == 200);
    CHECK(total.total_tokens() == 700);
    CHECK(tracker.get_total_calls() == 2);

    auto usage = tracker.get_usage();
    REQUIRE(usage.count("dedupe") == 1);
    auto& entry = usage.at("dedupe");
    CHECK(entry.call_count == 2);
    CHECK(entry.total_input_tokens == 500);
    CHECK(entry.total_output_tokens == 200);
    CHECK(entry.avg_input_tokens() == Catch::Approx(250.0));
    CHECK(entry.avg_output_tokens() == Catch::Approx(100.0));
}

TEST_CASE("TokenTracker: multiple prompts", "[token_tracker]") {
    TokenTracker tracker;
    tracker.record("extract_nodes", 100, 50);
    tracker.record("extract_edges", 200, 80);
    tracker.record("dedupe_nodes", 150, 60);

    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == 450);
    CHECK(total.output_tokens == 190);
    CHECK(total.total_tokens() == 640);
    CHECK(tracker.get_total_calls() == 3);

    auto usage = tracker.get_usage();
    CHECK(usage.size() == 3);
    CHECK(usage.count("extract_nodes") == 1);
    CHECK(usage.count("extract_edges") == 1);
    CHECK(usage.count("dedupe_nodes") == 1);
}

TEST_CASE("TokenTracker: reset clears all data", "[token_tracker]") {
    TokenTracker tracker;
    tracker.record("prompt_a", 100, 50);
    tracker.record("prompt_b", 200, 80);

    tracker.reset();

    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == 0);
    CHECK(total.output_tokens == 0);
    CHECK(tracker.get_total_calls() == 0);
    CHECK(tracker.get_usage().empty());
}

TEST_CASE("TokenTracker: zero-call averages are zero", "[token_tracker]") {
    PromptTokenUsage entry;
    entry.call_count = 0;
    entry.total_input_tokens = 0;
    entry.total_output_tokens = 0;
    CHECK(entry.avg_input_tokens() == Catch::Approx(0.0));
    CHECK(entry.avg_output_tokens() == Catch::Approx(0.0));
}

TEST_CASE("TokenUsage: total_tokens", "[token_tracker]") {
    TokenUsage usage;
    usage.input_tokens = 1000;
    usage.output_tokens = 500;
    CHECK(usage.total_tokens() == 1500);
}

TEST_CASE("TokenTracker: concurrent writes are safe", "[token_tracker]") {
    TokenTracker tracker;
    constexpr int N = 100;

    auto writer = [&](const std::string& name) {
        for (int i = 0; i < N; ++i) {
            tracker.record(name, 10, 5);
        }
    };

    std::thread t1(writer, "prompt_a");
    std::thread t2(writer, "prompt_b");
    std::thread t3(writer, "prompt_a");
    t1.join();
    t2.join();
    t3.join();

    auto total = tracker.get_total_usage();
    CHECK(total.input_tokens == N * 3 * 10);
    CHECK(total.output_tokens == N * 3 * 5);
    CHECK(tracker.get_total_calls() == N * 3);

    auto usage = tracker.get_usage();
    CHECK(usage.at("prompt_a").call_count == N * 2);
    CHECK(usage.at("prompt_b").call_count == N);
}
