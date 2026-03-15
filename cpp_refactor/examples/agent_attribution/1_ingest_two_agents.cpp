/*
 * Step 1: Ingest episodes from two different agents into the same project.
 *
 * Agent "scout" discovers information about people and places.
 * Agent "analyst" analyzes relationships and draws conclusions.
 *
 * Both agents mention "Alice" -- so the Alice entity should end up
 * with agent_ids=["scout", "analyst"] after dedup merges.
 */

#include "shared.h"

#include <chrono>
#include <filesystem>
#include <format>

struct Episode {
    std::string name;
    std::string body;
    std::string source;
    std::string agent_id;
};

int main() {
    std::vector<Episode> episodes = {
        // --- Scout agent's observations ---
        {"scout-obs-1",
         "Alice works at Acme Corp as a software engineer. She started last month.",
         "field report", "scout"},
        {"scout-obs-2",
         "Bob is the CTO of Acme Corp. He founded the company in 2019.",
         "field report", "scout"},
        {"scout-obs-3",
         "Acme Corp is headquartered in Denver, Colorado.",
         "field report", "scout"},
        // --- Analyst agent's conclusions ---
        {"analyst-note-1",
         "Alice reports to Bob at Acme Corp. She is on the infrastructure team.",
         "analysis", "analyst"},
        {"analyst-note-2",
         "Carol is a product manager at Acme Corp. She works closely with Alice.",
         "analysis", "analyst"},
    };

    // Wipe any previous test DB
    auto db = shared::db_path();
    if (std::filesystem::exists(db)) {
        std::cout << "Removing old test database at " << db << "\n";
        std::filesystem::remove_all(db);
    }
    // Also remove .lock file Kuzu sometimes creates
    auto lock_path = db + ".lock";
    if (std::filesystem::exists(lock_path)) {
        std::filesystem::remove(lock_path);
    }

    auto g = shared::make_graphiti();
    auto now = std::chrono::system_clock::now();

    std::cout << std::format("\nIngesting {} episodes (group_id=\"\")...\n\n", episodes.size());

    for (size_t i = 0; i < episodes.size(); ++i) {
        auto& ep = episodes[i];
        std::cout << std::format("  [{}/{}] agent={:12s} name=\"{}\"\n",
            i + 1, episodes.size(), ep.agent_id, ep.name);

        auto result = g.add_episode({
            .name = ep.name,
            .body = ep.body,
            .source_description = ep.source,
            .reference_time = now + std::chrono::seconds(i * 60), // stagger times
            .agent_id = ep.agent_id,
        });

        if (result.has_value()) {
            auto& r = result.value();
            std::cout << std::format("           -> {} nodes, {} edges extracted\n",
                r.nodes.size(), r.edges.size());
        } else {
            std::cout << std::format("           -> ERROR: {}\n", result.error().message);
        }
    }

    std::cout << "\nDone! Database saved to: " << db << "\n";
    std::cout << "Run 2_search_with_agent_filter next.\n";
    return 0;
}
