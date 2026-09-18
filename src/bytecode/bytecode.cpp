#include "myvmp/bytecode/bytecode.hpp"

#include <array>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace myvmp::bytecode {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'M', 'V', 'M', 'P'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint32_t kStreamIndexMix = 0x9e3779b9u;
constexpr std::uint64_t kImmediateIndexMix = 0x9e3779b97f4a7c15ull;
constexpr std::uint32_t kBlockIdentityMix = 0x9e3779b9u;
constexpr std::uint32_t kLayoutShuffleSalt = 0xd1b54a35u;
constexpr std::uint32_t kLcgMultiplier = 1664525u;
constexpr std::uint32_t kLcgIncrement = 1013904223u;

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_i64(std::vector<std::uint8_t>& out, std::int64_t value) {
    append_u64(out, static_cast<std::uint64_t>(value));
}

std::uint8_t key_byte(std::uint32_t key, std::size_t index) {
    const auto mixed = key ^ static_cast<std::uint32_t>(index * kStreamIndexMix);
    return static_cast<std::uint8_t>((mixed >> ((index % 4) * 8)) & 0xffu);
}

bool supports_immediate_diversification(ir::Opcode opcode) {
    return opcode == ir::Opcode::LoadImm || opcode == ir::Opcode::AddImm ||
           opcode == ir::Opcode::SubImm || opcode == ir::Opcode::XorImm;
}

std::uint64_t immediate_mask(std::uint32_t seed, std::size_t instruction_index) {
    // SplitMix64's finalizer provides stable avalanche from a reproducible seed/index pair;
    // this is diversification, not a cryptographic secrecy primitive.
    auto mask = static_cast<std::uint64_t>(seed) ^
                (kImmediateIndexMix * (instruction_index + 1));
    mask ^= mask >> 30u;
    mask *= 0xbf58476d1ce4e5b9ull;
    mask ^= mask >> 27u;
    mask *= 0x94d049bb133111ebull;
    mask ^= mask >> 31u;
    return mask == 0 ? 0xa5a5a5a5a5a5a5a5ull : mask;
}

std::uint32_t immediate_expansion_count(const EncodeOptions& options,
                                        const ir::Instruction& instruction,
                                        std::size_t instruction_index,
                                        std::uint32_t block_id) {
    if (!options.diversify_immediates || !supports_immediate_diversification(instruction.opcode)) {
        return 1;
    }
    const auto diversified_index = instruction_index ^ (static_cast<std::size_t>(block_id) << 20u);
    return (immediate_mask(options.diversification_seed ^ (block_id * kBlockIdentityMix),
                           diversified_index) & 3u) == 0 ? 3u : 2u;
}

std::vector<std::uint32_t> compute_ir_block_ids(const std::vector<ir::Instruction>& instructions) {
    std::vector<bool> leaders(instructions.size(), false);
    if (!instructions.empty()) leaders[0] = true;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto& instruction = instructions[index];
        const auto has_target = instruction.opcode == ir::Opcode::BranchIf ||
                                instruction.opcode == ir::Opcode::BranchRegZero ||
                                instruction.opcode == ir::Opcode::BranchRegBit ||
                                instruction.opcode == ir::Opcode::CallVm;
        const auto terminates_block = has_target || instruction.opcode == ir::Opcode::ReturnReg;
        if (has_target) {
            if (instruction.target < leaders.size()) leaders[instruction.target] = true;
        }
        if (terminates_block) {
            if (index + 1 < leaders.size()) leaders[index + 1] = true;
        }
    }
    std::vector<std::uint32_t> block_ids(instructions.size());
    std::uint32_t block_id = 0;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        if (index != 0 && leaders[index]) ++block_id;
        block_ids[index] = block_id;
    }
    return block_ids;
}

} // namespace

void crypt_payload(std::vector<std::uint8_t>& payload, std::uint32_t key) {
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= key_byte(key, i);
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || 4 > bytes.size() - offset) throw std::runtime_error("bytecode u32 is truncated");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

void write_u32_le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    if (offset > bytes.size() || 4 > bytes.size() - offset) throw std::runtime_error("bytecode u32 is truncated");
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset + shift / 8] = static_cast<std::uint8_t>(value >> shift);
}

std::size_t operand_size(Opcode opcode) {
    switch (opcode) {
    case Opcode::CompareImmBranch: return 14;
    case Opcode::JumpBlock: return 4;
    case Opcode::CompareRegBranch: return 7;
    case Opcode::TestImmBranch: return 14;
    case Opcode::FpConditionalCompare: return 6;
    case Opcode::FpSelect: return 5;
    case Opcode::LoadExclusivePair: return 4;
    case Opcode::StoreExclusivePair: return 5;
    case Opcode::VectorBinary: case Opcode::VectorCompare:
    case Opcode::VectorShiftImm: case Opcode::VectorFpBinary: return 5;
    case Opcode::VectorSelect: case Opcode::VectorPermute: return 4;
    case Opcode::VectorLoad: case Opcode::VectorStore: return 11;
    case Opcode::VectorMoveImmZero: return 1;
    case Opcode::LoadImm64: case Opcode::LoadAddress64: case Opcode::LoadFpImm64:
    case Opcode::LoadFpImm32: return 9;
    case Opcode::AddImm64: case Opcode::SubImm64: case Opcode::AndImm64:
    case Opcode::OrImm64: case Opcode::XorImm64: return 10;
    case Opcode::AddImmSetFlags64: case Opcode::SubImmSetFlags64: return 11;
    case Opcode::XorRegShifted64: case Opcode::XorNotRegShifted64:
    case Opcode::AndRegShifted64: case Opcode::AddRegShifted64:
    case Opcode::SubRegShifted64: case Opcode::SubRegExtended64:
    case Opcode::BitClearRegShifted64: case Opcode::OrRegShifted64:
    case Opcode::OrNotRegShifted64: case Opcode::NotRegShifted64:
    case Opcode::NegReg64: case Opcode::NegRegSetFlags64:
    case Opcode::AddRegExtended64: case Opcode::ShiftRegVariable64:
    case Opcode::DivideReg64: return 5;
    case Opcode::SubRegShiftedSetFlags64: case Opcode::AddRegShiftedSetFlags64:
    case Opcode::AddRegExtendedSetFlags64: case Opcode::SubRegExtendedSetFlags64:
    case Opcode::MulAdd64: case Opcode::MulSub64:
    case Opcode::LoadRegOffsetMem: case Opcode::StoreRegOffsetMem: return 6;
    case Opcode::BitfieldExtract64: case Opcode::ShiftReg64: case Opcode::ShiftReg32:
    case Opcode::TestRegShifted64: case Opcode::TestRegShifted32:
    case Opcode::CallVm: case Opcode::FpMadd64: case Opcode::FpMadd32: return 4;
    case Opcode::UnsignedMulHigh64: case Opcode::SignedMulLong64:
    case Opcode::UnsignedMulLong64: case Opcode::FpAdd64: case Opcode::FpSub64:
    case Opcode::FpMul64: case Opcode::FpDiv64: case Opcode::FpAdd32:
    case Opcode::FpSub32: case Opcode::FpMul32: case Opcode::FpDiv32: return 3;
    case Opcode::MoveKeep64: case Opcode::Load64: return 10;
    case Opcode::MoveReg64: case Opcode::CompareReg64: case Opcode::FpCompare64:
    case Opcode::FpCompare32: return 2;
    case Opcode::FpUnary: case Opcode::FpConvert: return 3;
    case Opcode::FpMinMax: return 4;
    case Opcode::LoadAcquire: case Opcode::StoreRelease: case Opcode::LoadExclusive: return 3;
    case Opcode::StoreExclusive: case Opcode::CompareSwap: return 4;
    case Opcode::CompareImm64: case Opcode::TestImm64: case Opcode::CallNativeReg: return 9;
    case Opcode::BranchIf: case Opcode::Select64: case Opcode::SelectInc64:
    case Opcode::SelectInv64: case Opcode::SelectNeg64: return 5;
    case Opcode::BranchRegZero: return 6;
    case Opcode::BranchRegBit: return 7;
    case Opcode::Store64: return 18;
    case Opcode::LoadMem: case Opcode::StorePair64: return 11;
    case Opcode::StoreMem: return 19;
    case Opcode::CallNative: return 16;
    case Opcode::Return: return 1;
    case Opcode::NoOp: case Opcode::Halt: return 0;
    }
    throw std::invalid_argument("unknown bytecode opcode");
}

std::optional<std::size_t> control_target_offset(Opcode opcode) {
    switch (opcode) {
    case Opcode::BranchIf: return 2;
    case Opcode::CompareImmBranch: return 11;
    case Opcode::CompareRegBranch: return 4;
    case Opcode::TestImmBranch: return 11;
    case Opcode::BranchRegZero: return 3;
    case Opcode::BranchRegBit: return 4;
    case Opcode::CallVm: case Opcode::JumpBlock: return 1;
    default: return std::nullopt;
    }
}

