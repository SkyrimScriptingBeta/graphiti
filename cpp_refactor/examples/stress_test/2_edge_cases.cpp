/*
 * EDGE CASES — things that shouldn't crash but might
 *
 * - Search on empty graph
 * - Double build_indices
 * - Delete group then search
 * - add_episode with empty body
 * - add_episode with gigantic body
 * - add_episode_bulk with empty vector
 * - add_episode_bulk with 1 episode
 * - Search with empty query
 * - Search with very long query
 * - Search with special character query
 * - Build indices after data (FTS rebuild)
 *
 * Requires OPENAI_API_KEY for full pipeline tests.
 */

#include "shared.h"

#include <chrono>
#include <format>

using namespace graphiti;

int main() {
    auto config = stress::make_config();

    auto now = std::chrono::system_clock::now();

    // ====================================================================
    stress::separator("SEARCH ON EMPTY GRAPH");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto result = g.search({.query = "hello", .group_id = "test_group"});
        stress::test("search on empty graph returns ok",
            result.has_value());
        stress::test("search on empty graph returns 0 results",
            result.has_value() && result.value().empty());
    }

    // ====================================================================
    stress::separator("DOUBLE BUILD_INDICES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        auto r1 = g.build_indices();
        stress::test("first build_indices succeeds", r1.has_value());

        auto r2 = g.build_indices();
        stress::test("second build_indices succeeds", r2.has_value());

        auto r3 = g.build_indices();
        stress::test("third build_indices succeeds", r3.has_value());
    }

    // ====================================================================
    stress::separator("DELETE GROUP THEN SEARCH");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Ingest something
        auto add = g.add_episode({
            .name = "ep1", .body = "Alice works at Acme Corp.",
            .source_description = "test", .reference_time = now, .group_id = "doomed_group"});
        stress::test("add_episode to doomed_group succeeds", add.has_value());

        // Delete the group
        auto del = g.delete_group("doomed_group");
        stress::test("delete_group succeeds", del.has_value());

        // Search the deleted group — should return empty, not error
        auto search = g.search({.query = "Alice", .group_id = "doomed_group"});
        stress::test("search deleted group returns ok", search.has_value());
        stress::test("search deleted group returns 0 results",
            search.has_value() && search.value().empty());
    }

    // ====================================================================
    stress::separator("ADD_EPISODE WITH EMPTY BODY");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto result = g.add_episode({
            .name = "empty-ep", .body = "",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        // Should either succeed with 0 entities or return an error — not crash
        bool didnt_crash = true;
        stress::test("empty body doesn't crash", didnt_crash);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());
        } else {
            std::cout << std::format("    -> error (ok): {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("ADD_EPISODE WITH EMPTY NAME");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto result = g.add_episode({
            .name = "", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        bool didnt_crash = true;
        stress::test("empty name doesn't crash", didnt_crash);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("ADD_EPISODE WITH VERY LONG BODY");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // 10KB of text — should work but might be slow
        std::string long_body;
        for (int i = 0; i < 100; ++i) {
            long_body += std::format("Person_{} works at Company_{} in City_{}. ", i, i, i);
        }

        auto result = g.add_episode({
            .name = "long-ep", .body = long_body,
            .source_description = "test", .reference_time = now, .group_id = "g"});

        stress::test("long body doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges (body was {} chars)\n",
                result.value().nodes.size(), result.value().edges.size(), long_body.size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("ADD_EPISODE_BULK WITH EMPTY VECTOR");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        std::vector<RawEpisode> empty_episodes;
        auto result = g.add_episode_bulk({.episodes = empty_episodes});
        stress::test("empty bulk doesn't crash", true);
        stress::test("empty bulk returns ok", result.has_value());
        if (result.has_value()) {
            stress::test("empty bulk returns 0 episodes",
                result.value().episodes.empty());
        }
    }

    // ====================================================================
    stress::separator("ADD_EPISODE_BULK WITH 1 EPISODE");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        std::vector<RawEpisode> single = {{
            .name = "solo",
            .content = "Dave is an engineer at Google.",
            .source_description = "test",
            .reference_time = now,
        }};

        auto result = g.add_episode_bulk({.episodes = single, .group_id = "g"});
        stress::test("single-item bulk doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} episodes, {} nodes, {} edges\n",
                result.value().episodes.size(),
                result.value().nodes.size(),
                result.value().edges.size());
            stress::test("single-item bulk returns 1 episode",
                result.value().episodes.size() == 1);
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
            stress::test("single-item bulk returns 1 episode", false);
        }
    }

    // ====================================================================
    stress::separator("SEARCH WITH EMPTY QUERY");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Add some data first
        g.add_episode({.name = "ep", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        auto result = g.search({.query = "", .group_id = "g"});
        stress::test("empty query doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} results\n", result.value().size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("SEARCH WITH SPECIAL CHARACTER QUERIES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme Corp.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        std::vector<std::pair<std::string, std::string>> queries = {
            {"single-quote", "Alice's job"},
            {"double-quote", R"(Alice "works")"},
            {"backslash", R"(Alice\Bob)"},
            {"wildcard-star", "Alice*"},
            {"wildcard-question", "Alice?"},
            {"parentheses", "(Alice)"},
            {"square-brackets", "[Alice]"},
            {"curly-braces", "{Alice}"},
            {"semicolon", "Alice; DROP TABLE"},
            {"newline", "Alice\nBob"},
            {"unicode-emoji", "\xF0\x9F\x92\xA9"},
            {"null-byte", std::string("Alice\0Bob", 9)},
        };

        for (auto& [label, query] : queries) {
            auto result = g.search({.query = query, .group_id = "g"});
            stress::test(
                std::format("search '{}' doesn't crash", label),
                true);  // If we got here, it didn't crash
            if (result.has_value()) {
                std::cout << std::format("    -> {} results\n", result.value().size());
            } else {
                std::cout << std::format("    -> error: {}\n", result.error().message);
            }
        }
    }

    // ====================================================================
    stress::separator("INGEST THEN REBUILD INDICES THEN SEARCH");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme Corp as an engineer.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        // Rebuild indices after data — FTS should still work
        auto rebuild = g.build_indices();
        stress::test("rebuild indices after data succeeds", rebuild.has_value());

        auto result = g.search({.query = "Alice", .group_id = "g"});
        stress::test("search after rebuild returns results",
            result.has_value() && !result.value().empty());
    }

    // ====================================================================
    stress::separator("SEARCH_ADVANCED WITH VARIOUS CONFIGS");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode({.name = "ep", .body = "Alice works at Acme Corp as an engineer.",
            .source_description = "test", .reference_time = now, .group_id = "g"});
        g.build_indices(); // rebuild for FTS

        // Try all the recipe configs
        struct Recipe {
            std::string name;
            SearchConfig config;
        };

        std::vector<Recipe> recipes = {
            {"edge_hybrid_rrf", edge_hybrid_search_rrf()},
            {"edge_hybrid_mmr", edge_hybrid_search_mmr()},
            {"node_hybrid_rrf", node_hybrid_search_rrf()},
            {"node_hybrid_mmr", node_hybrid_search_mmr()},
            {"combined_hybrid_rrf", combined_hybrid_search_rrf()},
            {"community_hybrid_rrf", community_hybrid_search_rrf()},
        };

        for (auto& [name, cfg] : recipes) {
            auto result = g.search_advanced({.query = "Alice", .config = cfg, .group_id = "g"});
            stress::test(
                std::format("search_advanced {} doesn't crash", name),
                true);
            if (result.has_value()) {
                std::cout << std::format("    -> {} edges, {} nodes, {} episodes, {} communities\n",
                    result.value().edges.size(),
                    result.value().nodes.size(),
                    result.value().episodes.size(),
                    result.value().communities.size());
            } else {
                std::cout << std::format("    -> error: {}\n", result.error().message);
            }
        }
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
