#pragma once

#include "myvmp/ir/ir.hpp"

#include <cstdint>
#include <string>
#include <vector>

// FunctionVmIsa is a synthesis descriptor. It deliberately does not alter the
// current bytecode encoder or runtime ABI; consumers may use the manifest to
// build a future function-local encoder/handler set.
namespace myvmp::vmisa {

enum class OperandField : std::uint8_t {
    Dst,
    Src,
    Rhs,
    AuxReg,
    MaskReg,
    Immediate,
    ShiftAmount,
    ShiftKind,
    Condition,
    Target,
    ExtendKind,
    PostIndex,
    ValueWidth,
};

enum class FieldEncoding : std::uint8_t {
    FixedLittleEndian,
    UnsignedLeb128,
    SignedLeb128,
    SplitLowHigh,
};

struct OperandFieldSpec {
    OperandField field{};
    FieldEncoding encoding{};
    std::uint8_t fixed_width_bits{};
};

struct InstructionGrammar {
    ir::Opcode opcode{};
    // Order in which the future function-specific encoder will emit fields.
    std::vector<OperandFieldSpec> fields;
};

struct FunctionVmIsa {
    std::string function_id;
    std::uint64_t function_hash{};
    std::uint64_t seed{};
    // Sorted by underlying IR opcode value for stable manifests.
    std::vector<ir::Opcode> logical_opcodes;
    // Exactly one grammar entry for every opcode above, in the same order.
    std::vector<InstructionGrammar> grammar;

    [[nodiscard]] bool validate(std::string* error = nullptr) const;
    [[nodiscard]] std::string to_manifest_json() const;
};

// The caller supplies a stable function identifier (for example a symbol plus
// section-relative address). The same ID, IR, and seed always produce the same
// descriptor regardless of standard-library implementation or host platform.
[[nodiscard]] FunctionVmIsa synthesize_function_isa(const ir::Program& program,
                                                    std::string function_id,
                                                    std::uint64_t seed);

[[nodiscard]] const char* to_string(OperandField field) noexcept;
[[nodiscard]] const char* to_string(FieldEncoding encoding) noexcept;

} // namespace myvmp::vmisa
