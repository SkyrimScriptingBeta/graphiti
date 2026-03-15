/*
 * Step 2: Search with and without agent_ids filtering.
 *
 * This program runs the same query three ways:
 *   1. No agent filter  -> should return results from both agents
 *   2. agent_ids=["scout"]   -> only scout's contributions
 *   3. agent_ids=["analyst"] -> only analyst's contributions
 *
 * You should see different result sets for each.
 */

#include "shared.h"

#include <format>

static void print_edges(const std::vector<graphiti::EntityEdge>& edges, const std::string& label,
                        const std::string& query) {
    shared::print_separator(label);
    std::cout << std::format("  Query: \"{}\"\n", query);
    std::cout << std::format("  Results: {}\n", edges.size());
    std::cout << std::string(70, '-') << "\n";

    for (size_t i = 0; i < edges.size(); ++i) {
        auto& edge = edges[i];
        std::string agent_info = "  agent_ids=[";
        for (size_t j = 0; j < edge.agent_ids.size(); ++j) {
            if (j > 0) agent_info += ", ";
            agent_info += "\"" + edge.agent_ids[j] + "\"";
        }
        agent_info += "]";
        std::cout << std::format("  [{}] {}{}\n", i + 1, edge.fact, agent_info);
    }
    if (edges.empty()) {
        std::cout << "  (no results)\n";
    }
}

int main() {
    auto g = shared::make_graphiti();

    std::string query = "Alice";

    // 1. No agent filter
    auto edges_all = g.search({.query = query});
    if (!edges_all.has_value()) {
        std::cerr << "Search error: " << edges_all.error().message << "\n";
        return 1;
    }
    print_edges(edges_all.value(), "ALL AGENTS (no filter)", query);

    // 2. Scout only
    graphiti::SearchFilters scout_filter;
    scout_filter.agent_ids = {"scout"};
    auto edges_scout = g.search({.query = query, .filters = scout_filter});
    if (!edges_scout.has_value()) {
        std::cerr << "Search error: " << edges_scout.error().message << "\n";
        return 1;
    }
    print_edges(edges_scout.value(), "SCOUT ONLY (agent_ids=[\"scout\"])", query);

    // 3. Analyst only
    graphiti::SearchFilters analyst_filter;
    analyst_filter.agent_ids = {"analyst"};
    auto edges_analyst = g.search({.query = query, .filters = analyst_filter});
    if (!edges_analyst.has_value()) {
        std::cerr << "Search error: " << edges_analyst.error().message << "\n";
        return 1;
    }
    print_edges(edges_analyst.value(), "ANALYST ONLY (agent_ids=[\"analyst\"])", query);

    // Summary
    shared::print_separator("SUMMARY");
    std::cout << std::format("  All agents:   {} results\n", edges_all.value().size());
    std::cout << std::format("  Scout only:   {} results\n", edges_scout.value().size());
    std::cout << std::format("  Analyst only: {} results\n", edges_analyst.value().size());

    if (edges_all.value().size() > edges_scout.value().size() ||
        edges_all.value().size() > edges_analyst.value().size()) {
        std::cout << "\n  Agent filtering is working -- filtered results are a subset.\n";
    } else if (edges_all.value().empty()) {
        std::cout << "\n  No results at all. The LLM may not have extracted edges for this query.\n";
    } else {
        std::cout << "\n  All counts are the same -- entities may have both agents in agent_ids\n";
        std::cout << "  (which is correct if both agents mentioned the same facts).\n";
    }

    std::cout << "\nRun 3_inspect_graph_data to see the raw agent attribution on nodes and edges.\n";
    return 0;
}
