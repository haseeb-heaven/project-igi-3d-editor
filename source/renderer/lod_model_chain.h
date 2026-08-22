#pragma once
#include <string>
#include <vector>
#include <functional>

// LOD virtual-model chain resolution — port of open-igi src/OpenIGI.Game/World/LodModelChain.cs,
// which documents the original engine's 0x4CED50 behavior (igi2.pdb symbol evidence):
//
//   0x4CED50 loads the requested model name, then INCREMENTS THE LAST CHARACTER of the
//   name and loads again, stopping at the first name that does not resolve or once it
//   has five levels. So "435_01_1" pulls in "435_01_2" ... "435_01_5". A name ending in
//   a non-digit still works because the step is character arithmetic, not a numeric parse
//   ('9' increments to ':', which simply will not resolve and ends the chain).
//
// This matters beyond drawing: every level is baked one lightmap per level of detail, so
// the chain length decides how many .olm entries an object contributes to lightmaps.res.

namespace igi {

// The most detail levels the original will chain, from 0x4CED50's loop bound.
inline constexpr int kLodMaxLevels = 5;

// Increments the last character of the model name (character arithmetic, exactly as
// 0x4CED50 does). "435_01_1" -> "435_01_2", "mesh9" -> "mesh:", "abc" -> "abd".
std::string IncrementModelNameLastChar(const std::string& name);

// Resolves the full LOD chain for `name`. `exists` is a probe that returns true when
// the given model name resolves to loadable data (ResCache hit or file on disk).
// Returns at least {name} itself (level 0 is always present), then each successive
// increment that `exists` accepts, up to kLodMaxLevels entries total.
std::vector<std::string> ResolveLodChain(
    const std::string& name,
    const std::function<bool(const std::string&)>& exists);

} // namespace igi
