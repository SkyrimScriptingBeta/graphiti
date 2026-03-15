#include <catch2/catch_all.hpp>

#include <graphiti/graphiti.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

using namespace graphiti;

static GraphitiConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    GraphitiConfig config;
    config.db_path = ":memory:";
    config.llm.api_key = key;
    config.embedder.api_key = key;
    return config;
}

TEST_CASE("Multi-agent dedup merges agent_ids on shared entities",
          "[integration][agent_dedup]") {
    auto config = make_config();
    Graphiti g(std::move(config));
    REQUIRE(g.build_indices().has_value());

    auto now = std::chrono::system_clock::now();

    // Agent A ingests an episode mentioning "Kuzu"
    auto r1 = g.add_episode({
        .name = "ep1",
        .body = "Kuzu is an embedded graph database written in C++.",
        .source_description = "chat",
        .reference_time = now,
        .group_id = "shared_group",
        .agent_id = "agent-alpha",
    });
    REQUIRE(r1.has_value());

    // Verify agent-alpha's entity has agent_ids = ["agent-alpha"]
    bool found_kuzu_alpha = false;
    for (auto& node : r1.value().nodes) {
        if (node.name.find("Kuzu") != std::string::npos ||
            node.name.find("kuzu") != std::string::npos) {
            found_kuzu_alpha = true;
            CHECK(node.agent_ids.size() == 1);
            CHECK(node.agent_ids[0] == "agent-alpha");
        }
    }
    CHECK(found_kuzu_alpha);

    // Agent B ingests an episode also mentioning "Kuzu"
    auto r2 = g.add_episode({
        .name = "ep2",
        .body = "Kuzu supports Cypher queries and has a C++ API.",
        .source_description = "chat",
        .reference_time = now + std::chrono::seconds(60),
        .group_id = "shared_group",
        .agent_id = "agent-beta",
    });
    REQUIRE(r2.has_value());

    // Search for Kuzu — should find a single entity with both agent_ids
    SearchFilters no_filter;
    auto search = g.search({.query = "Kuzu database", .group_id = "shared_group"});
    REQUIRE(search.has_value());

    // Collect all unique entity node UUIDs mentioned in edges
    std::vector<std::string> node_uuids;
    for (auto& edge : search.value()) {
        node_uuids.push_back(edge.source_node_uuid);
        node_uuids.push_back(edge.target_node_uuid);
    }

    // Also check the returned nodes from episode 2 — if dedup worked,
    // the Kuzu node should have been reused (same UUID as episode 1)
    bool found_merged = false;
    for (auto& node : r2.value().nodes) {
        if (node.name.find("Kuzu") != std::string::npos ||
            node.name.find("kuzu") != std::string::npos) {
            INFO("Node: " << node.name << " agent_ids count: " << node.agent_ids.size());
            for (auto& id : node.agent_ids) {
                INFO("  agent_id: " << id);
            }
            // After fix: the node returned in the result may still show only agent-beta
            // (since it's the extracted node), but the persisted version should have both.
            // Let's verify via get_nodes_and_edges_by_episode.
        }
    }

    // The definitive check: retrieve the actual persisted state
    auto ep1_data = g.get_nodes_and_edges_by_episode({r1.value().episode.uuid});
    auto ep2_data = g.get_nodes_and_edges_by_episode({r2.value().episode.uuid});
    REQUIRE(ep1_data.has_value());
    REQUIRE(ep2_data.has_value());

    // Find the Kuzu entity from each episode's perspective
    std::string kuzu_uuid_ep1, kuzu_uuid_ep2;
    for (auto& node : ep1_data.value().nodes) {
        if (node.name.find("Kuzu") != std::string::npos ||
            node.name.find("kuzu") != std::string::npos) {
            kuzu_uuid_ep1 = node.uuid;
        }
    }
    for (auto& node : ep2_data.value().nodes) {
        if (node.name.find("Kuzu") != std::string::npos ||
            node.name.find("kuzu") != std::string::npos) {
            kuzu_uuid_ep2 = node.uuid;
        }
    }

    INFO("Kuzu UUID from ep1: " << kuzu_uuid_ep1);
    INFO("Kuzu UUID from ep2: " << kuzu_uuid_ep2);

    // If dedup worked, both episodes should reference the same Kuzu entity
    if (!kuzu_uuid_ep1.empty() && !kuzu_uuid_ep2.empty()) {
        CHECK(kuzu_uuid_ep1 == kuzu_uuid_ep2);

        // And that entity should have both agent_ids
        // Find it from either result set
        for (auto& node : ep1_data.value().nodes) {
            if (node.uuid == kuzu_uuid_ep1) {
                found_merged = true;
                bool has_alpha = std::find(node.agent_ids.begin(), node.agent_ids.end(),
                                           "agent-alpha") != node.agent_ids.end();
                bool has_beta = std::find(node.agent_ids.begin(), node.agent_ids.end(),
                                          "agent-beta") != node.agent_ids.end();
                INFO("Persisted agent_ids:");
                for (auto& id : node.agent_ids) {
                    INFO("  " << id);
                }
                CHECK(has_alpha);
                CHECK(has_beta);
                break;
            }
        }
    }

    // If LLM didn't dedup (extracted different names), that's an LLM variance issue
    // not a code bug — so we only assert if UUIDs matched
    if (!kuzu_uuid_ep1.empty() && !kuzu_uuid_ep2.empty() &&
        kuzu_uuid_ep1 == kuzu_uuid_ep2) {
        CHECK(found_merged);
    }
}
