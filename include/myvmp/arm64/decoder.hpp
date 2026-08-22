#pragma once

#include "myvmp/ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace myvmp::arm64 {

struct LiftContext {
    std::uint64_t region_va = 0;
    std::function<std::optional<std::uint64_t>(std::uint64_t address, std::uint8_t width)> read_memory;
};

enum class Mnemonic {
    Movz,
    Movn,
    Movk,
    AddImm,
    SubImm,
    AddImmSetFlags,
    SubImmSetFlags,
    AddShiftedReg,
    AddExtendedReg,
    AddExtendedRegSetFlags,
    SubShiftedReg,
    AddShiftedRegSetFlags,
    SubShiftedRegSetFlags,
    SubExtendedReg,
    SubExtendedRegSetFlags,
    AndImm,
    OrrImm,
    EorImm,
    AndShiftedReg,
    AndSetFlagsShiftedReg,
    OrrShiftedReg,
    EorShiftedReg,
    LslImm,
    LsrImm,
    AsrImm,
    RorImm,
    ShiftVariable,
    SignedDivide,
    UnsignedDivide,
    Ubfx,
    SignExtendReg,
    UnsignedMulHigh,
    MulAdd,
    MulSub,
    SignedMulLong,
    UnsignedMulLong,
    CompareImm,
    CompareReg,
    TestImm,
    BranchCond,
    BranchUncond,
    BranchLink,
    BranchReg,
    BranchLinkReg,
    BranchZero,
    BranchBit,
    Select,
    SelectInc,
    SelectInv,
    SelectNeg,
    LoadUnsigned,
    LoadSigned,
    LoadLiteral,
    LoadPreIndex,
    LoadPostIndex,
    LoadSignedPreIndex,
    LoadSignedPostIndex,
    StoreUnsigned,
    StorePreIndex,
    StorePostIndex,
    LoadRegOffset,
    LoadSignedRegOffset,
    StoreRegOffset,
    Store64PostIndex,
    LoadPair,
    LoadPairPreIndex,
    LoadPairPostIndex,
    LoadSignedPair,
    LoadSignedPairPreIndex,
    LoadSignedPairPostIndex,
    StorePair,
    StorePairPreIndex,
    StorePairPostIndex,
    LoadAcquire,
    StoreRelease,
    LoadExclusive,
    StoreExclusive,
    LoadExclusivePair,
    StoreExclusivePair,
    CompareSwap,
    Barrier,
    Adr,
    Adrp,
    FmovImm64,
    Fadd64,
    Fsub64,
    Fmul64,
    Fdiv64,
    Fmadd64,
    Fcompare64,
    FmovImm32,
    Fadd32,
    Fsub32,
    Fmul32,
    Fdiv32,
    Fmadd32,
    Fcompare32,
    FpUnary,
    FpConvert,
    FpMinMax,
    FpConditionalCompare,
    FpSelect,
    Ret,
};

struct DecodedInstruction {
    Mnemonic mnemonic{};
    std::uint8_t rd{};
    std::uint8_t rn{};
    std::uint8_t rm{};
    std::uint64_t imm{};
    ir::ShiftKind shift_kind = ir::ShiftKind::Lsl;
    std::uint8_t shift_amount = 0;
    ir::Condition condition = ir::Condition::Eq;
    std::int64_t branch_offset = 0;
    ir::ExtendKind extend_kind = ir::ExtendKind::Uxtx;
    bool writes_32bit = false;
    std::uint8_t mem_width = 8;
};

class Decoder {
public:
    std::optional<DecodedInstruction> decode(std::uint32_t word) const;
};

class Lifter {
public:
    ir::Program lift(const std::vector<std::uint32_t>& words) const;
    ir::Program lift(const std::vector<std::uint32_t>& words, std::uint64_t region_va) const;
    ir::Program lift(const std::vector<std::uint32_t>& words, const LiftContext& context) const;
    ir::Program lift(const std::vector<std::uint8_t>& region_bytes) const;
    ir::Program lift(const std::vector<std::uint8_t>& region_bytes, std::size_t instruction_word_count) const;
    ir::Program lift(const std::vector<std::uint8_t>& region_bytes,
                     std::size_t instruction_word_count,
                     std::uint64_t region_va) const;
    ir::Program lift(const std::vector<std::uint8_t>& region_bytes,
                     std::size_t instruction_word_count,
                     const LiftContext& context) const;

private:
    Decoder decoder_;
};

std::string to_string(Mnemonic mnemonic);

} // namespace myvmp::arm64
