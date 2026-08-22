#include "myvmp/arm64/decoder.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>

namespace myvmp::arm64 {
namespace {

std::optional<ir::ShiftKind> decode_shift_kind(std::uint32_t shift) {
    switch (shift) {
    case 0:
        return ir::ShiftKind::Lsl;
    case 1:
        return ir::ShiftKind::Lsr;
    case 2:
        return ir::ShiftKind::Asr;
    case 3:
        return ir::ShiftKind::Ror;
    default:
        return std::nullopt;
    }
}

std::uint64_t ones(std::uint32_t count) {
    if (count == 64) {
        return ~0ull;
    }
    return (1ull << count) - 1ull;
}

std::uint64_t rotate_right(std::uint64_t value, std::uint32_t amount, std::uint32_t width) {
    amount %= width;
    const auto mask = ones(width);
    value &= mask;
    if (amount == 0) {
        return value;
    }
    return ((value >> amount) | (value << (width - amount))) & mask;
}

std::uint64_t expand_fp_imm64(std::uint8_t imm8) {
    const auto sign = static_cast<std::uint64_t>(imm8 >> 7u) << 63u;
    const auto exponent_bit = static_cast<std::uint64_t>((imm8 >> 6u) & 1u);
    const auto exponent = ((exponent_bit ^ 1u) << 10u) |
                          (exponent_bit != 0 ? 0x3fcu : 0u) |
                          ((imm8 >> 4u) & 0x3u);
    const auto fraction = static_cast<std::uint64_t>(imm8 & 0xfu) << 48u;
    return sign | (exponent << 52u) | fraction;
}

std::uint32_t expand_fp_imm32(std::uint8_t imm8) {
    const auto sign = static_cast<std::uint32_t>(imm8 >> 7u) << 31u;
    const auto exponent_bit = static_cast<std::uint32_t>((imm8 >> 6u) & 1u);
    const auto exponent = ((exponent_bit ^ 1u) << 7u) |
                          (exponent_bit != 0 ? 0x7cu : 0u) |
                          ((imm8 >> 4u) & 3u);
    return sign | (exponent << 23u) | (static_cast<std::uint32_t>(imm8 & 0xfu) << 19u);
}

std::optional<std::uint64_t> decode_logical_immediate(std::uint32_t word) {
    const auto n = (word >> 22) & 1u;
    const auto immr = (word >> 16) & 0x3fu;
    const auto imms = (word >> 10) & 0x3fu;
    const auto combined = (n << 6) | ((~imms) & 0x3fu);
    if (combined == 0) {
        return std::nullopt;
    }

    std::uint32_t len = 0;
    for (std::uint32_t bit = 6; bit > 0; --bit) {
        if ((combined & (1u << bit)) != 0) {
            len = bit;
            break;
        }
    }

    const auto size = 1u << len;
    const auto levels = size - 1u;
    const auto s = imms & levels;
    const auto r = immr & levels;
    if (s == levels) {
        return std::nullopt;
    }

    const auto element = rotate_right(ones(s + 1u), r, size);
    std::uint64_t value = 0;
    for (std::uint32_t offset = 0; offset < 64; offset += size) {
        value |= element << offset;
    }
    return value;
}

std::optional<ir::Condition> decode_condition(std::uint32_t cond) {
    switch (cond) {
    case 0:
        return ir::Condition::Eq;
    case 1:
        return ir::Condition::Ne;
    case 2:
        return ir::Condition::Hs;
    case 3:
        return ir::Condition::Lo;
    case 4:
        return ir::Condition::Mi;
    case 5:
        return ir::Condition::Pl;
    case 6:
        return ir::Condition::Vs;
    case 7:
        return ir::Condition::Vc;
    case 8:
        return ir::Condition::Hi;
    case 9:
        return ir::Condition::Ls;
    case 10:
        return ir::Condition::Ge;
    case 11:
        return ir::Condition::Lt;
    case 12:
        return ir::Condition::Gt;
    case 13:
        return ir::Condition::Le;
    case 14:
        return ir::Condition::Al;
    case 15:
        return ir::Condition::Nv;
    default:
        return std::nullopt;
    }
}

std::optional<ir::ExtendKind> decode_extend_kind(std::uint32_t option) {
    switch (option) {
    case 0:
        return ir::ExtendKind::Uxtb;
    case 1:
        return ir::ExtendKind::Uxth;
    case 2:
        return ir::ExtendKind::Uxtw;
    case 3:
        return ir::ExtendKind::Uxtx;
    case 4:
        return ir::ExtendKind::Sxtb;
    case 5:
        return ir::ExtendKind::Sxth;
    case 6:
        return ir::ExtendKind::Sxtw;
    case 7:
        return ir::ExtendKind::Sxtx;
    default:
        return std::nullopt;
    }
}

std::int32_t sign_extend(std::uint32_t value, std::uint32_t bits) {
    const auto sign = 1u << (bits - 1u);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

std::int64_t sign_extend64(std::uint64_t value, std::uint32_t bits) {
    const auto sign = 1ull << (bits - 1u);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

void zero_extend_32_if_needed(ir::Program& program, const DecodedInstruction& decoded) {
    if (decoded.writes_32bit && decoded.rd < 31) {
        program.and_imm(decoded.rd, decoded.rd, 0xffffffffull);
    }
}

ir::RegisterId xzr_register(std::uint8_t encoded) {
    return encoded == 31 ? ir::kZeroRegister : encoded;
}

void add_signed_offset(ir::Program& program, ir::RegisterId reg, std::int64_t offset) {
    if (offset > 0) {
        program.add_imm(reg, reg, static_cast<std::uint64_t>(offset));
    } else if (offset < 0) {
        program.sub_imm(reg, reg, static_cast<std::uint64_t>(-offset));
    }
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || 4 > bytes.size() - offset) {
        throw std::runtime_error("ARM64 region read past end");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || 8 > bytes.size() - offset) {
        throw std::runtime_error("ARM64 region read past end");
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

std::vector<std::uint32_t> words_from_region(const std::vector<std::uint8_t>& region_bytes) {
    if ((region_bytes.size() % 4) != 0) {
        throw std::invalid_argument("ARM64 region byte size must be 4-byte aligned");
    }
    std::vector<std::uint32_t> words;
    words.reserve(region_bytes.size() / 4);
    for (std::size_t offset = 0; offset < region_bytes.size(); offset += 4) {
        words.push_back(read_u32_le(region_bytes, offset));
    }
    return words;
}

} // namespace

std::optional<DecodedInstruction> Decoder::decode(std::uint32_t word) const {
    if (word == 0xD65F03C0u) {
        return DecodedInstruction{Mnemonic::Ret, 30, 0, 0, 0};
    }

    if ((word & 0xffe01fe0u) == 0x1e601000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto imm8 = static_cast<std::uint8_t>((word >> 13u) & 0xffu);
        return DecodedInstruction{Mnemonic::FmovImm64, rd, 0, 0, expand_fp_imm64(imm8)};
    }
    if ((word & 0xffe01fe0u) == 0x1e201000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto imm8 = static_cast<std::uint8_t>((word >> 13u) & 0xffu);
        return DecodedInstruction{Mnemonic::FmovImm32, rd, 0, 0, expand_fp_imm32(imm8)};
    }

    const auto fp_unary = word & 0xfffffc00u;
    const auto fp_unary_kind = fp_unary == 0x1e20c000u ? 0
                               : fp_unary == 0x1e60c000u ? 1
                               : fp_unary == 0x1e214000u ? 2
                               : fp_unary == 0x1e614000u ? 3
                               : fp_unary == 0x1e21c000u ? 4
                               : fp_unary == 0x1e61c000u ? 5
                               : fp_unary == 0x1e22c000u ? 6
                               : fp_unary == 0x1e624000u ? 7 : -1;
    if (fp_unary_kind >= 0) {
        return DecodedInstruction{Mnemonic::FpUnary,
                                  static_cast<std::uint8_t>(word & 0x1fu),
                                  static_cast<std::uint8_t>((word >> 5u) & 0x1fu), 0,
                                  static_cast<std::uint64_t>(fp_unary_kind)};
    }

    const auto fp_convert = word & 0xfffffc00u;
    const std::array<std::uint32_t, 16> fp_convert_patterns{{
        0x1e220000u, 0x1e620000u, 0x9e220000u, 0x9e620000u,
        0x1e230000u, 0x1e630000u, 0x9e230000u, 0x9e630000u,
        0x1e380000u, 0x1e780000u, 0x9e380000u, 0x9e780000u,
        0x1e390000u, 0x1e790000u, 0x9e390000u, 0x9e790000u,
    }};
    const auto fp_convert_it = std::find(fp_convert_patterns.begin(), fp_convert_patterns.end(), fp_convert);
    if (fp_convert_it != fp_convert_patterns.end()) {
        return DecodedInstruction{Mnemonic::FpConvert,
                                  static_cast<std::uint8_t>(word & 0x1fu),
                                  static_cast<std::uint8_t>((word >> 5u) & 0x1fu), 0,
                                  static_cast<std::uint64_t>(fp_convert_it - fp_convert_patterns.begin())};
    }

    const auto fp_min_max = word & 0xffe0fc00u;
    const auto fp_min_max_kind = fp_min_max == 0x1e205800u ? 0
                                 : fp_min_max == 0x1e605800u ? 1
                                 : fp_min_max == 0x1e204800u ? 2
                                 : fp_min_max == 0x1e604800u ? 3
                                 : fp_min_max == 0x1e207800u ? 4
                                 : fp_min_max == 0x1e607800u ? 5
                                 : fp_min_max == 0x1e206800u ? 6
                                 : fp_min_max == 0x1e606800u ? 7 : -1;
    if (fp_min_max_kind >= 0) {
        return DecodedInstruction{Mnemonic::FpMinMax,
                                  static_cast<std::uint8_t>(word & 0x1fu),
                                  static_cast<std::uint8_t>((word >> 5u) & 0x1fu),
                                  static_cast<std::uint8_t>((word >> 16u) & 0x1fu),
                                  static_cast<std::uint64_t>(fp_min_max_kind)};
    }

    const auto fp_conditional = word & 0xffe00c00u;
    if (fp_conditional == 0x1e200400u || fp_conditional == 0x1e600400u) {
        return DecodedInstruction{Mnemonic::FpConditionalCompare, 0,
            static_cast<std::uint8_t>((word >> 5u) & 0x1fu),
            static_cast<std::uint8_t>((word >> 16u) & 0x1fu),
            static_cast<std::uint64_t>((word & 0xfu) | (((word >> 4u) & 1u) << 4u)),
            ir::ShiftKind::Lsl, 0,
            static_cast<ir::Condition>((word >> 12u) & 0xfu), 0,
            ir::ExtendKind::Uxtx, false,
            static_cast<std::uint8_t>(fp_conditional == 0x1e600400u ? 8 : 4)};
    }
    if (fp_conditional == 0x1e200c00u || fp_conditional == 0x1e600c00u) {
        return DecodedInstruction{Mnemonic::FpSelect,
            static_cast<std::uint8_t>(word & 0x1fu),
            static_cast<std::uint8_t>((word >> 5u) & 0x1fu),
            static_cast<std::uint8_t>((word >> 16u) & 0x1fu), 0,
            ir::ShiftKind::Lsl, 0,
            static_cast<ir::Condition>((word >> 12u) & 0xfu), 0,
            ir::ExtendKind::Uxtx, false,
            static_cast<std::uint8_t>(fp_conditional == 0x1e600c00u ? 8 : 4)};
    }

    const auto fp_binary = word & 0xffe0fc00u;
    if (fp_binary == 0x1e602800u || fp_binary == 0x1e603800u ||
        fp_binary == 0x1e600800u || fp_binary == 0x1e601800u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto rm = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        const auto mnemonic = fp_binary == 0x1e602800u ? Mnemonic::Fadd64
                              : fp_binary == 0x1e603800u ? Mnemonic::Fsub64
                              : fp_binary == 0x1e600800u ? Mnemonic::Fmul64
                                                        : Mnemonic::Fdiv64;
        return DecodedInstruction{mnemonic, rd, rn, rm};
    }
    if (fp_binary == 0x1e202800u || fp_binary == 0x1e203800u ||
        fp_binary == 0x1e200800u || fp_binary == 0x1e201800u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto rm = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        const auto mnemonic = fp_binary == 0x1e202800u ? Mnemonic::Fadd32
                              : fp_binary == 0x1e203800u ? Mnemonic::Fsub32
                              : fp_binary == 0x1e200800u ? Mnemonic::Fmul32 : Mnemonic::Fdiv32;
        return DecodedInstruction{mnemonic, rd, rn, rm};
    }

    if ((word & 0xffe08000u) == 0x1f400000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto ra = static_cast<std::uint8_t>((word >> 10u) & 0x1fu);
        const auto rm = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        return DecodedInstruction{Mnemonic::Fmadd64, rd, rn, rm, ra};
    }
    if ((word & 0xffe08000u) == 0x1f000000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto ra = static_cast<std::uint8_t>((word >> 10u) & 0x1fu);
        const auto rm = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        return DecodedInstruction{Mnemonic::Fmadd32, rd, rn, rm, ra};
    }

    if ((word & 0xffe0fc0fu) == 0x1e602000u) {
        const auto rn = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto rm = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        return DecodedInstruction{Mnemonic::Fcompare64, 0, rn, rm};
    }
    if ((word & 0xffe0fc0fu) == 0x1e202000u) {
        return DecodedInstruction{Mnemonic::Fcompare32, 0,
            static_cast<std::uint8_t>((word >> 5u) & 0x1fu), static_cast<std::uint8_t>((word >> 16u) & 0x1fu)};
    }

    if ((word & 0xFFFFFC1Fu) == 0xD61F0000u) {
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        if (rn < 31) {
            return DecodedInstruction{Mnemonic::BranchReg, 0, rn, 0, 0};
        }
    }

    if ((word & 0xFFFFFC1Fu) == 0xD63F0000u) {
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        if (rn < 31) {
            return DecodedInstruction{Mnemonic::BranchLinkReg, 0, rn, 0, 0};
        }
    }

    if ((word & 0xFFFFF0FFu) == 0xD50330BFu || (word & 0xFFFFF0FFu) == 0xD503309Fu || word == 0xD5033FDFu) {
        return DecodedInstruction{Mnemonic::Barrier};
    }

    if ((word & 0x9F000000u) == 0x10000000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto immlo = (word >> 29) & 0x3u;
        const auto immhi = (word >> 5) & 0x7ffffu;
        const auto imm = (immhi << 2) | immlo;
        if (rd < 31) {
            return DecodedInstruction{Mnemonic::Adr, rd, 0, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, sign_extend(imm, 21)};
        }
    }

    if ((word & 0x9F000000u) == 0x90000000u) {
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto immlo = (word >> 29) & 0x3u;
        const auto immhi = (word >> 5) & 0x7ffffu;
        const auto imm = (immhi << 2) | immlo;
        if (rd < 31) {
            return DecodedInstruction{Mnemonic::Adrp, rd, 0, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, sign_extend64(imm, 21) << 12};
        }
    }

    if ((word & 0x7F800000u) == 0x12800000u || (word & 0x7F800000u) == 0x52800000u || (word & 0x7F800000u) == 0x72800000u) {
        const auto op = word & 0x7F800000u;
        const auto mnemonic = op == 0x72800000u ? Mnemonic::Movk : (op == 0x12800000u ? Mnemonic::Movn : Mnemonic::Movz);
        const bool is64 = (word & 0x80000000u) != 0;
        const auto hw = (word >> 21) & 0x3u;
        const auto imm16 = (word >> 5) & 0xffffu;
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && (is64 || hw < 2)) {
            const auto shifted = static_cast<std::uint64_t>(imm16) << (hw * 16);
            const auto imm = mnemonic == Mnemonic::Movk ? imm16 : (mnemonic == Mnemonic::Movn ? ~shifted : shifted);
            return DecodedInstruction{
                mnemonic,
                rd,
                0,
                0,
                is64 ? imm : (imm & 0xffffffffull),
                ir::ShiftKind::Lsl,
                static_cast<std::uint8_t>(hw * 16),
                ir::Condition::Eq,
                0,
                ir::ExtendKind::Uxtx,
                !is64,
            };
        }
    }

    if ((word & 0x1F000000u) == 0x11000000u) {
        const bool is_sub = (word & 0x40000000u) != 0;
        const bool sets_flags = (word & 0x20000000u) != 0;
        const auto shift = (word >> 22) & 0x3u;
        const auto imm12 = (word >> 10) & 0xfffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && shift <= 1) {
            const auto imm = static_cast<std::uint64_t>(imm12) << (shift * 12);
            Mnemonic mnemonic{};
            if (sets_flags) {
                mnemonic = is_sub ? Mnemonic::SubImmSetFlags : Mnemonic::AddImmSetFlags;
            } else {
                mnemonic = is_sub ? Mnemonic::SubImm : Mnemonic::AddImm;
            }
            return DecodedInstruction{mnemonic, rd, rn, 0, imm, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x1FE00000u) == 0x0B200000u) {
        const bool is_sub = (word & 0x40000000u) != 0;
        const bool sets_flags = (word & 0x20000000u) != 0;
        const auto option = decode_extend_kind((word >> 13) & 0x7u);
        const auto amount = static_cast<std::uint8_t>((word >> 10) & 0x7u);
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (option.has_value() && rd <= 31 && rn <= 31 && rm <= 31 && amount <= 4) {
            if (sets_flags) {
                return DecodedInstruction{is_sub ? Mnemonic::SubExtendedRegSetFlags : Mnemonic::AddExtendedRegSetFlags,
                                          rd, rn, rm, 0, ir::ShiftKind::Lsl, amount, ir::Condition::Eq, 0, *option,
                                          (word & 0x80000000u) == 0};
            }
            if (!sets_flags) {
                return DecodedInstruction{is_sub ? Mnemonic::SubExtendedReg : Mnemonic::AddExtendedReg, rd, rn, rm, 0, ir::ShiftKind::Lsl, amount, ir::Condition::Eq, 0, *option, (word & 0x80000000u) == 0};
            }
        }
    }

    if ((word & 0x1F000000u) == 0x0B000000u) {
        const bool is_sub = (word & 0x40000000u) != 0;
        const bool sets_flags = (word & 0x20000000u) != 0;
        const auto shift = decode_shift_kind((word >> 22) & 0x3u);
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto amount = static_cast<std::uint8_t>((word >> 10) & 0x3fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = (word & 0x80000000u) == 0 ? 32u : 64u;
        if (shift.has_value() && rd <= 31 && rn <= 31 && rm <= 31 && amount < width) {
            return DecodedInstruction{
                sets_flags ? (is_sub ? Mnemonic::SubShiftedRegSetFlags : Mnemonic::AddShiftedRegSetFlags)
                           : (is_sub ? Mnemonic::SubShiftedReg : Mnemonic::AddShiftedReg),
                rd,
                rn,
                rm,
                0,
                *shift,
                amount,
                ir::Condition::Eq,
                0,
                ir::ExtendKind::Uxtx,
                (word & 0x80000000u) == 0,
            };
        }
    }

    if ((word & 0x1F000000u) == 0x0A000000u) {
        const auto opc = (word >> 29) & 0x3u;
        const auto invert_rhs = ((word >> 21) & 0x1u) != 0;
        const auto shift = decode_shift_kind((word >> 22) & 0x3u);
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto amount = static_cast<std::uint8_t>((word >> 10) & 0x3fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = (word & 0x80000000u) == 0 ? 32u : 64u;
        if (shift.has_value() && rd <= 31 && rn <= 31 && rm <= 31 && amount < width) {
            if (opc == 0) {
                return DecodedInstruction{invert_rhs ? Mnemonic::AndShiftedReg : Mnemonic::AndShiftedReg, rd, rn, rm, invert_rhs ? 1u : 0u, *shift, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
            }
            if (opc == 1) {
                return DecodedInstruction{invert_rhs ? Mnemonic::OrrShiftedReg : Mnemonic::OrrShiftedReg, rd, rn, rm, invert_rhs ? 1u : 0u, *shift, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
            }
            if (opc == 2) {
                return DecodedInstruction{invert_rhs ? Mnemonic::EorShiftedReg : Mnemonic::EorShiftedReg, rd, rn, rm, invert_rhs ? 1u : 0u, *shift, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
            }
            if (opc == 3) {
                return DecodedInstruction{Mnemonic::AndSetFlagsShiftedReg, rd, rn, rm, invert_rhs ? 1u : 0u, *shift, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
            }
        }
        if (opc == 3 && shift.has_value() && rd == 31 && rn < 31 && rm < 31 && !invert_rhs) {
            return DecodedInstruction{Mnemonic::AndSetFlagsShiftedReg, rd, rn, rm, 0, *shift, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x7F200000u) == 0x6B000000u && (word & 0x1fu) == 0x1fu) {
        const auto shift = decode_shift_kind((word >> 22) & 0x3u);
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto amount = static_cast<std::uint8_t>((word >> 10) & 0x3fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        if (shift.has_value() && rn < 31 && rm < 31 && amount == 0) {
            return DecodedInstruction{Mnemonic::CompareReg, 0, rn, rm, 0};
        }
    }

    if ((word & 0x7F800000u) == 0x53000000u) {
        const bool writes_32bit = (word & 0x80000000u) == 0;
        const std::uint32_t width = writes_32bit ? 32u : 64u;
        const auto immr = (word >> 16) & 0x3fu;
        const auto imms = (word >> 10) & 0x3fu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && imms + 1u == immr && immr != 0 && immr < width) {
            return DecodedInstruction{Mnemonic::LslImm, rd, rn, 0, 0, ir::ShiftKind::Lsl, static_cast<std::uint8_t>(width - immr), ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
        if (rd <= 31 && rn <= 31 && imms == width - 1u) {
            return DecodedInstruction{Mnemonic::LsrImm, rd, rn, 0, 0, ir::ShiftKind::Lsr, static_cast<std::uint8_t>(immr), ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
        if (rd <= 31 && rn <= 31 && imms >= immr && imms < width) {
            return DecodedInstruction{Mnemonic::Ubfx, rd, rn, 0, static_cast<std::uint64_t>(imms - immr + 1u), ir::ShiftKind::Lsl, static_cast<std::uint8_t>(immr), ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
    }

    if ((word & 0x7F800000u) == 0x13000000u) {
        const bool writes_32bit = (word & 0x80000000u) == 0;
        const std::uint32_t width = writes_32bit ? 32u : 64u;
        const auto immr = (word >> 16) & 0x3fu;
        const auto imms = (word >> 10) & 0x3fu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && imms == width - 1u && immr < width) {
            return DecodedInstruction{Mnemonic::AsrImm, rd, rn, 0, 0, ir::ShiftKind::Asr, static_cast<std::uint8_t>(immr), ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
    }

    if ((word & 0x7F800000u) == 0x13000000u) {
        const bool writes_32bit = (word & 0x80000000u) == 0;
        const auto immr = (word >> 16) & 0x3fu;
        const auto imms = (word >> 10) & 0x3fu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && immr == 0 && (imms == 7 || imms == 15 || imms == 31)) {
            return DecodedInstruction{Mnemonic::SignExtendReg, rd, rn, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit, static_cast<std::uint8_t>((imms + 1u) / 8u)};
        }
    }

    if ((word & 0xFFE00000u) == 0x13800000u || (word & 0xFFE00000u) == 0x93C00000u) {
        const bool writes_32bit = (word & 0x80000000u) == 0;
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto amount = static_cast<std::uint8_t>((word >> 10) & 0x3fu);
        const std::uint8_t width = writes_32bit ? 32 : 64;
        if (rd <= 31 && rn <= 31 && rn == rm && amount < width) {
            return DecodedInstruction{Mnemonic::RorImm, rd, rn, 0, 0, ir::ShiftKind::Ror, amount, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
    }

    if ((word & 0x7FE0FC00u) == 0x1AC00800u || (word & 0x7FE0FC00u) == 0x1AC00C00u) {
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && rm <= 31) {
            return DecodedInstruction{(word & 0x00000400u) != 0 ? Mnemonic::SignedDivide : Mnemonic::UnsignedDivide,
                                      rd, rn, rm, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0,
                                      ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x7FE0F000u) == 0x1AC02000u) {
        const auto operation = (word >> 10) & 0x3u;
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && rm <= 31) {
            return DecodedInstruction{Mnemonic::ShiftVariable, rd, rn, rm, 0,
                                      static_cast<ir::ShiftKind>(operation), 0, ir::Condition::Eq, 0,
                                      ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x7F000000u) == 0x71000000u && (word & 0x1fu) == 0x1fu) {
        const auto shift = (word >> 22) & 0x3u;
        const auto imm12 = (word >> 10) & 0xfffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        if (rn < 31 && shift <= 1) {
            return DecodedInstruction{Mnemonic::CompareImm, 0, rn, 0, static_cast<std::uint64_t>(imm12) << (shift * 12)};
        }
    }

    if ((word & 0x7F800000u) == 0x72000000u && (word & 0x1fu) == 0x1fu) {
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto mask = decode_logical_immediate(word);
        if (rn <= 31 && mask.has_value()) {
            return DecodedInstruction{Mnemonic::TestImm, 0, rn, 0, *mask};
        }
    }

    if ((word & 0x1F800000u) == 0x12000000u && ((word >> 29) & 0x3u) == 0) {
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto mask = decode_logical_immediate(word);
        if (rd <= 31 && rn <= 31 && mask.has_value()) {
            const bool writes_32bit = (word & 0x80000000u) == 0;
            return DecodedInstruction{Mnemonic::AndImm, rd, rn, 0, writes_32bit ? (*mask & 0xffffffffull) : *mask, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
    }

    if ((word & 0x1F800000u) == 0x12000000u &&
        (((word >> 29) & 0x3u) == 1 || ((word >> 29) & 0x3u) == 2)) {
        const auto opc = (word >> 29) & 0x3u;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        const auto mask = decode_logical_immediate(word);
        if (rd <= 31 && rn <= 31 && mask.has_value()) {
            const bool writes_32bit = (word & 0x80000000u) == 0;
            return DecodedInstruction{opc == 1 ? Mnemonic::OrrImm : Mnemonic::EorImm, rd, rn, 0,
                                      writes_32bit ? (*mask & 0xffffffffull) : *mask, ir::ShiftKind::Lsl, 0,
                                      ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, writes_32bit};
        }
    }

    if ((word & 0xFF000010u) == 0x54000000u) {
        const auto condition = decode_condition(word & 0xfu);
        if (condition.has_value()) {
            const auto imm19 = (word >> 5) & 0x7ffffu;
            return DecodedInstruction{Mnemonic::BranchCond, 0, 0, 0, 0, ir::ShiftKind::Lsl, 0, *condition, sign_extend(imm19, 19)};
        }
    }

    if ((word & 0x7C000000u) == 0x14000000u) {
        const auto imm26 = word & 0x03ffffffu;
        const auto offset = sign_extend(imm26, 26);
        if ((word & 0x80000000u) != 0) {
            return DecodedInstruction{Mnemonic::BranchLink, 0, 0, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Al, offset};
        }
        return DecodedInstruction{Mnemonic::BranchUncond, 0, 0, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Al, offset};
    }

    if ((word & 0x7E000000u) == 0x34000000u) {
        const auto is_cbnz = (word & 0x01000000u) != 0;
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto imm19 = (word >> 5) & 0x7ffffu;
        if (rt <= 31) {
            return DecodedInstruction{Mnemonic::BranchZero, 0, rt, 0, is_cbnz ? 0u : 1u, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, sign_extend(imm19, 19)};
        }
    }

    if ((word & 0x7E000000u) == 0x36000000u) {
        const auto is_tbnz = (word & 0x01000000u) != 0;
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto imm14 = (word >> 5) & 0x3fffu;
        const auto bit = static_cast<std::uint8_t>(((word >> 26) & 0x20u) | ((word >> 19) & 0x1fu));
        if (rt <= 31) {
            return DecodedInstruction{Mnemonic::BranchBit, 0, rt, 0, is_tbnz ? 0u : 1u, ir::ShiftKind::Lsl, bit, ir::Condition::Eq, sign_extend(imm14, 14)};
        }
    }

    if ((word & 0x1FE00000u) == 0x1A800000u) {
        const auto condition = decode_condition((word >> 12) & 0xfu);
        const auto op = (word >> 30) & 0x1u;
        const auto op2 = (word >> 10) & 0x3u;
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (condition.has_value() && rd <= 31 && rn <= 31 && rm <= 31) {
            Mnemonic mnemonic{};
            if (op == 0 && op2 == 0) {
                mnemonic = Mnemonic::Select;
            } else if (op == 0 && op2 == 1) {
                mnemonic = Mnemonic::SelectInc;
            } else if (op == 1 && op2 == 0) {
                mnemonic = Mnemonic::SelectInv;
            } else if (op == 1 && op2 == 1) {
                mnemonic = Mnemonic::SelectNeg;
            } else {
                return std::nullopt;
            }
            return DecodedInstruction{mnemonic, rd, rn, rm, 0, ir::ShiftKind::Lsl, 0, *condition, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x1F000000u) == 0x1B000000u && ((word >> 21) & 0x7u) == 0) {
        const bool is_sub = (word & 0x00008000u) != 0;
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto ra = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && rm <= 31 && ra <= 31) {
            return DecodedInstruction{is_sub ? Mnemonic::MulSub : Mnemonic::MulAdd, rd, rn, rm, ra, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0xFFE00000u) == 0x9B200000u || (word & 0xFFE00000u) == 0x9BA00000u) {
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto ra = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && rm <= 31 && ra == 31) {
            const bool is_unsigned = (word & 0x00800000u) != 0;
            return DecodedInstruction{is_unsigned ? Mnemonic::UnsignedMulLong : Mnemonic::SignedMulLong, rd, rn, rm, 0};
        }
    }

    if ((word & 0xFFE0FC00u) == 0x9BC07C00u) {
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rd = static_cast<std::uint8_t>(word & 0x1fu);
        if (rd <= 31 && rn <= 31 && rm <= 31) {
            return DecodedInstruction{Mnemonic::UnsignedMulHigh, rd, rn, rm, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, (word & 0x80000000u) == 0};
        }
    }

    if ((word & 0x3FC00000u) == 0x39400000u) {
        const auto size = (word >> 30) & 0x3u;
        const auto imm12 = (word >> 10) & 0xfffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::LoadUnsigned, rt, rn, 0, static_cast<std::uint64_t>(imm12) * width, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3FFFFC00u) == 0x08DFFC00u) {
        const auto width = static_cast<std::uint8_t>(1u << ((word >> 30) & 0x3u));
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::LoadAcquire, rt, rn, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3FFFFC00u) == 0x089FFC00u) {
        const auto width = static_cast<std::uint8_t>(1u << ((word >> 30) & 0x3u));
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::StoreRelease, rt, rn, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3FA07C00u) == 0x08A07C00u) {
        const auto expected = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        const auto base = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto desired = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << ((word >> 30u) & 0x3u));
        const auto ordering = static_cast<std::uint64_t>(((word >> 22u) & 1u) |
                                                        (((word >> 15u) & 1u) << 1u));
        return DecodedInstruction{Mnemonic::CompareSwap, expected, base, desired, ordering,
                                  ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0,
                                  ir::ExtendKind::Uxtx, width < 8, width};
    }

    if ((word & 0x3FFF7C00u) == 0x085F7C00u) {
        const auto width = static_cast<std::uint8_t>(1u << ((word >> 30) & 0x3u));
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::LoadExclusive, rt, rn, 0,
                                      (word & 0x8000u) != 0 ? 1ull : 0ull,
                                      ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0,
                                      ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3fff0000u) == 0x087f0000u) {
        const auto first = static_cast<std::uint8_t>(word & 0x1fu);
        const auto second = static_cast<std::uint8_t>((word >> 10u) & 0x1fu);
        const auto base = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto width = static_cast<std::uint8_t>((word & 0x40000000u) != 0 ? 8 : 4);
        return DecodedInstruction{Mnemonic::LoadExclusivePair, first, base, second,
                                  (word & 0x8000u) != 0 ? 1ull : 0ull,
                                  ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0,
                                  ir::ExtendKind::Uxtx, width == 4, width};
    }

    if ((word & 0x3fe00000u) == 0x08200000u) {
        const auto status = static_cast<std::uint8_t>((word >> 16u) & 0x1fu);
        const auto first = static_cast<std::uint8_t>(word & 0x1fu);
        const auto second = static_cast<std::uint8_t>((word >> 10u) & 0x1fu);
        const auto base = static_cast<std::uint8_t>((word >> 5u) & 0x1fu);
        const auto width = static_cast<std::uint8_t>((word & 0x40000000u) != 0 ? 8 : 4);
        return DecodedInstruction{Mnemonic::StoreExclusivePair, status, base, first,
                                  (word & 0x8000u) != 0 ? 1ull : 0ull,
                                  ir::ShiftKind::Lsl, second, ir::Condition::Eq, 0,
                                  ir::ExtendKind::Uxtx, width == 4, width};
    }

    if ((word & 0x3FE07C00u) == 0x08007C00u) {
        const auto width = static_cast<std::uint8_t>(1u << ((word >> 30) & 0x3u));
        const auto rs = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rs <= 31 && rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::StoreExclusive, rs, rn, rt,
                                      (word & 0x8000u) != 0 ? 1ull : 0ull,
                                      ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0,
                                      ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3F800000u) == 0x39800000u) {
        const auto size = (word >> 30) & 0x3u;
        const auto imm12 = (word >> 10) & 0xfffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        if (rt <= 31 && rn <= 31 && width < 8) {
            return DecodedInstruction{Mnemonic::LoadSigned, rt, rn, 0, static_cast<std::uint64_t>(imm12) * width, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, false, width};
        }
    }

    if ((word & 0x3B000000u) == 0x18000000u) {
        const auto opc = (word >> 30) & 0x3u;
        const auto imm19 = (word >> 5) & 0x7ffffu;
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if ((opc == 0 || opc == 1) && rt <= 31) {
            const auto width = static_cast<std::uint8_t>(opc == 0 ? 4 : 8);
            return DecodedInstruction{Mnemonic::LoadLiteral, rt, 0, 0, 0, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, sign_extend(imm19, 19) * 4, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3B200C00u) == 0x38000000u) {
        const auto size = (word >> 30) & 0x3u;
        const auto imm9 = (word >> 12) & 0x1ffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        const auto offset = static_cast<std::uint64_t>(sign_extend(imm9, 9));
        const bool is_load = (word & 0x00400000u) != 0;
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{is_load ? Mnemonic::LoadUnsigned : Mnemonic::StoreUnsigned, rt, rn, 0, offset, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0x3B200C00u) == 0x38000400u || (word & 0x3B200C00u) == 0x38000C00u) {
        const auto size = (word >> 30) & 0x3u;
        const auto opc = (word >> 22) & 0x3u;
        const auto imm9 = (word >> 12) & 0x1ffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        const auto offset = static_cast<std::uint64_t>(sign_extend(imm9, 9));
        const bool is_pre_index = (word & 0x00000800u) != 0;
        const bool is_signed_load = opc == 2 && width < 8;
        if (rt <= 31 && rn <= 31 && (opc <= 1 || is_signed_load)) {
            Mnemonic mnemonic{};
            if (is_signed_load) {
                mnemonic = is_pre_index ? Mnemonic::LoadSignedPreIndex : Mnemonic::LoadSignedPostIndex;
            } else if (opc == 1) {
                mnemonic = is_pre_index ? Mnemonic::LoadPreIndex : Mnemonic::LoadPostIndex;
            } else {
                mnemonic = is_pre_index ? Mnemonic::StorePreIndex : Mnemonic::StorePostIndex;
            }
            return DecodedInstruction{mnemonic, rt, rn, 0, offset, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, !is_signed_load && width < 8, width};
        }
    }

    if ((word & 0x3B200C00u) == 0x38200800u) {
        const auto size = (word >> 30) & 0x3u;
        const auto opc = (word >> 22) & 0x3u;
        const auto option = decode_extend_kind((word >> 13) & 0x7u);
        const auto scaled = ((word >> 12) & 0x1u) != 0;
        const auto rm = static_cast<std::uint8_t>((word >> 16) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        if (opc == 2 && option.has_value() && rt <= 31 && rn <= 31 && rm <= 31 && width < 8) {
            return DecodedInstruction{Mnemonic::LoadSignedRegOffset,
                                      rt,
                                      rn,
                                      rm,
                                      0,
                                      ir::ShiftKind::Lsl,
                                      static_cast<std::uint8_t>(scaled ? size : 0),
                                      ir::Condition::Eq,
                                      0,
                                      *option,
                                      false,
                                      width};
        }
        const bool is_load = opc == 1;
        if (opc <= 1 && option.has_value() && rt <= 31 && rn <= 31 && rm <= 31) {
            return DecodedInstruction{is_load ? Mnemonic::LoadRegOffset : Mnemonic::StoreRegOffset,
                                      rt,
                                      rn,
                                      rm,
                                      0,
                                      ir::ShiftKind::Lsl,
                                      static_cast<std::uint8_t>(scaled ? size : 0),
                                      ir::Condition::Eq,
                                      0,
                                      *option,
                                      width < 8,
                                      width};
        }
    }

    if ((word & 0x3FC00000u) == 0x39000000u) {
        const auto size = (word >> 30) & 0x3u;
        const auto imm12 = (word >> 10) & 0xfffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const auto width = static_cast<std::uint8_t>(1u << size);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::StoreUnsigned, rt, rn, 0, static_cast<std::uint64_t>(imm12) * width, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
        }
    }

    if ((word & 0xFFC00C00u) == 0xF8000400u) {
        const auto imm9 = (word >> 12) & 0x1ffu;
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::Store64PostIndex, rt, rn, 0, static_cast<std::uint64_t>(sign_extend(imm9, 9))};
        }
    }

    if ((word & 0x3A000000u) == 0x28000000u) {
        const auto opc = (word >> 30) & 0x3u;
        const auto kind = (word >> 23) & 0x3u;
        const auto imm7 = (word >> 15) & 0x7fu;
        const auto rt2 = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        const bool is_load = (word & 0x00400000u) != 0;
        const bool is_signed_load = is_load && opc == 1;
        const bool is_pair64 = opc == 2;
        const bool is_pair32 = opc == 0 || is_signed_load;
        if (rt <= 31 && rt2 <= 31 && rn <= 31 && is_pair32 && (is_load || opc == 0)) {
            const auto scale = is_pair64 ? 3u : 2u;
            const auto width = static_cast<std::uint8_t>(is_pair64 ? 8 : 4);
            const auto offset = static_cast<std::uint64_t>(sign_extend(imm7, 7)) << scale;
            Mnemonic mnemonic{};
            if (is_signed_load) {
                mnemonic = kind == 3 ? Mnemonic::LoadSignedPairPreIndex
                         : kind == 1 ? Mnemonic::LoadSignedPairPostIndex
                                     : Mnemonic::LoadSignedPair;
            } else if (is_load) {
                mnemonic = kind == 3 ? Mnemonic::LoadPairPreIndex
                         : kind == 1 ? Mnemonic::LoadPairPostIndex
                                     : Mnemonic::LoadPair;
            } else {
                mnemonic = kind == 3 ? Mnemonic::StorePairPreIndex
                         : kind == 1 ? Mnemonic::StorePairPostIndex
                                     : Mnemonic::StorePair;
            }
            if (kind == 1 || kind == 2 || kind == 3) {
                return DecodedInstruction{mnemonic, rt2, rn, rt, offset, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, width < 8, width};
            }
        }
    }

    if ((word & 0xFFC00000u) == 0xA9000000u) {
        const auto imm7 = (word >> 15) & 0x7fu;
        const auto rt2 = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rt2 <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::StorePair, rt2, rn, rt, static_cast<std::uint64_t>(sign_extend(imm7, 7)) * 8u, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, false, 8};
        }
    }

    if ((word & 0xFFC00000u) == 0xA9800000u || (word & 0xFFC00000u) == 0xA8800000u) {
        const auto imm7 = (word >> 15) & 0x7fu;
        const auto rt2 = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rt2 <= 31 && rn <= 31) {
            const bool is_pre_index = (word & 0xFFC00000u) == 0xA9800000u;
            return DecodedInstruction{is_pre_index ? Mnemonic::StorePairPreIndex : Mnemonic::StorePairPostIndex,
                                      rt2,
                                      rn,
                                      rt,
                                      static_cast<std::uint64_t>(sign_extend(imm7, 7)) * 8u,
                                      ir::ShiftKind::Lsl,
                                      0,
                                      ir::Condition::Eq,
                                      0,
                                      ir::ExtendKind::Uxtx,
                                      false,
                                      8};
        }
    }

    if ((word & 0xFFC00000u) == 0xA9400000u) {
        const auto imm7 = (word >> 15) & 0x7fu;
        const auto rt2 = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rt2 <= 31 && rn <= 31) {
            return DecodedInstruction{Mnemonic::LoadPair, rt2, rn, rt, static_cast<std::uint64_t>(sign_extend(imm7, 7)) * 8u, ir::ShiftKind::Lsl, 0, ir::Condition::Eq, 0, ir::ExtendKind::Uxtx, false, 8};
        }
    }

    if ((word & 0xFFC00000u) == 0xA9C00000u || (word & 0xFFC00000u) == 0xA8C00000u) {
        const auto imm7 = (word >> 15) & 0x7fu;
        const auto rt2 = static_cast<std::uint8_t>((word >> 10) & 0x1fu);
        const auto rn = static_cast<std::uint8_t>((word >> 5) & 0x1fu);
        const auto rt = static_cast<std::uint8_t>(word & 0x1fu);
        if (rt <= 31 && rt2 <= 31 && rn <= 31) {
            const bool is_pre_index = (word & 0xFFC00000u) == 0xA9C00000u;
            return DecodedInstruction{is_pre_index ? Mnemonic::LoadPairPreIndex : Mnemonic::LoadPairPostIndex,
                                      rt2,
                                      rn,
                                      rt,
                                      static_cast<std::uint64_t>(sign_extend(imm7, 7)) * 8u,
                                      ir::ShiftKind::Lsl,
                                      0,
                                      ir::Condition::Eq,
                                      0,
                                      ir::ExtendKind::Uxtx,
                                      false,
                                      8};
        }
    }

    return std::nullopt;
}

namespace {

ir::Program lift_region(const Decoder& decoder,
                        const std::vector<std::uint32_t>& words,
                        const std::vector<std::uint8_t>* region_bytes,
                        const LiftContext& context) {
    const auto region_va = context.region_va;
    ir::Program program;
    std::vector<std::uint32_t> arm_index_to_ir_index;
    arm_index_to_ir_index.reserve(words.size());
    for (std::size_t i = 0; i < words.size(); ++i) {
        arm_index_to_ir_index.push_back(static_cast<std::uint32_t>(program.instructions().size()));
        const auto word = words[i];
        const auto decoded = decoder.decode(word);
        if (!decoded.has_value()) {
            std::ostringstream message;
            message << "unsupported ARM64 instruction at index " << i << ": 0x" << std::hex << word;
            throw std::runtime_error(message.str());
        }

        switch (decoded->mnemonic) {
        case Mnemonic::Movz:
            program.load_imm(xzr_register(decoded->rd), decoded->imm);
            break;
        case Mnemonic::Movn:
            program.load_imm(xzr_register(decoded->rd), decoded->imm);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::Movk:
            program.move_keep(xzr_register(decoded->rd), static_cast<std::uint16_t>(decoded->imm),
                              decoded->shift_amount);
            break;
        case Mnemonic::AddImm:
            program.add_imm(decoded->rd, decoded->rn, decoded->imm);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SubImm:
            program.sub_imm(decoded->rd, decoded->rn, decoded->imm);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AddImmSetFlags:
            program.add_imm_set_flags(decoded->rd, decoded->rn, decoded->imm, decoded->writes_32bit);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SubImmSetFlags:
            program.sub_imm_set_flags(decoded->rd, decoded->rn, decoded->imm, decoded->writes_32bit);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AddShiftedReg:
            program.add_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn), xzr_register(decoded->rm),
                                    decoded->shift_kind, decoded->shift_amount);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AddShiftedRegSetFlags:
            program.add_reg_shifted_set_flags(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                              xzr_register(decoded->rm), decoded->shift_kind,
                                              decoded->shift_amount, decoded->writes_32bit);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AddExtendedReg:
            program.add_reg_extended(decoded->rd, decoded->rn, xzr_register(decoded->rm), decoded->extend_kind,
                                     decoded->shift_amount);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AddExtendedRegSetFlags:
            program.add_reg_extended_set_flags(xzr_register(decoded->rd), decoded->rn, xzr_register(decoded->rm),
                                               decoded->extend_kind, decoded->shift_amount,
                                               decoded->writes_32bit);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SubShiftedReg:
            if (decoded->rn == 31) {
                program.neg_reg(xzr_register(decoded->rd), xzr_register(decoded->rm), decoded->shift_kind,
                                decoded->shift_amount, decoded->writes_32bit);
            } else {
                program.sub_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                        xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
                zero_extend_32_if_needed(program, *decoded);
            }
            break;
        case Mnemonic::SubShiftedRegSetFlags:
            if (decoded->rn == 31) {
                program.neg_reg_set_flags(xzr_register(decoded->rd), xzr_register(decoded->rm), decoded->shift_kind,
                                          decoded->shift_amount, decoded->writes_32bit);
            } else {
                program.sub_reg_shifted_set_flags(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                                  xzr_register(decoded->rm), decoded->shift_kind,
                                                  decoded->shift_amount, decoded->writes_32bit);
                zero_extend_32_if_needed(program, *decoded);
            }
            break;
        case Mnemonic::SubExtendedReg:
            program.sub_reg_extended(decoded->rd, decoded->rn, xzr_register(decoded->rm), decoded->extend_kind,
                                     decoded->shift_amount);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SubExtendedRegSetFlags:
            program.sub_reg_extended_set_flags(xzr_register(decoded->rd), decoded->rn, xzr_register(decoded->rm), decoded->extend_kind,
                                               decoded->shift_amount, decoded->writes_32bit);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AndImm:
            program.and_imm(xzr_register(decoded->rd), xzr_register(decoded->rn), decoded->imm);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::OrrImm:
            if (decoded->rn == 31) {
                program.load_imm(xzr_register(decoded->rd), decoded->imm);
            } else {
                program.or_imm(xzr_register(decoded->rd), decoded->rn, decoded->imm);
                zero_extend_32_if_needed(program, *decoded);
            }
            break;
        case Mnemonic::EorImm:
            if (decoded->rn == 31) {
                program.load_imm(xzr_register(decoded->rd), decoded->imm);
            } else {
                program.xor_imm(xzr_register(decoded->rd), decoded->rn, decoded->imm);
                zero_extend_32_if_needed(program, *decoded);
            }
            break;
        case Mnemonic::AndShiftedReg:
            if (decoded->imm != 0) {
                program.bit_clear_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                              xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            } else {
                program.and_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                        xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            }
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AndSetFlagsShiftedReg:
            if (decoded->rd == 31) {
                if (decoded->writes_32bit) {
                    program.test_reg_shifted32(xzr_register(decoded->rn), xzr_register(decoded->rm),
                                               decoded->shift_kind, decoded->shift_amount);
                } else {
                    program.test_reg_shifted(xzr_register(decoded->rn), xzr_register(decoded->rm),
                                             decoded->shift_kind, decoded->shift_amount);
                }
            } else {
                if (decoded->imm != 0) {
                    program.bit_clear_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                                  xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
                } else {
                    program.and_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                            xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
                }
                zero_extend_32_if_needed(program, *decoded);
                if (decoded->writes_32bit) {
                    program.test_reg_shifted32(decoded->rd, decoded->rd, ir::ShiftKind::Lsl, 0);
                } else {
                    program.test_reg_shifted(decoded->rd, decoded->rd, ir::ShiftKind::Lsl, 0);
                }
            }
            break;
        case Mnemonic::OrrShiftedReg:
            if (decoded->rn == 31) {
                if (decoded->imm != 0) {
                    program.not_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                            decoded->shift_kind, decoded->shift_amount, decoded->writes_32bit);
                } else if (decoded->shift_amount == 0) {
                    program.move_reg(xzr_register(decoded->rd), xzr_register(decoded->rm));
                } else if (decoded->writes_32bit) {
                    program.shift_reg32(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                        decoded->shift_kind, decoded->shift_amount);
                } else {
                    program.shift_reg(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                      decoded->shift_kind, decoded->shift_amount);
                }
            } else if (decoded->imm != 0) {
                program.or_not_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                           xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            } else {
                program.or_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                       xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            }
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::EorShiftedReg:
            if (decoded->imm != 0) {
                program.xor_not_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                            xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            } else {
                program.xor_reg_shifted(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                        xzr_register(decoded->rm), decoded->shift_kind, decoded->shift_amount);
            }
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LslImm:
            program.shift_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Lsl,
                              decoded->shift_amount);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LsrImm:
            program.shift_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Lsr,
                              decoded->shift_amount);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::AsrImm:
            if (decoded->writes_32bit) {
                program.shift_reg32(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Asr,
                                    decoded->shift_amount);
            } else {
                program.shift_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Asr,
                                  decoded->shift_amount);
            }
            break;
        case Mnemonic::RorImm:
            if (decoded->writes_32bit) {
                program.shift_reg32(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Ror,
                                    decoded->shift_amount);
            } else {
                program.shift_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), ir::ShiftKind::Ror,
                                  decoded->shift_amount);
            }
            break;
        case Mnemonic::ShiftVariable:
            program.shift_reg_variable(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                       xzr_register(decoded->rm), decoded->shift_kind,
                                       decoded->writes_32bit);
            break;
        case Mnemonic::SignedDivide:
            program.divide_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), xzr_register(decoded->rm), true,
                               decoded->writes_32bit);
            break;
        case Mnemonic::UnsignedDivide:
            program.divide_reg(xzr_register(decoded->rd), xzr_register(decoded->rn), xzr_register(decoded->rm), false,
                               decoded->writes_32bit);
            break;
        case Mnemonic::Ubfx:
            program.bitfield_extract(xzr_register(decoded->rd), xzr_register(decoded->rn), decoded->shift_amount,
                                     static_cast<std::uint8_t>(decoded->imm));
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SignExtendReg: {
            const auto dst = xzr_register(decoded->rd);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(dst, xzr_register(decoded->rn), ir::ShiftKind::Lsl, shift);
            program.shift_reg(dst, dst, ir::ShiftKind::Asr, shift);
            zero_extend_32_if_needed(program, *decoded);
            break;
        }
        case Mnemonic::UnsignedMulHigh:
            program.unsigned_mul_high(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                      xzr_register(decoded->rm));
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::MulAdd:
            program.mul_add(xzr_register(decoded->rd), xzr_register(decoded->rn), xzr_register(decoded->rm),
                            static_cast<ir::RegisterId>(decoded->imm), decoded->writes_32bit,
                            decoded->imm == 31);
            break;
        case Mnemonic::MulSub:
            program.mul_sub(xzr_register(decoded->rd), xzr_register(decoded->rn), xzr_register(decoded->rm),
                            static_cast<ir::RegisterId>(decoded->imm), decoded->writes_32bit,
                            decoded->imm == 31);
            break;
        case Mnemonic::SignedMulLong:
            program.signed_mul_long(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                    xzr_register(decoded->rm));
            break;
        case Mnemonic::UnsignedMulLong:
            program.unsigned_mul_long(xzr_register(decoded->rd), xzr_register(decoded->rn),
                                      xzr_register(decoded->rm));
            break;
        case Mnemonic::CompareImm:
            program.compare_imm(decoded->rn, decoded->imm);
            break;
        case Mnemonic::CompareReg:
            program.compare_reg(decoded->rn, decoded->rm);
            break;
        case Mnemonic::TestImm:
            program.test_imm(decoded->rn, decoded->imm);
            break;
        case Mnemonic::BranchCond: {
            const auto target = static_cast<std::int64_t>(i) + decoded->branch_offset;
            if (target < 0 || target >= static_cast<std::int64_t>(words.size())) {
                throw std::runtime_error("conditional branch target is outside lifted region");
            }
            program.branch_if(decoded->condition, static_cast<std::uint32_t>(target));
            break;
        }
        case Mnemonic::BranchUncond: {
            const auto target = static_cast<std::int64_t>(i) + decoded->branch_offset;
            if (target < 0 || target >= static_cast<std::int64_t>(words.size())) {
                const auto target_va = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(context.region_va) + target * 4);
                program.call_native(target_va, 0);
                program.return_reg(0);
                break;
            }
            program.branch_if(ir::Condition::Al, static_cast<std::uint32_t>(target));
            break;
        }
        case Mnemonic::BranchLink: {
            const auto target_index = static_cast<std::int64_t>(i) + decoded->branch_offset;
            const auto target = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(context.region_va) + target_index * 4);
            const auto region_end = context.region_va + words.size() * 4u;
            if (target >= context.region_va && target < region_end) {
                program.call_vm(static_cast<std::uint32_t>(target_index));
                break;
            }
            program.call_native(target, context.region_va + (i + 1u) * 4u);
            break;
        }
        case Mnemonic::BranchReg:
            program.call_native_reg(xzr_register(decoded->rn), 0);
            program.return_reg(0);
            break;
        case Mnemonic::BranchLinkReg:
            program.call_native_reg(xzr_register(decoded->rn),
                                    context.region_va + (i + 1u) * 4u);
            break;
        case Mnemonic::BranchZero: {
            const auto target = static_cast<std::int64_t>(i) + decoded->branch_offset;
            if (target < 0 || target >= static_cast<std::int64_t>(words.size())) {
                throw std::runtime_error("zero branch target is outside lifted region");
            }
            program.branch_reg_zero(xzr_register(decoded->rn), decoded->imm != 0,
                                    static_cast<std::uint32_t>(target));
            break;
        }
        case Mnemonic::BranchBit: {
            const auto target = static_cast<std::int64_t>(i) + decoded->branch_offset;
            if (target < 0 || target >= static_cast<std::int64_t>(words.size())) {
                throw std::runtime_error("bit branch target is outside lifted region");
            }
            program.branch_reg_bit(xzr_register(decoded->rn), decoded->shift_amount, decoded->imm != 0,
                                   static_cast<std::uint32_t>(target));
            break;
        }
        case Mnemonic::Select:
            program.select(xzr_register(decoded->rd), decoded->rn, decoded->rm, decoded->condition,
                           decoded->rn == 31, decoded->rm == 31);
            break;
        case Mnemonic::SelectInc:
            program.select_inc(xzr_register(decoded->rd), decoded->rn, decoded->rm, decoded->condition,
                               decoded->rn == 31, decoded->rm == 31);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SelectInv:
            program.select_inv(xzr_register(decoded->rd), decoded->rn, decoded->rm, decoded->condition,
                               decoded->rn == 31, decoded->rm == 31);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::SelectNeg:
            program.select_neg(xzr_register(decoded->rd), decoded->rn, decoded->rm, decoded->condition,
                               decoded->rn == 31, decoded->rm == 31);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LoadUnsigned:
            program.load_mem(xzr_register(decoded->rd), decoded->rn, decoded->imm, decoded->mem_width);
            break;
        case Mnemonic::LoadSigned: {
            const auto dst = xzr_register(decoded->rd);
            program.load_mem(dst, decoded->rn, decoded->imm, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(dst, dst, ir::ShiftKind::Lsl, shift);
            program.shift_reg(dst, dst, ir::ShiftKind::Asr, shift);
            break;
        }
        case Mnemonic::LoadLiteral: {
            const auto dst = xzr_register(decoded->rd);
            const auto literal_va = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(region_va + i * 4u) + decoded->branch_offset);
            if (context.read_memory) {
                const auto value = context.read_memory(literal_va, decoded->mem_width);
                if (!value.has_value()) {
                    throw std::runtime_error("LDR literal target is not readable at VA 0x" + [&] {
                        std::ostringstream out;
                        out << std::hex << literal_va;
                        return out.str();
                    }());
                }
                program.load_imm(dst, *value);
                zero_extend_32_if_needed(program, *decoded);
                break;
            }
            if (region_bytes == nullptr) {
                throw std::runtime_error("LDR literal requires ARM64 region bytes");
            }
            const auto literal_offset = static_cast<std::int64_t>(i * 4u) + decoded->branch_offset;
            if (literal_offset < 0) {
                throw std::runtime_error("LDR literal target is outside lifted region");
            }
            const auto offset = static_cast<std::size_t>(literal_offset);
            if (decoded->mem_width == 8) {
                program.load_imm(dst, read_u64_le(*region_bytes, offset));
            } else {
                program.load_imm(dst, read_u32_le(*region_bytes, offset));
            }
            zero_extend_32_if_needed(program, *decoded);
            break;
        }
        case Mnemonic::LoadPreIndex:
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.load_mem(xzr_register(decoded->rd), decoded->rn, 0, decoded->mem_width);
            break;
        case Mnemonic::LoadPostIndex:
            program.load_mem(xzr_register(decoded->rd), decoded->rn, 0, decoded->mem_width);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            break;
        case Mnemonic::LoadSignedPreIndex: {
            const auto dst = xzr_register(decoded->rd);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.load_mem(dst, decoded->rn, 0, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(dst, dst, ir::ShiftKind::Lsl, shift);
            program.shift_reg(dst, dst, ir::ShiftKind::Asr, shift);
            break;
        }
        case Mnemonic::LoadSignedPostIndex: {
            const auto dst = xzr_register(decoded->rd);
            program.load_mem(dst, decoded->rn, 0, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(dst, dst, ir::ShiftKind::Lsl, shift);
            program.shift_reg(dst, dst, ir::ShiftKind::Asr, shift);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            break;
        }
        case Mnemonic::StoreUnsigned:
            program.store_mem(xzr_register(decoded->rd), decoded->rn, decoded->imm, decoded->mem_width);
            break;
        case Mnemonic::StorePreIndex:
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.store_mem(xzr_register(decoded->rd), decoded->rn, 0, decoded->mem_width);
            break;
        case Mnemonic::StorePostIndex:
            program.store_mem(xzr_register(decoded->rd), decoded->rn, 0, decoded->mem_width,
                              static_cast<std::int64_t>(decoded->imm));
            break;
        case Mnemonic::LoadRegOffset:
            program.load_reg_offset_mem(xzr_register(decoded->rd), decoded->rn, xzr_register(decoded->rm),
                                        decoded->extend_kind, decoded->shift_amount, decoded->mem_width);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LoadSignedRegOffset: {
            const auto dst = xzr_register(decoded->rd);
            program.load_reg_offset_mem(dst, decoded->rn, xzr_register(decoded->rm), decoded->extend_kind,
                                        decoded->shift_amount, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(dst, dst, ir::ShiftKind::Lsl, shift);
            program.shift_reg(dst, dst, ir::ShiftKind::Asr, shift);
            break;
        }
        case Mnemonic::StoreRegOffset:
            program.store_reg_offset_mem(xzr_register(decoded->rd), decoded->rn, xzr_register(decoded->rm),
                                         decoded->extend_kind, decoded->shift_amount, decoded->mem_width);
            break;
        case Mnemonic::Store64PostIndex:
            program.store64(xzr_register(decoded->rd), decoded->rn, 0, static_cast<std::int64_t>(decoded->imm));
            break;
        case Mnemonic::LoadPair:
            program.load_mem(xzr_register(decoded->rm), decoded->rn, decoded->imm, decoded->mem_width);
            program.load_mem(xzr_register(decoded->rd), decoded->rn, decoded->imm + decoded->mem_width,
                             decoded->mem_width);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LoadPairPreIndex:
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.load_mem(xzr_register(decoded->rm), decoded->rn, 0, decoded->mem_width);
            program.load_mem(xzr_register(decoded->rd), decoded->rn, decoded->mem_width, decoded->mem_width);
            zero_extend_32_if_needed(program, *decoded);
            break;
        case Mnemonic::LoadPairPostIndex:
            program.load_mem(xzr_register(decoded->rm), decoded->rn, 0, decoded->mem_width);
            program.load_mem(xzr_register(decoded->rd), decoded->rn, decoded->mem_width, decoded->mem_width);
            zero_extend_32_if_needed(program, *decoded);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            break;
        case Mnemonic::LoadSignedPair: {
            program.load_mem(decoded->rm, decoded->rn, decoded->imm, decoded->mem_width);
            program.load_mem(decoded->rd, decoded->rn, decoded->imm + decoded->mem_width, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Asr, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Asr, shift);
            break;
        }
        case Mnemonic::LoadSignedPairPreIndex: {
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.load_mem(decoded->rm, decoded->rn, 0, decoded->mem_width);
            program.load_mem(decoded->rd, decoded->rn, decoded->mem_width, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Asr, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Asr, shift);
            break;
        }
        case Mnemonic::LoadSignedPairPostIndex: {
            program.load_mem(decoded->rm, decoded->rn, 0, decoded->mem_width);
            program.load_mem(decoded->rd, decoded->rn, decoded->mem_width, decoded->mem_width);
            const auto shift = static_cast<std::uint8_t>(64u - decoded->mem_width * 8u);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rm, decoded->rm, ir::ShiftKind::Asr, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Lsl, shift);
            program.shift_reg(decoded->rd, decoded->rd, ir::ShiftKind::Asr, shift);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            break;
        }
        case Mnemonic::StorePair:
            program.store_mem(xzr_register(decoded->rm), decoded->rn, decoded->imm, decoded->mem_width);
            program.store_mem(xzr_register(decoded->rd), decoded->rn, decoded->imm + decoded->mem_width,
                              decoded->mem_width);
            break;
        case Mnemonic::StorePairPreIndex:
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            program.store_mem(xzr_register(decoded->rm), decoded->rn, 0, decoded->mem_width);
            program.store_mem(xzr_register(decoded->rd), decoded->rn, decoded->mem_width, decoded->mem_width);
            break;
        case Mnemonic::StorePairPostIndex:
            program.store_mem(xzr_register(decoded->rm), decoded->rn, 0, decoded->mem_width);
            program.store_mem(xzr_register(decoded->rd), decoded->rn, decoded->mem_width, decoded->mem_width);
            add_signed_offset(program, decoded->rn, static_cast<std::int64_t>(decoded->imm));
            break;
        case Mnemonic::LoadAcquire:
            program.load_acquire(xzr_register(decoded->rd), decoded->rn, decoded->mem_width);
            break;
        case Mnemonic::LoadExclusive:
            program.load_exclusive(xzr_register(decoded->rd), decoded->rn, decoded->mem_width,
                                   decoded->imm != 0);
            break;
        case Mnemonic::StoreRelease:
            program.store_release(xzr_register(decoded->rd), decoded->rn, decoded->mem_width);
            break;
        case Mnemonic::StoreExclusive:
            program.store_exclusive(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                    decoded->rn, decoded->mem_width, decoded->imm != 0);
            break;
        case Mnemonic::LoadExclusivePair:
            program.load_exclusive_pair(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                        decoded->rn, decoded->mem_width, decoded->imm != 0);
            break;
        case Mnemonic::StoreExclusivePair:
            program.store_exclusive_pair(xzr_register(decoded->rd), xzr_register(decoded->rm),
                                         xzr_register(decoded->shift_amount), decoded->rn,
                                         decoded->mem_width, decoded->imm != 0);
            break;
        case Mnemonic::CompareSwap:
            program.compare_swap(xzr_register(decoded->rd), xzr_register(decoded->rm), decoded->rn,
                                 decoded->mem_width, (decoded->imm & 1u) != 0,
                                 (decoded->imm & 2u) != 0);
            break;
        case Mnemonic::Barrier:
            program.no_op();
            break;
        case Mnemonic::Adr:
            program.load_address(decoded->rd,
                                 static_cast<std::uint64_t>(static_cast<std::int64_t>(region_va + i * 4u) +
                                                            decoded->branch_offset));
            break;
        case Mnemonic::Adrp: {
            const auto pc = region_va + static_cast<std::uint64_t>(i * 4u);
            const auto page = pc & ~0xfffull;
            program.load_address(decoded->rd, static_cast<std::uint64_t>(static_cast<std::int64_t>(page) + decoded->branch_offset));
            break;
        }
        case Mnemonic::FmovImm64:
            program.load_fp_imm64(decoded->rd, decoded->imm);
            break;
        case Mnemonic::Fadd64:
            program.fp_add64(decoded->rd, decoded->rn, decoded->rm);
            break;
        case Mnemonic::Fsub64:
            program.fp_sub64(decoded->rd, decoded->rn, decoded->rm);
            break;
        case Mnemonic::Fmul64:
            program.fp_mul64(decoded->rd, decoded->rn, decoded->rm);
            break;
        case Mnemonic::Fdiv64:
            program.fp_div64(decoded->rd, decoded->rn, decoded->rm);
            break;
        case Mnemonic::Fmadd64:
            program.fp_madd64(decoded->rd, decoded->rn, decoded->rm,
                              static_cast<std::uint8_t>(decoded->imm));
            break;
        case Mnemonic::Fcompare64:
            program.fp_compare64(decoded->rn, decoded->rm);
            break;
        case Mnemonic::FmovImm32: program.load_fp_imm32(decoded->rd, static_cast<std::uint32_t>(decoded->imm)); break;
        case Mnemonic::Fadd32: program.fp_add32(decoded->rd, decoded->rn, decoded->rm); break;
        case Mnemonic::Fsub32: program.fp_sub32(decoded->rd, decoded->rn, decoded->rm); break;
        case Mnemonic::Fmul32: program.fp_mul32(decoded->rd, decoded->rn, decoded->rm); break;
        case Mnemonic::Fdiv32: program.fp_div32(decoded->rd, decoded->rn, decoded->rm); break;
        case Mnemonic::Fmadd32: program.fp_madd32(decoded->rd, decoded->rn, decoded->rm, static_cast<std::uint8_t>(decoded->imm)); break;
        case Mnemonic::Fcompare32: program.fp_compare32(decoded->rn, decoded->rm); break;
        case Mnemonic::FpUnary:
            program.fp_unary(decoded->rd, decoded->rn, static_cast<ir::FpUnaryKind>(decoded->imm));
            break;
        case Mnemonic::FpConvert:
            if (decoded->imm < 8) {
                program.fp_convert(decoded->rd, xzr_register(decoded->rn),
                                   static_cast<ir::FpConvertKind>(decoded->imm));
            } else {
                program.fp_convert(xzr_register(decoded->rd), decoded->rn,
                                   static_cast<ir::FpConvertKind>(decoded->imm));
            }
            break;
        case Mnemonic::FpMinMax:
            program.fp_min_max(decoded->rd, decoded->rn, decoded->rm,
                               static_cast<ir::FpMinMaxKind>(decoded->imm));
            break;
        case Mnemonic::FpConditionalCompare:
            program.fp_conditional_compare(decoded->rn, decoded->rm, decoded->condition,
                                           static_cast<std::uint8_t>(decoded->imm & 0xfu),
                                           decoded->mem_width == 8,
                                           (decoded->imm & 0x10u) != 0);
            break;
        case Mnemonic::FpSelect:
            program.fp_select(decoded->rd, decoded->rn, decoded->rm, decoded->condition,
                              decoded->mem_width == 8);
            break;
        case Mnemonic::Ret:
            program.return_reg(0);
            break;
        }
    }
    program.remap_branch_targets(arm_index_to_ir_index);
    program.validate();
    return program;
}

} // namespace

