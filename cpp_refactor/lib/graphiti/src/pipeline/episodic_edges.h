#pragma once

#include <graphiti/error.h>
#include <graphiti/types.h>

#include <string>
#include <vector>

namespace graphiti {
class GraphStore;
} // namespace graphiti

namespace graphiti::pipeline {

// Create MENTIONS edges linking an episode to each entity node it references.
VoidResult create_episodic_edges(
    GraphStore& store,
    const EpisodicNode& episode,
    const std::vector<EntityNode>& nodes
);

} // namespace graphiti::pipeline
