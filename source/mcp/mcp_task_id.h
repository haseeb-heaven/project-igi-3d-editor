#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

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

}  // namespace mcp