ir::Program Lifter::lift(const std::vector<std::uint32_t>& words) const {
    return lift(words, 0);
}

ir::Program Lifter::lift(const std::vector<std::uint32_t>& words, std::uint64_t region_va) const {
    return lift(words, LiftContext{.region_va = region_va, .read_memory = {}});
}

ir::Program Lifter::lift(const std::vector<std::uint32_t>& words, const LiftContext& context) const {
    return lift_region(decoder_, words, nullptr, context);
}

ir::Program Lifter::lift(const std::vector<std::uint8_t>& region_bytes) const {
    return lift_region(decoder_, words_from_region(region_bytes), &region_bytes, {});
}

ir::Program Lifter::lift(const std::vector<std::uint8_t>& region_bytes, std::size_t instruction_word_count) const {
    auto words = words_from_region(region_bytes);
    if (instruction_word_count > words.size()) {
        throw std::invalid_argument("ARM64 instruction word count exceeds region byte size");
    }
    words.resize(instruction_word_count);
    return lift_region(decoder_, words, &region_bytes, {});
}

ir::Program Lifter::lift(const std::vector<std::uint8_t>& region_bytes,
                         std::size_t instruction_word_count,
                         std::uint64_t region_va) const {
    return lift(region_bytes, instruction_word_count,
                LiftContext{.region_va = region_va, .read_memory = {}});
}

