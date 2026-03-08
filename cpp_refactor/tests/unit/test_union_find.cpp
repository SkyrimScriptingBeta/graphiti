#include <catch2/catch_test_macros.hpp>
#include <utils/union_find.h>

using namespace graphiti;

TEST_CASE("UnionFind: each element starts as its own root", "[union_find]") {
    UnionFind uf({"a", "b", "c"});
    REQUIRE(uf.find("a") == "a");
    REQUIRE(uf.find("b") == "b");
    REQUIRE(uf.find("c") == "c");
}

TEST_CASE("UnionFind: union picks lexicographically smaller root", "[union_find]") {
    UnionFind uf({"z", "a"});
    uf.unite("z", "a");
    REQUIRE(uf.find("z") == "a");
    REQUIRE(uf.find("a") == "a");
}

TEST_CASE("UnionFind: transitive chain resolves correctly", "[union_find]") {
    UnionFind uf({"c", "b", "a"});
    uf.unite("c", "b");
    uf.unite("b", "a");
    REQUIRE(uf.find("c") == "a");
    REQUIRE(uf.find("b") == "a");
    REQUIRE(uf.find("a") == "a");
}

TEST_CASE("UnionFind: resolve_all returns complete mapping", "[union_find]") {
    UnionFind uf({"x", "y", "z"});
    uf.unite("x", "y");
    uf.unite("y", "z");
    auto map = uf.resolve_all();
    REQUIRE(map.size() == 3);
    REQUIRE(map["x"] == "x");
    REQUIRE(map["y"] == "x");
    REQUIRE(map["z"] == "x");
}

TEST_CASE("UnionFind: separate components remain separate", "[union_find]") {
    UnionFind uf({"a1", "a2", "b1", "b2"});
    uf.unite("a1", "a2");
    uf.unite("b1", "b2");
    REQUIRE(uf.find("a1") == uf.find("a2"));
    REQUIRE(uf.find("b1") == uf.find("b2"));
    REQUIRE(uf.find("a1") != uf.find("b1"));
}

TEST_CASE("UnionFind: add and unite new elements", "[union_find]") {
    UnionFind uf;
    uf.add("one");
    uf.add("two");
    uf.unite("one", "two");
    REQUIRE(uf.find("one") == uf.find("two"));
}

TEST_CASE("UnionFind: single element", "[union_find]") {
    UnionFind uf({"solo"});
    REQUIRE(uf.find("solo") == "solo");
    auto map = uf.resolve_all();
    REQUIRE(map.size() == 1);
    REQUIRE(map["solo"] == "solo");
}

TEST_CASE("UnionFind: UUID-like strings", "[union_find]") {
    UnionFind uf({"550e8400-e29b-41d4-a716-446655440000",
                  "123e4567-e89b-12d3-a456-426614174000",
                  "f47ac10b-58cc-4372-a567-0e02b2c3d479"});
    uf.unite("550e8400-e29b-41d4-a716-446655440000",
             "123e4567-e89b-12d3-a456-426614174000");
    // 123... < 550... so 123... is root
    REQUIRE(uf.find("550e8400-e29b-41d4-a716-446655440000") ==
            "123e4567-e89b-12d3-a456-426614174000");
    // f47... is still separate
    REQUIRE(uf.find("f47ac10b-58cc-4372-a567-0e02b2c3d479") ==
            "f47ac10b-58cc-4372-a567-0e02b2c3d479");
}
