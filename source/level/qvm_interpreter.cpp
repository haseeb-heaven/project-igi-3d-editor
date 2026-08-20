// qvm_interpreter.cpp - Bounded QVM bytecode runtime interpreter implementation
#include "qvm_interpreter.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <sstream>

namespace {

bool ReadLittleEndianUint32(const std::vector<uint8_t>& bytecode, size_t& offset, uint32_t& value) {
    if (bytecode.size() - offset < sizeof(uint32_t)) {
        return false;
    }

    value = static_cast<uint32_t>(bytecode[offset])
        | (static_cast<uint32_t>(bytecode[offset + 1]) << 8U)
        | (static_cast<uint32_t>(bytecode[offset + 2]) << 16U)
        | (static_cast<uint32_t>(bytecode[offset + 3]) << 24U);
    offset += sizeof(uint32_t);
    return true;
}

bool ReadLittleEndianUint64(const std::vector<uint8_t>& bytecode, size_t& offset, uint64_t& value) {
    if (bytecode.size() - offset < sizeof(uint64_t)) {
        return false;
    }

    value = static_cast<uint64_t>(bytecode[offset])
        | (static_cast<uint64_t>(bytecode[offset + 1]) << 8U)
        | (static_cast<uint64_t>(bytecode[offset + 2]) << 16U)
        | (static_cast<uint64_t>(bytecode[offset + 3]) << 24U)
        | (static_cast<uint64_t>(bytecode[offset + 4]) << 32U)
        | (static_cast<uint64_t>(bytecode[offset + 5]) << 40U)
        | (static_cast<uint64_t>(bytecode[offset + 6]) << 48U)
        | (static_cast<uint64_t>(bytecode[offset + 7]) << 56U);
    offset += sizeof(uint64_t);
    return true;
}

} // namespace

namespace igi {

QvmExecutionContext::QvmExecutionContext(const QvmProgram& program, const QvmNativeRegistry& registry)
    : program_(program), registry_(registry) {
    Reset();
}

void QvmExecutionContext::Reset() {
    stack_.clear();
    stack_.reserve(MAX_STACK_DEPTH);
    call_stack_.clear();
    call_stack_.reserve(MAX_CALL_FRAMES);
    pc_ = program_.entry_point;
    step_count_ = 0;
    halted_ = false;
    errored_ = false;
    last_error_.clear();
}

void QvmExecutionContext::SetError(const std::string& msg) {
    errored_ = true;
    halted_ = true;
    last_error_ = msg;
}

void QvmExecutionContext::Push(const QvmRuntimeValue& val) {
    if (stack_.size() >= MAX_STACK_DEPTH) {
        SetError("Stack overflow in QVM execution context");
        return;
    }
    stack_.push_back(val);
}

bool QvmExecutionContext::TryPop(QvmRuntimeValue& out_value) {
    if (stack_.empty()) {
        SetError("Stack underflow in QVM execution context");
        return false;
    }

    out_value = stack_.back();
    stack_.pop_back();
    return true;
}

QvmRuntimeValue QvmExecutionContext::Pop() {
    QvmRuntimeValue value;
    if (!TryPop(value)) {
        return QvmRuntimeValue();
    }
    return value;
}

const QvmRuntimeValue& QvmExecutionContext::Peek(size_t depth) const {
    static const QvmRuntimeValue null_val;
    if (depth >= stack_.size()) {
        return null_val;
    }
    return stack_[stack_.size() - 1 - depth];
}

bool QvmExecutionContext::Run() {
    while (!halted_ && !errored_) {
        if (step_count_ >= MAX_INSTRUCTION_STEPS) {
            SetError("Instruction limit exceeded (possible infinite loop)");
            return false;
        }
        if (!Step()) {
            break;
        }
    }
    return !errored_;
}

bool QvmExecutionContext::Step() {
    if (halted_ || errored_) {
        return false;
    }

    if (pc_ >= program_.instructions.size()) {
        SetError("Instruction pointer is outside the QVM program");
        return false;
    }

    step_count_++;
    const QvmInstruction& inst = program_.instructions[pc_++];

    switch (inst.opcode) {
        case 0x00: // NOP
            break;

        case 0x01: // PUSH_INT
            Push(QvmRuntimeValue::FromInt(inst.operand_i32));
            break;

        case 0x02: // PUSH_REAL
            Push(QvmRuntimeValue::FromReal(inst.operand_f64));
            break;

        case 0x03: // PUSH_STRING
            Push(QvmRuntimeValue::FromString(inst.operand_str));
            break;

        case 0x04: // POP
            {
                QvmRuntimeValue discarded_value;
                TryPop(discarded_value);
            }
            break;

        case 0x05: // DUP
            if (!stack_.empty()) {
                Push(stack_.back());
            } else {
                SetError("DUP on empty stack");
            }
            break;

        case 0x10: // ADD_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val + right_value.int_val));
                }
            }
            break;

