#pragma once

#include "myvmp/ir/ir.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace myvmp::riscv64 {

enum class Mnemonic {
    Lui, Auipc, Jal, Jalr, Branch,
    AddImm, SetLessImm, SetLessUnsignedImm, XorImm, OrImm, AndImm,
    ShiftLeftImm, ShiftRightImm,
    Add, Sub, ShiftLeft, SetLess, SetLessUnsigned, Xor, ShiftRight, Or, And,
    Load, Store,
    AddWordImm, ShiftLeftWordImm, ShiftRightWordImm,
    AddWord, SubWord, ShiftLeftWord, ShiftRightWord,
    Mul, MulHigh, MulHighSignedUnsigned, MulHighUnsigned, Div, DivUnsigned, Rem, RemUnsigned,
    MulWord, DivWord, DivUnsignedWord, RemWord, RemUnsignedWord,
    Fence, Return,
};

struct DecodedInstruction {
    Mnemonic mnemonic{};
    std::uint8_t rd{};
    std::uint8_t rs1{};
    std::uint8_t rs2{};
    std::int64_t immediate{};
    ir::ShiftKind shift{ir::ShiftKind::Lsl};
    ir::Condition condition{ir::Condition::Eq};
    std::uint8_t width{8};
    bool is_signed{};
};

struct LiftContext {
    std::uint64_t region_va = 0;
    // Optional byte PC for each decoded instruction; used by mixed-width RVC streams.
    std::vector<std::uint32_t> instruction_offsets;
};

class Decoder {
public:
    std::optional<DecodedInstruction> decode(std::uint32_t word) const;
};

class Lifter {
public:
    ir::Program lift(const std::vector<std::uint32_t>& words,
                     const LiftContext& context = {}) const;
    ir::Program lift_bytes(const std::vector<std::uint8_t>& bytes,
                           const LiftContext& context = {}) const;
};

const char* to_string(Mnemonic mnemonic) noexcept;

} // namespace myvmp::riscv64
