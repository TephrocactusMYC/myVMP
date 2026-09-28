#include "myvmp/vmisa/function_isa.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace myvmp::vmisa {
namespace {

using Fields = std::vector<OperandField>;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t stable_hash(std::string_view text) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t next_random(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ull;
    std::uint64_t value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

Fields fields_for(ir::Opcode opcode) {
    using O = ir::Opcode;
    switch (opcode) {
    case O::LoadImm:
    case O::LoadAddress:
    case O::LoadFpImm64:
    case O::LoadFpImm32:
        return {OperandField::Dst, OperandField::Immediate};
    case O::AddImm:
    case O::SubImm:
    case O::AndImm:
    case O::OrImm:
    case O::XorImm:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate};
    case O::AddImmSetFlags:
    case O::SubImmSetFlags:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::AddRegShifted:
    case O::SubRegShifted:
    case O::AndRegShifted:
    case O::BitClearRegShifted:
    case O::OrRegShifted:
    case O::OrNotRegShifted:
    case O::XorRegShifted:
    case O::XorNotRegShifted:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ShiftKind, OperandField::ShiftAmount};
    case O::AddRegShiftedSetFlags:
    case O::SubRegShiftedSetFlags:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ShiftKind, OperandField::ShiftAmount, OperandField::ValueWidth};
    case O::AddRegExtended:
    case O::SubRegExtended:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ExtendKind, OperandField::ShiftAmount};
    case O::AddRegExtendedSetFlags:
    case O::SubRegExtendedSetFlags:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ExtendKind, OperandField::ShiftAmount, OperandField::ValueWidth};
    case O::NegReg:
    case O::NegRegSetFlags:
    case O::NotRegShifted:
        return {OperandField::Dst, OperandField::Src, OperandField::ShiftKind,
                OperandField::ShiftAmount, OperandField::ValueWidth};
    case O::ShiftReg:
    case O::ShiftReg32:
        return {OperandField::Dst, OperandField::Src, OperandField::ShiftKind,
                OperandField::ShiftAmount};
    case O::ShiftRegVariable:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ShiftKind, OperandField::ValueWidth};
    case O::DivideReg:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ValueWidth};
    case O::UnsignedMulHigh:
    case O::SignedMulLong:
    case O::UnsignedMulLong:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs};
    case O::BitfieldExtract:
        return {OperandField::Dst, OperandField::Src, OperandField::ShiftAmount,
                OperandField::Immediate};
    case O::MulAdd:
    case O::MulSub:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::AuxReg, OperandField::Immediate, OperandField::ValueWidth};
    case O::MoveReg:
        return {OperandField::Dst, OperandField::Src};
    case O::MoveKeep:
        return {OperandField::Dst, OperandField::Immediate, OperandField::ShiftAmount};
    case O::CompareImm:
    case O::TestImm:
        return {OperandField::Src, OperandField::Immediate};
    case O::CompareReg:
        return {OperandField::Src, OperandField::Rhs};
    case O::TestRegShifted:
    case O::TestRegShifted32:
        return {OperandField::Src, OperandField::Rhs, OperandField::ShiftKind,
                OperandField::ShiftAmount};
    case O::BranchIf:
        return {OperandField::Condition, OperandField::Target};
    case O::BranchRegZero:
        return {OperandField::Src, OperandField::Immediate, OperandField::Target};
    case O::BranchRegBit:
        return {OperandField::Src, OperandField::ShiftAmount, OperandField::Immediate,
                OperandField::Target};
    case O::CallVm:
        return {OperandField::Target};
    case O::FpAdd64:
    case O::FpSub64:
    case O::FpMul64:
    case O::FpDiv64:
    case O::FpAdd32:
    case O::FpSub32:
    case O::FpMul32:
    case O::FpDiv32:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs};
    case O::FpMadd64:
    case O::FpMadd32:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::AuxReg};
    case O::FpCompare64:
    case O::FpCompare32:
        return {OperandField::Src, OperandField::Rhs};
    case O::FpUnary:
    case O::FpConvert:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate};
    case O::FpMinMax:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate};
    case O::FpConditionalCompare:
        return {OperandField::Src, OperandField::Rhs, OperandField::Condition,
                OperandField::Immediate, OperandField::ShiftAmount, OperandField::ValueWidth};
    case O::FpSelect:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Condition, OperandField::ValueWidth};
    case O::Select:
    case O::SelectInc:
    case O::SelectInv:
    case O::SelectNeg:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Condition, OperandField::Immediate};
    case O::Load64:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate};
    case O::LoadMem:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::Store64:
        return {OperandField::Src, OperandField::Rhs, OperandField::Immediate,
                OperandField::PostIndex};
    case O::StoreMem:
        return {OperandField::Src, OperandField::Rhs, OperandField::Immediate,
                OperandField::PostIndex, OperandField::ValueWidth};
    case O::LoadRegOffsetMem:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::ExtendKind, OperandField::ShiftAmount,
                OperandField::ValueWidth};
    case O::StoreRegOffsetMem:
        return {OperandField::Src, OperandField::Rhs, OperandField::AuxReg,
                OperandField::ExtendKind, OperandField::ShiftAmount,
                OperandField::ValueWidth};
    case O::StorePair64:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate};
    case O::LoadAcquire:
        return {OperandField::Dst, OperandField::Src, OperandField::ValueWidth};
    case O::LoadExclusive:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::StoreRelease:
        return {OperandField::Src, OperandField::Rhs, OperandField::ValueWidth};
    case O::StoreExclusive:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ValueWidth};
    case O::LoadExclusivePair:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ValueWidth};
    case O::StoreExclusivePair:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::AuxReg, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::CompareSwap:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ValueWidth};
    case O::CallNative:
        return {OperandField::Immediate, OperandField::PostIndex};
    case O::CallNativeReg:
        return {OperandField::Src, OperandField::PostIndex};
    case O::VectorBinary:
    case O::VectorCompare:
    case O::VectorFpBinary:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ValueWidth};
    case O::VectorSelect:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::MaskReg};
    case O::VectorShiftImm:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate,
                OperandField::ShiftAmount, OperandField::ValueWidth};
    case O::VectorPermute:
        return {OperandField::Dst, OperandField::Src, OperandField::Rhs,
                OperandField::Immediate, OperandField::ShiftAmount};
    case O::VectorLoad:
        return {OperandField::Dst, OperandField::Src, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::VectorStore:
        return {OperandField::Src, OperandField::Rhs, OperandField::Immediate,
                OperandField::ValueWidth};
    case O::VectorMoveImmZero:
        return {OperandField::Dst};
    case O::NoOp:
        return {};
    case O::ReturnReg:
        return {OperandField::Src};
    }
    throw std::invalid_argument("FunctionVmIsa encountered an unknown IR opcode");
}

