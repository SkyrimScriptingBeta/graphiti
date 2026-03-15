/*
 * ERROR RECOVERY AND BAD CONFIG TESTS
 *
 * What happens when things go wrong?
 * - Bad API key
 * - Bad model name
 * - Operations after errors
 * - Invalid DB paths
 * - Search filters with contradictions
 * - Temporal filter edge cases
 */

#include "shared.h"
#include "driver/kuzu_driver.h"

#include <chrono>
#include <format>

using namespace graphiti;

int main() {
    auto now = std::chrono::system_clock::now();

    // ====================================================================
    stress::separator("BAD API KEY");
    // ====================================================================
    {
        GraphitiConfig config;
        config.db_path = ":memory:";
        config.llm.api_key = "sk-invalid-key-that-doesnt-exist-at-all";
        config.embedder.api_key = config.llm.api_key;

        Graphiti g(std::move(config));
        auto idx = g.build_indices();
        stress::test("build_indices with bad key succeeds (no API needed)", idx.has_value());

        auto result = g.add_episode({
            .name = "bad-key-ep", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        // Should fail gracefully, not crash
        stress::test("add_episode with bad API key returns error", !result.has_value());
        if (!result.has_value()) {
            std::cout << std::format("    -> error code: {}, message: {}...\n",
                static_cast<int>(result.error().code),
                result.error().message.substr(0, 80));
        }
    }

    // ====================================================================
    stress::separator("BAD MODEL NAME");
    // ====================================================================
    {
        auto api_key = stress::require_api_key();
        GraphitiConfig config;
        config.db_path = ":memory:";
        config.llm.api_key = api_key;
        config.llm.model = "gpt-nonexistent-model-that-doesnt-exist";
        config.llm.small_model = "gpt-also-nonexistent";
        config.embedder.api_key = api_key;

        Graphiti g(std::move(config));
        g.build_indices();

        auto result = g.add_episode({
            .name = "bad-model", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        stress::test("add_episode with bad model returns error", !result.has_value());
        if (!result.has_value()) {
            std::cout << std::format("    -> error: {}...\n",
                result.error().message.substr(0, 100));
        }
    }

    // ====================================================================
    stress::separator("BAD EMBEDDER MODEL");
    // ====================================================================
    {
        auto api_key = stress::require_api_key();
        GraphitiConfig config;
        config.db_path = ":memory:";
        config.llm.api_key = api_key;
        config.embedder.api_key = api_key;
        config.embedder.model = "text-embedding-nonexistent";

        Graphiti g(std::move(config));
        g.build_indices();

        auto result = g.add_episode({
            .name = "bad-embedder", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        // LLM extraction might work, but embedding will fail
        // The pipeline should handle this gracefully
        stress::test("bad embedder model doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges (embedding may be skipped)\n",
                result.value().nodes.size(), result.value().edges.size());
        } else {
            std::cout << std::format("    -> error: {}...\n",
                result.error().message.substr(0, 100));
        }
    }

    // ====================================================================
    stress::separator("OPERATIONS AFTER ERROR (recovery)");
    // ====================================================================
    {
        auto api_key = stress::require_api_key();

        // Start with bad key, then swap to good key... actually we can't
        // swap keys after construction. But we CAN verify the DB is still
        // usable after a failed operation.

        GraphitiConfig config;
        config.db_path = ":memory:";
        config.llm.api_key = api_key;
        config.embedder.api_key = api_key;

        Graphiti g(std::move(config));
        g.build_indices();

        // Succeed first
        auto r1 = g.add_episode({.name = "ok-1", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});
        stress::test("first episode succeeds", r1.has_value());

        // Force an error by searching with empty group (should work actually)
        // Let's try something that might trigger an error
        SearchFilters nasty_filter;
        nasty_filter.agent_ids = {"agent' OR 1=1 OR '"};
        auto bad_search = g.search({.query = "Alice", .group_id = "g", .filters = nasty_filter});
        std::cout << std::format("    -> nasty search: {}\n",
            bad_search.has_value() ? "ok" : "error");

        // Now try a normal operation — should still work
        auto r2 = g.add_episode({.name = "ok-2", .body = "Bob is the CTO.",
            .source_description = "test", .reference_time = now + std::chrono::seconds(60), .group_id = "g"});
        stress::test("normal operation after error still works", r2.has_value());

        auto normal_search = g.search({.query = "Alice", .group_id = "g"});
        stress::test("normal search after error works",
            normal_search.has_value() && !normal_search.value().empty());
    }

    // ====================================================================
    stress::separator("KUZU DRIVER: INVALID DB PATH");
    // ====================================================================
    {
        // Try various bad paths
        std::vector<std::string> bad_paths = {
            "",  // empty
            // (can't easily test truly invalid paths without filesystem issues)
        };

        for (auto& path : bad_paths) {
            try {
                KuzuDriver driver(path);
                auto r = driver.setup_schema();
                std::cout << std::format("    -> path '{}': {}\n",
                    path.empty() ? "(empty)" : path,
                    r.has_value() ? "succeeded" : "error");
            } catch (std::exception& e) {
                std::cout << std::format("    -> path '{}': exception: {}\n",
                    path.empty() ? "(empty)" : path, e.what());
            } catch (...) {
                std::cout << std::format("    -> path '{}': unknown exception\n",
                    path.empty() ? "(empty)" : path);
            }
            stress::test(std::format("bad path '{}' doesn't crash",
                path.empty() ? "(empty)" : path), true);
        }
    }

    // ====================================================================
    stress::separator("SEARCH FILTERS: TEMPORAL EDGE CASES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});
        g.build_indices();

        // Filter with epoch time (1970-01-01)
        SearchFilters epoch_filter;
        epoch_filter.created_at = DateFilterClause{
            {{DateFilter{.date = TimePoint{}, .op = ComparisonOp::gte}}}
        };
        auto r1 = g.search({.query = "Alice", .group_id = "g", .filters = epoch_filter});
        stress::test("epoch time filter doesn't crash",
            true);  // didn't crash = pass
        if (r1.has_value()) {
            std::cout << std::format("    -> {} results with epoch filter\n",
                r1.value().size());
        }

        // Filter with far future time
        auto far_future = now + std::chrono::hours(24 * 365 * 100); // ~100 years
        SearchFilters future_filter;
        future_filter.created_at = DateFilterClause{
            {{DateFilter{.date = far_future, .op = ComparisonOp::lte}}}
        };
        auto r2 = g.search({.query = "Alice", .group_id = "g", .filters = future_filter});
        stress::test("far-future filter doesn't crash", true);
        if (r2.has_value()) {
            std::cout << std::format("    -> {} results with future filter\n",
                r2.value().size());
        }

        // IS NULL filter
        SearchFilters null_filter;
        null_filter.expired_at = DateFilterClause{
            {{DateFilter{.op = ComparisonOp::is_null}}}
        };
        auto r3 = g.search({.query = "Alice", .group_id = "g", .filters = null_filter});
        stress::test("IS NULL filter doesn't crash", true);
        if (r3.has_value()) {
            std::cout << std::format("    -> {} results with IS NULL filter\n",
                r3.value().size());
        }

        // Complex compound filter: (created_at >= epoch AND created_at <= future) OR (expired_at IS NULL)
        SearchFilters compound;
        compound.created_at = DateFilterClause{
            {{DateFilter{.date = TimePoint{}, .op = ComparisonOp::gte},
              DateFilter{.date = far_future, .op = ComparisonOp::lte}}}
        };
        compound.expired_at = DateFilterClause{
            {{DateFilter{.op = ComparisonOp::is_null}}}
        };
        auto r4 = g.search({.query = "Alice", .group_id = "g", .filters = compound});
        stress::test("compound temporal filter doesn't crash", true);
        if (r4.has_value()) {
            std::cout << std::format("    -> {} results with compound filter\n",
                r4.value().size());
        }
    }

    // ====================================================================
    stress::separator("SEARCH WITH num_results = 0");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        auto result = g.search({.query = "Alice", .group_id = "g", .num_results = 0});
        stress::test("search with num_results=0 doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} results\n", result.value().size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("SEARCH WITH num_results = -1");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        auto result = g.search({.query = "Alice", .group_id = "g", .num_results = -1});
        stress::test("search with num_results=-1 doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} results\n", result.value().size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("MULTIPLE GRAPHITI INSTANCES SAME DB");
    // ====================================================================
    {
        // Two instances pointing at the same in-memory DB
        // (in-memory DBs are separate, but this tests construction/destruction)
        auto c1 = stress::make_config();
        auto c2 = stress::make_config();

        Graphiti g1(std::move(c1));
        g1.build_indices();

        Graphiti g2(std::move(c2));
        g2.build_indices();

        auto r1 = g1.add_episode({.name = "inst1", .body = "Alice from instance 1.",
            .source_description = "test", .reference_time = now, .group_id = "g1"});
        auto r2 = g2.add_episode({.name = "inst2", .body = "Bob from instance 2.",
            .source_description = "test", .reference_time = now, .group_id = "g2"});

        stress::test("two instances don't interfere", r1.has_value() && r2.has_value());
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
