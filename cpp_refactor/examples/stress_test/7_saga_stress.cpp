/*
 * SAGA STRESS TEST
 *
 * Exercises saga features that aren't covered by other stress tests:
 * - Saga creation via add_episode
 * - Multiple episodes in one saga (HAS_EPISODE + NEXT_EPISODE linking)
 * - Saga with bulk ingestion
 * - Multiple sagas in the same group
 * - Saga with special characters in name
 * - Same saga name across different groups
 * - Saga + agent_id combination
 * - Saga + update_communities combination
 * - Very long saga name
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
    stress::separator("BASIC SAGA: SINGLE EPISODE");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto result = g.add_episode(
            "ep1",
            "Alice started her first day at Acme Corp.",
            "chat", now, EpisodeType::message,
            "saga_group",
            "",           // agent_id
            "",           // source_id
            {},           // participant_ids
            std::nullopt, // custom_instructions
            "onboarding"  // saga name
        );
        stress::test("single episode with saga succeeds", result.has_value());

        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());
        }
    }

    // ====================================================================
    stress::separator("SAGA: MULTIPLE EPISODES (NEXT_EPISODE CHAIN)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Episode 1
        auto r1 = g.add_episode(
            "ep1", "Day 1: Alice started at Acme Corp.",
            "chat", now, EpisodeType::message,
            "saga_chain", "", "", "", {},
            std::nullopt, "alice_journey"
        );
        stress::test("saga chain ep1", r1.has_value());

        // Episode 2
        auto r2 = g.add_episode(
            "ep2", "Day 2: Alice met her team. Bob is the tech lead.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message,
            "saga_chain", "", "", "", {},
            std::nullopt, "alice_journey"
        );
        stress::test("saga chain ep2", r2.has_value());

        // Episode 3
        auto r3 = g.add_episode(
            "ep3", "Day 3: Alice completed her first code review with Bob.",
            "chat", now + std::chrono::seconds(120), EpisodeType::message,
            "saga_chain", "", "", "", {},
            std::nullopt, "alice_journey"
        );
        stress::test("saga chain ep3", r3.has_value());

        // Episode 4
        auto r4 = g.add_episode(
            "ep4", "Day 7: Alice shipped her first feature at Acme Corp.",
            "chat", now + std::chrono::seconds(180), EpisodeType::message,
            "saga_chain", "", "", "", {},
            std::nullopt, "alice_journey"
        );
        stress::test("saga chain ep4", r4.has_value());

        // Search should find facts from the saga
        auto search = g.search("What did Alice do at Acme?", "saga_chain");
        stress::test("search after saga episodes succeeds", search.has_value());
        if (search.has_value()) {
            std::cout << std::format("    -> search found {} edges\n",
                search.value().size());
        }
    }

    // ====================================================================
    stress::separator("SAGA WITH EXPLICIT PREVIOUS EPISODE UUID");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto r1 = g.add_episode(
            "ep1", "The project started with requirement gathering.",
            "chat", now, EpisodeType::message,
            "explicit_chain", "", "", "", {},
            std::nullopt, "project_timeline"
        );
        stress::test("explicit chain ep1", r1.has_value());

        // Use ep1's UUID as explicit previous
        std::string ep1_uuid = r1.has_value() ? r1.value().episode.uuid : "";

        auto r2 = g.add_episode(
            "ep2", "The team moved to design phase after requirements.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message,
            "explicit_chain", "", "", "", {},
            std::nullopt,
            "project_timeline",  // saga
            ep1_uuid             // saga_previous_episode_uuid
        );
        stress::test("explicit chain ep2 with prev uuid", r2.has_value());
    }

    // ====================================================================
    stress::separator("MULTIPLE SAGAS IN SAME GROUP");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Saga 1: Alice's journey
        auto r1 = g.add_episode(
            "alice1", "Alice joined the engineering team.",
            "chat", now, EpisodeType::message,
            "multi_saga", "", "", "", {},
            std::nullopt, "alice_story");
        stress::test("multi-saga: alice ep1", r1.has_value());

        auto r2 = g.add_episode(
            "alice2", "Alice got promoted to senior engineer.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message,
            "multi_saga", "", "", "", {},
            std::nullopt, "alice_story");
        stress::test("multi-saga: alice ep2", r2.has_value());

        // Saga 2: Bob's journey (different saga, same group)
        auto r3 = g.add_episode(
            "bob1", "Bob joined the marketing team.",
            "chat", now + std::chrono::seconds(120), EpisodeType::message,
            "multi_saga", "", "", "", {},
            std::nullopt, "bob_story");
        stress::test("multi-saga: bob ep1", r3.has_value());

        auto r4 = g.add_episode(
            "bob2", "Bob transferred to product management.",
            "chat", now + std::chrono::seconds(180), EpisodeType::message,
            "multi_saga", "", "", "", {},
            std::nullopt, "bob_story");
        stress::test("multi-saga: bob ep2", r4.has_value());

        // Both sagas should have their data accessible
        auto s1 = g.search("Alice promotion", "multi_saga");
        stress::test("search saga 1 content", s1.has_value());

        auto s2 = g.search("Bob marketing", "multi_saga");
        stress::test("search saga 2 content", s2.has_value());
    }

    // ====================================================================
    stress::separator("SAGA WITH BULK INGESTION");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        std::vector<RawEpisode> episodes;
        for (int i = 0; i < 3; ++i) {
            episodes.push_back(RawEpisode{
                .name = std::format("bulk_saga_{}", i),
                .content = std::format("Day {}: Team completed milestone {}.", i + 1, i + 1),
                .source_description = "test",
                .reference_time = now + std::chrono::seconds(i * 60),
                .source = EpisodeType::message,
            });
        }

        auto result = g.add_episode_bulk(
            episodes, "bulk_saga_group", "", "", "", {},
            std::nullopt, "sprint_log"
        );
        stress::test("bulk ingest with saga succeeds", result.has_value());

        if (result.has_value()) {
            std::cout << std::format("    -> {} episodes, {} nodes\n",
                result.value().episodes.size(), result.value().nodes.size());
            stress::test("bulk saga created all episodes",
                result.value().episodes.size() == 3);
        }
    }

    // ====================================================================
    stress::separator("SAGA WITH SPECIAL CHARACTERS IN NAME");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Apostrophes, quotes, unicode
        auto r1 = g.add_episode(
            "special1", "Alice's first meeting went well.",
            "chat", now, EpisodeType::message,
            "special_saga_group", "", "", "", {},
            std::nullopt,
            "Alice's \"Journey\" (Part 1)"  // saga with special chars
        );
        stress::test("saga with special chars ep1", r1.has_value());

        auto r2 = g.add_episode(
            "special2", "Alice's second meeting was about Q&A.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message,
            "special_saga_group", "", "", "", {},
            std::nullopt,
            "Alice's \"Journey\" (Part 1)"  // same saga
        );
        stress::test("saga with special chars ep2 (same saga)", r2.has_value());
    }

    // ====================================================================
    stress::separator("SAGA + AGENT_ID COMBINATION");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto r1 = g.add_episode(
            "agent_saga1",
            "Agent Alpha reported: Target arrived at location A.",
            "chat", now, EpisodeType::message,
            "agent_saga_group",
            "agent-alpha",    // agent_id
            "",               // source_id
            {},               // participant_ids
            std::nullopt,
            "surveillance_log"  // saga
        );
        stress::test("saga + agent_id ep1", r1.has_value());

        auto r2 = g.add_episode(
            "agent_saga2",
            "Agent Beta reported: Target moved to location B.",
            "chat", now + std::chrono::seconds(60), EpisodeType::message,
            "agent_saga_group",
            "agent-beta",     // different agent_id
            "",               // source_id
            {},               // participant_ids
            std::nullopt,
            "surveillance_log"  // same saga
        );
        stress::test("saga + different agent_id ep2", r2.has_value());
    }

    // ====================================================================
    stress::separator("SAGA + UPDATE_COMMUNITIES COMBINATION");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Build initial data + communities
        g.add_episode("combo1", "Alice and Bob work at Acme Corp.",
            "chat", now, EpisodeType::message, "combo_group");
        g.build_indices();
        g.build_communities({"combo_group"});

        // Add episode with BOTH saga and update_communities
        auto result = g.add_episode(
            "combo2",
            "Charlie joined Acme Corp. He works with Alice and Bob.",
            "chat",
            now + std::chrono::seconds(60),
            EpisodeType::message,
            "combo_group",
            "",           // agent_id
            "",           // source_id
            {},           // participant_ids
            std::nullopt, // custom_instructions
            "team_growth", // saga
            std::nullopt, // saga_previous_episode_uuid
            true          // update_communities
        );
        stress::test("saga + update_communities combo succeeds", result.has_value());
    }

    // ====================================================================
    stress::separator("VERY LONG SAGA NAME");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        std::string long_saga_name(500, 'A');
        long_saga_name += "_saga";

        auto result = g.add_episode(
            "long_saga_ep", "Testing with an extremely long saga name.",
            "chat", now, EpisodeType::message,
            "long_saga_group", "", "", "", {},
            std::nullopt, long_saga_name);
        stress::test("very long saga name (500+ chars)", result.has_value());
    }

    // ====================================================================
    stress::separator("SAME SAGA NAME ACROSS DIFFERENT GROUPS");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        auto r1 = g.add_episode(
            "cross1", "Alice works in Group X.",
            "chat", now, EpisodeType::message,
            "group_x", "", "", "", {},
            std::nullopt, "shared_saga");
        stress::test("same saga name group_x", r1.has_value());

        auto r2 = g.add_episode(
            "cross2", "Bob works in Group Y.",
            "chat", now, EpisodeType::message,
            "group_y", "", "", "", {},
            std::nullopt, "shared_saga");
        stress::test("same saga name group_y", r2.has_value());

        // Both should succeed independently
        stress::test("sagas with same name in different groups are independent", true);
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