bool is_register_field(OperandField field) noexcept {
    return field == OperandField::Dst || field == OperandField::Src ||
           field == OperandField::Rhs || field == OperandField::AuxReg ||
           field == OperandField::MaskReg;
}

OperandFieldSpec choose_spec(OperandField field, std::uint64_t& random) {
    const auto pick = static_cast<unsigned>(next_random(random) & 1u);
    if (is_register_field(field)) {
        return {field, pick == 0 ? FieldEncoding::FixedLittleEndian : FieldEncoding::UnsignedLeb128,
                static_cast<std::uint8_t>(pick == 0 ? 6 : 0)};
    }
    switch (field) {
    case OperandField::Immediate: {
        const auto choice = static_cast<unsigned>(next_random(random) % 3u);
        constexpr std::array<FieldEncoding, 3> encodings = {
            FieldEncoding::FixedLittleEndian, FieldEncoding::UnsignedLeb128,
            FieldEncoding::SplitLowHigh};
        return {field, encodings[choice], static_cast<std::uint8_t>(choice == 0 || choice == 2 ? 64 : 0)};
    }
    case OperandField::PostIndex:
        return {field, pick == 0 ? FieldEncoding::SignedLeb128 : FieldEncoding::FixedLittleEndian,
                static_cast<std::uint8_t>(pick == 0 ? 0 : 64)};
    case OperandField::Target:
        return {field, pick == 0 ? FieldEncoding::FixedLittleEndian : FieldEncoding::UnsignedLeb128,
                static_cast<std::uint8_t>(pick == 0 ? 32 : 0)};
    case OperandField::ShiftAmount:
    case OperandField::ShiftKind:
    case OperandField::Condition:
    case OperandField::ExtendKind:
    case OperandField::ValueWidth:
        return {field, pick == 0 ? FieldEncoding::FixedLittleEndian : FieldEncoding::UnsignedLeb128,
                static_cast<std::uint8_t>(pick == 0 ? 8 : 0)};
    case OperandField::Dst:
    case OperandField::Src:
    case OperandField::Rhs:
    case OperandField::AuxReg:
    case OperandField::MaskReg:
        break;
    }
    throw std::invalid_argument("FunctionVmIsa cannot describe operand field");
}

