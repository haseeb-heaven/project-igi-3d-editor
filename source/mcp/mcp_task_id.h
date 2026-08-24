#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mcp {

inline std::string AnonymousTaskId(std::string_view parent_id,
                                   std::string_view type,
                                   std::string_view name) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffsetBasis;
    const auto mix = [&](std::string_view value) {
        for (const unsigned char character : value) {
            hash ^= character;
            hash *= kPrime;
        }
    };
    mix(parent_id);
    mix("|");
    mix(type);
    mix("|");
    mix(name);
    mix("|");

    std::ostringstream result;
    result << "anon-" << std::hex << hash;
    return result.str();
}

inline std::string UniqueTaskId(std::string_view base_id,
                               std::unordered_map<std::string, int>& next_suffix,
                               std::unordered_set<std::string>& used_ids) {
    std::string candidate(base_id);
    int& suffix = next_suffix[std::string(base_id)];
    if (suffix == 0) suffix = 1;
    while (!used_ids.insert(candidate).second) {
        candidate = std::string(base_id) + "#" + std::to_string(suffix++);
    }
    return candidate;
}

}  // namespace mcp
