#include "datetime.h"

#include <charconv>
#include <chrono>
#include <format>
#include <stdexcept>
#include <string>

namespace graphiti::datetime {

TimePoint utc_now() {
    return std::chrono::system_clock::now();
}

std::string to_iso8601(TimePoint tp) {
    auto sys_time = std::chrono::time_point_cast<std::chrono::microseconds>(tp);
    auto dp = std::chrono::floor<std::chrono::days>(sys_time);
    std::chrono::year_month_day ymd{dp};
    auto tod = std::chrono::hh_mm_ss{sys_time - dp};

    return std::format(
        "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:06d}Z",
        static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()), tod.hours().count(), tod.minutes().count(),
        tod.seconds().count(), tod.subseconds().count()
    );
}

// Parse ISO8601 strings like:
//   2024-01-15T10:30:00Z
//   2024-01-15T10:30:00.123456Z
//   2024-01-15T10:30:00
TimePoint from_iso8601(std::string_view s) {
    if (s.empty()) return {};

    auto parse_int = [](std::string_view sv) -> int {
        int val = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    };

    // Minimum: YYYY-MM-DDTHH:MM:SS (19 chars)
    if (s.size() < 19) return {};

    int year = parse_int(s.substr(0, 4));
    int month = parse_int(s.substr(5, 2));
    int day = parse_int(s.substr(8, 2));
    int hour = parse_int(s.substr(11, 2));
    int minute = parse_int(s.substr(14, 2));
    int second = parse_int(s.substr(17, 2));
    int microseconds = 0;

    // Parse fractional seconds if present
    if (s.size() > 19 && s[19] == '.') {
        auto frac_start = 20;
        auto frac_end = frac_start;
        while (frac_end < static_cast<int>(s.size()) && s[frac_end] >= '0' && s[frac_end] <= '9')
            ++frac_end;
        auto frac_str = s.substr(frac_start, frac_end - frac_start);
        microseconds = parse_int(frac_str);
        // Normalize to microseconds (pad or truncate to 6 digits)
        int digits = static_cast<int>(frac_str.size());
        for (int i = digits; i < 6; ++i) microseconds *= 10;
        for (int i = digits; i > 6; --i) microseconds /= 10;
    }

    auto ymd = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
               std::chrono::day{static_cast<unsigned>(day)};
    auto dp = std::chrono::sys_days{ymd};
    return dp + std::chrono::hours{hour} + std::chrono::minutes{minute} +
           std::chrono::seconds{second} + std::chrono::microseconds{microseconds};
}

} // namespace graphiti::datetime
