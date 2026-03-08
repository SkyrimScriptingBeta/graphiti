#include <catch2/catch_all.hpp>

#include <chrono>
#include <string>

#include "../../src/utils/datetime.h"

using namespace graphiti;

TEST_CASE("ISO8601 round-trip", "[datetime]") {
    auto now = datetime::utc_now();
    auto str = datetime::to_iso8601(now);
    auto restored = datetime::from_iso8601(str);

    // Should be within 1 microsecond (our precision)
    auto diff =
        std::chrono::duration_cast<std::chrono::microseconds>(now - restored).count();
    REQUIRE(std::abs(diff) <= 1);
}

TEST_CASE("ISO8601 parsing known date", "[datetime]") {
    auto tp = datetime::from_iso8601("2024-01-15T10:30:00.000000Z");
    auto str = datetime::to_iso8601(tp);
    REQUIRE(str == "2024-01-15T10:30:00.000000Z");
}

TEST_CASE("ISO8601 parsing with microseconds", "[datetime]") {
    auto tp = datetime::from_iso8601("2024-06-15T14:30:45.123456Z");
    auto str = datetime::to_iso8601(tp);
    REQUIRE(str == "2024-06-15T14:30:45.123456Z");
}

TEST_CASE("ISO8601 parsing without fractional seconds", "[datetime]") {
    auto tp = datetime::from_iso8601("2024-01-15T10:30:00Z");
    auto str = datetime::to_iso8601(tp);
    REQUIRE(str == "2024-01-15T10:30:00.000000Z");
}

TEST_CASE("ISO8601 parsing without timezone", "[datetime]") {
    auto tp = datetime::from_iso8601("2024-01-15T10:30:00");
    auto str = datetime::to_iso8601(tp);
    REQUIRE(str == "2024-01-15T10:30:00.000000Z");
}

TEST_CASE("ISO8601 empty string returns epoch", "[datetime]") {
    auto tp = datetime::from_iso8601("");
    auto epoch = TimePoint{};
    REQUIRE(tp == epoch);
}

TEST_CASE("ISO8601 format has Z suffix", "[datetime]") {
    auto str = datetime::to_iso8601(datetime::utc_now());
    REQUIRE(str.back() == 'Z');
}
