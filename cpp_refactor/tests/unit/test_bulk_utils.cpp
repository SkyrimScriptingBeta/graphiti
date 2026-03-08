#include <catch2/catch_test_macros.hpp>
#include <pipeline/bulk_utils.h>

using namespace graphiti;
using namespace graphiti::pipeline;

// ============================================================================
// build_directed_uuid_map
// ============================================================================

TEST_CASE("build_directed_uuid_map: empty input", "[bulk_utils]") {
    auto map = build_directed_uuid_map({});
    REQUIRE(map.empty());
}

TEST_CASE("build_directed_uuid_map: single pair", "[bulk_utils]") {
    auto map = build_directed_uuid_map({{"new1", "existing1"}});
    REQUIRE(map["new1"] == "existing1");
    REQUIRE(map["existing1"] == "existing1");
}

TEST_CASE("build_directed_uuid_map: chain collapses to final target", "[bulk_utils]") {
    // new1 -> mid -> existing
    auto map = build_directed_uuid_map({{"new1", "mid"}, {"mid", "existing"}});
    REQUIRE(map["new1"] == "existing");
    REQUIRE(map["mid"] == "existing");
    REQUIRE(map["existing"] == "existing");
}

TEST_CASE("build_directed_uuid_map: multiple sources to same target", "[bulk_utils]") {
    auto map = build_directed_uuid_map({{"a", "canonical"}, {"b", "canonical"}});
    REQUIRE(map["a"] == "canonical");
    REQUIRE(map["b"] == "canonical");
    REQUIRE(map["canonical"] == "canonical");
}

TEST_CASE("build_directed_uuid_map: preserves direction (not lexicographic)", "[bulk_utils]") {
    // Even though "zzz" > "aaa", direction says aaa -> zzz
    auto map = build_directed_uuid_map({{"aaa", "zzz"}});
    REQUIRE(map["aaa"] == "zzz");
}

// ============================================================================
// compress_uuid_map
// ============================================================================

TEST_CASE("compress_uuid_map: empty input", "[bulk_utils]") {
    auto map = compress_uuid_map({});
    REQUIRE(map.empty());
}

TEST_CASE("compress_uuid_map: single pair picks lex smaller", "[bulk_utils]") {
    auto map = compress_uuid_map({{"zzz", "aaa"}});
    REQUIRE(map["zzz"] == "aaa");
    REQUIRE(map["aaa"] == "aaa");
}

TEST_CASE("compress_uuid_map: transitive closure", "[bulk_utils]") {
    auto map = compress_uuid_map({{"c", "b"}, {"b", "a"}});
    REQUIRE(map["a"] == "a");
    REQUIRE(map["b"] == "a");
    REQUIRE(map["c"] == "a");
}

TEST_CASE("compress_uuid_map: separate components", "[bulk_utils]") {
    auto map = compress_uuid_map({{"b", "a"}, {"d", "c"}});
    REQUIRE(map["a"] == "a");
    REQUIRE(map["b"] == "a");
    REQUIRE(map["c"] == "c");
    REQUIRE(map["d"] == "c");
}

// ============================================================================
// resolve_edge_pointers
// ============================================================================

TEST_CASE("resolve_edge_pointers: remaps source and target", "[bulk_utils]") {
    EntityEdge edge;
    edge.uuid = "edge1";
    edge.source_node_uuid = "old_src";
    edge.target_node_uuid = "old_tgt";
    edge.fact = "test fact";

    std::vector<EntityEdge> edges = {edge};
    std::unordered_map<std::string, std::string> uuid_map = {
        {"old_src", "new_src"},
        {"old_tgt", "new_tgt"},
    };

    resolve_edge_pointers(edges, uuid_map);
    REQUIRE(edges[0].source_node_uuid == "new_src");
    REQUIRE(edges[0].target_node_uuid == "new_tgt");
}

TEST_CASE("resolve_edge_pointers: unmapped UUIDs stay unchanged", "[bulk_utils]") {
    EntityEdge edge;
    edge.uuid = "edge1";
    edge.source_node_uuid = "src";
    edge.target_node_uuid = "tgt";
    edge.fact = "test";

    std::vector<EntityEdge> edges = {edge};
    std::unordered_map<std::string, std::string> uuid_map = {
        {"other", "mapped"},
    };

    resolve_edge_pointers(edges, uuid_map);
    REQUIRE(edges[0].source_node_uuid == "src");
    REQUIRE(edges[0].target_node_uuid == "tgt");
}

TEST_CASE("resolve_edge_pointers: empty edges", "[bulk_utils]") {
    std::vector<EntityEdge> edges;
    std::unordered_map<std::string, std::string> uuid_map = {{"a", "b"}};
    resolve_edge_pointers(edges, uuid_map);
    REQUIRE(edges.empty());
}

TEST_CASE("resolve_edge_pointers: partial remap (only source)", "[bulk_utils]") {
    EntityEdge edge;
    edge.uuid = "e";
    edge.source_node_uuid = "old";
    edge.target_node_uuid = "keep";
    edge.fact = "f";

    std::vector<EntityEdge> edges = {edge};
    resolve_edge_pointers(edges, {{"old", "new"}});
    REQUIRE(edges[0].source_node_uuid == "new");
    REQUIRE(edges[0].target_node_uuid == "keep");
}