        case 0x11: // SUB_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val - right_value.int_val));
                }
            }
            break;

        case 0x12: // MUL_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val * right_value.int_val));
                }
            }
            break;

        case 0x13: // DIV_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    if (right_value.int_val == 0) {
                        SetError("Divide by zero in QVM");
                    } else {
                        Push(QvmRuntimeValue::FromInt(left_value.int_val / right_value.int_val));
                    }
                }
            }
            break;

        case 0x20: // JUMP
            if (inst.operand_u32 >= program_.instructions.size()) {
                SetError("QVM jump target is outside the program");
            } else {
                pc_ = inst.operand_u32;
            }
            break;

        case 0x21: // JUMP_IF_ZERO
            {
                QvmRuntimeValue condition_value;
                if (!TryPop(condition_value)) {
                    break;
                }
                if (condition_value.int_val == 0) {
                    if (inst.operand_u32 >= program_.instructions.size()) {
                        SetError("QVM conditional jump target is outside the program");
                        break;
                    }
                    pc_ = inst.operand_u32;
                }
            }
            break;

        case 0x30: // CALL_NATIVE
            {
                uint32_t sym_id = inst.operand_u32;
                if (inst.operand_i32 < 0) {
                    SetError("QVM native call has a negative argument count");
                    break;
                }
                const uint32_t arg_count = static_cast<uint32_t>(inst.operand_i32);
                if (arg_count > stack_.size()) {
                    SetError("QVM native call has insufficient stack arguments");
                    break;
                }
                std::vector<QvmRuntimeValue> args;
                args.reserve(arg_count);

                for (uint32_t i = 0; i < arg_count; i++) {
                    QvmRuntimeValue argument;
                    if (!TryPop(argument)) {
                        return false;
                    }
                    args.push_back(argument);
                }
                std::reverse(args.begin(), args.end());

                QvmRuntimeValue result;
                try {
                    if (!registry_.TryExecute(sym_id, *this, args, result)) {
                        std::ostringstream ss;
                        ss << "Unknown native function ID 0x" << std::hex << sym_id;
                        SetError(ss.str());
                    } else if (!errored_) {
                        Push(result);
                    }
                } catch (const std::exception& exception) {
                    SetError(std::string("QVM native function threw: ") + exception.what());
                } catch (...) {
                    SetError("QVM native function threw an unknown exception");
                }
            }
            break;

        case 0xFF: // RETURN / HALT
            halted_ = true;
            return false;

        default:
            {
                std::ostringstream ss;
                ss << "Illegal or unsupported QVM opcode 0x" << std::hex << (int)inst.opcode;
                SetError(ss.str());
            }
            break;
    }

    return !errored_ && !halted_;
}

QvmInterpreter::QvmInterpreter(const QvmNativeRegistry& registry)
    : registry_(registry) {}

bool QvmInterpreter::LoadProgram(const std::vector<uint8_t>& bytecode, QvmProgram& out_program) {
    out_program = QvmProgram();
    last_error_.clear();

    auto fail = [this](const std::string& message) {
        last_error_ = message;
        return false;
    };

    if (bytecode.empty()) {
        return fail("QVM bytecode is empty");
    }

    std::vector<QvmInstruction> decoded_instructions;
    size_t offset = 0;
    while (offset < bytecode.size()) {
        QvmInstruction inst;
        inst.opcode = bytecode[offset++];

        if (inst.opcode == 0x01) { // PUSH_INT
            uint32_t encoded_value = 0;
            if (!ReadLittleEndianUint32(bytecode, offset, encoded_value)) {
                return fail("truncated PUSH_INT operand in QVM bytecode");
            }
            inst.operand_i32 = std::bit_cast<int32_t>(encoded_value);
        } else if (inst.opcode == 0x02) { // PUSH_REAL
            uint64_t encoded_value = 0;
            if (!ReadLittleEndianUint64(bytecode, offset, encoded_value)) {
                return fail("truncated PUSH_REAL operand in QVM bytecode");
            }
            inst.operand_f64 = std::bit_cast<double>(encoded_value);
        } else if (inst.opcode == 0x20 || inst.opcode == 0x21) { // JUMP
            if (!ReadLittleEndianUint32(bytecode, offset, inst.operand_u32)) {
                return fail("truncated jump operand in QVM bytecode");
            }
        } else if (inst.opcode == 0x30) { // CALL_NATIVE
            uint32_t argument_count = 0;
            if (!ReadLittleEndianUint32(bytecode, offset, inst.operand_u32)
                || !ReadLittleEndianUint32(bytecode, offset, argument_count)) {
                return fail("truncated native-call operand in QVM bytecode");
            }
            inst.operand_i32 = std::bit_cast<int32_t>(argument_count);
        }

        decoded_instructions.push_back(std::move(inst));
    }

    if (decoded_instructions.empty()) {
        return fail("QVM bytecode contains no instructions");
    }

    out_program.raw_bytecode = bytecode;
    out_program.instructions = std::move(decoded_instructions);
    return true;
}

std::unique_ptr<QvmExecutionContext> QvmInterpreter::CreateContext(const QvmProgram& program) {
    return std::make_unique<QvmExecutionContext>(program, registry_);
}

} // namespace igi
