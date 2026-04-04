#pragma once

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace graphiti {

enum class LogLevel { NONE = 0, INFO = 1, DEBUG = 2, TRACE = 3 };

inline LogLevel log_level_from_env() {
    auto* val = std::getenv("GRAPHITI_LOG_LEVEL");
    if (!val || !val[0]) return LogLevel::NONE;

    std::string s(val);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (s == "trace") return LogLevel::TRACE;
    if (s == "debug") return LogLevel::DEBUG;
    if (s == "info") return LogLevel::INFO;
    return LogLevel::NONE;
}

inline LogLevel& current_log_level() {
    static LogLevel level = log_level_from_env();
    return level;
}

inline void set_log_level(LogLevel level) { current_log_level() = level; }

// NOLINTBEGIN(cert-err33-c)
inline void log_info(const char* fmt, ...) {
    if (current_log_level() < LogLevel::INFO) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

inline void log_debug(const char* fmt, ...) {
    if (current_log_level() < LogLevel::DEBUG) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

inline void log_trace(const char* fmt, ...) {
    if (current_log_level() < LogLevel::TRACE) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
// NOLINTEND(cert-err33-c)

} // namespace graphiti
