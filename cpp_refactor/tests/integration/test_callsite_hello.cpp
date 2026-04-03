#include <catch2/catch_all.hpp>

#include <graphiti/callsite_log.h>
#include <graphiti/config.h>
#include <graphiti/graphiti.h>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace graphiti;

static GraphitiConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    auto config = GraphitiConfig::from_env();
    config.db_path = ":memory:";
    return config;
}

TEST_CASE("Hello World: single add_episode callsite trace", "[callsite][hello]") {
    // Set up callsite logging to a temp directory
    auto tmp = std::filesystem::temp_directory_path() / "graphiti_callsites";
    callsite_log_set_dir(tmp.string());
    callsite_log_open("hello_world_add_episode");

    auto config = make_config();
    Graphiti g(std::move(config));

    auto indices = g.build_indices();
    REQUIRE(indices.has_value());

    auto result = g.add_episode({
        .name = "hello",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "hello_test",
    });

    REQUIRE(result.has_value());
    CHECK(!result.value().nodes.empty());
    CHECK(!result.value().edges.empty());

    callsite_log_close();

    // Read back the callsite log and print it
    auto log_path = tmp / "hello_world_add_episode.callsites";
    REQUIRE(std::filesystem::exists(log_path));

    auto size = std::filesystem::file_size(log_path);
    INFO("Callsite log: " << log_path.string() << " (" << size << " bytes)");
    CHECK(size > 0);
}