std::uint8_t VmProfile::physical(Opcode opcode) const {
    switch (opcode) {
    case Opcode::VectorBinary: return vector_binary;
    case Opcode::VectorCompare: return vector_compare;
    case Opcode::VectorSelect: return vector_select;
    case Opcode::VectorShiftImm: return vector_shift_imm;
    case Opcode::VectorPermute: return vector_permute;
    case Opcode::VectorLoad: return vector_load;
    case Opcode::VectorStore: return vector_store;
    case Opcode::VectorFpBinary: return vector_fp_binary;
    case Opcode::VectorMoveImmZero: return vector_move_imm_zero;
    case Opcode::CompareImmBranch:
        return compare_imm_branch;
    case Opcode::JumpBlock:
        return jump_block;
    case Opcode::CompareRegBranch:
        return compare_reg_branch;
    case Opcode::TestImmBranch:
        return test_imm_branch;
    case Opcode::FpConditionalCompare: return fp_conditional_compare;
    case Opcode::FpSelect: return fp_select;
    case Opcode::LoadExclusivePair: return load_exclusive_pair;
    case Opcode::StoreExclusivePair: return store_exclusive_pair;
    case Opcode::LoadImm64:
        return load_imm64;
    case Opcode::LoadAddress64:
        return load_address64;
    case Opcode::AddImm64:
        return add_imm64;
    case Opcode::SubImm64:
        return sub_imm64;
    case Opcode::AndImm64:
        return and_imm64;
    case Opcode::OrImm64:
        return or_imm64;
    case Opcode::XorImm64:
        return xor_imm64;
    case Opcode::AddImmSetFlags64:
        return add_imm_set_flags64;
    case Opcode::SubImmSetFlags64:
        return sub_imm_set_flags64;
    case Opcode::AddRegShifted64:
        return add_reg_shifted64;
    case Opcode::AddRegShiftedSetFlags64:
        return add_reg_shifted_set_flags64;
    case Opcode::AddRegExtended64:
        return add_reg_extended64;
    case Opcode::AddRegExtendedSetFlags64:
        return add_reg_extended_set_flags64;
    case Opcode::SubRegShifted64:
        return sub_reg_shifted64;
    case Opcode::SubRegShiftedSetFlags64:
        return sub_reg_shifted_set_flags64;
    case Opcode::SubRegExtended64:
        return sub_reg_extended64;
    case Opcode::SubRegExtendedSetFlags64:
        return sub_reg_extended_set_flags64;
    case Opcode::NegReg64:
        return neg_reg64;
    case Opcode::NegRegSetFlags64:
        return neg_reg_set_flags64;
    case Opcode::AndRegShifted64:
        return and_reg_shifted64;
    case Opcode::BitClearRegShifted64:
        return bit_clear_reg_shifted64;
    case Opcode::NotRegShifted64:
        return not_reg_shifted64;
    case Opcode::OrRegShifted64:
        return or_reg_shifted64;
    case Opcode::OrNotRegShifted64:
        return or_not_reg_shifted64;
    case Opcode::XorRegShifted64:
        return xor_reg_shifted64;
    case Opcode::XorNotRegShifted64:
        return xor_not_reg_shifted64;
    case Opcode::ShiftReg64:
        return shift_reg64;
    case Opcode::ShiftReg32:
        return shift_reg32;
    case Opcode::ShiftRegVariable64:
        return shift_reg_variable64;
    case Opcode::DivideReg64:
        return divide_reg64;
    case Opcode::BitfieldExtract64:
        return bitfield_extract64;
    case Opcode::UnsignedMulHigh64:
        return unsigned_mul_high64;
    case Opcode::MulAdd64:
        return mul_add64;
    case Opcode::MulSub64:
        return mul_sub64;
    case Opcode::SignedMulLong64:
        return signed_mul_long64;
    case Opcode::UnsignedMulLong64:
        return unsigned_mul_long64;
    case Opcode::MoveKeep64:
        return move_keep64;
    case Opcode::MoveReg64:
        return move_reg64;
    case Opcode::CompareImm64:
        return compare_imm64;
    case Opcode::CompareReg64:
        return compare_reg64;
    case Opcode::TestImm64:
        return test_imm64;
    case Opcode::TestRegShifted64:
        return test_reg_shifted64;
    case Opcode::TestRegShifted32:
        return test_reg_shifted32;
    case Opcode::BranchIf:
        return branch_if;
    case Opcode::BranchRegZero:
        return branch_reg_zero;
    case Opcode::BranchRegBit:
        return branch_reg_bit;
    case Opcode::Select64:
        return select64;
    case Opcode::SelectInc64:
        return select_inc64;
    case Opcode::SelectInv64:
        return select_inv64;
    case Opcode::SelectNeg64:
        return select_neg64;
    case Opcode::Load64:
        return load64;
    case Opcode::Store64:
        return store64;
    case Opcode::LoadMem:
        return load_mem;
    case Opcode::StoreMem:
        return store_mem;
    case Opcode::LoadRegOffsetMem:
        return load_reg_offset_mem;
    case Opcode::StoreRegOffsetMem:
        return store_reg_offset_mem;
    case Opcode::StorePair64:
        return store_pair64;
    case Opcode::CallNative:
        return call_native;
    case Opcode::CallNativeReg:
        return call_native_reg;
    case Opcode::CallVm:
        return call_vm;
    case Opcode::LoadFpImm64: return load_fp_imm64;
    case Opcode::FpAdd64: return fp_add64;
    case Opcode::FpSub64: return fp_sub64;
    case Opcode::FpMul64: return fp_mul64;
    case Opcode::FpDiv64: return fp_div64;
    case Opcode::FpMadd64: return fp_madd64;
    case Opcode::FpCompare64: return fp_compare64;
    case Opcode::LoadFpImm32: return load_fp_imm32;
    case Opcode::FpAdd32: return fp_add32;
    case Opcode::FpSub32: return fp_sub32;
    case Opcode::FpMul32: return fp_mul32;
    case Opcode::FpDiv32: return fp_div32;
    case Opcode::FpMadd32: return fp_madd32;
    case Opcode::FpCompare32: return fp_compare32;
    case Opcode::LoadAcquire: return load_acquire;
    case Opcode::StoreRelease: return store_release;
    case Opcode::LoadExclusive: return load_exclusive;
    case Opcode::StoreExclusive: return store_exclusive;
    case Opcode::CompareSwap: return compare_swap;
    case Opcode::FpUnary: return fp_unary;
    case Opcode::FpConvert: return fp_convert;
    case Opcode::FpMinMax: return fp_min_max;
    case Opcode::NoOp:
        return no_op;
    case Opcode::Return:
        return ret;
    case Opcode::Halt:
        return halt;
    }
    throw std::invalid_argument("unknown logical opcode");
}

std::uint8_t VmProfile::duplicate_physical(Opcode opcode) const {
    switch (opcode) {
    case Opcode::LoadImm64: return load_imm64_duplicate;
    case Opcode::AddImm64: return add_imm64_duplicate;
    case Opcode::SubImm64: return sub_imm64_duplicate;
    case Opcode::XorImm64: return xor_imm64_duplicate;
    case Opcode::AndImm64: return and_imm64_duplicate;
    case Opcode::OrImm64: return or_imm64_duplicate;
    case Opcode::MoveReg64: return move_reg64_duplicate;
    default: return physical(opcode);
    }
}

