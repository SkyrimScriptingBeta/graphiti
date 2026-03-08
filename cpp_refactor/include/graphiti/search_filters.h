#pragma once

#include <graphiti/fwd.h>

#include <optional>
#include <string>
#include <vector>

namespace graphiti {

struct SearchFilters {
    std::optional<TimePoint> created_after;
    std::optional<TimePoint> created_before;
    std::optional<TimePoint> valid_after;
    std::optional<TimePoint> valid_before;
    std::vector<std::string> labels;
    std::vector<std::string> group_ids;
};

} // namespace graphiti
