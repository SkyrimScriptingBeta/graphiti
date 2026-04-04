#include "bulk_utils.h"

#include "utils/union_find.h"

namespace graphiti::pipeline {

std::unordered_map<std::string, std::string> build_directed_uuid_map(
    const std::vector<std::pair<std::string, std::string>>& pairs
) {
    // Directed union-find: source always maps to target's canonical root.
    // Uses iterative path compression.
    std::unordered_map<std::string, std::string> parent;

    auto find = [&](std::string uuid) -> std::string {
        if (parent.find(uuid) == parent.end()) parent[uuid] = uuid;
        std::string root = uuid;
        while (parent[root] != root) root = parent[root];
        // Path compression
        while (parent[uuid] != root) {
            auto next = parent[uuid];
            parent[uuid] = root;
            uuid = next;
        }
        return root;
    };

    for (auto& [source, target] : pairs) {
        if (parent.find(source) == parent.end()) parent[source] = source;
        if (parent.find(target) == parent.end()) parent[target] = target;
        parent[find(source)] = find(target);
    }

    std::unordered_map<std::string, std::string> result;
    for (auto& [uuid, _] : parent) {
        result[uuid] = find(uuid);
    }
    return result;
}

std::unordered_map<std::string, std::string> compress_uuid_map(
    const std::vector<std::pair<std::string, std::string>>& duplicate_pairs
) {
    std::vector<std::string> all_uuids;
    for (auto& [a, b] : duplicate_pairs) {
        all_uuids.push_back(a);
        all_uuids.push_back(b);
    }

    UnionFind uf(all_uuids);
    for (auto& [a, b] : duplicate_pairs) {
        uf.unite(a, b);
    }

    return uf.resolve_all();
}

void resolve_edge_pointers(
    std::vector<EntityEdge>& edges,
    const std::unordered_map<std::string, std::string>& uuid_map
) {
    for (auto& edge : edges) {
        if (auto it = uuid_map.find(edge.source_node_uuid); it != uuid_map.end()) {
            edge.source_node_uuid = it->second;
        }
        if (auto it = uuid_map.find(edge.target_node_uuid); it != uuid_map.end()) {
            edge.target_node_uuid = it->second;
        }
    }
}

} // namespace graphiti::pipeline
