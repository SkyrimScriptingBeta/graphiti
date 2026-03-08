#include <catch2/catch_all.hpp>

#include <graphiti/search_filters.h>

// Access internal filter construction
#include "search/search_filters.h"

using namespace graphiti;

TEST_CASE("comparison_op_to_cypher", "[search_filters]") {
    CHECK(comparison_op_to_cypher(ComparisonOp::eq) == "=");
    CHECK(comparison_op_to_cypher(ComparisonOp::neq) == "<>");
    CHECK(comparison_op_to_cypher(ComparisonOp::gt) == ">");
    CHECK(comparison_op_to_cypher(ComparisonOp::lt) == "<");
    CHECK(comparison_op_to_cypher(ComparisonOp::gte) == ">=");
    CHECK(comparison_op_to_cypher(ComparisonOp::lte) == "<=");
    CHECK(comparison_op_to_cypher(ComparisonOp::is_null) == "IS NULL");
    CHECK(comparison_op_to_cypher(ComparisonOp::is_not_null) == "IS NOT NULL");
}

TEST_CASE("join_filter_clauses: empty", "[search_filters]") {
    CHECK(join_filter_clauses({}).empty());
}

TEST_CASE("join_filter_clauses: single", "[search_filters]") {
    auto result = join_filter_clauses({"e.name = 'foo'"});
    CHECK(result == "e.name = 'foo'");
}

TEST_CASE("join_filter_clauses: multiple", "[search_filters]") {
    auto result = join_filter_clauses({"e.name = 'foo'", "e.group_id = 'bar'"});
    CHECK(result.find("AND") != std::string::npos);
    CHECK(result.find("e.name = 'foo'") != std::string::npos);
    CHECK(result.find("e.group_id = 'bar'") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: empty filters", "[search_filters]") {
    SearchFilters filters;
    auto result = build_edge_filter_clauses(filters);
    CHECK(result.clauses.empty());
}

TEST_CASE("build_edge_filter_clauses: edge_types", "[search_filters]") {
    SearchFilters filters;
    filters.edge_types = {"WORKS_AT", "KNOWS"};
    auto result = build_edge_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 1);
    CHECK(result.clauses[0].find("e.name IN") != std::string::npos);
    CHECK(result.clauses[0].find("WORKS_AT") != std::string::npos);
    CHECK(result.clauses[0].find("KNOWS") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: edge_uuids", "[search_filters]") {
    SearchFilters filters;
    filters.edge_uuids = {"uuid-1", "uuid-2"};
    auto result = build_edge_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 1);
    CHECK(result.clauses[0].find("e.uuid IN") != std::string::npos);
    CHECK(result.clauses[0].find("uuid-1") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: node_labels on both endpoints", "[search_filters]") {
    SearchFilters filters;
    filters.node_labels = {"Person"};
    auto result = build_edge_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 2);
    CHECK(result.clauses[0].find("n.labels") != std::string::npos);
    CHECK(result.clauses[1].find("m.labels") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: temporal valid_at", "[search_filters]") {
    using namespace std::chrono;
    auto now = system_clock::now();

    SearchFilters filters;
    // Single AND group: valid_at >= now
    filters.valid_at = DateFilterClause{
        {{DateFilter{now, ComparisonOp::gte}}}
    };

    auto result = build_edge_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 1);
    CHECK(result.clauses[0].find("e.valid_at") != std::string::npos);
    CHECK(result.clauses[0].find(">=") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: compound temporal OR-of-AND", "[search_filters]") {
    using namespace std::chrono;
    auto t1 = system_clock::now();
    auto t2 = t1 + hours(24);

    SearchFilters filters;
    // (valid_at >= t1 AND valid_at < t2) OR (valid_at IS NULL)
    filters.valid_at = DateFilterClause{
        {
            {DateFilter{t1, ComparisonOp::gte}, DateFilter{t2, ComparisonOp::lt}}
        },
        {
            {DateFilter{std::nullopt, ComparisonOp::is_null}}
        }
    };

    auto result = build_edge_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 1);
    CHECK(result.clauses[0].find(" OR ") != std::string::npos);
    CHECK(result.clauses[0].find("IS NULL") != std::string::npos);
    CHECK(result.clauses[0].find(">=") != std::string::npos);
    CHECK(result.clauses[0].find("<") != std::string::npos);
}

TEST_CASE("build_edge_filter_clauses: combined filters", "[search_filters]") {
    using namespace std::chrono;

    SearchFilters filters;
    filters.edge_types = {"WORKS_AT"};
    filters.node_labels = {"Person"};
    filters.created_at = DateFilterClause{
        {{DateFilter{system_clock::now(), ComparisonOp::gte}}}
    };

    auto result = build_edge_filter_clauses(filters);
    // edge_types(1) + node_labels(2, n and m) + created_at(1) = 4
    CHECK(result.clauses.size() == 4);
}

TEST_CASE("build_node_filter_clauses: empty filters", "[search_filters]") {
    SearchFilters filters;
    auto result = build_node_filter_clauses(filters);
    CHECK(result.clauses.empty());
}

TEST_CASE("build_node_filter_clauses: node_labels", "[search_filters]") {
    SearchFilters filters;
    filters.node_labels = {"Person", "Organization"};
    auto result = build_node_filter_clauses(filters);
    REQUIRE(result.clauses.size() == 1);
    CHECK(result.clauses[0].find("list_has_all") != std::string::npos);
    CHECK(result.clauses[0].find("Person") != std::string::npos);
    CHECK(result.clauses[0].find("Organization") != std::string::npos);
}

TEST_CASE("build_node_filter_clauses: ignores edge-only filters", "[search_filters]") {
    SearchFilters filters;
    filters.edge_types = {"WORKS_AT"};
    filters.edge_uuids = {"uuid-1"};
    auto result = build_node_filter_clauses(filters);
    CHECK(result.clauses.empty());
}