Opcode VmProfile::logical(std::uint8_t physical_opcode) const {
    if (physical_opcode == vector_binary) return Opcode::VectorBinary;
    if (physical_opcode == vector_compare) return Opcode::VectorCompare;
    if (physical_opcode == vector_select) return Opcode::VectorSelect;
    if (physical_opcode == vector_shift_imm) return Opcode::VectorShiftImm;
    if (physical_opcode == vector_permute) return Opcode::VectorPermute;
    if (physical_opcode == vector_load) return Opcode::VectorLoad;
    if (physical_opcode == vector_store) return Opcode::VectorStore;
    if (physical_opcode == vector_fp_binary) return Opcode::VectorFpBinary;
    if (physical_opcode == vector_move_imm_zero) return Opcode::VectorMoveImmZero;
    if (physical_opcode == compare_imm_branch) return Opcode::CompareImmBranch;
    if (physical_opcode == jump_block) return Opcode::JumpBlock;
    if (physical_opcode == compare_reg_branch) return Opcode::CompareRegBranch;
    if (physical_opcode == test_imm_branch) return Opcode::TestImmBranch;
    if (physical_opcode == fp_conditional_compare) return Opcode::FpConditionalCompare;
    if (physical_opcode == fp_select) return Opcode::FpSelect;
    if (physical_opcode == load_exclusive_pair) return Opcode::LoadExclusivePair;
    if (physical_opcode == store_exclusive_pair) return Opcode::StoreExclusivePair;
    if (physical_opcode == load_imm64 || physical_opcode == load_imm64_duplicate) {
        return Opcode::LoadImm64;
    }
    if (physical_opcode == load_address64) {
        return Opcode::LoadAddress64;
    }
    if (physical_opcode == add_imm64 || physical_opcode == add_imm64_duplicate) {
        return Opcode::AddImm64;
    }
    if (physical_opcode == sub_imm64 || physical_opcode == sub_imm64_duplicate) {
        return Opcode::SubImm64;
    }
    if (physical_opcode == and_imm64 || physical_opcode == and_imm64_duplicate) {
        return Opcode::AndImm64;
    }
    if (physical_opcode == or_imm64 || physical_opcode == or_imm64_duplicate) {
        return Opcode::OrImm64;
    }
    if (physical_opcode == xor_imm64 || physical_opcode == xor_imm64_duplicate) {
        return Opcode::XorImm64;
    }
    if (physical_opcode == add_imm_set_flags64) {
        return Opcode::AddImmSetFlags64;
    }
    if (physical_opcode == sub_imm_set_flags64) {
        return Opcode::SubImmSetFlags64;
    }
    if (physical_opcode == add_reg_shifted64) {
        return Opcode::AddRegShifted64;
    }
    if (physical_opcode == add_reg_shifted_set_flags64) {
        return Opcode::AddRegShiftedSetFlags64;
    }
    if (physical_opcode == add_reg_extended64) {
        return Opcode::AddRegExtended64;
    }
    if (physical_opcode == add_reg_extended_set_flags64) {
        return Opcode::AddRegExtendedSetFlags64;
    }
    if (physical_opcode == sub_reg_shifted64) {
        return Opcode::SubRegShifted64;
    }
    if (physical_opcode == sub_reg_shifted_set_flags64) {
        return Opcode::SubRegShiftedSetFlags64;
    }
    if (physical_opcode == sub_reg_extended64) {
        return Opcode::SubRegExtended64;
    }
    if (physical_opcode == sub_reg_extended_set_flags64) {
        return Opcode::SubRegExtendedSetFlags64;
    }
    if (physical_opcode == neg_reg64) {
        return Opcode::NegReg64;
    }
    if (physical_opcode == neg_reg_set_flags64) {
        return Opcode::NegRegSetFlags64;
    }
    if (physical_opcode == and_reg_shifted64) {
        return Opcode::AndRegShifted64;
    }
    if (physical_opcode == bit_clear_reg_shifted64) {
        return Opcode::BitClearRegShifted64;
    }
    if (physical_opcode == not_reg_shifted64) {
        return Opcode::NotRegShifted64;
    }
    if (physical_opcode == or_reg_shifted64) {
        return Opcode::OrRegShifted64;
    }
    if (physical_opcode == or_not_reg_shifted64) {
        return Opcode::OrNotRegShifted64;
    }
    if (physical_opcode == xor_reg_shifted64) {
        return Opcode::XorRegShifted64;
    }
    if (physical_opcode == xor_not_reg_shifted64) {
        return Opcode::XorNotRegShifted64;
    }
    if (physical_opcode == shift_reg64) {
        return Opcode::ShiftReg64;
    }
    if (physical_opcode == shift_reg32) {
        return Opcode::ShiftReg32;
    }
    if (physical_opcode == shift_reg_variable64) {
        return Opcode::ShiftRegVariable64;
    }
    if (physical_opcode == divide_reg64) {
        return Opcode::DivideReg64;
    }
    if (physical_opcode == bitfield_extract64) {
        return Opcode::BitfieldExtract64;
    }
    if (physical_opcode == unsigned_mul_high64) {
        return Opcode::UnsignedMulHigh64;
    }
    if (physical_opcode == mul_add64) {
        return Opcode::MulAdd64;
    }
    if (physical_opcode == mul_sub64) {
        return Opcode::MulSub64;
    }
    if (physical_opcode == signed_mul_long64) {
        return Opcode::SignedMulLong64;
    }
    if (physical_opcode == unsigned_mul_long64) {
        return Opcode::UnsignedMulLong64;
    }
    if (physical_opcode == move_keep64) {
        return Opcode::MoveKeep64;
    }
    if (physical_opcode == move_reg64 || physical_opcode == move_reg64_duplicate) {
        return Opcode::MoveReg64;
    }
    if (physical_opcode == compare_imm64) {
        return Opcode::CompareImm64;
    }
    if (physical_opcode == compare_reg64) {
        return Opcode::CompareReg64;
    }
    if (physical_opcode == test_imm64) {
        return Opcode::TestImm64;
    }
    if (physical_opcode == test_reg_shifted64) {
        return Opcode::TestRegShifted64;
    }
    if (physical_opcode == test_reg_shifted32) {
        return Opcode::TestRegShifted32;
    }
    if (physical_opcode == branch_if) {
        return Opcode::BranchIf;
    }
    if (physical_opcode == branch_reg_zero) {
        return Opcode::BranchRegZero;
    }
    if (physical_opcode == branch_reg_bit) {
        return Opcode::BranchRegBit;
    }
    if (physical_opcode == select64) {
        return Opcode::Select64;
    }
    if (physical_opcode == select_inc64) {
        return Opcode::SelectInc64;
    }
    if (physical_opcode == select_inv64) {
        return Opcode::SelectInv64;
    }
    if (physical_opcode == select_neg64) {
        return Opcode::SelectNeg64;
    }
    if (physical_opcode == load64) {
        return Opcode::Load64;
    }
    if (physical_opcode == store64) {
        return Opcode::Store64;
    }
    if (physical_opcode == load_mem) {
        return Opcode::LoadMem;
    }
    if (physical_opcode == store_mem) {
        return Opcode::StoreMem;
    }
    if (physical_opcode == load_reg_offset_mem) {
        return Opcode::LoadRegOffsetMem;
    }
    if (physical_opcode == store_reg_offset_mem) {
        return Opcode::StoreRegOffsetMem;
    }
    if (physical_opcode == store_pair64) {
        return Opcode::StorePair64;
    }
    if (physical_opcode == call_native) {
        return Opcode::CallNative;
    }
    if (physical_opcode == call_native_reg) {
        return Opcode::CallNativeReg;
    }
    if (physical_opcode == call_vm) {
        return Opcode::CallVm;
    }
    if (physical_opcode == load_fp_imm64) return Opcode::LoadFpImm64;
    if (physical_opcode == fp_add64) return Opcode::FpAdd64;
    if (physical_opcode == fp_sub64) return Opcode::FpSub64;
    if (physical_opcode == fp_mul64) return Opcode::FpMul64;
    if (physical_opcode == fp_div64) return Opcode::FpDiv64;
    if (physical_opcode == fp_madd64) return Opcode::FpMadd64;
    if (physical_opcode == fp_compare64) return Opcode::FpCompare64;
    if (physical_opcode == load_fp_imm32) return Opcode::LoadFpImm32;
    if (physical_opcode == fp_add32) return Opcode::FpAdd32;
    if (physical_opcode == fp_sub32) return Opcode::FpSub32;
    if (physical_opcode == fp_mul32) return Opcode::FpMul32;
    if (physical_opcode == fp_div32) return Opcode::FpDiv32;
    if (physical_opcode == fp_madd32) return Opcode::FpMadd32;
    if (physical_opcode == fp_compare32) return Opcode::FpCompare32;
    if (physical_opcode == load_acquire) return Opcode::LoadAcquire;
    if (physical_opcode == store_release) return Opcode::StoreRelease;
    if (physical_opcode == load_exclusive) return Opcode::LoadExclusive;
    if (physical_opcode == store_exclusive) return Opcode::StoreExclusive;
    if (physical_opcode == compare_swap) return Opcode::CompareSwap;
    if (physical_opcode == fp_unary) return Opcode::FpUnary;
    if (physical_opcode == fp_convert) return Opcode::FpConvert;
    if (physical_opcode == fp_min_max) return Opcode::FpMinMax;
    if (physical_opcode == no_op) {
        return Opcode::NoOp;
    }
    if (physical_opcode == ret) {
        return Opcode::Return;
    }
    if (physical_opcode == halt) {
        return Opcode::Halt;
    }
    throw std::runtime_error("unknown VM opcode");
}

