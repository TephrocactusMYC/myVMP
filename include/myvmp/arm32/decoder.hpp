#pragma once

#include "myvmp/ir/ir.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace myvmp::arm32 {

enum class Mnemonic {
    AddImm,
    SubImm,
    MovImm,
    MovReg,
    AddReg,
    SubReg,
    AndReg,
    OrrReg,
    EorReg,
    CompareImm,
    CompareReg,
    Multiply,
    LoadImmOffset,
    StoreImmOffset,
    Branch,
    BranchLink,
    BranchExchange,
};

struct DecodedInstruction {
    Mnemonic mnemonic{};
    std::uint8_t rd{};
    std::uint8_t rn{};
    std::uint8_t rm{};
    std::uint32_t imm{};
    ir::Condition condition = ir::Condition::Al;
    std::int32_t branch_offset{};
    ir::ShiftKind shift_kind = ir::ShiftKind::Lsl;
    std::uint8_t shift_amount{};
    std::uint8_t mem_width = 4;
};

class Decoder {
public:
    std::optional<DecodedInstruction> decode(std::uint32_t word) const;
};

class Lifter {
public:
    ir::Program lift(const std::vector<std::uint32_t>& words) const;
};

const char* to_string(Mnemonic mnemonic) noexcept;

} // namespace myvmp::arm32
