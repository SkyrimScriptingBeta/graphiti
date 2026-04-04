#pragma once

#include <string>
#include <unordered_map>

namespace graphiti {

// Union-Find with path compression and lexicographic canonical selection.
// Used for collapsing duplicate UUID chains in bulk operations.
// The lexicographically smaller UUID becomes the canonical root.
class UnionFind {
public:
    explicit UnionFind(const std::vector<std::string>& elements) {
        for (auto& e : elements) {
            parent_[e] = e;
        }
    }

    UnionFind() = default;

    void add(const std::string& element) {
        if (parent_.find(element) == parent_.end()) {
            parent_[element] = element;
        }
    }

    std::string find(const std::string& x) {
        auto it = parent_.find(x);
        if (it == parent_.end()) {
            parent_[x] = x;
            return x;
        }
        if (it->second != x) {
            it->second = find(it->second);
        }
        return it->second;
    }

    void unite(const std::string& a, const std::string& b) {
        auto ra = find(a);
        auto rb = find(b);
        if (ra == rb) return;

        // Attach the lexicographically larger root under the smaller
        if (ra < rb) {
            parent_[rb] = ra;
        } else {
            parent_[ra] = rb;
        }
    }

    // Resolve all elements to their canonical roots
    std::unordered_map<std::string, std::string> resolve_all() {
        std::unordered_map<std::string, std::string> result;
        for (auto& [k, _] : parent_) {
            result[k] = find(k);
        }
        return result;
    }

private:
    std::unordered_map<std::string, std::string> parent_;
};

} // namespace graphiti
