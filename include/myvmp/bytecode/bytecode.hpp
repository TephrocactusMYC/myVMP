#pragma once

#include "myvmp/ir/ir.hpp"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace myvmp::bytecode {

enum class Opcode : std::uint8_t {
    // Low fixed values are format-control/superoperator defaults. Seeded profiles replace
    // them with collision-free physical values just like ordinary logical opcodes.
    CompareImmBranch = 0x05,
    JumpBlock = 0x06,
    CompareRegBranch = 0x07,
    TestImmBranch = 0x08,
    FpConditionalCompare = 0x0C,
    FpSelect = 0x0D,
    LoadExclusivePair = 0x0E,
    StoreExclusivePair = 0x0F,
    VectorBinary = 0x10,
    VectorCompare = 0x12,
    VectorSelect = 0x13,
    VectorShiftImm = 0x14,
    VectorPermute = 0x15,
    VectorLoad = 0x16,
    VectorStore = 0x17,
    VectorFpBinary = 0x18,
    VectorMoveImmZero = 0x19,
    LoadImm64 = 0x11,
    LoadAddress64 = 0xE3,
    AddImm64 = 0x22,
    SubImm64 = 0x33,
    AndImm64 = 0xB2,
    OrImm64 = 0xDA,
    XorImm64 = 0xDB,
    AddImmSetFlags64 = 0xC3,
    SubImmSetFlags64 = 0xC4,
    AddRegShifted64 = 0x66,
    AddRegShiftedSetFlags64 = 0xC5,
    AddRegExtended64 = 0xBA,
    AddRegExtendedSetFlags64 = 0xCB,
    SubRegShifted64 = 0xAB,
    SubRegShiftedSetFlags64 = 0xC6,
    SubRegExtended64 = 0xB3,
    SubRegExtendedSetFlags64 = 0xD7,
    NegReg64 = 0xD4,
    NegRegSetFlags64 = 0xD5,
    AndRegShifted64 = 0x77,
    BitClearRegShifted64 = 0xBB,
    NotRegShifted64 = 0xD6,
    OrRegShifted64 = 0x88,
    OrNotRegShifted64 = 0xBC,
    XorRegShifted64 = 0x99,
    XorNotRegShifted64 = 0xBD,
    ShiftReg64 = 0xB1,
    ShiftReg32 = 0xC9,
    ShiftRegVariable64 = 0xD8,
    DivideReg64 = 0xD9,
    BitfieldExtract64 = 0xB7,
    UnsignedMulHigh64 = 0xB8,
    MulAdd64 = 0xCF,
    MulSub64 = 0xD0,
    SignedMulLong64 = 0xD1,
    UnsignedMulLong64 = 0xD2,
    MoveKeep64 = 0xAA,
    MoveReg64 = 0xAC,
    CompareImm64 = 0xAD,
    CompareReg64 = 0xBE,
    TestImm64 = 0xAE,
    TestRegShifted64 = 0xC1,
    TestRegShifted32 = 0xCA,
    BranchIf = 0xAF,
    BranchRegZero = 0xB9,
    BranchRegBit = 0xC2,
    Select64 = 0xB0,
    SelectInc64 = 0xCC,
    SelectInv64 = 0xCD,
    SelectNeg64 = 0xCE,
    Load64 = 0xB4,
    Store64 = 0xB5,
    LoadMem = 0xBF,
    StoreMem = 0xC0,
    LoadRegOffsetMem = 0xC7,
    StoreRegOffsetMem = 0xC8,
    StorePair64 = 0xB6,
    CallNative = 0xE1,
    CallNativeReg = 0xE2,
    CallVm = 0xE4,
    LoadFpImm64 = 0xE5,
    FpAdd64 = 0xE6,
    FpSub64 = 0xE7,
    FpMul64 = 0xE8,
    FpDiv64 = 0xE9,
    FpMadd64 = 0xEA,
    FpCompare64 = 0xEB,
    LoadFpImm32 = 0xEC,
    FpAdd32 = 0xED,
    FpSub32 = 0xEE,
    FpMul32 = 0xEF,
    FpDiv32 = 0xF0,
    FpMadd32 = 0xF1,
    FpCompare32 = 0xF2,
    LoadAcquire = 0xF3,
    StoreRelease = 0xF4,
    LoadExclusive = 0xF5,
    StoreExclusive = 0xF6,
    CompareSwap = 0xF7,
    FpUnary = 0xF8,
    FpConvert = 0xF9,
    FpMinMax = 0xFA,
    NoOp = 0xD3,
    Return = 0x44,
    Halt = 0x55,
};

