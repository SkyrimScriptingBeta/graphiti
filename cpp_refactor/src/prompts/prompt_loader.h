#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace graphiti::prompts {

// Attempt to load a prompt override from the GRAPHITI_PROMPTS directory.
// Returns the file contents if found, nullopt if not (fall back to compiled-in default).
//
// Files are named: <prompt_name>.<part>.txt
//   e.g. extract_message.system.txt, extract_message.user.txt
//
// The returned string is used as a std::format template — {0}, {1} etc are replaced
// with the same variables as the compiled-in prompt.
inline std::optional<std::string> load_prompt_override(
    std::string_view prompt_name,
    std::string_view part  // "system" or "user"
) {
    static const char* prompts_dir = std::getenv("GRAPHITI_PROMPTS");
    if (!prompts_dir || prompts_dir[0] == '\0') return std::nullopt;

    auto path = std::filesystem::path(prompts_dir) /
                (std::string(prompt_name) + "." + std::string(part) + ".txt");

    if (!std::filesystem::exists(path)) return std::nullopt;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Trim trailing whitespace/newlines
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' '))
        content.pop_back();

    return content;
}

// Resolve a prompt: use override file if it exists, otherwise use compiled-in default.
inline std::string resolve_prompt(
    std::string_view prompt_name,
    std::string_view part,
    std::string_view compiled_default
) {
    auto override_val = load_prompt_override(prompt_name, part);
    return override_val.value_or(std::string(compiled_default));
}

} // namespace graphiti::prompts
