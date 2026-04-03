#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace graphiti {

// Thread-local callsite logger.
//
// Each Catch2 test (or any caller) does:
//     callsite_log_open("my_test_name");
//     ... run Graphiti operations ...
//     callsite_log_close();
//
// Every call site in the application calls:
//     log_callsite("episode-save-entity-edge");
//
// Output: one file per context in the callsite log directory,
// each line: <ISO timestamp> <callsite-id>

inline std::string& callsite_log_dir() {
    static std::string dir;
    return dir;
}

inline FILE*& callsite_log_file() {
    static thread_local FILE* f = nullptr;
    return f;
}

inline void callsite_log_set_dir(const std::string& dir) {
    callsite_log_dir() = dir;
    std::filesystem::create_directories(dir);
}

inline void callsite_log_open(std::string_view context_id) {
    if (callsite_log_dir().empty()) return;

    auto path = std::filesystem::path(callsite_log_dir()) / (std::string(context_id) + ".callsites");
    callsite_log_file() = std::fopen(path.string().c_str(), "w");
}

inline void callsite_log_close() {
    if (callsite_log_file()) {
        std::fclose(callsite_log_file());
        callsite_log_file() = nullptr;
    }
}

inline void log_callsite(const char* callsite_id) {
    auto* f = callsite_log_file();
    if (!f) return;

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::fprintf(f, "%lld %s\n", static_cast<long long>(ms), callsite_id);
    std::fflush(f);
}

} // namespace graphiti
