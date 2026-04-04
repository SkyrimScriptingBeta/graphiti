#include <graphiti/search_filters.h>

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

} // namespace graphiti
