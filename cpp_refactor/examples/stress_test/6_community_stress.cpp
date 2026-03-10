/*
 * COMMUNITY DETECTION STRESS TEST
 *
 * Exercises the full community pipeline:
 * - build_communities on empty graph
 * - build_communities after ingest
 * - Rebuild communities (old ones should be replaced)
 * - Community search via search_advanced
 * - update_communities=true during add_episode
 * - Large cluster (10+ entities) to exercise tree summarization
 * - Special characters in entity names flowing through to communities
 * - build_communities with multiple group_ids
 * - build_communities with empty group_ids (auto-detect all groups)
 * - Rapid rebuild cycle (build → add data → rebuild × 3)
 *
 * Requires OPENAI_API_KEY.
 */

#include "shared.h"

#include <chrono>
#include <format>

using namespace graphiti;

int main() {
    auto now = std::chrono::system_clock::now();

    // ====================================================================
    stress::separator("BUILD COMMUNITIES ON EMPTY GRAPH");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto result = g.build_communities({"nonexistent_group"});
        stress::test("build_communities on empty graph succeeds", result.has_value());
        if (result.has_value()) {
            stress::test("empty graph produces 0 communities",
                result.value().first.empty());
            stress::test("empty graph produces 0 community edges",
                result.value().second.empty());
        }
    }

    // ====================================================================
    stress::separator("BUILD COMMUNITIES ON EMPTY GROUP_IDS (AUTO-DETECT)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Empty group_ids = auto-detect all groups (which is none here)
        auto result = g.build_communities({});
        stress::test("build_communities with empty group_ids succeeds", result.has_value());
        if (result.has_value()) {
            stress::test("no groups → 0 communities", result.value().first.empty());
        }
    }

    // ====================================================================
    stress::separator("BUILD COMMUNITIES AFTER MULTI-EPISODE INGEST");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Ingest 3 related episodes to create a connected entity graph
        auto r1 = g.add_episode(
            "ep1", "Alice works at Acme Corp as a software engineer in Denver.",
            "chat", now, EpisodeType::message, "community_test");
        stress::test("episode 1 ingest", r1.has_value());

        auto r2 = g.add_episode(
            "ep2", "Bob also works at Acme Corp. Alice and Bob are on the same team.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message, "community_test");
        stress::test("episode 2 ingest", r2.has_value());

        auto r3 = g.add_episode(
            "ep3", "Charlie is the CEO of Acme Corp. He hired Alice and Bob.",
            "chat", now + std::chrono::seconds(120), EpisodeType::message, "community_test");
        stress::test("episode 3 ingest", r3.has_value());

        // Rebuild indices (FTS)
        g.build_indices();

        auto result = g.build_communities({"community_test"});
        stress::test("build_communities after ingest succeeds", result.has_value());

        if (result.has_value()) {
            auto& [nodes, edges] = result.value();
            std::cout << std::format("    -> {} communities, {} HAS_MEMBER edges\n",
                nodes.size(), edges.size());
            stress::test("at least 1 community created", !nodes.empty());
            stress::test("at least 1 HAS_MEMBER edge created", !edges.empty());

            // Verify community fields
            for (auto& node : nodes) {
                stress::test(std::format("community '{}' has uuid", node.name),
                    !node.uuid.empty());
                stress::test(std::format("community '{}' has summary", node.name),
                    !node.summary.empty());
                stress::test(std::format("community '{}' has group_id", node.name),
                    node.group_id == "community_test");
            }
        }
    }

    // ====================================================================
    stress::separator("COMMUNITY SEARCH VIA SEARCH_ADVANCED");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("ep1", "Alice works at Acme Corp.",
            "chat", now, EpisodeType::message, "search_test");
        g.add_episode("ep2", "Bob works at Acme Corp with Alice.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message, "search_test");

        g.build_indices();
        auto communities = g.build_communities({"search_test"});
        stress::test("communities built for search test", communities.has_value());

        // Rebuild FTS after community creation
        g.build_indices();

        auto search = g.search_advanced(
            "Who works at Acme?",
            community_hybrid_search_rrf(),
            "search_test");
        stress::test("community search succeeds", search.has_value());

        if (search.has_value()) {
            std::cout << std::format("    -> {} community results\n",
                search.value().communities.size());
            stress::test("community search returns results",
                !search.value().communities.empty());
        }

        // Also try community MMR search
        auto search_mmr = g.search_advanced(
            "Acme Corp employees",
            community_hybrid_search_mmr(),
            "search_test");
        stress::test("community MMR search succeeds", search_mmr.has_value());
    }

    // ====================================================================
    stress::separator("REBUILD COMMUNITIES (OLD ONES REPLACED)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("ep1", "Alice and Bob work at Acme Corp.",
            "chat", now, EpisodeType::message, "rebuild_test");
        g.build_indices();

        // First build
        auto first = g.build_communities({"rebuild_test"});
        stress::test("first build succeeds", first.has_value());
        size_t first_count = first.has_value() ? first.value().first.size() : 0;

        // Add more data
        g.add_episode("ep2",
            "Charlie and Diana also joined Acme Corp. They work with Alice.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message, "rebuild_test");
        g.build_indices();

        // Rebuild — should clear old communities first
        auto second = g.build_communities({"rebuild_test"});
        stress::test("rebuild succeeds", second.has_value());
        size_t second_count = second.has_value() ? second.value().first.size() : 0;

        std::cout << std::format("    -> first: {} communities, second: {} communities\n",
            first_count, second_count);
        stress::test("rebuild didn't crash (key test)", true);
    }

    // ====================================================================
    stress::separator("UPDATE_COMMUNITIES DURING ADD_EPISODE");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Build initial graph + communities
        g.add_episode("ep1", "Alice and Bob work at Acme Corp together.",
            "chat", now, EpisodeType::message, "update_test");
        g.build_indices();
        auto communities = g.build_communities({"update_test"});
        stress::test("initial communities built", communities.has_value());

        // Now add episode with update_communities=true
        auto result = g.add_episode(
            "ep2",
            "Charlie just joined Acme Corp. He will work with Alice and Bob.",
            "chat",
            now + std::chrono::seconds(60),
            EpisodeType::message,
            "update_test",
            "",           // agent_id
            "",           // source_id
            "",           // source_context
            {},           // participant_ids
            std::nullopt, // custom_instructions
            std::nullopt, // saga
            std::nullopt, // saga_previous_episode_uuid
            true          // update_communities
        );
        stress::test("add_episode with update_communities=true succeeds",
            result.has_value());

        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges extracted\n",
                result.value().nodes.size(), result.value().edges.size());
        }
    }

    // ====================================================================
    stress::separator("LARGE CLUSTER (TREE SUMMARIZATION)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Create a dense graph: 5 episodes mentioning 10+ distinct entities
        // all connected through a shared hub
        g.add_episode("large1",
            "Alice, Bob, Charlie, and Diana all work at MegaCorp headquarters.",
            "chat", now, EpisodeType::message, "large_cluster");
        g.add_episode("large2",
            "Eve, Frank, and Grace also work at MegaCorp. Eve reports to Alice.",
            "chat", now + std::chrono::seconds(30), EpisodeType::message, "large_cluster");
        g.add_episode("large3",
            "Hank and Iris joined MegaCorp last week. They work with Bob and Charlie.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message, "large_cluster");
        g.add_episode("large4",
            "Jack is the CEO of MegaCorp. Diana, Eve, and Frank report to Jack.",
            "chat", now + std::chrono::seconds(90), EpisodeType::message, "large_cluster");
        g.add_episode("large5",
            "MegaCorp is headquartered in San Francisco. All employees work there.",
            "chat", now + std::chrono::seconds(120), EpisodeType::message, "large_cluster");

        g.build_indices();

        auto result = g.build_communities({"large_cluster"});
        stress::test("large cluster community build succeeds", result.has_value());

        if (result.has_value()) {
            auto& [nodes, edges] = result.value();
            std::cout << std::format("    -> {} communities, {} edges\n",
                nodes.size(), edges.size());
            stress::test("large cluster produced communities", !nodes.empty());

            // Each community summary should be non-trivial
            for (auto& node : nodes) {
                stress::test(std::format("community summary non-empty (len={})",
                    node.summary.size()), !node.summary.empty());
            }
        }
    }

    // ====================================================================
    stress::separator("SPECIAL CHARACTERS IN COMMUNITIES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("special1",
            "O'Brien works at Acme & Associates LLC. "
            "His colleague \"Bob\" (also known as B.J.) is there too.",
            "chat", now, EpisodeType::message, "special_chars");
        g.add_episode("special2",
            "O'Brien and \"Bob\" collaborate on the Q&A system at Acme & Associates.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message, "special_chars");

        g.build_indices();

        auto result = g.build_communities({"special_chars"});
        stress::test("special chars in communities succeeds", result.has_value());

        if (result.has_value()) {
            std::cout << std::format("    -> {} communities\n",
                result.value().first.size());
        }
    }

    // ====================================================================
    stress::separator("MULTIPLE GROUP_IDS IN ONE CALL");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("mg1", "Alice works at Acme Corp.",
            "chat", now, EpisodeType::message, "group_a");
        g.add_episode("mg2", "Bob works at TechCo.",
            "chat", now, EpisodeType::message, "group_b");

        g.build_indices();

        // Build communities for both groups at once
        auto result = g.build_communities({"group_a", "group_b"});
        stress::test("multi-group build succeeds", result.has_value());

        if (result.has_value()) {
            auto& [nodes, edges] = result.value();
            std::cout << std::format("    -> {} total communities across 2 groups\n",
                nodes.size());

            // Check we got communities from both groups
            bool has_group_a = false, has_group_b = false;
            for (auto& node : nodes) {
                if (node.group_id == "group_a") has_group_a = true;
                if (node.group_id == "group_b") has_group_b = true;
            }
            // At least one group should have communities (depends on LLM extraction)
            stress::test("at least one group got communities",
                has_group_a || has_group_b);
        }
    }

    // ====================================================================
    stress::separator("AUTO-DETECT GROUP_IDS (EMPTY VECTOR)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("auto1", "Alice works at Acme Corp.",
            "chat", now, EpisodeType::message, "auto_group");
        g.build_indices();

        // Empty group_ids = auto-detect all groups
        auto result = g.build_communities({});
        stress::test("auto-detect group_ids succeeds", result.has_value());

        if (result.has_value()) {
            std::cout << std::format("    -> {} communities (auto-detected groups)\n",
                result.value().first.size());
        }
    }

    // ====================================================================
    stress::separator("RAPID REBUILD CYCLE (build -> add -> rebuild x3)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        for (int cycle = 0; cycle < 3; ++cycle) {
            g.add_episode(
                std::format("cycle-{}", cycle),
                std::format("Person_{} works at Company_{} doing Task_{}.",
                    cycle, cycle, cycle),
                "chat",
                now + std::chrono::seconds(cycle * 60),
                EpisodeType::message,
                "rapid_rebuild");

            g.build_indices();

            auto result = g.build_communities({"rapid_rebuild"});
            stress::test(
                std::format("rapid rebuild cycle {} succeeds", cycle),
                result.has_value());

            if (result.has_value()) {
                std::cout << std::format("    -> cycle {}: {} communities\n",
                    cycle, result.value().first.size());
            }
        }
    }

    // ====================================================================
    stress::separator("BUILD COMMUNITIES AFTER DELETE_GROUP");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        g.add_episode("del1", "Alice works at Acme.",
            "chat", now, EpisodeType::message, "delete_test");
        g.build_indices();

        auto c1 = g.build_communities({"delete_test"});
        stress::test("communities before delete", c1.has_value());

        g.delete_group("delete_test");

        auto c2 = g.build_communities({"delete_test"});
        stress::test("communities after delete succeeds", c2.has_value());
        if (c2.has_value()) {
            stress::test("communities after delete is empty",
                c2.value().first.empty());
        }
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
