#pragma once

#include "qvm_parser.h"

#include <string>

namespace igi {

// Finds the first integer literal passed to a named native QVM call.
//
// Retail AI scripts encode native calls as a symbol push followed by CALL;
// each CALL argument is a code address pointing at its own expression block.
// This helper intentionally accepts only a single literal expression so a
// malformed or dynamic script cannot silently produce a guessed route id.
int FindFirstCallIntegerArgument(
    const QVMFile& qvm,
    const std::string& function_name);

} // namespace igi
