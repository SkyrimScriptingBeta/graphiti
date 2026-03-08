#include <catch2/catch_all.hpp>
#include <cstdlib>

TEST_CASE("Integration test placeholder", "[integration]") {
    auto* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key || std::string(api_key).empty()) {
        SKIP("OPENAI_API_KEY not set, skipping integration tests");
    }
    REQUIRE(true);
}