enum HandlerVariant : std::uint32_t {
    kVariantAddImm = 1u << 0,
    kVariantSubImm = 1u << 1,
    kVariantXorImm = 1u << 2,
    kVariantAndImm = 1u << 3,
    kVariantOrImm = 1u << 4,
    kVariantMoveReg = 1u << 5,
    kVariantCompareImm = 1u << 6,
    kVariantCompareReg = 1u << 7,
    kVariantTestImm = 1u << 8,
    kVariantLoad64 = 1u << 9,
    kVariantStore64 = 1u << 10,
    kVariantAddRegShifted = 1u << 11,
    kVariantSubRegShifted = 1u << 12,
    kVariantAndRegShifted = 1u << 13,
    kVariantXorRegShifted = 1u << 14,
    kVariantTestRegShifted = 1u << 15,
    kVariantTestRegShifted32 = 1u << 16,
    kVariantBranchIf = 1u << 17,
    kVariantBranchRegZero = 1u << 18,
    kVariantBranchRegBit = 1u << 19,
    kVariantSelect = 1u << 20,
    kVariantFpUnary = 1u << 21,
    kAllHandlerVariants = (1u << 22) - 1u,
};

struct VmProfile {
    std::uint32_t seed = 0;
    std::uint8_t load_imm64 = static_cast<std::uint8_t>(Opcode::LoadImm64);
    std::uint8_t load_address64 = static_cast<std::uint8_t>(Opcode::LoadAddress64);
    std::uint8_t add_imm64 = static_cast<std::uint8_t>(Opcode::AddImm64);
    std::uint8_t sub_imm64 = static_cast<std::uint8_t>(Opcode::SubImm64);
    std::uint8_t and_imm64 = static_cast<std::uint8_t>(Opcode::AndImm64);
    std::uint8_t or_imm64 = static_cast<std::uint8_t>(Opcode::OrImm64);
    std::uint8_t xor_imm64 = static_cast<std::uint8_t>(Opcode::XorImm64);
    std::uint8_t add_imm_set_flags64 = static_cast<std::uint8_t>(Opcode::AddImmSetFlags64);
    std::uint8_t sub_imm_set_flags64 = static_cast<std::uint8_t>(Opcode::SubImmSetFlags64);
    std::uint8_t add_reg_shifted64 = static_cast<std::uint8_t>(Opcode::AddRegShifted64);
    std::uint8_t add_reg_shifted_set_flags64 = static_cast<std::uint8_t>(Opcode::AddRegShiftedSetFlags64);
    std::uint8_t add_reg_extended64 = static_cast<std::uint8_t>(Opcode::AddRegExtended64);
    std::uint8_t add_reg_extended_set_flags64 = static_cast<std::uint8_t>(Opcode::AddRegExtendedSetFlags64);
    std::uint8_t sub_reg_shifted64 = static_cast<std::uint8_t>(Opcode::SubRegShifted64);
    std::uint8_t sub_reg_shifted_set_flags64 = static_cast<std::uint8_t>(Opcode::SubRegShiftedSetFlags64);
    std::uint8_t sub_reg_extended64 = static_cast<std::uint8_t>(Opcode::SubRegExtended64);
    std::uint8_t sub_reg_extended_set_flags64 = static_cast<std::uint8_t>(Opcode::SubRegExtendedSetFlags64);
    std::uint8_t neg_reg64 = static_cast<std::uint8_t>(Opcode::NegReg64);
    std::uint8_t neg_reg_set_flags64 = static_cast<std::uint8_t>(Opcode::NegRegSetFlags64);
    std::uint8_t and_reg_shifted64 = static_cast<std::uint8_t>(Opcode::AndRegShifted64);
    std::uint8_t bit_clear_reg_shifted64 = static_cast<std::uint8_t>(Opcode::BitClearRegShifted64);
    std::uint8_t not_reg_shifted64 = static_cast<std::uint8_t>(Opcode::NotRegShifted64);
    std::uint8_t or_reg_shifted64 = static_cast<std::uint8_t>(Opcode::OrRegShifted64);
    std::uint8_t or_not_reg_shifted64 = static_cast<std::uint8_t>(Opcode::OrNotRegShifted64);
    std::uint8_t xor_reg_shifted64 = static_cast<std::uint8_t>(Opcode::XorRegShifted64);
    std::uint8_t xor_not_reg_shifted64 = static_cast<std::uint8_t>(Opcode::XorNotRegShifted64);
    std::uint8_t shift_reg64 = static_cast<std::uint8_t>(Opcode::ShiftReg64);
    std::uint8_t shift_reg32 = static_cast<std::uint8_t>(Opcode::ShiftReg32);
    std::uint8_t shift_reg_variable64 = static_cast<std::uint8_t>(Opcode::ShiftRegVariable64);
    std::uint8_t divide_reg64 = static_cast<std::uint8_t>(Opcode::DivideReg64);
    std::uint8_t bitfield_extract64 = static_cast<std::uint8_t>(Opcode::BitfieldExtract64);
    std::uint8_t unsigned_mul_high64 = static_cast<std::uint8_t>(Opcode::UnsignedMulHigh64);
    std::uint8_t mul_add64 = static_cast<std::uint8_t>(Opcode::MulAdd64);
    std::uint8_t mul_sub64 = static_cast<std::uint8_t>(Opcode::MulSub64);
    std::uint8_t signed_mul_long64 = static_cast<std::uint8_t>(Opcode::SignedMulLong64);
    std::uint8_t unsigned_mul_long64 = static_cast<std::uint8_t>(Opcode::UnsignedMulLong64);
    std::uint8_t move_keep64 = static_cast<std::uint8_t>(Opcode::MoveKeep64);
    std::uint8_t move_reg64 = static_cast<std::uint8_t>(Opcode::MoveReg64);
    std::uint8_t compare_imm64 = static_cast<std::uint8_t>(Opcode::CompareImm64);
    std::uint8_t compare_reg64 = static_cast<std::uint8_t>(Opcode::CompareReg64);
    std::uint8_t test_imm64 = static_cast<std::uint8_t>(Opcode::TestImm64);
    std::uint8_t test_reg_shifted64 = static_cast<std::uint8_t>(Opcode::TestRegShifted64);
    std::uint8_t test_reg_shifted32 = static_cast<std::uint8_t>(Opcode::TestRegShifted32);
    std::uint8_t branch_if = static_cast<std::uint8_t>(Opcode::BranchIf);
    std::uint8_t branch_reg_zero = static_cast<std::uint8_t>(Opcode::BranchRegZero);
    std::uint8_t branch_reg_bit = static_cast<std::uint8_t>(Opcode::BranchRegBit);
    std::uint8_t select64 = static_cast<std::uint8_t>(Opcode::Select64);
    std::uint8_t select_inc64 = static_cast<std::uint8_t>(Opcode::SelectInc64);
    std::uint8_t select_inv64 = static_cast<std::uint8_t>(Opcode::SelectInv64);
    std::uint8_t select_neg64 = static_cast<std::uint8_t>(Opcode::SelectNeg64);
    std::uint8_t load64 = static_cast<std::uint8_t>(Opcode::Load64);
    std::uint8_t store64 = static_cast<std::uint8_t>(Opcode::Store64);
    std::uint8_t load_mem = static_cast<std::uint8_t>(Opcode::LoadMem);
    std::uint8_t store_mem = static_cast<std::uint8_t>(Opcode::StoreMem);
    std::uint8_t load_reg_offset_mem = static_cast<std::uint8_t>(Opcode::LoadRegOffsetMem);
    std::uint8_t store_reg_offset_mem = static_cast<std::uint8_t>(Opcode::StoreRegOffsetMem);
    std::uint8_t store_pair64 = static_cast<std::uint8_t>(Opcode::StorePair64);
    std::uint8_t call_native = static_cast<std::uint8_t>(Opcode::CallNative);
    std::uint8_t call_native_reg = static_cast<std::uint8_t>(Opcode::CallNativeReg);
    std::uint8_t call_vm = static_cast<std::uint8_t>(Opcode::CallVm);
    std::uint8_t load_fp_imm64 = static_cast<std::uint8_t>(Opcode::LoadFpImm64);
    std::uint8_t fp_add64 = static_cast<std::uint8_t>(Opcode::FpAdd64);
    std::uint8_t fp_sub64 = static_cast<std::uint8_t>(Opcode::FpSub64);
    std::uint8_t fp_mul64 = static_cast<std::uint8_t>(Opcode::FpMul64);
    std::uint8_t fp_div64 = static_cast<std::uint8_t>(Opcode::FpDiv64);
    std::uint8_t fp_madd64 = static_cast<std::uint8_t>(Opcode::FpMadd64);
    std::uint8_t fp_compare64 = static_cast<std::uint8_t>(Opcode::FpCompare64);
    std::uint8_t load_fp_imm32 = static_cast<std::uint8_t>(Opcode::LoadFpImm32);
    std::uint8_t fp_add32 = static_cast<std::uint8_t>(Opcode::FpAdd32);
    std::uint8_t fp_sub32 = static_cast<std::uint8_t>(Opcode::FpSub32);
    std::uint8_t fp_mul32 = static_cast<std::uint8_t>(Opcode::FpMul32);
    std::uint8_t fp_div32 = static_cast<std::uint8_t>(Opcode::FpDiv32);
    std::uint8_t fp_madd32 = static_cast<std::uint8_t>(Opcode::FpMadd32);
    std::uint8_t fp_compare32 = static_cast<std::uint8_t>(Opcode::FpCompare32);
    std::uint8_t load_acquire = static_cast<std::uint8_t>(Opcode::LoadAcquire);
    std::uint8_t store_release = static_cast<std::uint8_t>(Opcode::StoreRelease);
    std::uint8_t load_exclusive = static_cast<std::uint8_t>(Opcode::LoadExclusive);
    std::uint8_t store_exclusive = static_cast<std::uint8_t>(Opcode::StoreExclusive);
    std::uint8_t compare_swap = static_cast<std::uint8_t>(Opcode::CompareSwap);
    std::uint8_t fp_unary = static_cast<std::uint8_t>(Opcode::FpUnary);
    std::uint8_t fp_convert = static_cast<std::uint8_t>(Opcode::FpConvert);
    std::uint8_t fp_min_max = static_cast<std::uint8_t>(Opcode::FpMinMax);
    std::uint8_t no_op = static_cast<std::uint8_t>(Opcode::NoOp);
    std::uint8_t ret = static_cast<std::uint8_t>(Opcode::Return);
    std::uint8_t halt = static_cast<std::uint8_t>(Opcode::Halt);
    std::uint32_t handler_variant_mask = 0;
    std::uint8_t load_imm64_duplicate = 0x01;
    std::uint8_t add_imm64_duplicate = 0x02;
    std::uint8_t sub_imm64_duplicate = 0x03;
    std::uint8_t xor_imm64_duplicate = 0x04;
    std::uint8_t and_imm64_duplicate = 0x09;
    std::uint8_t or_imm64_duplicate = 0x0a;
    std::uint8_t move_reg64_duplicate = 0x0b;
    std::uint8_t compare_imm_branch = 0x05;
    std::uint8_t jump_block = 0x06;
    std::uint8_t compare_reg_branch = 0x07;
    std::uint8_t test_imm_branch = 0x08;
    std::uint8_t fp_conditional_compare = 0x0c;
    std::uint8_t fp_select = 0x0d;
    std::uint8_t load_exclusive_pair = 0x0e;
    std::uint8_t store_exclusive_pair = 0x0f;
    // Native dispatch may compare in a seed-specific XOR-transformed opcode domain.
    // Zero preserves the fixed-profile direct-compare organization.
    std::uint8_t dispatcher_xor_key = 0;
    std::uint8_t vector_binary = static_cast<std::uint8_t>(Opcode::VectorBinary);
    std::uint8_t vector_compare = static_cast<std::uint8_t>(Opcode::VectorCompare);
    std::uint8_t vector_select = static_cast<std::uint8_t>(Opcode::VectorSelect);
    std::uint8_t vector_shift_imm = static_cast<std::uint8_t>(Opcode::VectorShiftImm);
    std::uint8_t vector_permute = static_cast<std::uint8_t>(Opcode::VectorPermute);
    std::uint8_t vector_load = static_cast<std::uint8_t>(Opcode::VectorLoad);
    std::uint8_t vector_store = static_cast<std::uint8_t>(Opcode::VectorStore);
    std::uint8_t vector_fp_binary = static_cast<std::uint8_t>(Opcode::VectorFpBinary);
    std::uint8_t vector_move_imm_zero = static_cast<std::uint8_t>(Opcode::VectorMoveImmZero);

