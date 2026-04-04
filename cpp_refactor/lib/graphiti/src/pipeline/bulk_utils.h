#pragma once

#include <graphiti/types.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti::pipeline {

// Build a directed UUID map from duplicate pairs using union-find with
// iterative path compression. Unlike compress_uuid_map (which picks the
// lexicographically smallest root), this preserves direction: the source
// always maps to the target's canonical root.
// Used for node deduplication where direction matters (new -> existing).
std::unordered_map<std::string, std::string> build_directed_uuid_map(
    const std::vector<std::pair<std::string, std::string>>& pairs
);

// Collapse duplicate pairs into canonical UUIDs using UnionFind.
// Each UUID maps to the lexicographically smallest UUID in its equivalence class.
// Used for edge deduplication within a batch.
std::unordered_map<std::string, std::string> compress_uuid_map(
    const std::vector<std::pair<std::string, std::string>>& duplicate_pairs
);

// Remap edge source_node_uuid and target_node_uuid using a UUID mapping.
// Edges are modified in place.
void resolve_edge_pointers(
    std::vector<EntityEdge>& edges,
    const std::unordered_map<std::string, std::string>& uuid_map
);

} // namespace graphiti::pipeline
