/*
 * CYPHER INJECTION ATTACKS
 *
 * The search filter builder in search_filters.cpp uses std::format("'{}'", ...)
 * to embed user-provided strings directly into Cypher queries. This test tries
 * to break out of the string literal and inject arbitrary Cypher.
 *
 * Attack vectors:
 *   - agent_ids filter (lines 106-112)
 *   - edge_types filter (lines 76-80)
 *   - node_labels filter (lines 96-99)
 *   - edge_uuids filter (lines 86-90)
 *
 * This test does NOT require OpenAI — it targets the DB layer directly.
 */

#include "shared.h"
#include "driver/kuzu_driver.h"

#include <format>

using namespace graphiti;

static KuzuDriver make_driver() {
    KuzuDriver driver(":memory:");
    auto r = driver.setup_schema();
    if (!r.has_value()) {
        std::cerr << "Schema setup failed: " << r.error().message << "\n";
        std::exit(1);
    }
    driver.build_fts_indices();
    return driver;
}

static void save_test_data(KuzuDriver& driver) {
    auto now = std::chrono::system_clock::now();

    EntityNode alice;
    alice.uuid = "alice-uuid";
    alice.name = "Alice";
    alice.group_id = "g";
    alice.created_at = now;
    alice.agent_ids = {"scout"};
    driver.save_entity_node(alice);

    EntityNode bob;
    bob.uuid = "bob-uuid";
    bob.name = "Bob";
    bob.group_id = "g";
    bob.created_at = now;
    bob.agent_ids = {"analyst"};
    driver.save_entity_node(bob);

    EntityEdge edge;
    edge.uuid = "edge-uuid";
    edge.group_id = "g";
    edge.source_node_uuid = "alice-uuid";
    edge.target_node_uuid = "bob-uuid";
    edge.name = "KNOWS";
    edge.fact = "Alice knows Bob";
    edge.created_at = now;
    edge.agent_ids = {"scout"};
    driver.save_entity_edge(edge);

    driver.build_fts_indices();
}

