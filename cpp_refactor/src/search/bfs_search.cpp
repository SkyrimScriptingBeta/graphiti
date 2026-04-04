#include "bfs_search.h"

#include <graphiti/graph_store.h>
#include <graphiti/callsite_log.h>

namespace graphiti {

Result<std::vector<EntityEdge>> edge_bfs_search(
    GraphStore& store,
    const std::vector<std::string>& origin_uuids,
    int max_depth,
    const SearchFilters* filters,
    std::string_view group_id,
    int limit
) {
    graphiti::log_callsite("bfs-edge-traversal");
    return store.search_edges_bfs(origin_uuids, group_id, max_depth, limit, filters);
}

Result<std::vector<EntityNode>> node_bfs_search(
    GraphStore& store,
    const std::vector<std::string>& origin_uuids,
    int max_depth,
    const SearchFilters* filters,
    std::string_view group_id,
    int limit
) {
    graphiti::log_callsite("bfs-node-traversal");
    return store.search_nodes_bfs(origin_uuids, group_id, max_depth, limit, filters);
}

} // namespace graphiti
