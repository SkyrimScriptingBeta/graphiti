#include <catch2/catch_all.hpp>

#include <graphiti/callsite_log.h>
#include <graphiti/config.h>
#include <graphiti/graphiti.h>
#include <graphiti/recording_embedder.h>
#include <graphiti/recording_llm_client.h>
#include <graphiti/search_config.h>

#include "embedder/openai_embedder.h"
#include "llm/openai_client.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace graphiti;

static auto recording_dir() {
    static auto dir = std::filesystem::temp_directory_path() / "graphiti_recordings";
    return dir;
}

// Helper: create a recording Graphiti instance
struct RecordingGraphiti {
    std::unique_ptr<Graphiti> g;
    RecordingLLMClient* llm_recorder;
    RecordingEmbedder* embed_recorder;

    static RecordingGraphiti create(const std::string& name, GraphitiConfig config = {}) {
        auto* key = std::getenv("OPENAI_API_KEY");
        if (!key || std::string(key).empty()) {
            SKIP("OPENAI_API_KEY not set");
        }

        if (config.llm.api_key.empty()) {
            config = GraphitiConfig::from_env();
        }
        config.db_path = ":memory:";

        auto real_llm = std::make_unique<OpenAIClient>(config.llm);
        auto real_embed = std::make_unique<OpenAIEmbedder>(config.embedder);

        auto rec_llm = std::make_unique<RecordingLLMClient>(
            std::move(real_llm), recording_dir() / (name + "_llm.json"));
        auto rec_embed = std::make_unique<RecordingEmbedder>(
            std::move(real_embed), recording_dir() / (name + "_embedder.json"));

        auto* llm_ptr = rec_llm.get();
        auto* embed_ptr = rec_embed.get();

        auto g = std::make_unique<Graphiti>(
            std::move(config), std::move(rec_llm), std::move(rec_embed));

        return {std::move(g), llm_ptr, embed_ptr};
    }
};

// ============================================================================
// Record: simple add_episode
// ============================================================================
TEST_CASE("Record: add_episode hello world", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("add_episode_hello");
    REQUIRE(g->build_indices().has_value());

    auto r = g->add_episode({
        .name = "hello",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_test",
    });
    REQUIRE(r.has_value());
    CHECK(!r.value().nodes.empty());
}

// ============================================================================
// Record: multi-episode with dedup
// ============================================================================
TEST_CASE("Record: multi-episode dedup", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("multi_episode_dedup");
    REQUIRE(g->build_indices().has_value());
    auto now = std::chrono::system_clock::now();

    auto r1 = g->add_episode({
        .name = "ep1",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "rec_dedup",
    });
    REQUIRE(r1.has_value());
    REQUIRE(g->build_indices().has_value());

    auto r2 = g->add_episode({
        .name = "ep2",
        .body = "Bob also works at Acme Corp with Alice.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(1),
        .group_id = "rec_dedup",
    });
    REQUIRE(r2.has_value());
}

// ============================================================================
// Record: add_episode + search
// ============================================================================
TEST_CASE("Record: add_episode + search", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("add_episode_search");
    REQUIRE(g->build_indices().has_value());

    auto r = g->add_episode({
        .name = "search_ep",
        .body = "Alice works at Acme Corp as a software engineer in Denver.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_search",
    });
    REQUIRE(r.has_value());
    REQUIRE(g->build_indices().has_value());

    auto sr = g->search({.query = "Where does Alice work?", .group_id = "rec_search"});
    REQUIRE(sr.has_value());
    CHECK(!sr.value().empty());
}

// ============================================================================
// Record: contradiction (edge invalidation)
// ============================================================================
TEST_CASE("Record: contradiction", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("contradiction");
    REQUIRE(g->build_indices().has_value());
    auto now = std::chrono::system_clock::now();

    auto r1 = g->add_episode({
        .name = "fact",
        .body = "Alice works at Acme Corp as a software engineer.",
        .source_description = "test",
        .reference_time = now,
        .group_id = "rec_contra",
    });
    REQUIRE(r1.has_value());
    REQUIRE(g->build_indices().has_value());

    auto r2 = g->add_episode({
        .name = "update",
        .body = "Alice left Acme Corp and joined Google as a senior engineer.",
        .source_description = "test",
        .reference_time = now + std::chrono::hours(24),
        .group_id = "rec_contra",
    });
    REQUIRE(r2.has_value());
}

// ============================================================================
// Record: remove_episode
// ============================================================================
TEST_CASE("Record: remove_episode", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("remove_episode");
    REQUIRE(g->build_indices().has_value());

    auto r = g->add_episode({
        .name = "to_remove",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_remove",
    });
    REQUIRE(r.has_value());

    auto del = g->remove_episode(r.value().episode.uuid);
    REQUIRE(del.has_value());
}

// ============================================================================
// Record: add_triplet
// ============================================================================
TEST_CASE("Record: add_triplet", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("add_triplet");
    REQUIRE(g->build_indices().has_value());

    EntityNode source;
    source.name = "Alice";
    source.group_id = "rec_triplet";
    source.created_at = std::chrono::system_clock::now();

    EntityNode target;
    target.name = "Acme Corp";
    target.group_id = "rec_triplet";
    target.created_at = std::chrono::system_clock::now();

    EntityEdge edge;
    edge.name = "WORKS_AT";
    edge.fact = "Alice works at Acme Corp";
    edge.group_id = "rec_triplet";
    edge.created_at = std::chrono::system_clock::now();

    auto r = g->add_triplet(source, edge, target);
    REQUIRE(r.has_value());
    CHECK(r.value().nodes.size() == 2);
    CHECK(r.value().edges.size() == 1);
}

// ============================================================================
// Record: management APIs
// ============================================================================
TEST_CASE("Record: management APIs", "[record]") {
    auto [g, llm, embed] = RecordingGraphiti::create("management_apis");
    REQUIRE(g->build_indices().has_value());

    auto r = g->add_episode({
        .name = "mgmt",
        .body = "Alice works at Acme Corp.",
        .source_description = "test",
        .reference_time = std::chrono::system_clock::now(),
        .group_id = "rec_mgmt",
    });
    REQUIRE(r.has_value());
    REQUIRE(g->build_indices().has_value());

    auto overview = g->get_graph_overview("rec_mgmt");
    CHECK(overview.has_value());

    auto bm25 = g->search_entity_nodes_bm25("Alice", "rec_mgmt");
    CHECK(bm25.has_value());

    auto del = g->delete_group("rec_mgmt");
    CHECK(del.has_value());
}
