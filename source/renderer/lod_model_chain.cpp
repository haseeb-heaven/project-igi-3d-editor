#include "lod_model_chain.h"

namespace igi {

std::string IncrementModelNameLastChar(const std::string& name) {
    if (name.empty()) return name;
    std::string out = name;
    out.back() = static_cast<char>(out.back() + 1);
    return out;
}

std::vector<std::string> ResolveLodChain(
    const std::string& name,
    const std::function<bool(const std::string&)>& exists) {
    // Level 0 is the requested name itself; 0x4CED50 always loads it.
    std::vector<std::string> chain;
    chain.reserve(kLodMaxLevels);
    chain.push_back(name);

    if (!exists) return chain;

    std::string current = name;
    while (static_cast<int>(chain.size()) < kLodMaxLevels) {
        current = IncrementModelNameLastChar(current);
        if (!exists(current)) break; // first unresolved name ends the chain
        chain.push_back(current);
    }
    return chain;
}

} // namespace igi
