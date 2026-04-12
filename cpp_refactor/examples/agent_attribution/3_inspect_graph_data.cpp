/*
 * Step 3: Inspect raw graph data to verify agent attribution.
 *
 * This program queries the Kuzu database directly to show:
 *   - Episodes and their agent_id
 *   - Entity nodes and their agent_ids (should accumulate from dedup)
 *   - Entity edges (via RelatesToNode_) and their agent_ids
 *   - Episodic edges (MENTIONS) and their agent_id
 */

#include "shared.h"
#include <driver/kuzu_graph_store.h>

#include <main/kuzu.h>

#include <format>

static std::string get_str(kuzu::common::Value* v) {
    if (!v || v->isNull()) return "";
    return v->getValue<std::string>();
}

static std::vector<std::string> get_string_list(kuzu::common::Value* v) {
    std::vector<std::string> result;
    if (!v || v->isNull()) return result;
    auto size = kuzu::common::NestedVal::getChildrenSize(v);
    for (uint32_t i = 0; i < size; ++i) {
        auto* child = kuzu::common::NestedVal::getChildVal(v, i);
        result.push_back(child->getValue<std::string>());
    }
    return result;
}

static std::string format_ids(const std::vector<std::string>& ids) {
    std::string s = "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) s += ", ";
        s += ids[i];
    }
    s += "]";
    return s;
}

int main() {
    shared::require_api_key(); // validate env before opening DB

    // Open the DB directly via KuzuGraphStore for raw query access
    graphiti::KuzuGraphStore store(shared::db_path());
    auto setup = store.setup_schema();
    if (!setup.has_value()) {
        std::cerr << "Schema setup error: " << setup.error().message << "\n";
        return 1;
    }

    auto* conn = store.connection();

    // --- Episodes ---
    shared::print_separator("EPISODES (each has a single agent_id)");
    {
        auto result = conn->query(
            "MATCH (e:Episodic) RETURN e.name, e.agent_id, e.uuid ORDER BY e.name");
        if (result->isSuccess()) {
            while (result->hasNext()) {
                auto row = result->getNext();
                auto name = get_str(row->getValue(0));
                auto agent_id = get_str(row->getValue(1));
                std::cout << std::format("  {:25s} agent_id={}\n", name, agent_id);
            }
        } else {
            std::cerr << "  Query failed: " << result->getErrorMessage() << "\n";
        }
    }

    // --- Entity Nodes ---
    shared::print_separator("ENTITY NODES (agent_ids accumulates through dedup)");
    {
        auto result = conn->query(
            "MATCH (n:Entity) RETURN n.name, n.agent_ids, n.uuid ORDER BY n.name");
        if (result->isSuccess()) {
            while (result->hasNext()) {
                auto row = result->getNext();
                auto name = get_str(row->getValue(0));
                auto agent_ids = get_string_list(row->getValue(1));
                std::string marker = agent_ids.size() > 1 ? " <-- BOTH AGENTS" : "";
                std::cout << std::format("  {:25s} agent_ids={}{}\n",
                    name, format_ids(agent_ids), marker);
            }
        } else {
            std::cerr << "  Query failed: " << result->getErrorMessage() << "\n";
        }
    }

    // --- Entity Edges (RelatesToNode_) ---
    shared::print_separator("ENTITY EDGES (agent_ids on RelatesToNode_)");
    {
        auto result = conn->query(
            "MATCH (src:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(tgt:Entity) "
            "RETURN src.name, e.name, e.fact, e.agent_ids, tgt.name ORDER BY src.name, tgt.name");
        if (result->isSuccess()) {
            while (result->hasNext()) {
                auto row = result->getNext();
                auto src = get_str(row->getValue(0));
                auto edge_name = get_str(row->getValue(1));
                auto fact = get_str(row->getValue(2));
                auto agent_ids = get_string_list(row->getValue(3));
                auto tgt = get_str(row->getValue(4));
                std::string marker = agent_ids.size() > 1 ? " <-- BOTH" : "";
                std::cout << std::format("  {} --[{}]--> {}\n", src, edge_name, tgt);
                std::cout << std::format("    fact: {}\n", fact);
                std::cout << std::format("    agent_ids: {}{}\n", format_ids(agent_ids), marker);
            }
        } else {
            std::cerr << "  Query failed: " << result->getErrorMessage() << "\n";
        }
    }

    // --- Episodic Edges (MENTIONS) ---
    shared::print_separator("EPISODIC EDGES (MENTIONS - each has single agent_id)");
    {
        auto result = conn->query(
            "MATCH (ep:Episodic)-[m:MENTIONS]->(n:Entity) "
            "RETURN ep.name, n.name, m.agent_id ORDER BY ep.name, n.name");
        if (result->isSuccess()) {
            while (result->hasNext()) {
                auto row = result->getNext();
                auto episode = get_str(row->getValue(0));
                auto entity = get_str(row->getValue(1));
                auto agent_id = get_str(row->getValue(2));
                std::cout << std::format("  {:25s} --MENTIONS--> {:20s} agent_id={}\n",
                    episode, entity, agent_id);
            }
        } else {
            std::cerr << "  Query failed: " << result->getErrorMessage() << "\n";
        }
    }

    // --- Summary stats ---
    shared::print_separator("SUMMARY");
    {
        int multi_agent_count = 0;
        int total_entities = 0;

        auto r1 = conn->query(
            "MATCH (n:Entity) WHERE size(n.agent_ids) > 1 RETURN count(*) AS cnt");
        if (r1->isSuccess() && r1->hasNext()) {
            auto row = r1->getNext();
            multi_agent_count = static_cast<int>(row->getValue(0)->getValue<int64_t>());
        }

        auto r2 = conn->query("MATCH (n:Entity) RETURN count(*) AS cnt");
        if (r2->isSuccess() && r2->hasNext()) {
            auto row = r2->getNext();
            total_entities = static_cast<int>(row->getValue(0)->getValue<int64_t>());
        }

        std::cout << std::format("  Total entity nodes:              {}\n", total_entities);
        std::cout << std::format("  Nodes with multiple agent_ids:    {}\n", multi_agent_count);

        if (multi_agent_count > 0) {
            std::cout << std::format(
                "\n  Dedup merge is working -- {} entities have contributions from both agents.\n",
                multi_agent_count);
        } else {
            std::cout << "\n  No multi-agent nodes. The LLM may have extracted different entity names\n";
            std::cout << "  for each agent, so no dedup merging occurred. This is still valid!\n";
        }
    }

    std::cout << "\nDone!\n";
    return 0;
}