int main() {
    stress::separator("CYPHER INJECTION ATTACKS");
    std::cout << "  Testing if malicious strings in SearchFilters can break queries\n";

    // ====================================================================
    // 1. agent_ids injection (most likely attack vector)
    // ====================================================================
    stress::separator("agent_ids injection");
    {
        auto driver = make_driver();
        save_test_data(driver);

        // Classic SQL injection: break out of string literal
        std::vector<std::string> payloads = {
            "scout' OR 1=1 OR '",
            "scout']) OR TRUE OR list_contains(['",
            "'; MATCH (n) DETACH DELETE n; //",
            "scout\\",
            "scout\\'",
            "scout\"",
            "scout\nOR TRUE",
            "scout\0hidden",
            "scout'; DROP TABLE Entity; --",
            R"(scout']) RETURN * UNION MATCH (n) DETACH DELETE n RETURN n.uuid AS n_uuid, n.name AS n_name //)",
        };

        for (auto& payload : payloads) {
            SearchFilters filters;
            filters.agent_ids = {payload};

            auto result = driver.search_entity_edges_bm25(
                "Alice", "g", 10, &filters);

            // We expect either:
            // - An error (injection blocked / malformed query)
            // - Empty results (injection didn't match anything)
            // We do NOT expect it to return ALL results (injection succeeded)
            bool safe = !result.has_value() || result.value().size() <= 1;
            stress::test(
                std::format("agent_ids payload: {}...{}",
                    payload.substr(0, std::min<size_t>(30, payload.size())),
                    safe ? "" : " INJECTION MAY HAVE WORKED!"),
                safe);

            if (result.has_value()) {
                std::cout << std::format("    -> returned {} edges (error=no)\n",
                    result.value().size());
            } else {
                std::cout << std::format("    -> error: {}\n", result.error().message);
            }
        }
    }

    // ====================================================================
    // 2. edge_types injection
    // ====================================================================
    stress::separator("edge_types injection");
    {
        auto driver = make_driver();
        save_test_data(driver);

        SearchFilters filters;
        filters.edge_types = {"KNOWS' OR TRUE OR name='"};

        auto result = driver.search_entity_edges_bm25("Alice", "g", 10, &filters);
        bool safe = !result.has_value() || result.value().size() <= 1;
        stress::test("edge_types with quote injection", safe);
        if (result.has_value()) {
            std::cout << std::format("    -> returned {} edges\n", result.value().size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    // 3. node_labels injection
    // ====================================================================
    stress::separator("node_labels injection");
    {
        auto driver = make_driver();
        save_test_data(driver);

        SearchFilters filters;
        filters.node_labels = {"Person']) OR TRUE OR list_has_all(n.labels, ['"};

        auto result = driver.search_entity_nodes_bm25("Alice", "g", 10, &filters);
        bool safe = !result.has_value() || result.value().size() <= 1;
        stress::test("node_labels with quote injection", safe);
        if (result.has_value()) {
            std::cout << std::format("    -> returned {} nodes\n", result.value().size());
        } else {
            std::cout << std::format("    -> error: {}\n", result.error().message);
        }
    }

    // ====================================================================
    // 4. Strings with special characters persisted to DB
    // ====================================================================
    stress::separator("Special character persistence");
    {
        auto driver = make_driver();

        auto now = std::chrono::system_clock::now();

        // Try saving entities with nasty names
        std::vector<std::pair<std::string, std::string>> nasty_names = {
            {"single-quote", "O'Brien"},
            {"double-quote", R"(She said "hello")"},
            {"backslash", R"(path\to\file)"},
            {"newline", "line1\nline2"},
            {"tab", "col1\tcol2"},
            {"null-byte", std::string("before\0after", 12)},
            {"unicode-emoji", "\xF0\x9F\x92\xA9"},  // poop emoji
            {"unicode-cjk", "\xe4\xb8\xad\xe6\x96\x87"},  // 中文
            {"unicode-rtl", "\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a"},  // عربي
            {"curly-braces", "{key: value}"},
            {"square-brackets", "[1, 2, 3]"},
            {"cypher-syntax", "MATCH (n) DELETE n"},
            {"html-xss", "<script>alert('xss')</script>"},
            {"very-long", std::string(10000, 'A')},
            {"empty", ""},
        };

        for (auto& [label, name] : nasty_names) {
            EntityNode node;
            node.uuid = "nasty-" + label;
            node.name = name;
            node.group_id = "g";
            node.created_at = now;

            auto save_result = driver.save_entity_node(node);
            if (!save_result.has_value()) {
                stress::test(
                    std::format("save '{}' (len={})", label, name.size()),
                    false);
                std::cout << std::format("    -> save error: {}\n", save_result.error().message);
                continue;
            }

            // Read it back and verify round-trip
            auto get_result = driver.get_entity_node("nasty-" + label);
            if (!get_result.has_value()) {
                stress::test(
                    std::format("roundtrip '{}' (len={})", label, name.size()),
                    false);
                std::cout << std::format("    -> get error: {}\n", get_result.error().message);
                continue;
            }

            bool matches = get_result.value().name == name;
            stress::test(
                std::format("roundtrip '{}' (len={})", label, name.size()),
                matches);
            if (!matches) {
                std::cout << std::format("    -> expected len={}, got len={}\n",
                    name.size(), get_result.value().name.size());
            }
        }
    }

    // ====================================================================
    // 5. Entity edge with nasty fact strings
    // ====================================================================
    stress::separator("Entity edge with injection-style facts");
    {
        auto driver = make_driver();

        auto now = std::chrono::system_clock::now();

        EntityNode src;
        src.uuid = "src";
        src.name = "Src";
        src.group_id = "g";
        src.created_at = now;
        driver.save_entity_node(src);

        EntityNode tgt;
        tgt.uuid = "tgt";
        tgt.name = "Tgt";
        tgt.group_id = "g";
        tgt.created_at = now;
        driver.save_entity_node(tgt);

        std::vector<std::pair<std::string, std::string>> nasty_facts = {
            {"quote-in-fact", "Alice said 'hello' to Bob"},
            {"double-quote-fact", R"(Bob replied "goodbye" loudly)"},
            {"backslash-fact", R"(Path is C:\Users\Alice)"},
            {"newline-fact", "First line\nSecond line\nThird line"},
            {"mega-fact", std::string(50000, 'X')},
            {"json-fact", R"({"key": "value", "nested": {"a": 1}})"},
            {"cypher-fact", "MATCH (n)-[r]->(m) DELETE r"},
        };

        for (size_t i = 0; i < nasty_facts.size(); ++i) {
            auto& [label, fact] = nasty_facts[i];
            EntityEdge edge;
            edge.uuid = std::format("nasty-edge-{}", i);
            edge.group_id = "g";
            edge.source_node_uuid = "src";
            edge.target_node_uuid = "tgt";
            edge.name = "TEST";
            edge.fact = fact;
            edge.created_at = now;

            auto save_result = driver.save_entity_edge(edge);
            if (!save_result.has_value()) {
                stress::test(std::format("edge save '{}'", label), false);
                std::cout << std::format("    -> error: {}\n", save_result.error().message);
                continue;
            }

            auto get_result = driver.get_entity_edge(edge.uuid);
            bool matches = get_result.has_value() && get_result.value().fact == fact;
            stress::test(std::format("edge roundtrip '{}' (len={})", label, fact.size()), matches);
        }
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
