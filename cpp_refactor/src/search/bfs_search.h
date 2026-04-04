#pragma once

#include <graphiti/error.h>
#include <graphiti/search_filters.h>
#include <graphiti/types.h>

#include <string>
#include <vector>

namespace graphiti {

class GraphStore;

// BFS graph traversal search for edges.
// Traverses from origin nodes through RELATES_TO relationships,
// collecting RelatesToNode_ intermediate nodes (which represent edges).
// Each logical hop = 2 physical hops in Kuzu due to the intermediate node pattern.
Result<std::vector<EntityEdge>> edge_bfs_search(
    GraphStore& store,
    const std::vector<std::string>& origin_uuids,
    int max_depth = 3,
    const SearchFilters* filters = nullptr,
    std::string_view group_id = "",
    int limit = 20
);

// BFS graph traversal search for nodes.
// Traverses from origin nodes, collecting Entity nodes within max_depth hops.
Result<std::vector<EntityNode>> node_bfs_search(
    GraphStore& store,
    const std::vector<std::string>& origin_uuids,
    int max_depth = 3,
    const SearchFilters* filters = nullptr,
    std::string_view group_id = "",
    int limit = 20
);

} // namespace graphiti
