#pragma once

#include <string>
#include <vector>

namespace graphiti {

class EmbedderClient {
public:
    virtual ~EmbedderClient() = default;
    virtual std::vector<float> create(std::string_view input) = 0;
    virtual std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) = 0;
};

} // namespace graphiti
