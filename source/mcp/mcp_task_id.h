#pragma once

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mcp {

inline std::string AnonymousArgumentSignature(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit:
    case qsc::NodeKind::FloatLit: {
        std::ostringstream output;
        output << std::setprecision(9) << static_cast<float>(
            node.kind == qsc::NodeKind::IntLit ? node.i_val : node.f_val);
        return output.str();
    }
    case qsc::NodeKind::BoolLit: return node.b_val ? "true" : "false";
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return node.s_val;
    case qsc::NodeKind::Call: return "call:" + node.s_val;
    case qsc::NodeKind::Unary:
        return node.s_val + (node.children.empty() ? std::string{} : AnonymousArgumentSignature(*node.children.front()));
    default: return "expression";
    }
}

inline std::string AnonymousArgumentSignature(std::string_view source) {
    const qsc::LexResult lexed = qsc::Lex(std::string(source) + ";");
    if (!lexed.ok) return "expression";
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program || parsed.program->children.size() != 1 ||
        parsed.program->children.front()->kind != qsc::NodeKind::ExprStmt ||
        parsed.program->children.front()->children.empty()) return "expression";
    return AnonymousArgumentSignature(*parsed.program->children.front()->children.front());
}

inline std::string AnonymousTaskId(std::string_view parent_id,
                                   std::string_view stable_signature) {
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
    mix(stable_signature);
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
