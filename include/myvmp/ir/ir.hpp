#pragma once

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace myvmp::ir {

using RegisterId = std::uint8_t;

constexpr RegisterId kStackPointerRegister = 31;
constexpr RegisterId kZeroRegister = 32;
constexpr std::size_t kVirtualRegisterCount = 33;

constexpr std::uint64_t kSelectTrueZero = 0x1;
constexpr std::uint64_t kSelectFalseZero = 0x2;
constexpr std::uint64_t kMulAccumulatorZero = 0x1;

enum class Opcode : std::uint8_t {
    LoadImm,
    LoadAddress,
    AddImm,
    SubImm,
    AndImm,
    OrImm,
    XorImm,
    AddImmSetFlags,
    SubImmSetFlags,
    AddRegShifted,
    AddRegExtended,
    SubRegShifted,
    SubRegExtended,
    AddRegShiftedSetFlags,
    AddRegExtendedSetFlags,
    SubRegShiftedSetFlags,
    SubRegExtendedSetFlags,
    NegReg,
    NegRegSetFlags,
    AndRegShifted,
    BitClearRegShifted,
    NotRegShifted,
    OrRegShifted,
    OrNotRegShifted,
    XorRegShifted,
    XorNotRegShifted,
    ShiftReg,
    ShiftReg32,
    ShiftRegVariable,
    DivideReg,
    BitfieldExtract,
    UnsignedMulHigh,
    MulAdd,
    MulSub,
    SignedMulLong,
    UnsignedMulLong,
    MoveReg,
    MoveKeep,
    CompareImm,
    CompareReg,
    TestImm,
    TestRegShifted,
    TestRegShifted32,
    BranchIf,
    BranchRegZero,
    BranchRegBit,
    CallVm,
    LoadFpImm64,
    FpAdd64,
    FpSub64,
    FpMul64,
    FpDiv64,
    FpMadd64,
    FpCompare64,
    LoadFpImm32,
    FpAdd32,
    FpSub32,
    FpMul32,
    FpDiv32,
    FpMadd32,
    FpCompare32,
    FpUnary,
    FpConvert,
    FpMinMax,
    FpConditionalCompare,
    FpSelect,
    Select,
    SelectInc,
    SelectInv,
    SelectNeg,
    Load64,
    Store64,
    LoadMem,
    StoreMem,
    LoadRegOffsetMem,
    StoreRegOffsetMem,
    StorePair64,
    LoadAcquire,
    StoreRelease,
    LoadExclusive,
    StoreExclusive,
    LoadExclusivePair,
    StoreExclusivePair,
    CompareSwap,
    CallNative,
    CallNativeReg,
    NoOp,
    ReturnReg,
};

enum class ShiftKind : std::uint8_t {
    Lsl,
    Lsr,
    Asr,
    Ror,
};

enum class Condition : std::uint8_t {
    Eq,
    Ne,
    Hs,
    Lo,
    Mi,
    Pl,
    Vs,
    Vc,
    Hi,
    Ls,
    Ge,
    Lt,
    Gt,
    Le,
    Al,
    Nv,
};

enum class ExtendKind : std::uint8_t {
    Uxtb,
    Uxth,
    Uxtw,
    Uxtx,
    Sxtb,
    Sxth,
    Sxtw,
    Sxtx,
};

enum class FpUnaryKind : std::uint8_t {
    Abs32, Abs64, Neg32, Neg64, Sqrt32, Sqrt64, Convert32To64, Convert64To32,
};

enum class FpConvertKind : std::uint8_t {
    Signed32ToFp32, Signed32ToFp64, Signed64ToFp32, Signed64ToFp64,
    Unsigned32ToFp32, Unsigned32ToFp64, Unsigned64ToFp32, Unsigned64ToFp64,
    Fp32ToSigned32, Fp64ToSigned32, Fp32ToSigned64, Fp64ToSigned64,
    Fp32ToUnsigned32, Fp64ToUnsigned32, Fp32ToUnsigned64, Fp64ToUnsigned64,
};

enum class FpMinMaxKind : std::uint8_t {
    Min32, Min64, Max32, Max64, MinNumber32, MinNumber64, MaxNumber32, MaxNumber64,
};

struct Instruction {
    Opcode opcode{};
    RegisterId dst{};
    RegisterId src{};
    RegisterId rhs{};
    std::uint64_t imm{};
    ShiftKind shift_kind = ShiftKind::Lsl;
    std::uint8_t shift_amount = 0;
    Condition condition = Condition::Eq;
    std::uint32_t target = 0;
    ExtendKind extend_kind = ExtendKind::Uxtx;
    std::int64_t post_index = 0;
    std::uint8_t mem_width = 8;
};

class Program {
public:
    void load_imm(RegisterId dst, std::uint64_t value);
    void load_address(RegisterId dst, std::uint64_t value);
    void add_imm(RegisterId dst, RegisterId src, std::uint64_t value);
    void sub_imm(RegisterId dst, RegisterId src, std::uint64_t value);
    void and_imm(RegisterId dst, RegisterId src, std::uint64_t value);
    void or_imm(RegisterId dst, RegisterId src, std::uint64_t value);
    void xor_imm(RegisterId dst, RegisterId src, std::uint64_t value);
    void add_imm_set_flags(RegisterId dst, RegisterId src, std::uint64_t value, bool writes_32bit = false);
    void sub_imm_set_flags(RegisterId dst, RegisterId src, std::uint64_t value, bool writes_32bit = false);
    void add_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void add_reg_shifted_set_flags(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void add_reg_extended(RegisterId dst, RegisterId lhs, RegisterId rhs, ExtendKind extend_kind, std::uint8_t shift_amount);
    void add_reg_extended_set_flags(RegisterId dst, RegisterId lhs, RegisterId rhs, ExtendKind extend_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void sub_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void sub_reg_shifted_set_flags(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void sub_reg_extended(RegisterId dst, RegisterId lhs, RegisterId rhs, ExtendKind extend_kind, std::uint8_t shift_amount);
    void sub_reg_extended_set_flags(RegisterId dst, RegisterId lhs, RegisterId rhs, ExtendKind extend_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void neg_reg(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void neg_reg_set_flags(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void and_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void bit_clear_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void not_reg_shifted(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit = false);
    void or_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void or_not_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void xor_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void xor_not_reg_shifted(RegisterId dst, RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void shift_reg(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount);
    void shift_reg32(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount);
    void shift_reg_variable(RegisterId dst, RegisterId src, RegisterId amount, ShiftKind shift_kind,
                            bool writes_32bit = false);
    void divide_reg(RegisterId dst, RegisterId lhs, RegisterId rhs, bool is_signed,
                    bool writes_32bit = false);
    void bitfield_extract(RegisterId dst, RegisterId src, std::uint8_t lsb, std::uint8_t width);
    void unsigned_mul_high(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void mul_add(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend, bool writes_32bit = false, bool addend_is_zero = false);
    void mul_sub(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId minuend, bool writes_32bit = false, bool minuend_is_zero = false);
    void signed_mul_long(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void unsigned_mul_long(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void move_reg(RegisterId dst, RegisterId src);
    void xor_reg(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void move_keep(RegisterId dst, std::uint16_t value, std::uint8_t shift_amount);
    void compare_imm(RegisterId src, std::uint64_t value);
    void compare_reg(RegisterId lhs, RegisterId rhs);
    void test_imm(RegisterId src, std::uint64_t mask);
    void test_reg_shifted(RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void test_reg_shifted32(RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount);
    void branch_if(Condition condition, std::uint32_t target);
    void branch_reg_zero(RegisterId src, bool branch_when_zero, std::uint32_t target);
    void branch_reg_bit(RegisterId src, std::uint8_t bit, bool branch_when_zero, std::uint32_t target);
    void call_vm(std::uint32_t target);
    void load_fp_imm64(RegisterId dst, std::uint64_t bits);
    void fp_add64(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_sub64(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_mul64(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_div64(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_madd64(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend);
    void fp_compare64(RegisterId lhs, RegisterId rhs);
    void load_fp_imm32(RegisterId dst, std::uint32_t bits);
    void fp_add32(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_sub32(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_mul32(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_div32(RegisterId dst, RegisterId lhs, RegisterId rhs);
    void fp_madd32(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend);
    void fp_compare32(RegisterId lhs, RegisterId rhs);
    void fp_unary(RegisterId dst, RegisterId src, FpUnaryKind kind);
    void fp_convert(RegisterId dst, RegisterId src, FpConvertKind kind);
    void fp_min_max(RegisterId dst, RegisterId lhs, RegisterId rhs, FpMinMaxKind kind);
    void fp_conditional_compare(RegisterId lhs, RegisterId rhs, Condition condition,
                                std::uint8_t fallback_nzcv, bool is_double,
                                bool signaling = false);
    void fp_select(RegisterId dst, RegisterId true_src, RegisterId false_src,
                   Condition condition, bool is_double);
    void select(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero = false, bool false_is_zero = false);
    void select_inc(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero = false, bool false_is_zero = false);
    void select_inv(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero = false, bool false_is_zero = false);
    void select_neg(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero = false, bool false_is_zero = false);
    void load64(RegisterId dst, RegisterId base, std::uint64_t offset);
    void store64(RegisterId src, RegisterId base, std::uint64_t offset, std::int64_t post_index = 0);
    void load_mem(RegisterId dst, RegisterId base, std::uint64_t offset, std::uint8_t width);
    void store_mem(RegisterId src, RegisterId base, std::uint64_t offset, std::uint8_t width, std::int64_t post_index = 0);
    void load_reg_offset_mem(RegisterId dst, RegisterId base, RegisterId index, ExtendKind extend_kind, std::uint8_t shift_amount, std::uint8_t width);
    void store_reg_offset_mem(RegisterId src, RegisterId base, RegisterId index, ExtendKind extend_kind, std::uint8_t shift_amount, std::uint8_t width);
    void store_pair64(RegisterId first, RegisterId second, RegisterId base, std::uint64_t offset);
    void load_acquire(RegisterId dst, RegisterId base, std::uint8_t width);
    void store_release(RegisterId src, RegisterId base, std::uint8_t width);
    void load_exclusive(RegisterId dst, RegisterId base, std::uint8_t width, bool acquire = false);
    void store_exclusive(RegisterId status, RegisterId src, RegisterId base, std::uint8_t width,
                         bool release = false);
    void load_exclusive_pair(RegisterId first, RegisterId second, RegisterId base,
                             std::uint8_t width, bool acquire = false);
    void store_exclusive_pair(RegisterId status, RegisterId first, RegisterId second,
                              RegisterId base, std::uint8_t width, bool release = false);
    void compare_swap(RegisterId expected, RegisterId desired, RegisterId base, std::uint8_t width,
                      bool acquire = false, bool release = false);
    void call_native(std::uint64_t target, std::uint64_t link_address);
    void call_native_reg(RegisterId target, std::uint64_t link_address);
    void no_op();
    void return_reg(RegisterId src);

    // Translate frontend instruction indices used by structured control-flow
    // operations into final IR indices after one source instruction expands to many IR ops.
    void remap_control_flow_targets(const std::vector<std::uint32_t>& source_to_ir);

    [[nodiscard]] const std::vector<Instruction>& instructions() const noexcept;
    void validate() const;
    void remap_branch_targets(const std::vector<std::uint32_t>& old_to_new_targets);

private:
    std::vector<Instruction> instructions_;
};

std::string to_string(Opcode opcode);
std::string to_string(Condition condition);
std::uint64_t apply_extend(std::uint64_t value, ExtendKind extend_kind);
std::uint64_t apply_shift(std::uint64_t value, ShiftKind shift_kind, std::uint8_t amount);

} // namespace myvmp::ir