ir::Program Lifter::lift(const std::vector<std::uint8_t>& region_bytes,
                         std::size_t instruction_word_count,
                         const LiftContext& context) const {
    auto words = words_from_region(region_bytes);
    if (instruction_word_count > words.size()) {
        throw std::invalid_argument("ARM64 instruction word count exceeds region byte size");
    }
    words.resize(instruction_word_count);
    return lift_region(decoder_, words, &region_bytes, context);
}

std::string to_string(Mnemonic mnemonic) {
    switch (mnemonic) {
    case Mnemonic::Movz:
        return "MOVZ";
    case Mnemonic::Movn:
        return "MOVN";
    case Mnemonic::Movk:
        return "MOVK";
    case Mnemonic::AddImm:
        return "ADD_imm";
    case Mnemonic::SubImm:
        return "SUB_imm";
    case Mnemonic::AddImmSetFlags:
        return "ADDS_imm";
    case Mnemonic::SubImmSetFlags:
        return "SUBS_imm";
    case Mnemonic::AddShiftedReg:
        return "ADD_shifted_reg";
    case Mnemonic::AddShiftedRegSetFlags:
        return "ADDS_shifted_reg";
    case Mnemonic::AddExtendedReg:
        return "ADD_extended_reg";
    case Mnemonic::AddExtendedRegSetFlags:
        return "ADDS_extended_reg";
    case Mnemonic::SubShiftedReg:
        return "SUB_shifted_reg";
    case Mnemonic::SubShiftedRegSetFlags:
        return "SUBS_shifted_reg";
    case Mnemonic::SubExtendedReg:
        return "SUB_extended_reg";
    case Mnemonic::SubExtendedRegSetFlags:
        return "SUBS_extended_reg";
    case Mnemonic::AndImm:
        return "AND_imm";
    case Mnemonic::OrrImm:
        return "ORR_imm";
    case Mnemonic::EorImm:
        return "EOR_imm";
    case Mnemonic::AndShiftedReg:
        return "AND_shifted_reg";
    case Mnemonic::AndSetFlagsShiftedReg:
        return "ANDS_shifted_reg";
    case Mnemonic::OrrShiftedReg:
        return "ORR_shifted_reg";
    case Mnemonic::EorShiftedReg:
        return "EOR_shifted_reg";
    case Mnemonic::LslImm:
        return "LSL_imm";
    case Mnemonic::LsrImm:
        return "LSR_imm";
    case Mnemonic::AsrImm:
        return "ASR_imm";
    case Mnemonic::RorImm:
        return "ROR_imm";
    case Mnemonic::ShiftVariable:
        return "shift_variable";
    case Mnemonic::SignedDivide:
        return "SDIV";
    case Mnemonic::UnsignedDivide:
        return "UDIV";
    case Mnemonic::Ubfx:
        return "UBFX";
    case Mnemonic::SignExtendReg:
        return "SXT";
    case Mnemonic::UnsignedMulHigh:
        return "UMULH";
    case Mnemonic::MulAdd:
        return "MADD";
    case Mnemonic::MulSub:
        return "MSUB";
    case Mnemonic::SignedMulLong:
        return "SMULL";
    case Mnemonic::UnsignedMulLong:
        return "UMULL";
    case Mnemonic::CompareImm:
        return "CMP_imm";
    case Mnemonic::CompareReg:
        return "CMP_reg";
    case Mnemonic::TestImm:
        return "TST_imm";
    case Mnemonic::BranchCond:
        return "B_cond";
    case Mnemonic::BranchUncond:
        return "B";
    case Mnemonic::BranchLink:
        return "BL";
    case Mnemonic::BranchReg:
        return "BR";
    case Mnemonic::BranchLinkReg:
        return "BLR";
    case Mnemonic::BranchZero:
        return "CBZ_CBNZ";
    case Mnemonic::BranchBit:
        return "TBZ_TBNZ";
    case Mnemonic::Select:
        return "CSEL";
    case Mnemonic::SelectInc:
        return "CSINC";
    case Mnemonic::SelectInv:
        return "CSINV";
    case Mnemonic::SelectNeg:
        return "CSNEG";
    case Mnemonic::LoadUnsigned:
        return "LDR_unsigned";
    case Mnemonic::LoadSigned:
        return "LDR_signed";
    case Mnemonic::LoadLiteral:
        return "LDR_literal";
    case Mnemonic::LoadPreIndex:
        return "LDR_pre_index";
    case Mnemonic::LoadPostIndex:
        return "LDR_post_index";
    case Mnemonic::LoadSignedPreIndex:
        return "LDR_signed_pre_index";
    case Mnemonic::LoadSignedPostIndex:
        return "LDR_signed_post_index";
    case Mnemonic::StoreUnsigned:
        return "STR_unsigned";
    case Mnemonic::StorePreIndex:
        return "STR_pre_index";
    case Mnemonic::StorePostIndex:
        return "STR_post_index";
    case Mnemonic::LoadRegOffset:
        return "LDR_reg_offset";
    case Mnemonic::LoadSignedRegOffset:
        return "LDR_signed_reg_offset";
    case Mnemonic::StoreRegOffset:
        return "STR_reg_offset";
    case Mnemonic::Store64PostIndex:
        return "STR_64_post_index";
    case Mnemonic::LoadPair:
        return "LDP";
    case Mnemonic::LoadPairPreIndex:
        return "LDP_pre_index";
    case Mnemonic::LoadPairPostIndex:
        return "LDP_post_index";
    case Mnemonic::LoadSignedPair:
        return "LDPSW";
    case Mnemonic::LoadSignedPairPreIndex:
        return "LDPSW_pre_index";
    case Mnemonic::LoadSignedPairPostIndex:
        return "LDPSW_post_index";
    case Mnemonic::StorePair:
        return "STP";
    case Mnemonic::StorePairPreIndex:
        return "STP_pre_index";
    case Mnemonic::StorePairPostIndex:
        return "STP_post_index";
    case Mnemonic::LoadAcquire:
        return "LDAR";
    case Mnemonic::StoreRelease:
        return "STLR";
    case Mnemonic::LoadExclusive:
        return "LDXR";
    case Mnemonic::StoreExclusive:
        return "STXR";
    case Mnemonic::LoadExclusivePair: return "LDXP";
    case Mnemonic::StoreExclusivePair: return "STXP";
    case Mnemonic::CompareSwap:
        return "CAS";
    case Mnemonic::Barrier:
        return "BARRIER";
    case Mnemonic::Adr:
        return "ADR";
    case Mnemonic::Adrp:
        return "ADRP";
    case Mnemonic::FmovImm64:
        return "FMOV_imm_64";
    case Mnemonic::Fadd64:
        return "FADD_64";
    case Mnemonic::Fsub64:
        return "FSUB_64";
    case Mnemonic::Fmul64:
        return "FMUL_64";
    case Mnemonic::Fdiv64:
        return "FDIV_64";
    case Mnemonic::Fmadd64:
        return "FMADD_64";
    case Mnemonic::Fcompare64:
        return "FCMP_64";
    case Mnemonic::FmovImm32: return "FMOV_imm_32";
    case Mnemonic::Fadd32: return "FADD_32";
    case Mnemonic::Fsub32: return "FSUB_32";
    case Mnemonic::Fmul32: return "FMUL_32";
    case Mnemonic::Fdiv32: return "FDIV_32";
    case Mnemonic::Fmadd32: return "FMADD_32";
    case Mnemonic::Fcompare32: return "FCMP_32";
    case Mnemonic::FpUnary: return "FP_UNARY";
    case Mnemonic::FpConvert: return "FP_CONVERT";
    case Mnemonic::FpMinMax: return "FP_MIN_MAX";
    case Mnemonic::FpConditionalCompare: return "FP_CONDITIONAL_COMPARE";
    case Mnemonic::FpSelect: return "FP_SELECT";
    case Mnemonic::Ret:
        return "RET";
    }
    return "UNKNOWN";
}

} // namespace myvmp::arm64
