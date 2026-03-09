#pragma once

#include <graphiti/graphiti.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace stress {

inline std::string require_api_key() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        std::cerr << "ERROR: Set OPENAI_API_KEY environment variable first\n";
        std::exit(1);
    }
    return key;
}

inline graphiti::GraphitiConfig make_config() {
    auto api_key = require_api_key();
    graphiti::GraphitiConfig config;
    config.db_path = ":memory:";
    config.llm.api_key = api_key;
    config.embedder.api_key = api_key;
    return config;
}

inline void separator(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n";
}

inline int passed = 0;
inline int failed = 0;
inline int total = 0;

inline void test(const std::string& name, bool condition) {
    ++total;
    if (condition) {
        ++passed;
        std::cout << std::format("  [PASS] {}\n", name);
    } else {
        ++failed;
        std::cout << std::format("  [FAIL] {}\n", name);
    }
}

inline void summary() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << std::format("  RESULTS: {} passed, {} failed, {} total\n", passed, failed, total);
    std::cout << std::string(70, '=') << "\n";
}

} // namespace stress
