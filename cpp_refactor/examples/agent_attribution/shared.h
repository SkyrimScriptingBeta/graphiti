#pragma once

#include <graphiti/graphiti.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace shared {

// Disk-based DB so state persists between example programs
inline std::string db_path() {
    auto dir = std::filesystem::path(__FILE__).parent_path() / "_test_kuzu_db";
    return dir.string();
}

inline std::string require_api_key() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        std::cerr << "ERROR: Set OPENAI_API_KEY environment variable first\n";
        std::exit(1);
    }
    return key;
}

inline graphiti::Graphiti make_graphiti(const std::string& path = "") {
    auto api_key = require_api_key();

    graphiti::GraphitiConfig config;
    config.db_path = path.empty() ? db_path() : path;
    config.llm.api_key = api_key;
    config.embedder.api_key = api_key;

    graphiti::Graphiti g(std::move(config));

    auto r = g.build_indices();
    if (!r.has_value()) {
        std::cerr << "ERROR building indices: " << r.error().message << "\n";
        std::exit(1);
    }

    return g;
}

inline void print_separator(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n";
}

} // namespace shared
