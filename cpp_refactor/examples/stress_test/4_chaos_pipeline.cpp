/*
 * CHAOS PIPELINE — throw the worst possible content at the full pipeline
 *
 * This sends absurd, adversarial content through add_episode() to see
 * what the LLM + pipeline + Kuzu stack does with it.
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
    stress::separator("UNICODE CONTENT THROUGH FULL PIPELINE");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto result = g.add_episode({
            .name = "unicode-test",
            .body = "田中太郎は東京のソフトウェアエンジニアです。"
            "彼はAcme株式会社で働いています。"
            "Tanaka Taro is a software engineer in Tokyo.",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        stress::test("unicode/CJK episode doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());
            stress::test("unicode episode extracted entities",
                !result.value().nodes.empty());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("EMOJI-HEAVY CONTENT");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto result = g.add_episode({
            .name = "emoji-test",
            .body = "\xF0\x9F\x91\xA9\xe2\x80\x8d\xF0\x9F\x92\xBB Alice loves coding \xF0\x9F\x92\xBB. "
            "She works at \xF0\x9F\x8F\xA2 Acme Corp \xF0\x9F\x8F\xA2 in Denver \xF0\x9F\x8F\x94. "
            "Her boss Bob \xF0\x9F\x91\xA8\xe2\x80\x8d\xF0\x9F\x92\xBC is great!",
            .source_description = "test", .reference_time = now, .group_id = "g"});

        stress::test("emoji episode doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());

            // Search for emoji content
            auto search = g.search({.query = "Alice", .group_id = "g"});
            stress::test("search after emoji ingest works",
                search.has_value() && !search.value().empty());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("CONTENT THAT LOOKS LIKE CODE/JSON/CYPHER");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        auto result = g.add_episode({
            .name = "code-injection",
            .body = R"(Alice wrote this code:
```python
def hack():
    import os; os.system("rm -rf /")
    return {"exploit": True}
```
Bob reviewed it and said "MATCH (n) DETACH DELETE n" is dangerous.
Carol added a SQL injection: ' OR 1=1; DROP TABLE users; --
The team uses JSON: {"name": "Alice", "role": "engineer"})",
            .source_description = "code review", .reference_time = now, .group_id = "g"});

        stress::test("code/injection content doesn't crash", true);
        if (result.has_value()) {
            std::cout << std::format("    -> {} nodes, {} edges\n",
                result.value().nodes.size(), result.value().edges.size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    stress::separator("CONTRADICTORY FACTS (temporal invalidation)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        // Episode 1: Alice works at Acme
        auto r1 = g.add_episode({
            .name = "fact-1", .body = "Alice works at Acme Corp as a software engineer.",
            .source_description = "test", .reference_time = now, .group_id = "g"});
        stress::test("first episode succeeds", r1.has_value());

        // Episode 2: Alice left Acme and joined Google
        auto r2 = g.add_episode({
            .name = "fact-2", .body = "Alice left Acme Corp and now works at Google as a staff engineer.",
            .source_description = "test", .reference_time = now + std::chrono::hours(24), .group_id = "g"});
        stress::test("contradictory episode succeeds", r2.has_value());

        // Search should reflect the updated state
        (void)g.build_indices();
        auto search = g.search({.query = "Where does Alice work?", .group_id = "g"});
        stress::test("search after contradiction works", search.has_value());
        if (search.has_value()) {
            bool found_google = false;
            for (auto& edge : search.value()) {
                std::cout << std::format("    -> fact: {} (expired_at={})\n",
                    edge.fact,
                    edge.expired_at.has_value() ? "set" : "none");
                if (edge.fact.find("Google") != std::string::npos) {
                    found_google = true;
                }
            }
            stress::test("search finds Google fact", found_google);
        }
    }

    // ====================================================================
    stress::separator("SAME CONTENT INGESTED TWICE (dedup stress)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        std::string content = "Alice works at Acme Corp as a software engineer.";

        auto r1 = g.add_episode({.name = "dup-1", .body = content,
            .source_description = "test", .reference_time = now, .group_id = "g"});
        stress::test("first ingest succeeds", r1.has_value());
        int nodes_1 = r1.has_value() ? r1.value().nodes.size() : 0;

        auto r2 = g.add_episode({.name = "dup-2", .body = content,
            .source_description = "test", .reference_time = now + std::chrono::seconds(60), .group_id = "g"});
        stress::test("duplicate ingest succeeds", r2.has_value());

        if (r2.has_value()) {
            std::cout << std::format("    -> 1st: {} nodes, 2nd: {} nodes\n",
                nodes_1, r2.value().nodes.size());
            // Dedup should recognize existing entities
            // The second ingest might have fewer NEW nodes
        }
    }

    // ====================================================================
    stress::separator("RAPID-FIRE SMALL EPISODES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        int success = 0;
        int fail = 0;
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < 5; ++i) {
            auto r = g.add_episode({
                .name = std::format("rapid-{}", i),
                .body = std::format("Person_{} is a {} at Company_{}.",
                    i, (i % 2 == 0 ? "engineer" : "designer"), i),
                .source_description = "test",
                .reference_time = now + std::chrono::seconds(i),
                .group_id = "rapid"});

            if (r.has_value()) ++success;
            else ++fail;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        std::cout << std::format("    -> {}/5 succeeded in {}ms ({}ms/episode)\n",
            success, elapsed.count(), elapsed.count() / 5);
        stress::test("rapid-fire: all 5 episodes succeed", success == 5);

        // Search across all rapid episodes
        (void)g.build_indices();
        auto search = g.search({.query = "Person engineer", .group_id = "rapid", .num_results = 20});
        stress::test("search across rapid episodes works",
            search.has_value());
        if (search.has_value()) {
            std::cout << std::format("    -> {} edges found\n", search.value().size());
        }
    }

    // ====================================================================
    stress::separator("AGENT_ID EDGE CASES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        // Empty agent_id
        auto r1 = g.add_episode({.name = "no-agent", .body = "Alice works at Acme.",
            .source_description = "test", .reference_time = now, .group_id = "g"});
        stress::test("empty agent_id succeeds", r1.has_value());

        // Very long agent_id
        std::string long_agent(1000, 'A');
        auto r2 = g.add_episode({.name = "long-agent", .body = "Bob works at Google.",
            .source_description = "test", .reference_time = now + std::chrono::seconds(1),
            .group_id = "g", .agent_id = long_agent});
        stress::test("1000-char agent_id succeeds", r2.has_value());

        // Agent ID with special characters
        auto r3 = g.add_episode({.name = "special-agent",
            .body = "Carol works at Meta.", .source_description = "test",
            .reference_time = now + std::chrono::seconds(2), .group_id = "g",
            .agent_id = "agent/with'special\"chars\\and\nnewlines"});
        stress::test("special-char agent_id succeeds", r3.has_value());
    }

    // ====================================================================
    stress::separator("GROUP_ID EDGE CASES");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        (void)g.build_indices();

        // Very long group_id
        std::string long_group(500, 'G');
        auto r1 = g.add_episode({.name = "long-group", .body = "Test data.",
            .source_description = "test", .reference_time = now, .group_id = long_group});
        stress::test("500-char group_id succeeds", r1.has_value());

        // Group ID with special characters
        auto r2 = g.add_episode({.name = "special-group", .body = "More test data.",
            .source_description = "test", .reference_time = now + std::chrono::seconds(1),
            .group_id = "group/with'special\"chars"});
        stress::test("special-char group_id succeeds", r2.has_value());
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
