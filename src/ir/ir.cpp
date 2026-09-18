#include "myvmp/ir/ir.hpp"

namespace myvmp::ir {
namespace {

constexpr RegisterId kMaxRegister = kZeroRegister;

void check_register(RegisterId reg) {
    if (reg > kMaxRegister) {
        throw std::invalid_argument("IR register index out of range");
    }
}

} // namespace

void Program::load_imm(RegisterId dst, std::uint64_t value) {
    check_register(dst);
    instructions_.push_back({Opcode::LoadImm, dst, 0, 0, value});
}

void Program::load_address(RegisterId dst, std::uint64_t value) {
    check_register(dst);
    instructions_.push_back({Opcode::LoadAddress, dst, 0, 0, value});
}

void Program::add_imm(RegisterId dst, RegisterId src, std::uint64_t value) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::AddImm, dst, src, 0, value});
}

void Program::sub_imm(RegisterId dst, RegisterId src, std::uint64_t value) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::SubImm, dst, src, 0, value});
}

void Program::and_imm(RegisterId dst, RegisterId src, std::uint64_t value) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::AndImm, dst, src, 0, value});
}

void Program::or_imm(RegisterId dst, RegisterId src, std::uint64_t value) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::OrImm, dst, src, 0, value});
}

void Program::xor_imm(RegisterId dst, RegisterId src, std::uint64_t value) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::XorImm, dst, src, 0, value});
}

void Program::add_imm_set_flags(RegisterId dst, RegisterId src, std::uint64_t value, bool writes_32bit) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::AddImmSetFlags, dst, src, 0, value, ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::sub_imm_set_flags(RegisterId dst, RegisterId src, std::uint64_t value, bool writes_32bit) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::SubImmSetFlags, dst, src, 0, value, ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::add_reg_shifted(RegisterId dst,
                              RegisterId lhs,
                              RegisterId rhs,
                              ShiftKind shift_kind,
                              std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::AddRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::add_reg_shifted_set_flags(RegisterId dst,
                                        RegisterId lhs,
                                        RegisterId rhs,
                                        ShiftKind shift_kind,
                                        std::uint8_t shift_amount,
                                        bool writes_32bit) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= (writes_32bit ? 32 : 64)) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::AddRegShiftedSetFlags, dst, lhs, rhs, 0, shift_kind, shift_amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::sub_reg_shifted(RegisterId dst,
                              RegisterId lhs,
                              RegisterId rhs,
                              ShiftKind shift_kind,
                              std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::SubRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::sub_reg_shifted_set_flags(RegisterId dst,
                                        RegisterId lhs,
                                        RegisterId rhs,
                                        ShiftKind shift_kind,
                                        std::uint8_t shift_amount,
                                        bool writes_32bit) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= (writes_32bit ? 32 : 64)) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::SubRegShiftedSetFlags, dst, lhs, rhs, 0, shift_kind, shift_amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::sub_reg_extended(RegisterId dst,
                               RegisterId lhs,
                               RegisterId rhs,
                               ExtendKind extend_kind,
                               std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount > 4) {
        throw std::invalid_argument("IR extend shift amount out of range");
    }
    instructions_.push_back({Opcode::SubRegExtended, dst, lhs, rhs, 0, ShiftKind::Lsl, shift_amount, Condition::Eq, 0, extend_kind});
}