bool valid_spec(const OperandFieldSpec& spec) noexcept {
    if (is_register_field(spec.field)) {
        return (spec.encoding == FieldEncoding::FixedLittleEndian && spec.fixed_width_bits == 6) ||
               (spec.encoding == FieldEncoding::UnsignedLeb128 && spec.fixed_width_bits == 0);
    }
    switch (spec.field) {
    case OperandField::Immediate:
        return (spec.encoding == FieldEncoding::FixedLittleEndian && spec.fixed_width_bits == 64) ||
               (spec.encoding == FieldEncoding::UnsignedLeb128 && spec.fixed_width_bits == 0) ||
               (spec.encoding == FieldEncoding::SplitLowHigh && spec.fixed_width_bits == 64);
    case OperandField::PostIndex:
        return (spec.encoding == FieldEncoding::SignedLeb128 && spec.fixed_width_bits == 0) ||
               (spec.encoding == FieldEncoding::FixedLittleEndian && spec.fixed_width_bits == 64);
    case OperandField::Target:
        return (spec.encoding == FieldEncoding::FixedLittleEndian && spec.fixed_width_bits == 32) ||
               (spec.encoding == FieldEncoding::UnsignedLeb128 && spec.fixed_width_bits == 0);
    case OperandField::ShiftAmount:
    case OperandField::ShiftKind:
    case OperandField::Condition:
    case OperandField::ExtendKind:
    case OperandField::ValueWidth:
        return (spec.encoding == FieldEncoding::FixedLittleEndian && spec.fixed_width_bits == 8) ||
               (spec.encoding == FieldEncoding::UnsignedLeb128 && spec.fixed_width_bits == 0);
    case OperandField::Dst:
    case OperandField::Src:
    case OperandField::Rhs:
    case OperandField::AuxReg:
    case OperandField::MaskReg:
        return false;
    }
    return false;
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped += hex[ch >> 4];
                escaped += hex[ch & 0x0f];
            } else {
                escaped += static_cast<char>(ch);
            }
        }
    }
    return escaped;
}

} // namespace

const char* to_string(OperandField field) noexcept {
    switch (field) {
    case OperandField::Dst: return "dst";
    case OperandField::Src: return "src";
    case OperandField::Rhs: return "rhs";
    case OperandField::AuxReg: return "aux_reg";
    case OperandField::MaskReg: return "mask_reg";
    case OperandField::Immediate: return "immediate";
    case OperandField::ShiftAmount: return "shift_amount";
    case OperandField::ShiftKind: return "shift_kind";
    case OperandField::Condition: return "condition";
    case OperandField::Target: return "target";
    case OperandField::ExtendKind: return "extend_kind";
    case OperandField::PostIndex: return "post_index";
    case OperandField::ValueWidth: return "value_width";
    }
    return "invalid";
}

const char* to_string(FieldEncoding encoding) noexcept {
    switch (encoding) {
    case FieldEncoding::FixedLittleEndian: return "fixed_le";
    case FieldEncoding::UnsignedLeb128: return "uleb128";
    case FieldEncoding::SignedLeb128: return "sleb128";
    case FieldEncoding::SplitLowHigh: return "split_low_high";
    }
    return "invalid";
}

