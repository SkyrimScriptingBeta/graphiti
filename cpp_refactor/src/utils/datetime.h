#pragma once

#include <graphiti/fwd.h>

#include <string>

namespace graphiti::datetime {

TimePoint utc_now();
std::string to_iso8601(TimePoint tp);
TimePoint from_iso8601(std::string_view s);

} // namespace graphiti::datetime
