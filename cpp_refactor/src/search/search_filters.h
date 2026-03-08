#pragma once

#include <graphiti/search_filters.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace graphiti {

// Result of building filter queries: a list of WHERE clause fragments
// and a map of parameter name -> parameter value (as strings for Cypher injection).
struct FilterQueryResult {
    std::vector<std::string> clauses;    // e.g. "e.name IN $edge_types"
    // Parameters are injected directly into the Cypher query string
    // because Kuzu parameterized queries don't support list params well in all contexts.
    // The clauses reference $param_name placeholders that must be filled.
};

// Build WHERE clause fragments for edge searches.
// Variables: e = RelatesToNode_ (edge proxy), n = source Entity, m = target Entity
FilterQueryResult build_edge_filter_clauses(const SearchFilters& filters);

// Build WHERE clause fragments for node searches.
// Variables: n = Entity node
FilterQueryResult build_node_filter_clauses(const SearchFilters& filters);

// Join filter clauses into a single AND-joined WHERE fragment.
// Returns empty string if no clauses.
std::string join_filter_clauses(const std::vector<std::string>& clauses);

} // namespace graphiti
