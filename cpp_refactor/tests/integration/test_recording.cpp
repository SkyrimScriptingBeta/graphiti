#include <catch2/catch_all.hpp>

#include <graphiti/config.h>
#include <graphiti/graphiti.h>
#include <graphiti/recording_embedder.h>
#include <graphiti/recording_llm_client.h>

#include "embedder/openai_embedder.h"
#include "llm/openai_client.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace graphiti;

static auto recording_dir() {
    static auto dir = std::filesystem::temp_directory_path() / "graphiti_recordings";
    return dir;
}

TEST_CASE("Recording: record and replay hello world add_episode", "[recording]") {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }

    auto full_config = GraphitiConfig::from_env();

    auto llm_path = recording_dir() / "hello_llm.json";
    auto embed_path = recording_dir() / "hello_embedder.json";

    // ========================================================================
    // Phase 1: RECORD — run with real OpenAI, capture everything
    // ========================================================================
    {
        auto config = full_config;
        config.db_path = ":memory:";

        auto real_llm = std::make_unique<OpenAIClient>(config.llm);
        auto real_embedder = std::make_unique<OpenAIEmbedder>(config.embedder);

        auto recording_llm = std::make_unique<RecordingLLMClient>(std::move(real_llm), llm_path);
        auto recording_embedder = std::make_unique<RecordingEmbedder>(std::move(real_embedder), embed_path);

        Graphiti g(std::move(config), std::move(recording_llm), std::move(recording_embedder));
        REQUIRE(g.build_indices().has_value());

        auto result = g.add_episode({
            .name = "record_hello",
            .body = "Alice works at Acme Corp.",
            .source_description = "test",
            .reference_time = std::chrono::system_clock::now(),
            .group_id = "recording_test",
        });
        REQUIRE(result.has_value());
        CHECK(!result.value().nodes.empty());
        CHECK(!result.value().edges.empty());

        INFO("Recorded " << result.value().nodes.size() << " nodes, "
             << result.value().edges.size() << " edges");
    }
    // RecordingLLMClient and RecordingEmbedder flush on destruction

    // Verify recordings exist
    REQUIRE(std::filesystem::exists(llm_path));
    REQUIRE(std::filesystem::exists(embed_path));
    REQUIRE(std::filesystem::file_size(llm_path) > 0);
    REQUIRE(std::filesystem::file_size(embed_path) > 0);

    INFO("LLM recording: " << std::filesystem::file_size(llm_path) << " bytes");
    INFO("Embedder recording: " << std::filesystem::file_size(embed_path) << " bytes");

    // ========================================================================
    // Phase 2: REPLAY — run with recorded responses, no network
    // ========================================================================
    {
        auto config = full_config;
        config.db_path = ":memory:";

        auto replay_llm = std::make_unique<ReplayLLMClient>(llm_path);
        auto replay_embedder = std::make_unique<ReplayEmbedder>(embed_path);

        auto* replay_llm_ptr = replay_llm.get();
        auto* replay_embedder_ptr = replay_embedder.get();

        Graphiti g(std::move(config), std::move(replay_llm), std::move(replay_embedder));
        REQUIRE(g.build_indices().has_value());

        auto result = g.add_episode({
            .name = "record_hello",
            .body = "Alice works at Acme Corp.",
            .source_description = "test",
            .reference_time = std::chrono::system_clock::now(),
            .group_id = "recording_test",
        });
        REQUIRE(result.has_value());
        CHECK(!result.value().nodes.empty());
        CHECK(!result.value().edges.empty());

        // All recordings should have been consumed
        CHECK(replay_llm_ptr->calls_replayed() == replay_llm_ptr->total_recordings());
        CHECK(replay_embedder_ptr->calls_replayed() == replay_embedder_ptr->total_recordings());

        INFO("Replay used " << replay_llm_ptr->calls_replayed() << " LLM calls, "
             << replay_embedder_ptr->calls_replayed() << " embedder calls");
    }
}
