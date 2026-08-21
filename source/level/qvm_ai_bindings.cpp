#include "qvm_ai_bindings.h"

namespace igi {
namespace {

const QVMInstruction* FindInstructionAtAddress(
    const QVMFile& qvm,
    uint32_t address) {
    for (const QVMInstruction& instruction : qvm.instructions) {
        if (instruction.address == address) {
            return &instruction;
        }
    }
    return nullptr;
}

bool TryReadIntegerLiteral(
    const QVMInstruction& instruction,
    int& out_value) {
    switch (instruction.type) {
    case QVMOpType::PUSH:
    case QVMOpType::PUSHB:
    case QVMOpType::PUSHW:
        out_value = instruction.signed_operand;
        return true;
    case QVMOpType::PUSH0:
        out_value = 0;
        return true;
    case QVMOpType::PUSH1:
        out_value = 1;
        return true;
    case QVMOpType::PUSHM:
        out_value = -1;
        return true;
    default:
        return false;
    }
}

} // namespace

int FindFirstCallIntegerArgument(
    const QVMFile& qvm,
    const std::string& function_name) {
    if (!qvm.valid || function_name.empty()) {
        return -1;
    }

    for (size_t instruction_index = 0;
         instruction_index + 1 < qvm.instructions.size();
         ++instruction_index) {
        const QVMInstruction& symbol_push = qvm.instructions[instruction_index];
        if (symbol_push.type != QVMOpType::PUSHII &&
            symbol_push.type != QVMOpType::PUSHIIB &&
            symbol_push.type != QVMOpType::PUSHIIW) {
            continue;
        }
        if (symbol_push.operand >= qvm.identifiers.size() ||
            qvm.identifiers[symbol_push.operand] != function_name) {
            continue;
        }

        const QVMInstruction& call = qvm.instructions[instruction_index + 1];
        if (call.type != QVMOpType::CALL || call.call_targets.empty()) {
            continue;
        }

        const int32_t first_argument_address = call.call_targets.front();
        if (first_argument_address < 0) {
            continue;
        }

        const QVMInstruction* argument = FindInstructionAtAddress(
            qvm,
            static_cast<uint32_t>(first_argument_address));
        int value = -1;
        if (argument != nullptr && TryReadIntegerLiteral(*argument, value)) {
            return value;
        }
    }

    return -1;
}

} // namespace igi
