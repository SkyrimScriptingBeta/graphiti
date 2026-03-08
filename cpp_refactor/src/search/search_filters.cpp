#include "search_filters.h"

#include "utils/datetime.h"

#include <format>

namespace graphiti {

std::string_view comparison_op_to_cypher(ComparisonOp op) {
    switch (op) {
        case ComparisonOp::eq:           return "=";
        case ComparisonOp::neq:          return "<>";
        case ComparisonOp::gt:           return ">";
        case ComparisonOp::lt:           return "<";
        case ComparisonOp::gte:          return ">=";
        case ComparisonOp::lte:          return "<=";
        case ComparisonOp::is_null:      return "IS NULL";
        case ComparisonOp::is_not_null:  return "IS NOT NULL";
    }
    return "=";
}

// Build a date filter expression for a single temporal field.
// The DateFilterClause is OR-of-ANDs: [[a AND b], [c AND d]] => (a AND b) OR (c AND d)
static std::string build_date_filter_expr(
    const std::string& field_name,
    const DateFilterClause& clause,
    int& param_counter
) {
    std::vector<std::string> or_parts;

    for (auto& and_group : clause) {
        std::vector<std::string> and_parts;
        for (auto& filter : and_group) {
            auto op = comparison_op_to_cypher(filter.op);
            if (filter.op == ComparisonOp::is_null || filter.op == ComparisonOp::is_not_null) {
                and_parts.push_back(std::format("{} {}", field_name, op));
            } else if (filter.date.has_value()) {
                auto ts = datetime::to_iso8601(filter.date.value());
                and_parts.push_back(std::format(
                    "{} {} timestamp('{}')", field_name, op, ts
                ));
                ++param_counter;
            }
        }
        if (!and_parts.empty()) {
            std::string combined;
            for (size_t i = 0; i < and_parts.size(); ++i) {
                if (i > 0) combined += " AND ";
                combined += and_parts[i];
            }
            or_parts.push_back(std::format("({})", combined));
        }
    }

    if (or_parts.empty()) return {};

    std::string result;
    for (size_t i = 0; i < or_parts.size(); ++i) {
        if (i > 0) result += " OR ";
        result += or_parts[i];
    }
    if (or_parts.size() > 1) {
        result = std::format("({})", result);
    }
    return result;
}

FilterQueryResult build_edge_filter_clauses(const SearchFilters& filters) {
    FilterQueryResult result;
    int param_counter = 0;

    // Edge type filter
    if (!filters.edge_types.empty()) {
        std::string types_list;
        for (size_t i = 0; i < filters.edge_types.size(); ++i) {
            if (i > 0) types_list += ", ";
            types_list += std::format("'{}'", filters.edge_types[i]);
        }
        result.clauses.push_back(std::format("e.name IN [{}]", types_list));
    }

    // Edge UUID filter
    if (!filters.edge_uuids.empty()) {
        std::string uuids_list;
        for (size_t i = 0; i < filters.edge_uuids.size(); ++i) {
            if (i > 0) uuids_list += ", ";
            uuids_list += std::format("'{}'", filters.edge_uuids[i]);
        }
        result.clauses.push_back(std::format("e.uuid IN [{}]", uuids_list));
    }

    // Node label filter (on both endpoints)
    if (!filters.node_labels.empty()) {
        std::string labels_list;
        for (size_t i = 0; i < filters.node_labels.size(); ++i) {
            if (i > 0) labels_list += ", ";
            labels_list += std::format("'{}'", filters.node_labels[i]);
        }
        result.clauses.push_back(std::format("list_has_all(n.labels, [{}])", labels_list));
        result.clauses.push_back(std::format("list_has_all(m.labels, [{}])", labels_list));
    }

    // Temporal filters
    if (filters.valid_at.has_value()) {
        auto expr = build_date_filter_expr("e.valid_at", filters.valid_at.value(), param_counter);
        if (!expr.empty()) result.clauses.push_back(std::move(expr));
    }
    if (filters.invalid_at.has_value()) {
        auto expr = build_date_filter_expr("e.invalid_at", filters.invalid_at.value(), param_counter);
        if (!expr.empty()) result.clauses.push_back(std::move(expr));
    }
    if (filters.created_at.has_value()) {
        auto expr = build_date_filter_expr("e.created_at", filters.created_at.value(), param_counter);
        if (!expr.empty()) result.clauses.push_back(std::move(expr));
    }
    if (filters.expired_at.has_value()) {
        auto expr = build_date_filter_expr("e.expired_at", filters.expired_at.value(), param_counter);
        if (!expr.empty()) result.clauses.push_back(std::move(expr));
    }

    return result;
}

FilterQueryResult build_node_filter_clauses(const SearchFilters& filters) {
    FilterQueryResult result;

    // Node label filter
    if (!filters.node_labels.empty()) {
        std::string labels_list;
        for (size_t i = 0; i < filters.node_labels.size(); ++i) {
            if (i > 0) labels_list += ", ";
            labels_list += std::format("'{}'", filters.node_labels[i]);
        }
        result.clauses.push_back(std::format("list_has_all(n.labels, [{}])", labels_list));
    }

    return result;
}

std::string join_filter_clauses(const std::vector<std::string>& clauses) {
    if (clauses.empty()) return {};

    std::string result;
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0) result += "\nAND ";
        result += clauses[i];
    }
    return result;
}

} // namespace graphiti
