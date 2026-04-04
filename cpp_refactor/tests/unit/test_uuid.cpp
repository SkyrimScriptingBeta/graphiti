#include <catch2/catch_all.hpp>

#include <regex>
#include <set>
#include <string>

// Access internal header for testing
#include "utils/uuid.h"

using namespace graphiti;

TEST_CASE("UUID format is valid v4", "[uuid]") {
    auto id = uuid::generate();

    // UUID v4 format: 8-4-4-4-12 hex characters
    std::regex uuid_re(R"([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})");
    REQUIRE(std::regex_match(id, uuid_re));
}

TEST_CASE("UUID uniqueness across 1000 generations", "[uuid]") {
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        auto id = uuid::generate();
        REQUIRE(ids.insert(id).second);  // insert returns false if duplicate
    }
    REQUIRE(ids.size() == 1000);
}

TEST_CASE("UUID length is 36 characters", "[uuid]") {
    auto id = uuid::generate();
    REQUIRE(id.size() == 36);
}
