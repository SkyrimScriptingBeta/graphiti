#include "bfs_search.h"

#include "driver/kuzu_driver.h"
#include <graphiti/callsite_log.h>

namespace graphiti {

Result<std::vector<EntityEdge>> edge_bfs_search(
    KuzuDriver& driver,
    const std::vector<std::string>& origin_uuids,
    int max_depth,
    const SearchFilters* filters,
    std::string_view group_id,
    int limit
) {
    graphiti::log_callsite("bfs-edge-traversal");
    return driver.search_entity_edges_bfs(origin_uuids, group_id, max_depth, limit, filters);
}

Result<std::vector<EntityNode>> node_bfs_search(
    KuzuDriver& driver,
    const std::vector<std::string>& origin_uuids,
    int max_depth,
    const SearchFilters* filters,
    std::string_view group_id,
    int limit
) {
    graphiti::log_callsite("bfs-node-traversal");
    return driver.search_entity_nodes_bfs(origin_uuids, group_id, max_depth, limit, filters);
}

} // namespace graphiti
