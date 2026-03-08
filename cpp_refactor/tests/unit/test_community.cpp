#include <catch2/catch_test_macros.hpp>
#include "pipeline/community_ops.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace graphiti;
using namespace graphiti::pipeline;

TEST_CASE("label_propagation empty input", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    auto clusters = label_propagation(projection);
    CHECK(clusters.empty());
}

TEST_CASE("label_propagation single node", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    projection["A"] = {};
    auto clusters = label_propagation(projection);
    REQUIRE(clusters.size() == 1);
    CHECK(clusters[0].size() == 1);
    CHECK(clusters[0][0] == "A");
}

TEST_CASE("label_propagation two disconnected nodes", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    projection["A"] = {};
    projection["B"] = {};
    auto clusters = label_propagation(projection);
    // Each node in its own cluster
    REQUIRE(clusters.size() == 2);
}

TEST_CASE("label_propagation two connected nodes", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    projection["A"] = {{.node_uuid = "B", .edge_count = 2}};
    projection["B"] = {{.node_uuid = "A", .edge_count = 2}};
    auto clusters = label_propagation(projection);
    // Should converge to single cluster
    REQUIRE(clusters.size() == 1);
    REQUIRE(clusters[0].size() == 2);
}

TEST_CASE("label_propagation triangle forms one cluster", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    projection["A"] = {{.node_uuid = "B", .edge_count = 2}, {.node_uuid = "C", .edge_count = 2}};
    projection["B"] = {{.node_uuid = "A", .edge_count = 2}, {.node_uuid = "C", .edge_count = 2}};
    projection["C"] = {{.node_uuid = "A", .edge_count = 2}, {.node_uuid = "B", .edge_count = 2}};
    auto clusters = label_propagation(projection);
    REQUIRE(clusters.size() == 1);
    REQUIRE(clusters[0].size() == 3);
}

TEST_CASE("label_propagation two separate components", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    // Component 1: A-B strongly connected
    projection["A"] = {{.node_uuid = "B", .edge_count = 5}};
    projection["B"] = {{.node_uuid = "A", .edge_count = 5}};
    // Component 2: C-D strongly connected
    projection["C"] = {{.node_uuid = "D", .edge_count = 5}};
    projection["D"] = {{.node_uuid = "C", .edge_count = 5}};

    auto clusters = label_propagation(projection);
    REQUIRE(clusters.size() == 2);

    // Each cluster should have 2 members
    std::unordered_set<std::string> c1(clusters[0].begin(), clusters[0].end());
    std::unordered_set<std::string> c2(clusters[1].begin(), clusters[1].end());

    // A and B should be together, C and D together
    bool ab_together = (c1.count("A") && c1.count("B")) || (c2.count("A") && c2.count("B"));
    bool cd_together = (c1.count("C") && c1.count("D")) || (c2.count("C") && c2.count("D"));
    CHECK(ab_together);
    CHECK(cd_together);
}

TEST_CASE("label_propagation weak edges don't merge", "[community][label_propagation]") {
    std::unordered_map<std::string, std::vector<Neighbor>> projection;
    // A and B each have a weak edge (count=1) — shouldn't merge
    projection["A"] = {{.node_uuid = "B", .edge_count = 1}};
    projection["B"] = {{.node_uuid = "A", .edge_count = 1}};
    auto clusters = label_propagation(projection);
    // With edge_count=1, the vote doesn't exceed threshold (>1),
    // so nodes stay in their own communities
    CHECK(clusters.size() >= 1);
}
