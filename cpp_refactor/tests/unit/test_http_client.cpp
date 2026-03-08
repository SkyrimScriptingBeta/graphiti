#include <catch2/catch_all.hpp>

#include "http/http_client.h"

using namespace graphiti;

// Unit tests for HttpClient — no network calls, just verifying construction
// and error handling. Integration tests will make real HTTPS requests.

TEST_CASE("HttpClient construction", "[http]") {
    SECTION("default construction") {
        HttpClient client;
        // Should not crash, no connections until first use
    }

    SECTION("move construction") {
        HttpClient a;
        HttpClient b(std::move(a));
        // b should be usable, a should be empty
    }

    SECTION("move assignment") {
        HttpClient a;
        HttpClient b;
        b = std::move(a);
    }
}

TEST_CASE("HttpClient error on bad host", "[http]") {
    HttpClient client;
    auto result = client.post_json(
        "https://localhost:1", // nothing listening
        "/test",
        {},
        "{}",
        2 // short timeout
    );
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::http_error);
    CHECK(!result.error().message.empty());
}
