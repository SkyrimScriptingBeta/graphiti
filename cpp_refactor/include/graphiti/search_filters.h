#pragma once

#include <graphiti/fwd.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace graphiti {

enum class ComparisonOp {
    eq,              // =
    neq,             // <>
    gt,              // >
    lt,              // <
    gte,             // >=
    lte,             // <=
    is_null,         // IS NULL
    is_not_null,     // IS NOT NULL
};

struct DateFilter {
    std::optional<TimePoint> date;  // Not needed for IS NULL / IS NOT NULL
    ComparisonOp op;
};

using PropertyValue = std::variant<std::string, int64_t, double>;

struct PropertyFilter {
    std::string property_name;
    std::optional<PropertyValue> value;  // Not needed for IS NULL / IS NOT NULL
    ComparisonOp op;
};

// Date filter clauses use nested AND/OR:
//   outer vector = OR groups
//   inner vector = AND within each group
// Example: ((valid_at >= A AND valid_at < B) OR (valid_at >= C))
using DateFilterClause = std::vector<std::vector<DateFilter>>;

struct SearchFilters {
    // Node label filter (applies to entity nodes)
    std::vector<std::string> node_labels;

    // Edge type filter (filters on edge name/relation type)
    std::vector<std::string> edge_types;

    // Temporal filters on edges (AND/OR compound expressions)
    std::optional<DateFilterClause> valid_at;
    std::optional<DateFilterClause> invalid_at;
    std::optional<DateFilterClause> created_at;
    std::optional<DateFilterClause> expired_at;

    // Restrict to specific edge UUIDs (used internally for dedup)
    std::vector<std::string> edge_uuids;

    // Generic property filters (future use)
    std::vector<PropertyFilter> property_filters;

    // Agent attribution filter (optional, empty = no filter)
    // When set, results are filtered to items attributed to any of these agents.
    // Uses: any(aid IN e.agent_ids WHERE list_contains($agent_ids, aid))
    std::vector<std::string> agent_ids;
};

// Convert ComparisonOp to Cypher operator string
std::string_view comparison_op_to_cypher(ComparisonOp op);

} // namespace graphiti