FunctionVmIsa synthesize_function_isa(const ir::Program& program, std::string function_id,
                                      std::uint64_t seed) {
    if (function_id.empty()) {
        throw std::invalid_argument("FunctionVmIsa function_id must not be empty");
    }
    program.validate();

    FunctionVmIsa result;
    result.function_hash = stable_hash(function_id);
    result.function_id = std::move(function_id);
    result.seed = seed;
    for (const auto& instruction : program.instructions()) {
        result.logical_opcodes.push_back(instruction.opcode);
    }
    std::sort(result.logical_opcodes.begin(), result.logical_opcodes.end(), [](ir::Opcode lhs, ir::Opcode rhs) {
        return static_cast<std::uint8_t>(lhs) < static_cast<std::uint8_t>(rhs);
    });
    result.logical_opcodes.erase(std::unique(result.logical_opcodes.begin(), result.logical_opcodes.end()),
                                 result.logical_opcodes.end());

    for (const auto opcode : result.logical_opcodes) {
        Fields expected = fields_for(opcode);
        std::uint64_t random = seed ^ result.function_hash ^
                               (static_cast<std::uint64_t>(static_cast<std::uint8_t>(opcode)) << 32);
        for (std::size_t i = expected.size(); i > 1; --i) {
            const auto j = static_cast<std::size_t>(next_random(random) % i);
            std::swap(expected[i - 1], expected[j]);
        }
        InstructionGrammar instruction_grammar;
        instruction_grammar.opcode = opcode;
        instruction_grammar.fields.reserve(expected.size());
        for (const auto field : expected) {
            instruction_grammar.fields.push_back(choose_spec(field, random));
        }
        result.grammar.push_back(std::move(instruction_grammar));
    }
    std::string error;
    if (!result.validate(&error)) {
        throw std::logic_error("synthesized invalid FunctionVmIsa: " + error);
    }
    return result;
}

bool FunctionVmIsa::validate(std::string* error) const {
    const auto fail = [error](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (function_id.empty()) {
        return fail("empty function_id");
    }
    if (function_hash != stable_hash(function_id)) {
        return fail("function_hash does not match function_id");
    }
    if (logical_opcodes.empty() || logical_opcodes.size() != grammar.size()) {
        return fail("logical opcode and grammar cardinalities differ or are empty");
    }
    for (std::size_t i = 0; i < logical_opcodes.size(); ++i) {
        if (i != 0 && static_cast<std::uint8_t>(logical_opcodes[i - 1]) >=
                          static_cast<std::uint8_t>(logical_opcodes[i])) {
            return fail("logical opcodes are not unique and strictly sorted");
        }
        if (grammar[i].opcode != logical_opcodes[i]) {
            return fail("grammar opcode sequence does not match logical opcode set");
        }
        Fields expected;
        try {
            expected = fields_for(logical_opcodes[i]);
        } catch (const std::invalid_argument&) {
            return fail("unknown logical opcode");
        }
        if (grammar[i].fields.size() != expected.size()) {
            return fail("grammar field count does not match opcode schema");
        }
        std::array<bool, 13> seen{};
        for (const auto& spec : grammar[i].fields) {
            const auto field_index = static_cast<std::size_t>(spec.field);
            if (field_index >= seen.size() || seen[field_index]) {
                return fail("grammar contains an invalid or duplicate operand field");
            }
            seen[field_index] = true;
            if (std::find(expected.begin(), expected.end(), spec.field) == expected.end()) {
                return fail("grammar contains an unexpected operand field");
            }
            if (!valid_spec(spec)) {
                return fail("operand field uses an incompatible width encoding");
            }
        }
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::string FunctionVmIsa::to_manifest_json() const {
    std::string error;
    if (!validate(&error)) {
        throw std::invalid_argument("cannot serialize invalid FunctionVmIsa: " + error);
    }
    std::ostringstream output;
    output << "{\"schema\":1,\"function_id\":\"" << json_escape(function_id)
           << "\",\"function_hash\":\"" << std::hex << function_hash
           << "\",\"seed\":\"" << seed << std::dec << "\",\"logical_opcodes\":[";
    for (std::size_t i = 0; i < logical_opcodes.size(); ++i) {
        if (i != 0) output << ',';
        output << static_cast<unsigned>(static_cast<std::uint8_t>(logical_opcodes[i]));
    }
    output << "],\"grammar\":[";
    for (std::size_t i = 0; i < grammar.size(); ++i) {
        if (i != 0) output << ',';
        output << "{\"opcode\":" << static_cast<unsigned>(static_cast<std::uint8_t>(grammar[i].opcode))
               << ",\"fields\":[";
        for (std::size_t j = 0; j < grammar[i].fields.size(); ++j) {
            if (j != 0) output << ',';
            const auto& field = grammar[i].fields[j];
            output << "{\"name\":\"" << to_string(field.field) << "\",\"encoding\":\""
                   << to_string(field.encoding) << "\",\"width_bits\":"
                   << static_cast<unsigned>(field.fixed_width_bits) << '}';
        }
        output << "]}";
    }
    output << "]}";
    return output.str();
}

} // namespace myvmp::vmisa
