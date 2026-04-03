#include "episodic_edges.h"

#include "driver/kuzu_driver.h"
#include "utils/uuid.h"

#include <graphiti/callsite_log.h>

namespace graphiti::pipeline {

VoidResult create_episodic_edges(
    KuzuDriver& driver,
    const EpisodicNode& episode,
    const std::vector<EntityNode>& nodes
) {
    auto now = std::chrono::system_clock::now();

    for (auto& node : nodes) {
        EpisodicEdge edge;
        edge.uuid = uuid::generate();
        edge.group_id = episode.group_id;
        edge.source_node_uuid = episode.uuid;
        edge.target_node_uuid = node.uuid;
        edge.created_at = now;
        edge.agent_id = episode.agent_id;
        edge.source_id = episode.source_id;
        edge.source_context = episode.source_context;
        edge.participant_ids = episode.participant_ids;

        graphiti::log_callsite("episodic-edges-save-mentions");
        auto result = driver.save_episodic_edge(edge);
        if (!result.has_value()) {
            return result;
        }
    }

    return {};
}

} // namespace graphiti::pipeline