void Program::sub_reg_extended_set_flags(RegisterId dst,
                                         RegisterId lhs,
                                         RegisterId rhs,
                                         ExtendKind extend_kind,
                                         std::uint8_t shift_amount,
                                         bool writes_32bit) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount > 4) {
        throw std::invalid_argument("IR extend shift amount out of range");
    }
    instructions_.push_back({Opcode::SubRegExtendedSetFlags, dst, lhs, rhs, 0, ShiftKind::Lsl, shift_amount,
                             Condition::Eq, 0, extend_kind, 0,
                             static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::neg_reg(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit) {
    check_register(dst);
    check_register(src);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::NegReg, dst, src, 0, 0, shift_kind, shift_amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::neg_reg_set_flags(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit) {
    check_register(dst);
    check_register(src);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::NegRegSetFlags, dst, src, 0, 0, shift_kind, shift_amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::add_reg_extended(RegisterId dst,
                               RegisterId lhs,
                               RegisterId rhs,
                               ExtendKind extend_kind,
                               std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount > 4) {
        throw std::invalid_argument("IR extend shift amount out of range");
    }
    instructions_.push_back({Opcode::AddRegExtended, dst, lhs, rhs, 0, ShiftKind::Lsl, shift_amount, Condition::Eq, 0, extend_kind});
}

void Program::add_reg_extended_set_flags(RegisterId dst,
                                         RegisterId lhs,
                                         RegisterId rhs,
                                         ExtendKind extend_kind,
                                         std::uint8_t shift_amount,
                                         bool writes_32bit) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount > 4) {
        throw std::invalid_argument("IR extend shift amount out of range");
    }
    instructions_.push_back({Opcode::AddRegExtendedSetFlags, dst, lhs, rhs, 0, ShiftKind::Lsl, shift_amount,
                             Condition::Eq, 0, extend_kind, 0,
                             static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::and_reg_shifted(RegisterId dst,
                              RegisterId lhs,
                              RegisterId rhs,
                              ShiftKind shift_kind,
                              std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::AndRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::bit_clear_reg_shifted(RegisterId dst,
                                    RegisterId lhs,
                                    RegisterId rhs,
                                    ShiftKind shift_kind,
                                    std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::BitClearRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::not_reg_shifted(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount, bool writes_32bit) {
    check_register(dst);
    check_register(src);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::NotRegShifted, dst, src, 0, 0, shift_kind, shift_amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::or_reg_shifted(RegisterId dst,
                             RegisterId lhs,
                             RegisterId rhs,
                             ShiftKind shift_kind,
                             std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::OrRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::or_not_reg_shifted(RegisterId dst,
                                 RegisterId lhs,
                                 RegisterId rhs,
                                 ShiftKind shift_kind,
                                 std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::OrNotRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::xor_reg_shifted(RegisterId dst,
                              RegisterId lhs,
                              RegisterId rhs,
                              ShiftKind shift_kind,
                              std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::XorRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::xor_not_reg_shifted(RegisterId dst,
                                  RegisterId lhs,
                                  RegisterId rhs,
                                  ShiftKind shift_kind,
                                  std::uint8_t shift_amount) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::XorNotRegShifted, dst, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::xor_reg(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    xor_reg_shifted(dst, lhs, rhs, ShiftKind::Lsl, 0);
}

void Program::shift_reg(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount) {
    check_register(dst);
    check_register(src);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::ShiftReg, dst, src, 0, 0, shift_kind, shift_amount});
}

void Program::shift_reg32(RegisterId dst, RegisterId src, ShiftKind shift_kind, std::uint8_t shift_amount) {
    check_register(dst);
    check_register(src);
    if (shift_amount >= 32) {
        throw std::invalid_argument("IR 32-bit shift amount out of range");
    }
    instructions_.push_back({Opcode::ShiftReg32, dst, src, 0, 0, shift_kind, shift_amount});
}

void Program::shift_reg_variable(RegisterId dst,
                                 RegisterId src,
                                 RegisterId amount,
                                 ShiftKind shift_kind,
                                 bool writes_32bit) {
    check_register(dst);
    check_register(src);
    check_register(amount);
    instructions_.push_back({Opcode::ShiftRegVariable, dst, src, amount, 0, shift_kind, 0, Condition::Eq, 0,
                             ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::divide_reg(RegisterId dst,
                         RegisterId lhs,
                         RegisterId rhs,
                         bool is_signed,
                         bool writes_32bit) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    instructions_.push_back({Opcode::DivideReg, dst, lhs, rhs, static_cast<std::uint64_t>(is_signed), ShiftKind::Lsl,
                             0, Condition::Eq, 0, ExtendKind::Uxtx, 0,
                             static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::bitfield_extract(RegisterId dst, RegisterId src, std::uint8_t lsb, std::uint8_t width) {
    check_register(dst);
    check_register(src);
    if (width == 0 || width > 64 || lsb >= 64 || static_cast<unsigned>(lsb) + width > 64) {
        throw std::invalid_argument("IR bitfield extract range out of bounds");
    }
    instructions_.push_back({Opcode::BitfieldExtract, dst, src, 0, width, ShiftKind::Lsl, lsb});
}

void Program::unsigned_mul_high(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    instructions_.push_back({Opcode::UnsignedMulHigh, dst, lhs, rhs, 0});
}

void Program::mul_add(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend, bool writes_32bit, bool addend_is_zero) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    check_register(addend);
    instructions_.push_back({Opcode::MulAdd, dst, lhs, rhs, addend_is_zero ? kMulAccumulatorZero : 0ull, ShiftKind::Lsl, addend, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::mul_sub(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId minuend, bool writes_32bit, bool minuend_is_zero) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    check_register(minuend);
    instructions_.push_back({Opcode::MulSub, dst, lhs, rhs, minuend_is_zero ? kMulAccumulatorZero : 0ull, ShiftKind::Lsl, minuend, Condition::Eq, 0, ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(writes_32bit ? 4 : 8)});
}

void Program::signed_mul_long(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    instructions_.push_back({Opcode::SignedMulLong, dst, lhs, rhs, 0});
}

void Program::unsigned_mul_long(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst);
    check_register(lhs);
    check_register(rhs);
    instructions_.push_back({Opcode::UnsignedMulLong, dst, lhs, rhs, 0});
}

void Program::move_reg(RegisterId dst, RegisterId src) {
    check_register(dst);
    check_register(src);
    instructions_.push_back({Opcode::MoveReg, dst, src, 0, 0});
}

void Program::move_keep(RegisterId dst, std::uint16_t value, std::uint8_t shift_amount) {
    check_register(dst);
    if (shift_amount > 48 || (shift_amount % 16) != 0) {
        throw std::invalid_argument("MOVK shift amount must be 0, 16, 32, or 48");
    }
    instructions_.push_back({Opcode::MoveKeep, dst, 0, 0, value, ShiftKind::Lsl, shift_amount});
}

void Program::compare_imm(RegisterId src, std::uint64_t value) {
    check_register(src);
    instructions_.push_back({Opcode::CompareImm, 0, src, 0, value});
}

void Program::compare_reg(RegisterId lhs, RegisterId rhs) {
    check_register(lhs);
    check_register(rhs);
    instructions_.push_back({Opcode::CompareReg, 0, lhs, rhs, 0});
}

void Program::test_imm(RegisterId src, std::uint64_t mask) {
    check_register(src);
    instructions_.push_back({Opcode::TestImm, 0, src, 0, mask});
}

void Program::test_reg_shifted(RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount) {
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::TestRegShifted, 0, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::test_reg_shifted32(RegisterId lhs, RegisterId rhs, ShiftKind shift_kind, std::uint8_t shift_amount) {
    check_register(lhs);
    check_register(rhs);
    if (shift_amount >= 32) {
        throw std::invalid_argument("IR 32-bit shift amount out of range");
    }
    instructions_.push_back({Opcode::TestRegShifted32, 0, lhs, rhs, 0, shift_kind, shift_amount});
}

void Program::branch_if(Condition condition, std::uint32_t target) {
    instructions_.push_back({Opcode::BranchIf, 0, 0, 0, 0, ShiftKind::Lsl, 0, condition, target});
}

void Program::branch_reg_zero(RegisterId src, bool branch_when_zero, std::uint32_t target) {
    check_register(src);
    instructions_.push_back({Opcode::BranchRegZero, 0, src, 0, branch_when_zero ? 1u : 0u, ShiftKind::Lsl, 0, Condition::Eq, target});
}

void Program::branch_reg_bit(RegisterId src, std::uint8_t bit, bool branch_when_zero, std::uint32_t target) {
    check_register(src);
    if (bit >= 64) {
        throw std::invalid_argument("IR branch bit index out of range");
    }
    instructions_.push_back({Opcode::BranchRegBit, 0, src, 0, branch_when_zero ? 1u : 0u, ShiftKind::Lsl, bit, Condition::Eq, target});
}

void Program::call_vm(std::uint32_t target) {
    instructions_.push_back({Opcode::CallVm, 0, 0, 0, 0, ShiftKind::Lsl, 0,
                             Condition::Al, target});
}

void Program::load_fp_imm64(RegisterId dst, std::uint64_t bits) {
    check_register(dst);
    instructions_.push_back({Opcode::LoadFpImm64, dst, 0, 0, bits});
}

void Program::fp_add64(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpAdd64, dst, lhs, rhs});
}

void Program::fp_sub64(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpSub64, dst, lhs, rhs});
}

void Program::fp_mul64(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpMul64, dst, lhs, rhs});
}

void Program::fp_div64(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpDiv64, dst, lhs, rhs});
}

void Program::fp_madd64(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend) {
    check_register(dst); check_register(lhs); check_register(rhs); check_register(addend);
    instructions_.push_back({Opcode::FpMadd64, dst, lhs, rhs, 0, ShiftKind::Lsl,
                             addend});
}

void Program::fp_compare64(RegisterId lhs, RegisterId rhs) {
    check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpCompare64, 0, lhs, rhs});
}

void Program::load_fp_imm32(RegisterId dst, std::uint32_t bits) {
    check_register(dst); instructions_.push_back({Opcode::LoadFpImm32, dst, 0, 0, bits});
}
void Program::fp_add32(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs); instructions_.push_back({Opcode::FpAdd32, dst, lhs, rhs});
}
void Program::fp_sub32(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs); instructions_.push_back({Opcode::FpSub32, dst, lhs, rhs});
}
void Program::fp_mul32(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs); instructions_.push_back({Opcode::FpMul32, dst, lhs, rhs});
}
void Program::fp_div32(RegisterId dst, RegisterId lhs, RegisterId rhs) {
    check_register(dst); check_register(lhs); check_register(rhs); instructions_.push_back({Opcode::FpDiv32, dst, lhs, rhs});
}
void Program::fp_madd32(RegisterId dst, RegisterId lhs, RegisterId rhs, RegisterId addend) {
    check_register(dst); check_register(lhs); check_register(rhs); check_register(addend);
    instructions_.push_back({Opcode::FpMadd32, dst, lhs, rhs, 0, ShiftKind::Lsl, addend});
}
void Program::fp_compare32(RegisterId lhs, RegisterId rhs) {
    check_register(lhs); check_register(rhs); instructions_.push_back({Opcode::FpCompare32, 0, lhs, rhs});
}
void Program::fp_unary(RegisterId dst, RegisterId src, FpUnaryKind kind) {
    check_register(dst); check_register(src);
    instructions_.push_back({Opcode::FpUnary, dst, src, 0, static_cast<std::uint64_t>(kind)});
}
void Program::fp_convert(RegisterId dst, RegisterId src, FpConvertKind kind) {
    check_register(dst); check_register(src);
    instructions_.push_back({Opcode::FpConvert, dst, src, 0, static_cast<std::uint64_t>(kind)});
}
void Program::fp_min_max(RegisterId dst, RegisterId lhs, RegisterId rhs, FpMinMaxKind kind) {
    check_register(dst); check_register(lhs); check_register(rhs);
    instructions_.push_back({Opcode::FpMinMax, dst, lhs, rhs, static_cast<std::uint64_t>(kind)});
}
void Program::fp_conditional_compare(RegisterId lhs, RegisterId rhs, Condition condition,
                                     std::uint8_t fallback_nzcv, bool is_double,
                                     bool signaling) {
    check_register(lhs); check_register(rhs);
    if (fallback_nzcv > 0xfu) throw std::invalid_argument("FP conditional compare NZCV out of range");
    instructions_.push_back({Opcode::FpConditionalCompare, 0, lhs, rhs,
                             static_cast<std::uint64_t>(fallback_nzcv), ShiftKind::Lsl,
                             static_cast<std::uint8_t>(signaling), condition, 0,
                             ExtendKind::Uxtx, 0, static_cast<std::uint8_t>(is_double ? 8 : 4)});
}
void Program::fp_select(RegisterId dst, RegisterId true_src, RegisterId false_src,
                        Condition condition, bool is_double) {
    check_register(dst); check_register(true_src); check_register(false_src);
    instructions_.push_back({Opcode::FpSelect, dst, true_src, false_src, 0,
                             ShiftKind::Lsl, 0, condition, 0, ExtendKind::Uxtx, 0,
                             static_cast<std::uint8_t>(is_double ? 8 : 4)});
}

void Program::select(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero, bool false_is_zero) {
    check_register(dst);
    check_register(true_src);
    check_register(false_src);
    const auto flags = (true_is_zero ? kSelectTrueZero : 0ull) | (false_is_zero ? kSelectFalseZero : 0ull);
    instructions_.push_back({Opcode::Select, dst, true_src, false_src, flags, ShiftKind::Lsl, 0, condition, 0});
}

void Program::select_inc(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero, bool false_is_zero) {
    check_register(dst);
    check_register(true_src);
    check_register(false_src);
    const auto flags = (true_is_zero ? kSelectTrueZero : 0ull) | (false_is_zero ? kSelectFalseZero : 0ull);
    instructions_.push_back({Opcode::SelectInc, dst, true_src, false_src, flags, ShiftKind::Lsl, 0, condition, 0});
}

void Program::select_inv(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero, bool false_is_zero) {
    check_register(dst);
    check_register(true_src);
    check_register(false_src);
    const auto flags = (true_is_zero ? kSelectTrueZero : 0ull) | (false_is_zero ? kSelectFalseZero : 0ull);
    instructions_.push_back({Opcode::SelectInv, dst, true_src, false_src, flags, ShiftKind::Lsl, 0, condition, 0});
}

void Program::select_neg(RegisterId dst, RegisterId true_src, RegisterId false_src, Condition condition, bool true_is_zero, bool false_is_zero) {
    check_register(dst);
    check_register(true_src);
    check_register(false_src);
    const auto flags = (true_is_zero ? kSelectTrueZero : 0ull) | (false_is_zero ? kSelectFalseZero : 0ull);
    instructions_.push_back({Opcode::SelectNeg, dst, true_src, false_src, flags, ShiftKind::Lsl, 0, condition, 0});
}

void Program::load64(RegisterId dst, RegisterId base, std::uint64_t offset) {
    load_mem(dst, base, offset, 8);
}

void Program::store64(RegisterId src, RegisterId base, std::uint64_t offset, std::int64_t post_index) {
    store_mem(src, base, offset, 8, post_index);
}

void Program::load_mem(RegisterId dst, RegisterId base, std::uint64_t offset, std::uint8_t width) {
    check_register(dst);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::invalid_argument("IR memory width must be 1, 2, 4, or 8");
    }
    instructions_.push_back({Opcode::LoadMem, dst, base, 0, offset, ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::store_mem(RegisterId src, RegisterId base, std::uint64_t offset, std::uint8_t width, std::int64_t post_index) {
    check_register(src);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::invalid_argument("IR memory width must be 1, 2, 4, or 8");
    }
    instructions_.push_back({Opcode::StoreMem, 0, base, src, offset, ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, post_index, width});
}

void Program::load_acquire(RegisterId dst, RegisterId base, std::uint8_t width) {
    check_register(dst);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::invalid_argument("IR atomic width must be 1, 2, 4, or 8");
    instructions_.push_back({Opcode::LoadAcquire, dst, base, 0, 0, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::store_release(RegisterId src, RegisterId base, std::uint8_t width) {
    check_register(src);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::invalid_argument("IR atomic width must be 1, 2, 4, or 8");
    instructions_.push_back({Opcode::StoreRelease, 0, base, src, 0, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::load_exclusive(RegisterId dst, RegisterId base, std::uint8_t width, bool acquire) {
    check_register(dst);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::invalid_argument("IR atomic width must be 1, 2, 4, or 8");
    instructions_.push_back({Opcode::LoadExclusive, dst, base, 0, acquire ? 1ull : 0ull, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::store_exclusive(RegisterId status, RegisterId src, RegisterId base, std::uint8_t width,
                              bool release) {
    check_register(status);
    check_register(src);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::invalid_argument("IR atomic width must be 1, 2, 4, or 8");
    instructions_.push_back({Opcode::StoreExclusive, status, base, src, release ? 1ull : 0ull, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::load_exclusive_pair(RegisterId first, RegisterId second, RegisterId base,
                                  std::uint8_t width, bool acquire) {
    check_register(first); check_register(second); check_register(base);
    if (width != 4 && width != 8) throw std::invalid_argument("IR atomic-pair width must be 4 or 8");
    instructions_.push_back({Opcode::LoadExclusivePair, first, base, second,
                             acquire ? 1ull : 0ull, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::store_exclusive_pair(RegisterId status, RegisterId first, RegisterId second,
                                   RegisterId base, std::uint8_t width, bool release) {
    check_register(status); check_register(first); check_register(second); check_register(base);
    if (width != 4 && width != 8) throw std::invalid_argument("IR atomic-pair width must be 4 or 8");
    instructions_.push_back({Opcode::StoreExclusivePair, status, base, first,
                             release ? 1ull : 0ull, ShiftKind::Lsl, second,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::compare_swap(RegisterId expected, RegisterId desired, RegisterId base, std::uint8_t width,
                           bool acquire, bool release) {
    check_register(expected);
    check_register(desired);
    check_register(base);
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::invalid_argument("IR atomic width must be 1, 2, 4, or 8");
    const auto ordering = (acquire ? 1ull : 0ull) | (release ? 2ull : 0ull);
    instructions_.push_back({Opcode::CompareSwap, expected, base, desired, ordering, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, width});
}

void Program::load_reg_offset_mem(RegisterId dst,
                                  RegisterId base,
                                  RegisterId index,
                                  ExtendKind extend_kind,
                                  std::uint8_t shift_amount,
                                  std::uint8_t width) {
    check_register(dst);
    check_register(base);
    check_register(index);
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::invalid_argument("IR memory width must be 1, 2, 4, or 8");
    }
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::LoadRegOffsetMem, dst, base, index, 0, ShiftKind::Lsl, shift_amount, Condition::Eq, 0, extend_kind, 0, width});
}

void Program::store_reg_offset_mem(RegisterId src,
                                   RegisterId base,
                                   RegisterId index,
                                   ExtendKind extend_kind,
                                   std::uint8_t shift_amount,
                                   std::uint8_t width) {
    check_register(src);
    check_register(base);
    check_register(index);
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::invalid_argument("IR memory width must be 1, 2, 4, or 8");
    }
    if (shift_amount >= 64) {
        throw std::invalid_argument("IR shift amount out of range");
    }
    instructions_.push_back({Opcode::StoreRegOffsetMem, src, base, index, 0, ShiftKind::Lsl, shift_amount, Condition::Eq, 0, extend_kind, 0, width});
}

void Program::store_pair64(RegisterId first, RegisterId second, RegisterId base, std::uint64_t offset) {
    check_register(first);
    check_register(second);
    check_register(base);
    instructions_.push_back({Opcode::StorePair64, second, base, first, offset});
}

void Program::call_native(std::uint64_t target, std::uint64_t link_address) {
    instructions_.push_back({Opcode::CallNative, 0, 0, 0, target, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx,
                             static_cast<std::int64_t>(link_address)});
}

void Program::call_native_reg(RegisterId target, std::uint64_t link_address) {
    check_register(target);
    instructions_.push_back({Opcode::CallNativeReg, 0, target, 0, 0, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx,
                             static_cast<std::int64_t>(link_address)});
}

namespace {
void check_vector_register(RegisterId reg) {
    if (reg >= 32) {
        throw std::invalid_argument("IR vector register must be v0..v31");
    }
}

void check_vector_lane_width(std::uint8_t lane_bytes) {
    if (lane_bytes != 1 && lane_bytes != 2 && lane_bytes != 4 && lane_bytes != 8) {
        throw std::invalid_argument("IR vector lane width must be 1, 2, 4, or 8 bytes");
    }
}
} // namespace

void Program::vector_binary(RegisterId dst, RegisterId lhs, RegisterId rhs,
                            VectorBinaryKind kind, std::uint8_t lane_bytes) {
    check_vector_register(dst); check_vector_register(lhs); check_vector_register(rhs);
    check_vector_lane_width(lane_bytes);
    instructions_.push_back({Opcode::VectorBinary, dst, lhs, rhs, static_cast<std::uint64_t>(kind),
                             ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, lane_bytes});
}

void Program::vector_compare(RegisterId dst, RegisterId lhs, RegisterId rhs,
                             VectorCompareKind kind, std::uint8_t lane_bytes) {
    check_vector_register(dst); check_vector_register(lhs); check_vector_register(rhs);
    check_vector_lane_width(lane_bytes);
    instructions_.push_back({Opcode::VectorCompare, dst, lhs, rhs, static_cast<std::uint64_t>(kind),
                             ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, lane_bytes});
}

void Program::vector_select(RegisterId dst, RegisterId true_src, RegisterId false_src,
                            RegisterId mask) {
    check_vector_register(dst); check_vector_register(true_src); check_vector_register(false_src);
    check_vector_register(mask);
    instructions_.push_back({Opcode::VectorSelect, dst, true_src, false_src, mask});
}

void Program::vector_shift_imm(RegisterId dst, RegisterId src, VectorShiftKind kind,
                               std::uint8_t amount, std::uint8_t lane_bytes) {
    check_vector_register(dst); check_vector_register(src); check_vector_lane_width(lane_bytes);
    const auto lane_bits = static_cast<std::uint8_t>(lane_bytes * 8);
    if ((kind == VectorShiftKind::Left && amount >= lane_bits) ||
        (kind == VectorShiftKind::LogicalRight && (amount == 0 || amount > lane_bits))) {
        throw std::invalid_argument("IR vector shift is out of lane range");
    }
    instructions_.push_back({Opcode::VectorShiftImm, dst, src, 0, static_cast<std::uint64_t>(kind),
                             ShiftKind::Lsl, amount, Condition::Eq, 0, ExtendKind::Uxtx, 0, lane_bytes});
}

void Program::vector_permute(RegisterId dst, RegisterId lhs, RegisterId rhs,
                             VectorPermuteKind kind, std::uint8_t index) {
    check_vector_register(dst); check_vector_register(lhs); check_vector_register(rhs);
    if (index >= 16) throw std::invalid_argument("IR vector extract index is out of range");
    instructions_.push_back({Opcode::VectorPermute, dst, lhs, rhs, static_cast<std::uint64_t>(kind),
                             ShiftKind::Lsl, index});
}

void Program::vector_load(RegisterId dst, RegisterId base, std::uint64_t offset) {
    check_vector_register(dst); check_register(base);
    instructions_.push_back({Opcode::VectorLoad, dst, base, 0, offset, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, 16});
}

void Program::vector_store(RegisterId src, RegisterId base, std::uint64_t offset) {
    check_vector_register(src); check_register(base);
    instructions_.push_back({Opcode::VectorStore, src, base, 0, offset, ShiftKind::Lsl, 0,
                             Condition::Eq, 0, ExtendKind::Uxtx, 0, 16});
}

void Program::vector_fp_binary(RegisterId dst, RegisterId lhs, RegisterId rhs,
                               VectorFpBinaryKind kind, std::uint8_t lane_bytes) {
    check_vector_register(dst); check_vector_register(lhs); check_vector_register(rhs);
    if (lane_bytes != 4 && lane_bytes != 8) {
        throw std::invalid_argument("IR vector FP lane width must be 4 or 8 bytes");
    }
    instructions_.push_back({Opcode::VectorFpBinary, dst, lhs, rhs, static_cast<std::uint64_t>(kind),
                             ShiftKind::Lsl, 0, Condition::Eq, 0, ExtendKind::Uxtx, 0, lane_bytes});
}

void Program::vector_move_imm_zero(RegisterId dst) {
    check_vector_register(dst);
    instructions_.push_back({Opcode::VectorMoveImmZero, dst});
}

void Program::no_op() {
    instructions_.push_back({Opcode::NoOp});
}

void Program::return_reg(RegisterId src) {
    check_register(src);
    instructions_.push_back({Opcode::ReturnReg, 0, src, 0, 0});
}

void Program::remap_control_flow_targets(const std::vector<std::uint32_t>& source_to_ir) {
    for (auto& inst : instructions_) {
        switch (inst.opcode) {
        case Opcode::BranchIf:
        case Opcode::BranchRegZero:
        case Opcode::BranchRegBit:
        case Opcode::CallVm:
            if (inst.target >= source_to_ir.size()) {
                throw std::invalid_argument("IR source control-flow target is out of range");
            }
            inst.target = source_to_ir[inst.target];
            break;
        default:
            break;
        }
    }
}

const std::vector<Instruction>& Program::instructions() const noexcept {
    return instructions_;
}

void Program::validate() const {
    if (instructions_.empty()) {
        throw std::invalid_argument("IR program is empty");
    }
    bool has_return = false;
    for (const auto& inst : instructions_) {
        check_register(inst.dst);
        check_register(inst.src);
        check_register(inst.rhs);
        if (inst.shift_amount >= 64) {
            throw std::invalid_argument("IR shift amount out of range");
        }
        if ((inst.opcode == Opcode::BranchIf || inst.opcode == Opcode::BranchRegZero ||
             inst.opcode == Opcode::BranchRegBit || inst.opcode == Opcode::CallVm) &&
            inst.target >= instructions_.size()) {
            throw std::invalid_argument("IR branch target out of range");
        }
        if (inst.opcode == Opcode::BranchRegBit && inst.shift_amount >= 64) {
            throw std::invalid_argument("IR branch bit index out of range");
        }
        if (inst.opcode == Opcode::ReturnReg) {
            has_return = true;
        }
    }
    if (!has_return) {
        throw std::invalid_argument("IR program has no return");
    }
}

void Program::remap_branch_targets(const std::vector<std::uint32_t>& old_to_new_targets) {
    for (auto& inst : instructions_) {
        switch (inst.opcode) {
        case Opcode::BranchIf:
        case Opcode::BranchRegZero:
        case Opcode::BranchRegBit:
        case Opcode::CallVm:
            if (inst.target >= old_to_new_targets.size()) {
                throw std::invalid_argument("IR branch target remap index out of range");
            }
            inst.target = old_to_new_targets[inst.target];
            break;
        default:
            break;
        }
    }
}

std::string to_string(Opcode opcode) {
    switch (opcode) {
    case Opcode::LoadImm:
        return "LoadImm";
    case Opcode::LoadAddress:
        return "LoadAddress";
    case Opcode::AddImm:
        return "AddImm";
    case Opcode::SubImm:
        return "SubImm";
    case Opcode::AndImm:
        return "AndImm";
    case Opcode::OrImm:
        return "OrImm";
    case Opcode::XorImm:
        return "XorImm";
    case Opcode::AddImmSetFlags:
        return "AddImmSetFlags";
    case Opcode::SubImmSetFlags:
        return "SubImmSetFlags";
    case Opcode::AddRegShifted:
        return "AddRegShifted";
    case Opcode::AddRegShiftedSetFlags:
        return "AddRegShiftedSetFlags";
    case Opcode::AddRegExtended:
        return "AddRegExtended";
    case Opcode::AddRegExtendedSetFlags:
        return "AddRegExtendedSetFlags";
    case Opcode::SubRegShifted:
        return "SubRegShifted";
    case Opcode::SubRegShiftedSetFlags:
        return "SubRegShiftedSetFlags";
    case Opcode::SubRegExtended:
        return "SubRegExtended";
    case Opcode::SubRegExtendedSetFlags:
        return "SubRegExtendedSetFlags";
    case Opcode::NegReg:
        return "NegReg";
    case Opcode::NegRegSetFlags:
        return "NegRegSetFlags";
    case Opcode::AndRegShifted:
        return "AndRegShifted";
    case Opcode::BitClearRegShifted:
        return "BitClearRegShifted";
    case Opcode::NotRegShifted:
        return "NotRegShifted";
    case Opcode::OrRegShifted:
        return "OrRegShifted";
    case Opcode::OrNotRegShifted:
        return "OrNotRegShifted";
    case Opcode::XorRegShifted:
        return "XorRegShifted";
    case Opcode::XorNotRegShifted:
        return "XorNotRegShifted";
    case Opcode::ShiftReg:
        return "ShiftReg";
    case Opcode::ShiftReg32:
        return "ShiftReg32";
    case Opcode::ShiftRegVariable:
        return "ShiftRegVariable";
    case Opcode::DivideReg:
        return "DivideReg";
    case Opcode::BitfieldExtract:
        return "BitfieldExtract";
    case Opcode::UnsignedMulHigh:
        return "UnsignedMulHigh";
    case Opcode::MulAdd:
        return "MulAdd";
    case Opcode::MulSub:
        return "MulSub";
    case Opcode::SignedMulLong:
        return "SignedMulLong";
    case Opcode::UnsignedMulLong:
        return "UnsignedMulLong";
    case Opcode::MoveReg:
        return "MoveReg";
    case Opcode::MoveKeep:
        return "MoveKeep";
    case Opcode::CompareImm:
        return "CompareImm";
    case Opcode::CompareReg:
        return "CompareReg";
    case Opcode::TestImm:
        return "TestImm";
    case Opcode::TestRegShifted:
        return "TestRegShifted";
    case Opcode::TestRegShifted32:
        return "TestRegShifted32";
    case Opcode::BranchIf:
        return "BranchIf";
    case Opcode::BranchRegZero:
        return "BranchRegZero";
    case Opcode::BranchRegBit:
        return "BranchRegBit";
    case Opcode::CallVm:
        return "CallVm";
    case Opcode::LoadFpImm64:
        return "LoadFpImm64";
    case Opcode::FpAdd64:
        return "FpAdd64";
    case Opcode::FpSub64:
        return "FpSub64";
    case Opcode::FpMul64:
        return "FpMul64";
    case Opcode::FpDiv64:
        return "FpDiv64";
    case Opcode::FpMadd64:
        return "FpMadd64";
    case Opcode::FpCompare64:
        return "FpCompare64";
    case Opcode::LoadFpImm32: return "LoadFpImm32";
    case Opcode::FpAdd32: return "FpAdd32";
    case Opcode::FpSub32: return "FpSub32";
    case Opcode::FpMul32: return "FpMul32";
    case Opcode::FpDiv32: return "FpDiv32";
    case Opcode::FpMadd32: return "FpMadd32";
    case Opcode::FpCompare32: return "FpCompare32";
    case Opcode::FpUnary: return "FpUnary";
    case Opcode::FpConvert: return "FpConvert";
    case Opcode::FpMinMax: return "FpMinMax";
    case Opcode::FpConditionalCompare: return "FpConditionalCompare";
    case Opcode::FpSelect: return "FpSelect";
    case Opcode::Select:
        return "Select";
    case Opcode::SelectInc:
        return "SelectInc";
    case Opcode::SelectInv:
        return "SelectInv";
    case Opcode::SelectNeg:
        return "SelectNeg";
    case Opcode::Load64:
        return "Load64";
    case Opcode::Store64:
        return "Store64";
    case Opcode::LoadMem:
        return "LoadMem";
    case Opcode::StoreMem:
        return "StoreMem";
    case Opcode::LoadAcquire:
        return "LoadAcquire";
    case Opcode::StoreRelease:
        return "StoreRelease";
    case Opcode::LoadExclusive:
        return "LoadExclusive";
    case Opcode::StoreExclusive:
        return "StoreExclusive";
    case Opcode::LoadExclusivePair: return "LoadExclusivePair";
    case Opcode::StoreExclusivePair: return "StoreExclusivePair";
    case Opcode::CompareSwap:
        return "CompareSwap";
    case Opcode::LoadRegOffsetMem:
        return "LoadRegOffsetMem";
    case Opcode::StoreRegOffsetMem:
        return "StoreRegOffsetMem";
    case Opcode::StorePair64:
        return "StorePair64";
    case Opcode::CallNative:
        return "CallNative";
    case Opcode::CallNativeReg:
        return "CallNativeReg";
    case Opcode::VectorBinary: return "VectorBinary";
    case Opcode::VectorCompare: return "VectorCompare";
    case Opcode::VectorSelect: return "VectorSelect";
    case Opcode::VectorShiftImm: return "VectorShiftImm";
    case Opcode::VectorPermute: return "VectorPermute";
    case Opcode::VectorLoad: return "VectorLoad";
    case Opcode::VectorStore: return "VectorStore";
    case Opcode::VectorFpBinary: return "VectorFpBinary";
    case Opcode::VectorMoveImmZero: return "VectorMoveImmZero";
    case Opcode::NoOp:
        return "NoOp";
    case Opcode::ReturnReg:
        return "ReturnReg";
    }
    return "Unknown";
}

std::string to_string(Condition condition) {
    switch (condition) {
    case Condition::Eq:
        return "eq";
    case Condition::Ne:
        return "ne";
    case Condition::Hs:
        return "hs";
    case Condition::Lo:
        return "lo";
    case Condition::Mi:
        return "mi";
    case Condition::Pl:
        return "pl";
    case Condition::Vs:
        return "vs";
    case Condition::Vc:
        return "vc";
    case Condition::Hi:
        return "hi";
    case Condition::Ls:
        return "ls";
    case Condition::Ge:
        return "ge";
    case Condition::Lt:
        return "lt";
    case Condition::Gt:
        return "gt";
    case Condition::Le:
        return "le";
    case Condition::Al:
        return "al";
    case Condition::Nv:
        return "nv";
    }
    return "unknown";
}

std::uint64_t apply_extend(std::uint64_t value, ExtendKind extend_kind) {
    switch (extend_kind) {
    case ExtendKind::Uxtb:
        return value & 0xffu;
    case ExtendKind::Uxth:
        return value & 0xffffu;
    case ExtendKind::Uxtw:
        return value & 0xffffffffu;
    case ExtendKind::Uxtx:
        return value;
    case ExtendKind::Sxtb:
        return static_cast<std::uint64_t>(static_cast<std::int8_t>(value & 0xffu));
    case ExtendKind::Sxth:
        return static_cast<std::uint64_t>(static_cast<std::int16_t>(value & 0xffffu));
    case ExtendKind::Sxtw:
        return static_cast<std::uint64_t>(static_cast<std::int32_t>(value & 0xffffffffu));
    case ExtendKind::Sxtx:
        return value;
    }
    return value;
}

std::uint64_t apply_shift(std::uint64_t value, ShiftKind shift_kind, std::uint8_t amount) {
    if (amount == 0) {
        return value;
    }
    switch (shift_kind) {
    case ShiftKind::Lsl:
        return value << amount;
    case ShiftKind::Lsr:
        return value >> amount;
    case ShiftKind::Asr:
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(value) >> amount);
    case ShiftKind::Ror:
        return (value >> amount) | (value << (64 - amount));
    }
    return value;
}

} // namespace myvmp::ir