    [[nodiscard]] std::uint8_t physical(Opcode opcode) const;
    [[nodiscard]] std::uint8_t duplicate_physical(Opcode opcode) const;
    [[nodiscard]] Opcode logical(std::uint8_t physical_opcode) const;
    [[nodiscard]] std::string manifest() const;
};

struct EncodeOptions {
    std::uint32_t key = 0xC0DEC0DEu;
    VmProfile profile{};
    bool diversify_immediates = false;
    std::uint32_t diversification_seed = 0;
    bool shuffle_blocks = false;
};

struct BasicBlock {
    // Stable CFG identity. Vector order may change after physical layout shuffling.
    std::uint32_t id{};
    std::uint32_t first_ir_instruction{};
    std::uint32_t ir_instruction_count{};
    std::uint32_t first_vm_instruction{};
    std::uint32_t vm_instruction_count{};
    std::size_t payload_offset{};
    std::size_t payload_size{};
    // Stable block IDs, never physical vector indices or byte offsets.
    std::vector<std::uint32_t> successors;
};

struct Image {
    std::vector<std::uint8_t> bytes;
    std::uint32_t key{};
    VmProfile profile{};
    // Explicit CFG ownership for every bytecode range. Blocks remain in source order for
    // now; this metadata is the stable boundary used by rolling keys and later shuffling.
    std::vector<BasicBlock> blocks;
    bool branch_targets_are_offsets = false;
};

VmProfile fixed_profile();
VmProfile profile_from_seed(std::uint32_t seed);
Image encode(const ir::Program& program, EncodeOptions options = {});
std::vector<std::uint8_t> decode_payload(const Image& image);
std::size_t operand_size(Opcode opcode);
// Returns the byte offset from the opcode byte to a u32 control-flow target operand.
std::optional<std::size_t> control_target_offset(Opcode opcode);
void crypt_payload(std::vector<std::uint8_t>& payload, std::uint32_t key);
std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset);
void write_u32_le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value);

} // namespace myvmp::bytecode
