#pragma once

#include <expected>
#include <string>

namespace graphiti {

enum class ErrorCode {
    ok,
    llm_error,
    llm_parse_error,
    llm_rate_limit,
    db_error,
    embedding_error,
    http_error,
    invalid_config,
    not_found,
};

struct GraphitiError {
    ErrorCode code;
    std::string message;

    GraphitiError(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};

template <typename T>
using Result = std::expected<T, GraphitiError>;

using VoidResult = std::expected<void, GraphitiError>;

} // namespace graphiti