std::string VmProfile::manifest() const {
    std::ostringstream out;
    out << "{\n"
        << "  \"seed\": " << seed << ",\n"
        << "  \"dispatcher\": {\"organization\": \""
        << (dispatcher_xor_key == 0 ? "direct-compare" : "xor-domain-compare")
        << "\", \"xor_key\": " << static_cast<unsigned>(dispatcher_xor_key) << "},\n"
        << "  \"handler_variant_mask\": " << handler_variant_mask << ",\n"
        << "  \"handler_variants\": {\n"
        << "    \"AddImm\": " << ((handler_variant_mask & kVariantAddImm) != 0 ? "true" : "false") << ",\n"
        << "    \"SubImm\": " << ((handler_variant_mask & kVariantSubImm) != 0 ? "true" : "false") << ",\n"
        << "    \"XorImm\": " << ((handler_variant_mask & kVariantXorImm) != 0 ? "true" : "false") << ",\n"
        << "    \"AndImm\": " << ((handler_variant_mask & kVariantAndImm) != 0 ? "true" : "false") << ",\n"
        << "    \"OrImm\": " << ((handler_variant_mask & kVariantOrImm) != 0 ? "true" : "false") << ",\n"
        << "    \"MoveReg\": " << ((handler_variant_mask & kVariantMoveReg) != 0 ? "true" : "false") << ",\n"
        << "    \"CompareImm\": " << ((handler_variant_mask & kVariantCompareImm) != 0 ? "true" : "false") << ",\n"
        << "    \"CompareReg\": " << ((handler_variant_mask & kVariantCompareReg) != 0 ? "true" : "false") << ",\n"
        << "    \"TestImm\": " << ((handler_variant_mask & kVariantTestImm) != 0 ? "true" : "false") << ",\n"
        << "    \"Load64\": " << ((handler_variant_mask & kVariantLoad64) != 0 ? "true" : "false") << ",\n"
        << "    \"Store64\": " << ((handler_variant_mask & kVariantStore64) != 0 ? "true" : "false") << ",\n"
        << "    \"AddRegShifted\": " << ((handler_variant_mask & kVariantAddRegShifted) != 0 ? "true" : "false") << ",\n"
        << "    \"SubRegShifted\": " << ((handler_variant_mask & kVariantSubRegShifted) != 0 ? "true" : "false") << ",\n"
        << "    \"AndRegShifted\": " << ((handler_variant_mask & kVariantAndRegShifted) != 0 ? "true" : "false") << ",\n"
        << "    \"XorRegShifted\": " << ((handler_variant_mask & kVariantXorRegShifted) != 0 ? "true" : "false") << ",\n"
        << "    \"TestRegShifted\": " << ((handler_variant_mask & kVariantTestRegShifted) != 0 ? "true" : "false") << ",\n"
        << "    \"TestRegShifted32\": " << ((handler_variant_mask & kVariantTestRegShifted32) != 0 ? "true" : "false") << ",\n"
        << "    \"BranchIf\": " << ((handler_variant_mask & kVariantBranchIf) != 0 ? "true" : "false") << ",\n"
        << "    \"BranchRegZero\": " << ((handler_variant_mask & kVariantBranchRegZero) != 0 ? "true" : "false") << ",\n"
        << "    \"BranchRegBit\": " << ((handler_variant_mask & kVariantBranchRegBit) != 0 ? "true" : "false") << ",\n"
        << "    \"Select\": " << ((handler_variant_mask & kVariantSelect) != 0 ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"duplicate_opcodes\": {\n"
        << "    \"LoadImm64\": " << static_cast<unsigned>(load_imm64_duplicate) << ",\n"
        << "    \"AddImm64\": " << static_cast<unsigned>(add_imm64_duplicate) << ",\n"
        << "    \"SubImm64\": " << static_cast<unsigned>(sub_imm64_duplicate) << ",\n"
        << "    \"XorImm64\": " << static_cast<unsigned>(xor_imm64_duplicate) << ",\n"
        << "    \"AndImm64\": " << static_cast<unsigned>(and_imm64_duplicate) << ",\n"
        << "    \"OrImm64\": " << static_cast<unsigned>(or_imm64_duplicate) << ",\n"
        << "    \"MoveReg64\": " << static_cast<unsigned>(move_reg64_duplicate) << "\n"
        << "  },\n"
        << "  \"superoperators\": {\n"
        << "    \"CompareImmBranch\": " << static_cast<unsigned>(compare_imm_branch) << ",\n"
        << "    \"JumpBlock\": " << static_cast<unsigned>(jump_block) << ",\n"
        << "    \"CompareRegBranch\": " << static_cast<unsigned>(compare_reg_branch) << ",\n"
        << "    \"TestImmBranch\": " << static_cast<unsigned>(test_imm_branch) << "\n"
        << "  },\n"
        << "  \"conditional_fp_opcodes\": {\"FpConditionalCompare\": "
        << static_cast<unsigned>(fp_conditional_compare) << ", \"FpSelect\": "
        << static_cast<unsigned>(fp_select) << "},\n"
        << "  \"atomic_pair_opcodes\": {\"LoadExclusivePair\": "
        << static_cast<unsigned>(load_exclusive_pair) << ", \"StoreExclusivePair\": "
        << static_cast<unsigned>(store_exclusive_pair) << "},\n"
        << "  \"opcodes\": {\n"
        << "    \"LoadImm64\": " << static_cast<unsigned>(load_imm64) << ",\n"
        << "    \"LoadAddress64\": " << static_cast<unsigned>(load_address64) << ",\n"
        << "    \"AddImm64\": " << static_cast<unsigned>(add_imm64) << ",\n"
        << "    \"SubImm64\": " << static_cast<unsigned>(sub_imm64) << ",\n"
        << "    \"AndImm64\": " << static_cast<unsigned>(and_imm64) << ",\n"
        << "    \"OrImm64\": " << static_cast<unsigned>(or_imm64) << ",\n"
        << "    \"XorImm64\": " << static_cast<unsigned>(xor_imm64) << ",\n"
        << "    \"AddImmSetFlags64\": " << static_cast<unsigned>(add_imm_set_flags64) << ",\n"
        << "    \"SubImmSetFlags64\": " << static_cast<unsigned>(sub_imm_set_flags64) << ",\n"
        << "    \"AddRegShifted64\": " << static_cast<unsigned>(add_reg_shifted64) << ",\n"
        << "    \"AddRegShiftedSetFlags64\": " << static_cast<unsigned>(add_reg_shifted_set_flags64) << ",\n"
        << "    \"AddRegExtended64\": " << static_cast<unsigned>(add_reg_extended64) << ",\n"
        << "    \"AddRegExtendedSetFlags64\": " << static_cast<unsigned>(add_reg_extended_set_flags64) << ",\n"
        << "    \"SubRegShifted64\": " << static_cast<unsigned>(sub_reg_shifted64) << ",\n"
        << "    \"SubRegShiftedSetFlags64\": " << static_cast<unsigned>(sub_reg_shifted_set_flags64) << ",\n"
        << "    \"SubRegExtended64\": " << static_cast<unsigned>(sub_reg_extended64) << ",\n"
        << "    \"SubRegExtendedSetFlags64\": " << static_cast<unsigned>(sub_reg_extended_set_flags64) << ",\n"
        << "    \"NegReg64\": " << static_cast<unsigned>(neg_reg64) << ",\n"
        << "    \"NegRegSetFlags64\": " << static_cast<unsigned>(neg_reg_set_flags64) << ",\n"
        << "    \"AndRegShifted64\": " << static_cast<unsigned>(and_reg_shifted64) << ",\n"
        << "    \"BitClearRegShifted64\": " << static_cast<unsigned>(bit_clear_reg_shifted64) << ",\n"
        << "    \"NotRegShifted64\": " << static_cast<unsigned>(not_reg_shifted64) << ",\n"
        << "    \"OrRegShifted64\": " << static_cast<unsigned>(or_reg_shifted64) << ",\n"
        << "    \"OrNotRegShifted64\": " << static_cast<unsigned>(or_not_reg_shifted64) << ",\n"
        << "    \"XorRegShifted64\": " << static_cast<unsigned>(xor_reg_shifted64) << ",\n"
        << "    \"XorNotRegShifted64\": " << static_cast<unsigned>(xor_not_reg_shifted64) << ",\n"
        << "    \"ShiftReg64\": " << static_cast<unsigned>(shift_reg64) << ",\n"
        << "    \"ShiftReg32\": " << static_cast<unsigned>(shift_reg32) << ",\n"
        << "    \"ShiftRegVariable64\": " << static_cast<unsigned>(shift_reg_variable64) << ",\n"
        << "    \"DivideReg64\": " << static_cast<unsigned>(divide_reg64) << ",\n"
        << "    \"BitfieldExtract64\": " << static_cast<unsigned>(bitfield_extract64) << ",\n"
        << "    \"UnsignedMulHigh64\": " << static_cast<unsigned>(unsigned_mul_high64) << ",\n"
        << "    \"MulAdd64\": " << static_cast<unsigned>(mul_add64) << ",\n"
        << "    \"MulSub64\": " << static_cast<unsigned>(mul_sub64) << ",\n"
        << "    \"SignedMulLong64\": " << static_cast<unsigned>(signed_mul_long64) << ",\n"
        << "    \"UnsignedMulLong64\": " << static_cast<unsigned>(unsigned_mul_long64) << ",\n"
        << "    \"MoveKeep64\": " << static_cast<unsigned>(move_keep64) << ",\n"
        << "    \"MoveReg64\": " << static_cast<unsigned>(move_reg64) << ",\n"
        << "    \"CompareImm64\": " << static_cast<unsigned>(compare_imm64) << ",\n"
        << "    \"CompareReg64\": " << static_cast<unsigned>(compare_reg64) << ",\n"
        << "    \"TestImm64\": " << static_cast<unsigned>(test_imm64) << ",\n"
        << "    \"TestRegShifted64\": " << static_cast<unsigned>(test_reg_shifted64) << ",\n"
        << "    \"TestRegShifted32\": " << static_cast<unsigned>(test_reg_shifted32) << ",\n"
        << "    \"BranchIf\": " << static_cast<unsigned>(branch_if) << ",\n"
        << "    \"BranchRegZero\": " << static_cast<unsigned>(branch_reg_zero) << ",\n"
        << "    \"BranchRegBit\": " << static_cast<unsigned>(branch_reg_bit) << ",\n"
        << "    \"Select64\": " << static_cast<unsigned>(select64) << ",\n"
        << "    \"SelectInc64\": " << static_cast<unsigned>(select_inc64) << ",\n"
        << "    \"SelectInv64\": " << static_cast<unsigned>(select_inv64) << ",\n"
        << "    \"SelectNeg64\": " << static_cast<unsigned>(select_neg64) << ",\n"
        << "    \"Load64\": " << static_cast<unsigned>(load64) << ",\n"
        << "    \"Store64\": " << static_cast<unsigned>(store64) << ",\n"
        << "    \"LoadMem\": " << static_cast<unsigned>(load_mem) << ",\n"
        << "    \"StoreMem\": " << static_cast<unsigned>(store_mem) << ",\n"
        << "    \"LoadRegOffsetMem\": " << static_cast<unsigned>(load_reg_offset_mem) << ",\n"
        << "    \"StoreRegOffsetMem\": " << static_cast<unsigned>(store_reg_offset_mem) << ",\n"
        << "    \"StorePair64\": " << static_cast<unsigned>(store_pair64) << ",\n"
        << "    \"CallNative\": " << static_cast<unsigned>(call_native) << ",\n"
        << "    \"CallNativeReg\": " << static_cast<unsigned>(call_native_reg) << ",\n"
        << "    \"CallVm\": " << static_cast<unsigned>(call_vm) << ",\n"
        << "    \"LoadFpImm64\": " << static_cast<unsigned>(load_fp_imm64) << ",\n"
        << "    \"FpAdd64\": " << static_cast<unsigned>(fp_add64) << ",\n"
        << "    \"FpSub64\": " << static_cast<unsigned>(fp_sub64) << ",\n"
        << "    \"FpMul64\": " << static_cast<unsigned>(fp_mul64) << ",\n"
        << "    \"FpDiv64\": " << static_cast<unsigned>(fp_div64) << ",\n"
        << "    \"FpMadd64\": " << static_cast<unsigned>(fp_madd64) << ",\n"
        << "    \"FpCompare64\": " << static_cast<unsigned>(fp_compare64) << ",\n"
        << "    \"LoadFpImm32\": " << static_cast<unsigned>(load_fp_imm32) << ",\n"
        << "    \"FpAdd32\": " << static_cast<unsigned>(fp_add32) << ",\n"
        << "    \"FpSub32\": " << static_cast<unsigned>(fp_sub32) << ",\n"
        << "    \"FpMul32\": " << static_cast<unsigned>(fp_mul32) << ",\n"
        << "    \"FpDiv32\": " << static_cast<unsigned>(fp_div32) << ",\n"
        << "    \"FpMadd32\": " << static_cast<unsigned>(fp_madd32) << ",\n"
        << "    \"FpCompare32\": " << static_cast<unsigned>(fp_compare32) << ",\n"
        << "    \"LoadAcquire\": " << static_cast<unsigned>(load_acquire) << ",\n"
        << "    \"StoreRelease\": " << static_cast<unsigned>(store_release) << ",\n"
        << "    \"LoadExclusive\": " << static_cast<unsigned>(load_exclusive) << ",\n"
        << "    \"StoreExclusive\": " << static_cast<unsigned>(store_exclusive) << ",\n"
        << "    \"CompareSwap\": " << static_cast<unsigned>(compare_swap) << ",\n"
        << "    \"FpUnary\": " << static_cast<unsigned>(fp_unary) << ",\n"
        << "    \"FpConvert\": " << static_cast<unsigned>(fp_convert) << ",\n"
        << "    \"FpMinMax\": " << static_cast<unsigned>(fp_min_max) << ",\n"
        << "    \"VectorBinary\": " << static_cast<unsigned>(vector_binary) << ",\n"
        << "    \"VectorCompare\": " << static_cast<unsigned>(vector_compare) << ",\n"
        << "    \"VectorSelect\": " << static_cast<unsigned>(vector_select) << ",\n"
        << "    \"VectorShiftImm\": " << static_cast<unsigned>(vector_shift_imm) << ",\n"
        << "    \"VectorPermute\": " << static_cast<unsigned>(vector_permute) << ",\n"
        << "    \"VectorLoad\": " << static_cast<unsigned>(vector_load) << ",\n"
        << "    \"VectorStore\": " << static_cast<unsigned>(vector_store) << ",\n"
        << "    \"VectorFpBinary\": " << static_cast<unsigned>(vector_fp_binary) << ",\n"
        << "    \"VectorMoveImmZero\": " << static_cast<unsigned>(vector_move_imm_zero) << ",\n"
        << "    \"NoOp\": " << static_cast<unsigned>(no_op) << ",\n"
        << "    \"Return\": " << static_cast<unsigned>(ret) << ",\n"
        << "    \"Halt\": " << static_cast<unsigned>(halt) << "\n"
        << "  }\n"
        << "}";
    return out.str();
}

VmProfile fixed_profile() {
    return {};
}

VmProfile profile_from_seed(std::uint32_t seed) {
    std::array<std::uint8_t, 85> opcodes{
        static_cast<std::uint8_t>(Opcode::LoadImm64),
        static_cast<std::uint8_t>(Opcode::LoadAddress64),
        static_cast<std::uint8_t>(Opcode::AddImm64),
        static_cast<std::uint8_t>(Opcode::SubImm64),
        static_cast<std::uint8_t>(Opcode::AndImm64),
        static_cast<std::uint8_t>(Opcode::OrImm64),
        static_cast<std::uint8_t>(Opcode::XorImm64),
        static_cast<std::uint8_t>(Opcode::AddImmSetFlags64),
        static_cast<std::uint8_t>(Opcode::SubImmSetFlags64),
        static_cast<std::uint8_t>(Opcode::AddRegShifted64),
        static_cast<std::uint8_t>(Opcode::AddRegShiftedSetFlags64),
        static_cast<std::uint8_t>(Opcode::AddRegExtended64),
        static_cast<std::uint8_t>(Opcode::AddRegExtendedSetFlags64),
        static_cast<std::uint8_t>(Opcode::SubRegShifted64),
        static_cast<std::uint8_t>(Opcode::SubRegShiftedSetFlags64),
        static_cast<std::uint8_t>(Opcode::SubRegExtended64),
        static_cast<std::uint8_t>(Opcode::SubRegExtendedSetFlags64),
        static_cast<std::uint8_t>(Opcode::NegReg64),
        static_cast<std::uint8_t>(Opcode::NegRegSetFlags64),
        static_cast<std::uint8_t>(Opcode::AndRegShifted64),
        static_cast<std::uint8_t>(Opcode::BitClearRegShifted64),
        static_cast<std::uint8_t>(Opcode::NotRegShifted64),
        static_cast<std::uint8_t>(Opcode::OrRegShifted64),
        static_cast<std::uint8_t>(Opcode::OrNotRegShifted64),
        static_cast<std::uint8_t>(Opcode::XorRegShifted64),
        static_cast<std::uint8_t>(Opcode::XorNotRegShifted64),
        static_cast<std::uint8_t>(Opcode::ShiftReg64),
        static_cast<std::uint8_t>(Opcode::ShiftReg32),
        static_cast<std::uint8_t>(Opcode::ShiftRegVariable64),
        static_cast<std::uint8_t>(Opcode::DivideReg64),
        static_cast<std::uint8_t>(Opcode::BitfieldExtract64),
        static_cast<std::uint8_t>(Opcode::UnsignedMulHigh64),
        static_cast<std::uint8_t>(Opcode::MulAdd64),
        static_cast<std::uint8_t>(Opcode::MulSub64),
        static_cast<std::uint8_t>(Opcode::SignedMulLong64),
        static_cast<std::uint8_t>(Opcode::UnsignedMulLong64),
        static_cast<std::uint8_t>(Opcode::MoveKeep64),
        static_cast<std::uint8_t>(Opcode::MoveReg64),
        static_cast<std::uint8_t>(Opcode::CompareImm64),
        static_cast<std::uint8_t>(Opcode::CompareReg64),
        static_cast<std::uint8_t>(Opcode::TestImm64),
        static_cast<std::uint8_t>(Opcode::TestRegShifted64),
        static_cast<std::uint8_t>(Opcode::TestRegShifted32),
        static_cast<std::uint8_t>(Opcode::BranchIf),
        static_cast<std::uint8_t>(Opcode::BranchRegZero),
        static_cast<std::uint8_t>(Opcode::BranchRegBit),
        static_cast<std::uint8_t>(Opcode::Select64),
        static_cast<std::uint8_t>(Opcode::SelectInc64),
        static_cast<std::uint8_t>(Opcode::SelectInv64),
        static_cast<std::uint8_t>(Opcode::SelectNeg64),
        static_cast<std::uint8_t>(Opcode::Load64),
        static_cast<std::uint8_t>(Opcode::Store64),
        static_cast<std::uint8_t>(Opcode::LoadMem),
        static_cast<std::uint8_t>(Opcode::StoreMem),
        static_cast<std::uint8_t>(Opcode::LoadRegOffsetMem),
        static_cast<std::uint8_t>(Opcode::StoreRegOffsetMem),
        static_cast<std::uint8_t>(Opcode::StorePair64),
        static_cast<std::uint8_t>(Opcode::CallNative),
        static_cast<std::uint8_t>(Opcode::CallNativeReg),
        static_cast<std::uint8_t>(Opcode::CallVm),
        static_cast<std::uint8_t>(Opcode::LoadFpImm64),
        static_cast<std::uint8_t>(Opcode::FpAdd64),
        static_cast<std::uint8_t>(Opcode::FpSub64),
        static_cast<std::uint8_t>(Opcode::FpMul64),
        static_cast<std::uint8_t>(Opcode::FpDiv64),
        static_cast<std::uint8_t>(Opcode::FpMadd64),
        static_cast<std::uint8_t>(Opcode::FpCompare64),
        static_cast<std::uint8_t>(Opcode::LoadFpImm32),
        static_cast<std::uint8_t>(Opcode::FpAdd32),
        static_cast<std::uint8_t>(Opcode::FpSub32),
        static_cast<std::uint8_t>(Opcode::FpMul32),
        static_cast<std::uint8_t>(Opcode::FpDiv32),
        static_cast<std::uint8_t>(Opcode::FpMadd32),
        static_cast<std::uint8_t>(Opcode::FpCompare32),
        static_cast<std::uint8_t>(Opcode::LoadAcquire),
        static_cast<std::uint8_t>(Opcode::StoreRelease),
        static_cast<std::uint8_t>(Opcode::LoadExclusive),
        static_cast<std::uint8_t>(Opcode::StoreExclusive),
        static_cast<std::uint8_t>(Opcode::CompareSwap),
        static_cast<std::uint8_t>(Opcode::FpUnary),
        static_cast<std::uint8_t>(Opcode::FpConvert),
        static_cast<std::uint8_t>(Opcode::FpMinMax),
        static_cast<std::uint8_t>(Opcode::NoOp),
        static_cast<std::uint8_t>(Opcode::Return),
        static_cast<std::uint8_t>(Opcode::Halt),
    };

    auto state = seed == 0 ? 0xA341316Cu : seed;
    for (std::size_t i = opcodes.size() - 1; i > 0; --i) {
        state = state * kLcgMultiplier + kLcgIncrement;
        const auto j = static_cast<std::size_t>(state % (i + 1));
        std::swap(opcodes[i], opcodes[j]);
    }

    auto profile = VmProfile{
        seed,
        opcodes[0],
        opcodes[1],
        opcodes[2],
        opcodes[3],
        opcodes[4],
        opcodes[5],
        opcodes[6],
        opcodes[7],
        opcodes[8],
        opcodes[9],
        opcodes[10],
        opcodes[11],
        opcodes[12],
        opcodes[13],
        opcodes[14],
        opcodes[15],
        opcodes[16],
        opcodes[17],
        opcodes[18],
        opcodes[19],
        opcodes[20],
        opcodes[21],
        opcodes[22],
        opcodes[23],
        opcodes[24],
        opcodes[25],
        opcodes[26],
        opcodes[27],
        opcodes[28],
        opcodes[29],
        opcodes[30],
        opcodes[31],
        opcodes[32],
        opcodes[33],
        opcodes[34],
        opcodes[35],
        opcodes[36],
        opcodes[37],
        opcodes[38],
        opcodes[39],
        opcodes[40],
        opcodes[41],
        opcodes[42],
        opcodes[43],
        opcodes[44],
        opcodes[45],
        opcodes[46],
        opcodes[47],
        opcodes[48],
        opcodes[49],
        opcodes[50],
        opcodes[51],
        opcodes[52],
        opcodes[53],
        opcodes[54],
        opcodes[55],
        opcodes[56],
        opcodes[57],
        opcodes[58],
        opcodes[59],
        opcodes[60],
        opcodes[61],
        opcodes[62],
        opcodes[63], opcodes[64], opcodes[65], opcodes[66], opcodes[67], opcodes[68], opcodes[69],
        opcodes[70], opcodes[71], opcodes[72], opcodes[73], opcodes[74], opcodes[75], opcodes[76],
        opcodes[77], opcodes[78], opcodes[79], opcodes[80], opcodes[81], opcodes[82],
        opcodes[83], opcodes[84],
        ((seed * 0x9e3779b9u) ^ (seed * 0x85ebca6bu >> 11u)) & kAllHandlerVariants,
    };
    std::array<bool, 256> used{};
    used[0] = true;
    for (const auto opcode : opcodes) used[opcode] = true;
    std::array<std::uint8_t*, 15> duplicates{
        &profile.load_imm64_duplicate, &profile.add_imm64_duplicate,
        &profile.sub_imm64_duplicate, &profile.xor_imm64_duplicate,
        &profile.and_imm64_duplicate, &profile.or_imm64_duplicate,
        &profile.move_reg64_duplicate,
        &profile.compare_imm_branch,
        &profile.jump_block,
        &profile.compare_reg_branch,
        &profile.test_imm_branch,
        &profile.fp_conditional_compare,
        &profile.fp_select,
        &profile.load_exclusive_pair,
        &profile.store_exclusive_pair,
    };
    for (auto* duplicate : duplicates) {
        do {
            state = state * kLcgMultiplier + kLcgIncrement;
            *duplicate = static_cast<std::uint8_t>((state % 255u) + 1u);
        } while (used[*duplicate]);
        used[*duplicate] = true;
    }
    std::array<std::uint8_t*, 9> vector_opcodes{
        &profile.vector_binary, &profile.vector_compare, &profile.vector_select,
        &profile.vector_shift_imm, &profile.vector_permute, &profile.vector_load,
        &profile.vector_store, &profile.vector_fp_binary, &profile.vector_move_imm_zero,
    };
    for (auto* vector_opcode : vector_opcodes) {
        do {
            state = state * kLcgMultiplier + kLcgIncrement;
            *vector_opcode = static_cast<std::uint8_t>((state % 255u) + 1u);
        } while (used[*vector_opcode]);
        used[*vector_opcode] = true;
    }
    // Keep zero reserved for the fixed direct-compare dispatcher. Odd seeds and even
    // seeds deliberately land in different transformed opcode domains.
    state = state * kLcgMultiplier + kLcgIncrement;
    profile.dispatcher_xor_key = static_cast<std::uint8_t>((state % 255u) + 1u);
    return profile;
}

Image encode(const ir::Program& program, EncodeOptions options) {
    program.validate();

    const auto& instructions = program.instructions();
    const auto block_ids = compute_ir_block_ids(instructions);
    const auto can_fuse_flag_branch = [&](std::size_t index) {
        return options.diversify_immediates && index + 1 < instructions.size() &&
               (instructions[index].opcode == ir::Opcode::CompareImm ||
                instructions[index].opcode == ir::Opcode::CompareReg ||
                instructions[index].opcode == ir::Opcode::TestImm) &&
               instructions[index + 1].opcode == ir::Opcode::BranchIf &&
               block_ids[index] == block_ids[index + 1];
    };
    std::vector<std::uint32_t> instruction_map(instructions.size() + 1);
    std::uint32_t encoded_index = 0;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        instruction_map[index] = encoded_index;
        if (can_fuse_flag_branch(index)) {
            instruction_map[index + 1] = encoded_index;
            ++encoded_index;
            ++index;
            continue;
        }
        encoded_index += immediate_expansion_count(options, instructions[index], index, block_ids[index]);
    }
    instruction_map.back() = encoded_index;

    const auto mapped_target = [&instruction_map](std::uint32_t target) {
        if (target >= instruction_map.size() - 1) {
            throw std::runtime_error("bytecode branch target is outside the IR program");
        }
        return instruction_map[target];
    };

    std::vector<std::uint8_t> payload;
    std::vector<std::size_t> instruction_payload_offsets(instructions.size() + 1);
    for (std::size_t instruction_index = 0; instruction_index < instructions.size(); ++instruction_index) {
        instruction_payload_offsets[instruction_index] = payload.size();
        const auto& inst = instructions[instruction_index];
        const auto block_id = block_ids[instruction_index];
        const auto block_seed = options.diversification_seed ^ (block_id * kBlockIdentityMix);
        const auto diversified_index = instruction_index ^ (static_cast<std::size_t>(block_id) << 20u);
        const auto physical = [&](Opcode opcode, std::uint32_t salt) {
            const auto use_duplicate = options.diversify_immediates &&
                ((immediate_mask(block_seed ^ salt, diversified_index + salt) >> 7u) & 1u) != 0;
            return use_duplicate ? options.profile.duplicate_physical(opcode)
                                 : options.profile.physical(opcode);
        };
        switch (inst.opcode) {
        case ir::Opcode::LoadImm: {
            if (!options.diversify_immediates) {
                append_u8(payload, physical(Opcode::LoadImm64, 1));
                append_u8(payload, inst.dst);
                append_u64(payload, inst.imm);
                break;
            }
            const auto mask = immediate_mask(block_seed, diversified_index);
            const auto mask2 = immediate_mask(block_seed ^ 0x6a09e667u, diversified_index + 0x100u);
            const auto use_three = immediate_expansion_count(options, inst, instruction_index, block_id) == 3;
            append_u8(payload, physical(Opcode::LoadImm64, 2));
            append_u8(payload, inst.dst);
            if (use_three) {
                append_u64(payload, inst.imm ^ mask ^ mask2);
                append_u8(payload, physical(Opcode::XorImm64, 3));
            } else if ((mask & 1u) == 0) {
                append_u64(payload, inst.imm ^ mask);
                append_u8(payload, physical(Opcode::XorImm64, 4));
            } else {
                append_u64(payload, inst.imm - mask);
                append_u8(payload, physical(Opcode::AddImm64, 5));
            }
            append_u8(payload, inst.dst);
            append_u8(payload, inst.dst);
            append_u64(payload, mask);
            if (use_three) {
                append_u8(payload, physical(Opcode::XorImm64, 6));
                append_u8(payload, inst.dst);
                append_u8(payload, inst.dst);
                append_u64(payload, mask2);
            }
            break;
        }
        case ir::Opcode::LoadAddress:
            append_u8(payload, options.profile.physical(Opcode::LoadAddress64));
            append_u8(payload, inst.dst);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::AddImm: {
            const auto mask = immediate_mask(block_seed, diversified_index);
            append_u8(payload, physical(Opcode::AddImm64, 7));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, options.diversify_immediates ? mask : inst.imm);
            if (options.diversify_immediates) {
                const auto use_three = immediate_expansion_count(options, inst, instruction_index, block_id) == 3;
                const auto mask2 = immediate_mask(block_seed ^ 0xbb67ae85u, diversified_index + 0x200u);
                if (use_three) {
                    append_u8(payload, physical(Opcode::SubImm64, 8));
                    append_u8(payload, inst.dst); append_u8(payload, inst.dst); append_u64(payload, mask2);
                }
                append_u8(payload, physical(Opcode::AddImm64, 9));
                append_u8(payload, inst.dst);
                append_u8(payload, inst.dst);
                append_u64(payload, inst.imm - mask + (use_three ? mask2 : 0));
            }
            break;
        }
        case ir::Opcode::SubImm: {
            const auto mask = immediate_mask(block_seed, diversified_index);
            append_u8(payload, physical(Opcode::SubImm64, 10));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, options.diversify_immediates ? mask : inst.imm);
            if (options.diversify_immediates) {
                const auto use_three = immediate_expansion_count(options, inst, instruction_index, block_id) == 3;
                const auto mask2 = immediate_mask(block_seed ^ 0x3c6ef372u, diversified_index + 0x300u);
                if (use_three) {
                    append_u8(payload, physical(Opcode::AddImm64, 11));
                    append_u8(payload, inst.dst); append_u8(payload, inst.dst); append_u64(payload, mask2);
                }
                append_u8(payload, physical(Opcode::SubImm64, 12));
                append_u8(payload, inst.dst);
                append_u8(payload, inst.dst);
                append_u64(payload, inst.imm - mask + (use_three ? mask2 : 0));
            }
            break;
        }
        case ir::Opcode::AndImm:
            append_u8(payload, physical(Opcode::AndImm64, 16));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::OrImm:
            append_u8(payload, physical(Opcode::OrImm64, 17));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::XorImm: {
            const auto mask = immediate_mask(block_seed, diversified_index);
            append_u8(payload, physical(Opcode::XorImm64, 13));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, options.diversify_immediates ? mask : inst.imm);
            if (options.diversify_immediates) {
                const auto use_three = immediate_expansion_count(options, inst, instruction_index, block_id) == 3;
                const auto mask2 = immediate_mask(block_seed ^ 0xa54ff53au, diversified_index + 0x400u);
                if (use_three) {
                    append_u8(payload, physical(Opcode::XorImm64, 14));
                    append_u8(payload, inst.dst); append_u8(payload, inst.dst); append_u64(payload, mask2);
                }
                append_u8(payload, physical(Opcode::XorImm64, 15));
                append_u8(payload, inst.dst);
                append_u8(payload, inst.dst);
                append_u64(payload, inst.imm ^ mask ^ (use_three ? mask2 : 0));
            }
            break;
        }
        case ir::Opcode::AddImmSetFlags:
            append_u8(payload, options.profile.physical(Opcode::AddImmSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::SubImmSetFlags:
            append_u8(payload, options.profile.physical(Opcode::SubImmSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::AddRegShifted:
            append_u8(payload, options.profile.physical(Opcode::AddRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::AddRegShiftedSetFlags:
            append_u8(payload, options.profile.physical(Opcode::AddRegShiftedSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::AddRegExtended:
            append_u8(payload, options.profile.physical(Opcode::AddRegExtended64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::AddRegExtendedSetFlags:
            append_u8(payload, options.profile.physical(Opcode::AddRegExtendedSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::SubRegShifted:
            append_u8(payload, options.profile.physical(Opcode::SubRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::SubRegShiftedSetFlags:
            append_u8(payload, options.profile.physical(Opcode::SubRegShiftedSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::SubRegExtended:
            append_u8(payload, options.profile.physical(Opcode::SubRegExtended64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::SubRegExtendedSetFlags:
            append_u8(payload, options.profile.physical(Opcode::SubRegExtendedSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::NegReg:
            append_u8(payload, options.profile.physical(Opcode::NegReg64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::NegRegSetFlags:
            append_u8(payload, options.profile.physical(Opcode::NegRegSetFlags64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::AndRegShifted:
            append_u8(payload, options.profile.physical(Opcode::AndRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::BitClearRegShifted:
            append_u8(payload, options.profile.physical(Opcode::BitClearRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::NotRegShifted:
            append_u8(payload, options.profile.physical(Opcode::NotRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::OrRegShifted:
            append_u8(payload, options.profile.physical(Opcode::OrRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::OrNotRegShifted:
            append_u8(payload, options.profile.physical(Opcode::OrNotRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::XorRegShifted:
            append_u8(payload, options.profile.physical(Opcode::XorRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::XorNotRegShifted:
            append_u8(payload, options.profile.physical(Opcode::XorNotRegShifted64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::ShiftReg:
            append_u8(payload, options.profile.physical(Opcode::ShiftReg64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::ShiftReg32:
            append_u8(payload, options.profile.physical(Opcode::ShiftReg32));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::ShiftRegVariable:
            append_u8(payload, options.profile.physical(Opcode::ShiftRegVariable64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::DivideReg:
            append_u8(payload, options.profile.physical(Opcode::DivideReg64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm != 0));
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::BitfieldExtract:
            append_u8(payload, options.profile.physical(Opcode::BitfieldExtract64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.shift_amount);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm));
            break;
        case ir::Opcode::UnsignedMulHigh:
            append_u8(payload, options.profile.physical(Opcode::UnsignedMulHigh64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            break;
        case ir::Opcode::MulAdd:
            append_u8(payload, options.profile.physical(Opcode::MulAdd64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & ir::kMulAccumulatorZero));
            break;
        case ir::Opcode::MulSub:
            append_u8(payload, options.profile.physical(Opcode::MulSub64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & ir::kMulAccumulatorZero));
            break;
        case ir::Opcode::SignedMulLong:
            append_u8(payload, options.profile.physical(Opcode::SignedMulLong64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            break;
        case ir::Opcode::UnsignedMulLong:
            append_u8(payload, options.profile.physical(Opcode::UnsignedMulLong64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            break;
        case ir::Opcode::MoveKeep:
            append_u8(payload, options.profile.physical(Opcode::MoveKeep64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.shift_amount);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::MoveReg:
            append_u8(payload, physical(Opcode::MoveReg64, 18));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            break;
        case ir::Opcode::CompareImm:
            if (can_fuse_flag_branch(instruction_index)) {
                const auto& branch = instructions[instruction_index + 1];
                append_u8(payload, options.profile.physical(Opcode::CompareImmBranch));
                append_u8(payload, inst.src);
                append_u64(payload, inst.imm);
                append_u8(payload, static_cast<std::uint8_t>(branch.condition));
                append_u32(payload, mapped_target(branch.target));
                instruction_payload_offsets[instruction_index + 1] = payload.size();
                ++instruction_index;
                break;
            }
            append_u8(payload, options.profile.physical(Opcode::CompareImm64));
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::CompareReg:
            if (can_fuse_flag_branch(instruction_index)) {
                const auto& branch = instructions[instruction_index + 1];
                append_u8(payload, options.profile.physical(Opcode::CompareRegBranch));
                append_u8(payload, inst.src);
                append_u8(payload, inst.rhs);
                append_u8(payload, static_cast<std::uint8_t>(branch.condition));
                append_u32(payload, mapped_target(branch.target));
                instruction_payload_offsets[instruction_index + 1] = payload.size();
                ++instruction_index;
                break;
            }
            append_u8(payload, options.profile.physical(Opcode::CompareReg64));
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            break;
        case ir::Opcode::TestImm:
            if (can_fuse_flag_branch(instruction_index)) {
                const auto& branch = instructions[instruction_index + 1];
                append_u8(payload, options.profile.physical(Opcode::TestImmBranch));
                append_u8(payload, inst.src);
                append_u64(payload, inst.imm);
                append_u8(payload, static_cast<std::uint8_t>(branch.condition));
                append_u32(payload, mapped_target(branch.target));
                instruction_payload_offsets[instruction_index + 1] = payload.size();
                ++instruction_index;
                break;
            }
            append_u8(payload, options.profile.physical(Opcode::TestImm64));
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::TestRegShifted:
            append_u8(payload, options.profile.physical(Opcode::TestRegShifted64));
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::TestRegShifted32:
            append_u8(payload, options.profile.physical(Opcode::TestRegShifted32));
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.shift_kind));
            append_u8(payload, inst.shift_amount);
            break;
        case ir::Opcode::BranchIf:
            append_u8(payload, options.profile.physical(Opcode::BranchIf));
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u32(payload, mapped_target(inst.target));
            break;
        case ir::Opcode::BranchRegZero:
            append_u8(payload, options.profile.physical(Opcode::BranchRegZero));
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm));
            append_u32(payload, mapped_target(inst.target));
            break;
        case ir::Opcode::BranchRegBit:
            append_u8(payload, options.profile.physical(Opcode::BranchRegBit));
            append_u8(payload, inst.src);
            append_u8(payload, inst.shift_amount);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm));
            append_u32(payload, mapped_target(inst.target));
            break;
        case ir::Opcode::Select:
            append_u8(payload, options.profile.physical(Opcode::Select64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & (ir::kSelectTrueZero | ir::kSelectFalseZero)));
            break;
        case ir::Opcode::SelectInc:
            append_u8(payload, options.profile.physical(Opcode::SelectInc64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & (ir::kSelectTrueZero | ir::kSelectFalseZero)));
            break;
        case ir::Opcode::SelectInv:
            append_u8(payload, options.profile.physical(Opcode::SelectInv64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & (ir::kSelectTrueZero | ir::kSelectFalseZero)));
            break;
        case ir::Opcode::SelectNeg:
            append_u8(payload, options.profile.physical(Opcode::SelectNeg64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, static_cast<std::uint8_t>(inst.imm & (ir::kSelectTrueZero | ir::kSelectFalseZero)));
            break;
        case ir::Opcode::Load64:
            append_u8(payload, options.profile.physical(Opcode::Load64));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::Store64:
            append_u8(payload, options.profile.physical(Opcode::Store64));
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            append_i64(payload, inst.post_index);
            break;
        case ir::Opcode::LoadMem:
            append_u8(payload, options.profile.physical(Opcode::LoadMem));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.mem_width);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::StoreMem:
            append_u8(payload, options.profile.physical(Opcode::StoreMem));
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u8(payload, inst.mem_width);
            append_u64(payload, inst.imm);
            append_i64(payload, inst.post_index);
            break;
        case ir::Opcode::LoadRegOffsetMem:
            append_u8(payload, options.profile.physical(Opcode::LoadRegOffsetMem));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::StoreRegOffsetMem:
            append_u8(payload, options.profile.physical(Opcode::StoreRegOffsetMem));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.extend_kind));
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::StorePair64:
            append_u8(payload, options.profile.physical(Opcode::StorePair64));
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u64(payload, inst.imm);
            break;
        case ir::Opcode::LoadAcquire:
            append_u8(payload, options.profile.physical(Opcode::LoadAcquire));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::StoreRelease:
            append_u8(payload, options.profile.physical(Opcode::StoreRelease));
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u8(payload, inst.mem_width);
            break;
        case ir::Opcode::LoadExclusive:
            append_u8(payload, options.profile.physical(Opcode::LoadExclusive));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.mem_width | (inst.imm != 0 ? 0x80u : 0u)));
            break;
        case ir::Opcode::StoreExclusive:
            append_u8(payload, options.profile.physical(Opcode::StoreExclusive));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.mem_width | (inst.imm != 0 ? 0x80u : 0u)));
            break;
        case ir::Opcode::LoadExclusivePair:
            append_u8(payload, options.profile.physical(Opcode::LoadExclusivePair));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.mem_width | (inst.imm != 0 ? 0x80u : 0u)));
            break;
        case ir::Opcode::StoreExclusivePair:
            append_u8(payload, options.profile.physical(Opcode::StoreExclusivePair));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.mem_width | (inst.imm != 0 ? 0x80u : 0u)));
            break;
        case ir::Opcode::CompareSwap:
            append_u8(payload, options.profile.physical(Opcode::CompareSwap));
            append_u8(payload, inst.dst);
            append_u8(payload, inst.rhs);
            append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.mem_width | ((inst.imm & 3u) << 6u)));
            break;
        case ir::Opcode::CallNative:
            append_u8(payload, options.profile.physical(Opcode::CallNative));
            append_u64(payload, inst.imm);
            append_u64(payload, static_cast<std::uint64_t>(inst.post_index));
            break;
        case ir::Opcode::CallNativeReg:
            append_u8(payload, options.profile.physical(Opcode::CallNativeReg));
            append_u8(payload, inst.src);
            append_u64(payload, static_cast<std::uint64_t>(inst.post_index));
            break;
        case ir::Opcode::CallVm:
            append_u8(payload, options.profile.physical(Opcode::CallVm));
            append_u32(payload, mapped_target(inst.target));
            break;
        case ir::Opcode::LoadFpImm64:
            append_u8(payload, options.profile.physical(Opcode::LoadFpImm64)); append_u8(payload, inst.dst); append_u64(payload, inst.imm); break;
        case ir::Opcode::FpAdd64:
        case ir::Opcode::FpSub64:
        case ir::Opcode::FpMul64:
        case ir::Opcode::FpDiv64: {
            const auto op = inst.opcode == ir::Opcode::FpAdd64 ? Opcode::FpAdd64 : inst.opcode == ir::Opcode::FpSub64 ? Opcode::FpSub64 : inst.opcode == ir::Opcode::FpMul64 ? Opcode::FpMul64 : Opcode::FpDiv64;
            append_u8(payload, options.profile.physical(op)); append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs); break;
        }
        case ir::Opcode::FpMadd64:
            append_u8(payload, options.profile.physical(Opcode::FpMadd64)); append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs); append_u8(payload, inst.shift_amount); break;
        case ir::Opcode::FpCompare64:
            append_u8(payload, options.profile.physical(Opcode::FpCompare64)); append_u8(payload, inst.src); append_u8(payload, inst.rhs); break;
        case ir::Opcode::LoadFpImm32:
            append_u8(payload, options.profile.physical(Opcode::LoadFpImm32)); append_u8(payload, inst.dst); append_u64(payload, inst.imm); break;
        case ir::Opcode::FpAdd32:
        case ir::Opcode::FpSub32:
        case ir::Opcode::FpMul32:
        case ir::Opcode::FpDiv32: {
            const auto op = inst.opcode == ir::Opcode::FpAdd32 ? Opcode::FpAdd32 : inst.opcode == ir::Opcode::FpSub32 ? Opcode::FpSub32 : inst.opcode == ir::Opcode::FpMul32 ? Opcode::FpMul32 : Opcode::FpDiv32;
            append_u8(payload, options.profile.physical(op)); append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs); break;
        }
        case ir::Opcode::FpMadd32:
            append_u8(payload, options.profile.physical(Opcode::FpMadd32)); append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs); append_u8(payload, inst.shift_amount); break;
        case ir::Opcode::FpCompare32:
            append_u8(payload, options.profile.physical(Opcode::FpCompare32)); append_u8(payload, inst.src); append_u8(payload, inst.rhs); break;
        case ir::Opcode::FpUnary:
            append_u8(payload, options.profile.physical(Opcode::FpUnary));
            append_u8(payload, inst.dst); append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); break;
        case ir::Opcode::FpConvert:
            append_u8(payload, options.profile.physical(Opcode::FpConvert));
            append_u8(payload, inst.dst); append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); break;
        case ir::Opcode::FpMinMax:
            append_u8(payload, options.profile.physical(Opcode::FpMinMax));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); break;
        case ir::Opcode::FpConditionalCompare:
            append_u8(payload, options.profile.physical(Opcode::FpConditionalCompare));
            append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, static_cast<std::uint8_t>(inst.imm));
            append_u8(payload, inst.mem_width); append_u8(payload, inst.shift_amount); break;
        case ir::Opcode::FpSelect:
            append_u8(payload, options.profile.physical(Opcode::FpSelect));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.condition));
            append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorBinary:
            append_u8(payload, options.profile.physical(Opcode::VectorBinary));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorCompare:
            append_u8(payload, options.profile.physical(Opcode::VectorCompare));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorSelect:
            append_u8(payload, options.profile.physical(Opcode::VectorSelect));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); break;
        case ir::Opcode::VectorShiftImm:
            append_u8(payload, options.profile.physical(Opcode::VectorShiftImm));
            append_u8(payload, inst.dst); append_u8(payload, inst.src);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); append_u8(payload, inst.shift_amount);
            append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorPermute:
            append_u8(payload, options.profile.physical(Opcode::VectorPermute));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>((inst.imm << 5u) | inst.shift_amount)); break;
        case ir::Opcode::VectorLoad:
            append_u8(payload, options.profile.physical(Opcode::VectorLoad));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u64(payload, inst.imm);
            append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorStore:
            append_u8(payload, options.profile.physical(Opcode::VectorStore));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u64(payload, inst.imm);
            append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorFpBinary:
            append_u8(payload, options.profile.physical(Opcode::VectorFpBinary));
            append_u8(payload, inst.dst); append_u8(payload, inst.src); append_u8(payload, inst.rhs);
            append_u8(payload, static_cast<std::uint8_t>(inst.imm)); append_u8(payload, inst.mem_width); break;
        case ir::Opcode::VectorMoveImmZero:
            append_u8(payload, options.profile.physical(Opcode::VectorMoveImmZero));
            append_u8(payload, inst.dst); break;
        case ir::Opcode::NoOp:
            append_u8(payload, options.profile.physical(Opcode::NoOp));
            break;
        case ir::Opcode::ReturnReg:
            append_u8(payload, options.profile.physical(Opcode::Return));
            append_u8(payload, inst.src);
            break;
        }
    }
    instruction_payload_offsets.back() = payload.size();
    append_u8(payload, options.profile.physical(Opcode::Halt));

    std::vector<BasicBlock> blocks;
    for (std::size_t first = 0; first < instructions.size();) {
        const auto id = block_ids[first];
        auto end = first + 1;
        while (end < instructions.size() && block_ids[end] == id) ++end;

        BasicBlock block;
        block.id = id;
        block.first_ir_instruction = static_cast<std::uint32_t>(first);
        block.ir_instruction_count = static_cast<std::uint32_t>(end - first);
        block.first_vm_instruction = instruction_map[first];
        block.vm_instruction_count = instruction_map[end] - instruction_map[first];
        block.payload_offset = instruction_payload_offsets[first];
        block.payload_size = instruction_payload_offsets[end] - instruction_payload_offsets[first];

        const auto add_successor = [&block, &block_ids, &instructions](std::uint32_t target) {
            if (target >= instructions.size()) return;
            const auto successor = block_ids[target];
            if (std::find(block.successors.begin(), block.successors.end(), successor) ==
                block.successors.end()) block.successors.push_back(successor);
        };
        const auto& tail = instructions[end - 1];
        const auto has_target = tail.opcode == ir::Opcode::BranchIf ||
                                tail.opcode == ir::Opcode::BranchRegZero ||
                                tail.opcode == ir::Opcode::BranchRegBit ||
                                tail.opcode == ir::Opcode::CallVm;
        if (has_target) add_successor(tail.target);
        const auto unconditional_branch = tail.opcode == ir::Opcode::BranchIf &&
                                          tail.condition == ir::Condition::Al;
        if (tail.opcode != ir::Opcode::ReturnReg && !unconditional_branch &&
            end < instructions.size()) {
            add_successor(static_cast<std::uint32_t>(end));
        }
        blocks.push_back(std::move(block));
        first = end;
    }
    if (!blocks.empty()) {
        // The terminal Halt is a format sentinel and belongs to the final physical block.
        ++blocks.back().payload_size;
        ++blocks.back().vm_instruction_count;
    }

    if (options.shuffle_blocks && blocks.size() > 1) {
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            if (blocks[index].id != index) {
                throw std::runtime_error("bytecode block IDs are not dense source-order identities");
            }
        }
        std::vector<std::vector<std::uint8_t>> block_payloads;
        block_payloads.reserve(blocks.size());
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const auto& block = blocks[index];
            block_payloads.emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(block.payload_offset),
                                        payload.begin() + static_cast<std::ptrdiff_t>(block.payload_offset + block.payload_size));
            const auto ir_end = block.first_ir_instruction + block.ir_instruction_count;
            const auto& tail = instructions[ir_end - 1];
            const auto unconditional = tail.opcode == ir::Opcode::BranchIf &&
                                       tail.condition == ir::Condition::Al;
            if (tail.opcode != ir::Opcode::ReturnReg && !unconditional &&
                ir_end < instructions.size() && !block.successors.empty()) {
                auto& bytes = block_payloads.back();
                append_u8(bytes, options.profile.physical(Opcode::JumpBlock));
                append_u32(bytes, block.successors.back());
                ++blocks[index].vm_instruction_count;
                blocks[index].payload_size += 5;
            }
        }

        std::vector<std::uint32_t> order(blocks.size());
        for (std::uint32_t index = 0; index < order.size(); ++index) order[index] = index;
        // Block zero is the entry and remains first; only the remaining physical layout moves.
        auto shuffle_state = options.diversification_seed ^ kLayoutShuffleSalt;
        for (std::size_t index = order.size() - 1; index > 1; --index) {
            shuffle_state = shuffle_state * kLcgMultiplier + kLcgIncrement;
            const auto selected = 1u + static_cast<std::size_t>(shuffle_state % index);
            std::swap(order[index], order[selected]);
        }

        std::vector<std::size_t> block_offsets(blocks.size());
        std::size_t next_offset = 0;
        for (const auto id : order) {
            block_offsets[id] = next_offset;
            next_offset += block_payloads[id].size();
        }
        const auto block_for_vm_target = [&](std::uint32_t target) {
            const auto found = std::find_if(blocks.begin(), blocks.end(), [&](const BasicBlock& block) {
                return block.first_vm_instruction == target;
            });
            if (found == blocks.end()) throw std::runtime_error("bytecode target is not a block leader");
            return found->id;
        };
        for (std::size_t id = 0; id < block_payloads.size(); ++id) {
            auto& bytes = block_payloads[id];
            for (std::size_t pc = 0; pc < bytes.size();) {
                const auto opcode = options.profile.logical(bytes[pc]);
                const auto operand_offset = control_target_offset(opcode);
                if (operand_offset.has_value()) {
                    const auto target_offset = pc + *operand_offset;
                    const auto raw_target = read_u32_le(bytes, target_offset);
                    const auto target_is_block_id = opcode == Opcode::JumpBlock;
                    const auto target_block = target_is_block_id ? raw_target : block_for_vm_target(raw_target);
                    if (target_block >= block_offsets.size() || block_offsets[target_block] > 0xffffffffu) {
                        throw std::runtime_error("shuffled bytecode target is out of range");
                    }
                    write_u32_le(bytes, target_offset, static_cast<std::uint32_t>(block_offsets[target_block]));
                }
                pc += 1 + operand_size(opcode);
            }
        }

        payload.clear();
        std::vector<BasicBlock> physical_blocks;
        physical_blocks.reserve(blocks.size());
        for (const auto id : order) {
            auto block = blocks[id];
            block.payload_offset = payload.size();
            block.payload_size = block_payloads[id].size();
            payload.insert(payload.end(), block_payloads[id].begin(), block_payloads[id].end());
            physical_blocks.push_back(std::move(block));
        }
        blocks = std::move(physical_blocks);
    }
    crypt_payload(payload, options.key);

    std::vector<std::uint8_t> image;
    image.insert(image.end(), kMagic.begin(), kMagic.end());
    image.push_back(kVersion);
    image.insert(image.end(), payload.begin(), payload.end());
    return {image, options.key, options.profile, std::move(blocks), options.shuffle_blocks};
}

std::vector<std::uint8_t> decode_payload(const Image& image) {
    if (image.bytes.size() < kMagic.size() + 1) {
        throw std::invalid_argument("bytecode image is too small");
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (image.bytes[i] != kMagic[i]) {
            throw std::invalid_argument("bad bytecode magic");
        }
    }
    if (image.bytes[kMagic.size()] != kVersion) {
        throw std::invalid_argument("unsupported bytecode version");
    }

    std::vector<std::uint8_t> payload(image.bytes.begin() + kMagic.size() + 1, image.bytes.end());
    crypt_payload(payload, image.key);
    return payload;
}

} // namespace myvmp::bytecode
