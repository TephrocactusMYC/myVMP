#include "myvmp/riscv64/decoder.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace myvmp::riscv64 {
namespace {

std::int64_t sx(std::uint64_t value, unsigned bits) {
    const auto sign = std::uint64_t{1} << (bits - 1u);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

ir::RegisterId reg(std::uint8_t value) {
    return value == 0 ? ir::kZeroRegister : static_cast<ir::RegisterId>(value - 1u);
}

std::int64_t branch_imm(std::uint32_t w) {
    return sx(((w >> 31u) << 12u) | (((w >> 7u) & 1u) << 11u) |
              (((w >> 25u) & 0x3fu) << 5u) | (((w >> 8u) & 0xfu) << 1u), 13);
}

std::int64_t jump_imm(std::uint32_t w) {
    return sx(((w >> 31u) << 20u) | (((w >> 12u) & 0xffu) << 12u) |
              (((w >> 20u) & 1u) << 11u) | (((w >> 21u) & 0x3ffu) << 1u), 21);
}

void sign_extend_word(ir::Program& p, ir::RegisterId dst) {
    p.shift_reg(dst, dst, ir::ShiftKind::Lsl, 32);
    p.shift_reg(dst, dst, ir::ShiftKind::Asr, 32);
}

void normalize_word(ir::Program& p, ir::RegisterId dst, bool signed_result = true) {
    p.shift_reg32(dst, dst, ir::ShiftKind::Lsl, 0);
    if (signed_result) sign_extend_word(p, dst);
}

std::uint32_t encode_i(std::uint8_t opcode, std::uint8_t rd, std::uint8_t funct3,
                       std::uint8_t rs1, std::int32_t imm) {
    return (static_cast<std::uint32_t>(imm) & 0xfffu) << 20u |
           static_cast<std::uint32_t>(rs1) << 15u | static_cast<std::uint32_t>(funct3) << 12u |
           static_cast<std::uint32_t>(rd) << 7u | opcode;
}
std::uint32_t encode_r(std::uint8_t rd, std::uint8_t funct3, std::uint8_t rs1,
                       std::uint8_t rs2, std::uint8_t funct7 = 0) {
    return static_cast<std::uint32_t>(funct7) << 25u | static_cast<std::uint32_t>(rs2) << 20u |
           static_cast<std::uint32_t>(rs1) << 15u | static_cast<std::uint32_t>(funct3) << 12u |
           static_cast<std::uint32_t>(rd) << 7u | 0x33u;
}
std::uint32_t encode_s(std::uint8_t funct3, std::uint8_t rs1, std::uint8_t rs2,
                       std::uint32_t imm) {
    return ((imm >> 5u) & 0x7fu) << 25u | static_cast<std::uint32_t>(rs2) << 20u |
           static_cast<std::uint32_t>(rs1) << 15u | static_cast<std::uint32_t>(funct3) << 12u |
           (imm & 31u) << 7u | 0x23u;
}
std::uint32_t reencode_branch(std::uint32_t word, std::int32_t offset);
std::uint32_t reencode_jump(std::uint32_t word, std::int32_t offset);

std::optional<std::uint32_t> decompress(std::uint16_t h) {
    const auto quadrant = h & 3u, funct3 = (h >> 13u) & 7u;
    const auto rd = static_cast<std::uint8_t>((h >> 7u) & 31u);
    const auto rs2 = static_cast<std::uint8_t>((h >> 2u) & 31u);
    const auto ci = static_cast<std::int32_t>(sx(((h >> 12u) & 1u) << 5u | ((h >> 2u) & 31u), 6));
    if (quadrant == 1) {
        if (funct3 == 0) return rd == 0 ? 0x0000000fu : encode_i(0x13, rd, 0, rd, ci); // C.NOP/ADDI
        if (funct3 == 1 && rd != 0) return encode_i(0x1b, rd, 0, rd, ci);       // C.ADDIW
        if (funct3 == 2 && rd != 0) return encode_i(0x13, rd, 0, 0, ci);        // C.LI
        if (funct3 == 3 && rd == 2) { // C.ADDI16SP
            const auto imm = static_cast<std::int32_t>(sx(((h >> 12u) & 1u) << 9u |
                ((h >> 6u) & 1u) << 4u | ((h >> 5u) & 1u) << 6u |
                ((h >> 3u) & 3u) << 7u | ((h >> 2u) & 1u) << 5u, 10));
            return encode_i(0x13, 2, 0, 2, imm);
        }
        if (funct3 == 3 && rd != 0) return 0x37u | static_cast<std::uint32_t>(rd) << 7u |
            (static_cast<std::uint32_t>(ci) & 0xfffffu) << 12u;                 // C.LUI
        if (funct3 == 4 && ((h >> 10u) & 3u) < 3u) {
            const auto crd = static_cast<std::uint8_t>(8u + ((h >> 7u) & 7u));
            const auto op = (h >> 10u) & 3u;
            if (op == 2) return encode_i(0x13, crd, 7, crd, ci);               // C.ANDI
            const auto shamt = ((h >> 12u) & 1u) << 5u | ((h >> 2u) & 31u);
            return encode_i(0x13, crd, 5, crd, shamt | (op == 1 ? 0x400 : 0)); // C.SRLI/SRAI
        }
        if (funct3 == 4 && ((h >> 10u) & 3u) == 3u) {
            const auto crd = static_cast<std::uint8_t>(8u + ((h >> 7u) & 7u));
            const auto crs2 = static_cast<std::uint8_t>(8u + ((h >> 2u) & 7u));
            const auto op = (h >> 5u) & 3u;
            if (((h >> 12u) & 1u) != 0 && op <= 1)
                return 0x3bu | static_cast<std::uint32_t>(crd) << 7u |
                    static_cast<std::uint32_t>(crd) << 15u |
                    static_cast<std::uint32_t>(crs2) << 20u | (op == 0 ? 0x40000000u : 0); // SUBW/ADDW
            if (((h >> 12u) & 1u) != 0) return std::nullopt;
            return encode_r(crd, op == 0 ? 0 : op == 1 ? 4 : op == 2 ? 6 : 7,
                            crd, crs2, op == 0 ? 0x20 : 0);
        }
        if (funct3 == 5) { // C.J
            const auto imm = static_cast<std::int32_t>(sx(((h >> 12u) & 1u) << 11u |
                ((h >> 11u) & 1u) << 4u | ((h >> 9u) & 3u) << 8u |
                ((h >> 8u) & 1u) << 10u | ((h >> 7u) & 1u) << 6u |
                ((h >> 6u) & 1u) << 7u | ((h >> 3u) & 7u) << 1u |
                ((h >> 2u) & 1u) << 5u, 12));
            return reencode_jump(0x6fu, imm);
        }
        if (funct3 == 6 || funct3 == 7) { // C.BEQZ/C.BNEZ
            const auto imm = static_cast<std::int32_t>(sx(((h >> 12u) & 1u) << 8u |
                ((h >> 10u) & 3u) << 3u | ((h >> 5u) & 3u) << 6u |
                ((h >> 3u) & 3u) << 1u | ((h >> 2u) & 1u) << 5u, 9));
            const auto r1 = static_cast<std::uint8_t>(8u + ((h >> 7u) & 7u));
            return reencode_branch(0x63u | static_cast<std::uint32_t>(funct3 == 7) << 12u |
                                   static_cast<std::uint32_t>(r1) << 15u, imm);
        }
    }
    if (quadrant == 2) {
        if (funct3 == 0 && rd != 0) return encode_i(0x13, rd, 1, rd, ci & 63); // C.SLLI
        if (funct3 == 4) {
            const bool high = ((h >> 12u) & 1u) != 0;
            if (!high && rs2 == 0 && rd != 0) return encode_i(0x67, 0, 0, rd, 0); // C.JR
            if (!high && rs2 != 0 && rd != 0) return encode_r(rd, 0, 0, rs2);     // C.MV
            if (high && rs2 == 0 && rd != 0) return encode_i(0x67, 1, 0, rd, 0);  // C.JALR
            if (high && rs2 != 0 && rd != 0) return encode_r(rd, 0, rd, rs2);     // C.ADD
        }
        if (funct3 == 3 && rd != 0) { // C.LDSP
            const auto imm = ((h >> 2u) & 7u) << 6u | ((h >> 12u) & 1u) << 5u | ((h >> 5u) & 3u) << 3u;
            return encode_i(0x03, rd, 3, 2, imm);
        }
        if (funct3 == 2 && rd != 0) { // C.LWSP
            const auto imm = ((h >> 12u) & 1u) << 5u | ((h >> 4u) & 7u) << 2u |
                             ((h >> 2u) & 3u) << 6u;
            return encode_i(0x03, rd, 2, 2, imm);
        }
        if (funct3 == 7) { // C.SDSP
            const auto imm = ((h >> 7u) & 7u) << 6u | ((h >> 10u) & 7u) << 3u;
            return encode_s(3, 2, rs2, imm);
        }
        if (funct3 == 6) { // C.SWSP
            const auto imm = ((h >> 9u) & 15u) << 2u | ((h >> 7u) & 3u) << 6u;
            return encode_s(2, 2, rs2, imm);
        }
    }
    if (quadrant == 0 && funct3 == 0) { // C.ADDI4SPN
        const auto crd = static_cast<std::uint8_t>(8u + ((h >> 2u) & 7u));
        const auto imm = ((h >> 7u) & 15u) << 6u | ((h >> 11u) & 3u) << 4u |
                         ((h >> 5u) & 1u) << 3u | ((h >> 6u) & 1u) << 2u;
        if (imm != 0) return encode_i(0x13, crd, 0, 2, imm);
    }
    if (quadrant == 0 && (funct3 == 2 || funct3 == 3 || funct3 == 6 || funct3 == 7)) {
        const auto r1 = static_cast<std::uint8_t>(8u + ((h >> 7u) & 7u));
        const auto r2 = static_cast<std::uint8_t>(8u + ((h >> 2u) & 7u));
        if (funct3 == 3 || funct3 == 7) {
            const auto imm = ((h >> 5u) & 3u) << 6u | ((h >> 10u) & 7u) << 3u;
            return funct3 == 3 ? encode_i(0x03, r2, 3, r1, imm) : encode_s(3, r1, r2, imm);
        }
        const auto imm = ((h >> 10u) & 7u) << 3u | ((h >> 6u) & 1u) << 2u |
                         ((h >> 5u) & 1u) << 6u;
        return funct3 == 2 ? encode_i(0x03, r2, 2, r1, imm) : encode_s(2, r1, r2, imm);
    }
    return std::nullopt;
}

std::uint32_t reencode_branch(std::uint32_t word, std::int32_t offset) {
    const auto imm = static_cast<std::uint32_t>(offset) & 0x1fffu;
    return (word & 0x01fff07fu) | ((imm >> 12u) & 1u) << 31u |
           ((imm >> 5u) & 0x3fu) << 25u | ((imm >> 1u) & 15u) << 8u |
           ((imm >> 11u) & 1u) << 7u;
}
std::uint32_t reencode_jump(std::uint32_t word, std::int32_t offset) {
    const auto imm = static_cast<std::uint32_t>(offset) & 0x1fffffu;
    return (word & 0xfffu) | ((imm >> 20u) & 1u) << 31u |
           ((imm >> 1u) & 0x3ffu) << 21u | ((imm >> 11u) & 1u) << 20u |
           ((imm >> 12u) & 0xffu) << 12u;
}

} // namespace

std::optional<DecodedInstruction> Decoder::decode(std::uint32_t w) const {
    if ((w & 3u) != 3u) return std::nullopt; // RVC is a separate mixed-width phase.
    const auto op = w & 0x7fu;
    const auto rd = static_cast<std::uint8_t>((w >> 7u) & 31u);
    const auto f3 = static_cast<std::uint8_t>((w >> 12u) & 7u);
    const auto rs1 = static_cast<std::uint8_t>((w >> 15u) & 31u);
    const auto rs2 = static_cast<std::uint8_t>((w >> 20u) & 31u);
    const auto f7 = static_cast<std::uint8_t>(w >> 25u);
    if (op == 0x37u) return DecodedInstruction{Mnemonic::Lui, rd, 0, 0, sx(w & 0xfffff000u, 32)};
    if (op == 0x17u) return DecodedInstruction{Mnemonic::Auipc, rd, 0, 0, sx(w & 0xfffff000u, 32)};
    if (op == 0x6fu) return DecodedInstruction{Mnemonic::Jal, rd, 0, 0, jump_imm(w)};
    if (op == 0x67u && f3 == 0) {
        if (w == 0x00008067u) return DecodedInstruction{Mnemonic::Return};
        return DecodedInstruction{Mnemonic::Jalr, rd, rs1, 0, sx(w >> 20u, 12)};
    }
    if (op == 0x63u) {
        ir::Condition c;
        switch (f3) {
        case 0: c = ir::Condition::Eq; break; case 1: c = ir::Condition::Ne; break;
        case 4: c = ir::Condition::Lt; break; case 5: c = ir::Condition::Ge; break;
        case 6: c = ir::Condition::Lo; break; case 7: c = ir::Condition::Hs; break;
        default: return std::nullopt;
        }
        return DecodedInstruction{Mnemonic::Branch, 0, rs1, rs2, branch_imm(w), ir::ShiftKind::Lsl, c};
    }
    if (op == 0x13u) {
        const auto imm = sx(w >> 20u, 12);
        if (f3 == 0) return DecodedInstruction{Mnemonic::AddImm, rd, rs1, 0, imm};
        if (f3 == 2) return DecodedInstruction{Mnemonic::SetLessImm, rd, rs1, 0, imm};
        if (f3 == 3) return DecodedInstruction{Mnemonic::SetLessUnsignedImm, rd, rs1, 0, imm};
        if (f3 == 4) return DecodedInstruction{Mnemonic::XorImm, rd, rs1, 0, imm};
        if (f3 == 6) return DecodedInstruction{Mnemonic::OrImm, rd, rs1, 0, imm};
        if (f3 == 7) return DecodedInstruction{Mnemonic::AndImm, rd, rs1, 0, imm};
        if (f3 == 1 && (w >> 26u) == 0) return DecodedInstruction{Mnemonic::ShiftLeftImm, rd, rs1, 0, static_cast<std::int64_t>((w >> 20u) & 63u)};
        if (f3 == 5 && ((w >> 26u) == 0 || (w >> 26u) == 0x10u))
            return DecodedInstruction{Mnemonic::ShiftRightImm, rd, rs1, 0, static_cast<std::int64_t>((w >> 20u) & 63u), (w >> 26u) == 0x10u ? ir::ShiftKind::Asr : ir::ShiftKind::Lsr};
    }
    if (op == 0x1bu) {
        const auto imm = sx(w >> 20u, 12);
        if (f3 == 0) return DecodedInstruction{Mnemonic::AddWordImm, rd, rs1, 0, imm};
        if (f3 == 1 && f7 == 0) return DecodedInstruction{Mnemonic::ShiftLeftWordImm, rd, rs1, 0, static_cast<std::int64_t>((w >> 20u) & 31u)};
        if (f3 == 5 && (f7 == 0 || f7 == 0x20)) return DecodedInstruction{Mnemonic::ShiftRightWordImm, rd, rs1, 0, static_cast<std::int64_t>((w >> 20u) & 31u), f7 == 0x20 ? ir::ShiftKind::Asr : ir::ShiftKind::Lsr};
    }
    if (op == 0x33u || op == 0x3bu) {
        const bool word = op == 0x3bu;
        if (f7 == 1) {
            static constexpr Mnemonic m64[] = {Mnemonic::Mul, Mnemonic::MulHigh, Mnemonic::MulHighSignedUnsigned, Mnemonic::MulHighUnsigned, Mnemonic::Div, Mnemonic::DivUnsigned, Mnemonic::Rem, Mnemonic::RemUnsigned};
            static constexpr Mnemonic m32[] = {Mnemonic::MulWord, Mnemonic::MulWord, Mnemonic::MulWord, Mnemonic::MulWord, Mnemonic::DivWord, Mnemonic::DivUnsignedWord, Mnemonic::RemWord, Mnemonic::RemUnsignedWord};
            if (word && f3 >= 1 && f3 <= 3) return std::nullopt;
            return DecodedInstruction{word ? m32[f3] : m64[f3], rd, rs1, rs2};
        }
        if (f3 == 0 && f7 == 0) return DecodedInstruction{word ? Mnemonic::AddWord : Mnemonic::Add, rd, rs1, rs2};
        if (f3 == 0 && f7 == 0x20) return DecodedInstruction{word ? Mnemonic::SubWord : Mnemonic::Sub, rd, rs1, rs2};
        if (f3 == 1 && f7 == 0) return DecodedInstruction{word ? Mnemonic::ShiftLeftWord : Mnemonic::ShiftLeft, rd, rs1, rs2};
        if (f3 == 5 && (f7 == 0 || f7 == 0x20)) return DecodedInstruction{word ? Mnemonic::ShiftRightWord : Mnemonic::ShiftRight, rd, rs1, rs2, 0, f7 == 0x20 ? ir::ShiftKind::Asr : ir::ShiftKind::Lsr};
        if (!word && f7 == 0) {
            if (f3 == 2) return DecodedInstruction{Mnemonic::SetLess, rd, rs1, rs2};
            if (f3 == 3) return DecodedInstruction{Mnemonic::SetLessUnsigned, rd, rs1, rs2};
            if (f3 == 4) return DecodedInstruction{Mnemonic::Xor, rd, rs1, rs2};
            if (f3 == 6) return DecodedInstruction{Mnemonic::Or, rd, rs1, rs2};
            if (f3 == 7) return DecodedInstruction{Mnemonic::And, rd, rs1, rs2};
        }
    }
    if (op == 0x03u) {
        if (f3 == 7) return std::nullopt;
        const std::uint8_t width = f3 == 0 || f3 == 4 ? 1 : f3 == 1 || f3 == 5 ? 2 : f3 == 2 || f3 == 6 ? 4 : 8;
        const bool sign = f3 < 4;
        return DecodedInstruction{Mnemonic::Load, rd, rs1, 0, sx(w >> 20u, 12), ir::ShiftKind::Lsl, ir::Condition::Eq, width, sign};
    }
    if (op == 0x23u && f3 <= 3) {
        const auto imm = ((w >> 7u) & 31u) | (((w >> 25u) & 127u) << 5u);
        return DecodedInstruction{Mnemonic::Store, 0, rs1, rs2, sx(imm, 12), ir::ShiftKind::Lsl, ir::Condition::Eq, static_cast<std::uint8_t>(1u << f3)};
    }
    if (op == 0x0fu && (f3 == 0 || f3 == 1)) return DecodedInstruction{Mnemonic::Fence};
    return std::nullopt;
}

ir::Program Lifter::lift(const std::vector<std::uint32_t>& words, const LiftContext& context) const {
    Decoder d;
    ir::Program p;
    std::vector<std::uint32_t> source_to_ir;
    source_to_ir.reserve(words.size());
    for (std::size_t i = 0; i < words.size(); ++i) {
        source_to_ir.push_back(static_cast<std::uint32_t>(p.instructions().size()));
        const auto x = d.decode(words[i]);
        if (!x) throw std::runtime_error("unsupported RV64 instruction at index " + std::to_string(i));
        const auto rd = reg(x->rd), a = reg(x->rs1), b = reg(x->rs2);
        const auto uimm = static_cast<std::uint64_t>(x->immediate);
        switch (x->mnemonic) {
        case Mnemonic::Lui: p.load_imm(rd, uimm); break;
        case Mnemonic::Auipc: {
            const auto pc_offset = context.instruction_offsets.size() == words.size()
                ? context.instruction_offsets[i] : static_cast<std::uint32_t>(i * 4u);
            p.load_address(rd, context.region_va + pc_offset + x->immediate);
            break;
        }
        case Mnemonic::AddImm: p.add_imm(rd, a, uimm); break;
        case Mnemonic::XorImm: p.xor_imm(rd, a, uimm); break; case Mnemonic::OrImm: p.or_imm(rd, a, uimm); break; case Mnemonic::AndImm: p.and_imm(rd, a, uimm); break;
        case Mnemonic::ShiftLeftImm: p.shift_reg(rd, a, ir::ShiftKind::Lsl, x->immediate); break; case Mnemonic::ShiftRightImm: p.shift_reg(rd, a, x->shift, x->immediate); break;
        case Mnemonic::Add: p.add_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); break; case Mnemonic::Sub: p.sub_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); break;
        case Mnemonic::Xor: p.xor_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); break; case Mnemonic::Or: p.or_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); break; case Mnemonic::And: p.and_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); break;
        case Mnemonic::ShiftLeft: p.shift_reg_variable(rd, a, b, ir::ShiftKind::Lsl); break; case Mnemonic::ShiftRight: p.shift_reg_variable(rd, a, b, x->shift); break;
        case Mnemonic::Load:
            p.load_mem(rd, a, uimm, x->width);
            if (x->is_signed && x->width < 8) { const auto n = static_cast<std::uint8_t>(64 - x->width * 8); p.shift_reg(rd, rd, ir::ShiftKind::Lsl, n); p.shift_reg(rd, rd, ir::ShiftKind::Asr, n); }
            break;
        case Mnemonic::Store: p.store_mem(b, a, uimm, x->width); break;
        case Mnemonic::AddWordImm: p.add_imm(rd, a, uimm); normalize_word(p, rd); break;
        case Mnemonic::ShiftLeftWordImm: p.shift_reg32(rd, a, ir::ShiftKind::Lsl, x->immediate); sign_extend_word(p, rd); break;
        case Mnemonic::ShiftRightWordImm: p.shift_reg32(rd, a, x->shift, x->immediate); sign_extend_word(p, rd); break;
        case Mnemonic::AddWord: p.add_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); normalize_word(p, rd); break;
        case Mnemonic::SubWord: p.sub_reg_shifted(rd, a, b, ir::ShiftKind::Lsl, 0); normalize_word(p, rd); break;
        case Mnemonic::ShiftLeftWord: p.shift_reg_variable(rd, a, b, ir::ShiftKind::Lsl, true); sign_extend_word(p, rd); break;
        case Mnemonic::ShiftRightWord: p.shift_reg_variable(rd, a, b, x->shift, true); sign_extend_word(p, rd); break;
        case Mnemonic::Mul: p.mul_add(rd, a, b, ir::kZeroRegister, false, true); break;
        case Mnemonic::MulHighUnsigned: p.unsigned_mul_high(rd, a, b); break;
        case Mnemonic::MulWord: p.mul_add(rd, a, b, ir::kZeroRegister, true, true); sign_extend_word(p, rd); break;
        case Mnemonic::Div: case Mnemonic::DivUnsigned: case Mnemonic::DivWord: case Mnemonic::DivUnsignedWord: {
            const bool word = x->mnemonic == Mnemonic::DivWord || x->mnemonic == Mnemonic::DivUnsignedWord;
            const bool sign = x->mnemonic == Mnemonic::Div || x->mnemonic == Mnemonic::DivWord;
            p.divide_reg(rd, a, b, sign, word);
            p.load_imm(ir::kStackPointerRegister, ~std::uint64_t{0});
            p.compare_imm(b, 0);
            p.select(rd, ir::kStackPointerRegister, rd, ir::Condition::Eq);
            if (word) sign_extend_word(p, rd);
            break;
        }
        case Mnemonic::Rem: case Mnemonic::RemUnsigned: case Mnemonic::RemWord: case Mnemonic::RemUnsignedWord: {
            const bool word = x->mnemonic == Mnemonic::RemWord || x->mnemonic == Mnemonic::RemUnsignedWord;
            const bool sign = x->mnemonic == Mnemonic::Rem || x->mnemonic == Mnemonic::RemWord;
            p.divide_reg(rd, a, b, sign, word); p.mul_add(rd, rd, b, ir::kZeroRegister, word, true); p.sub_reg_shifted(rd, a, rd, ir::ShiftKind::Lsl, 0); if (word) normalize_word(p, rd); break;
        }
        case Mnemonic::Branch: {
            if ((x->immediate & 3) != 0) throw std::runtime_error("RV64 branch targets a non-32-bit boundary");
            const auto target = static_cast<std::int64_t>(i) + x->immediate / 4;
            if (target < 0 || target >= static_cast<std::int64_t>(words.size())) throw std::runtime_error("RV64 branch target is outside lifted region");
            p.compare_reg(a, b); p.branch_if(x->condition, static_cast<std::uint32_t>(target)); break;
        }
        case Mnemonic::Jal: {
            if ((x->immediate & 3) != 0) throw std::runtime_error("RV64 JAL targets a non-32-bit boundary");
            const auto target = static_cast<std::int64_t>(i) + x->immediate / 4;
            if (target >= 0 && target < static_cast<std::int64_t>(words.size())) {
                if (x->rd == 0) p.branch_if(ir::Condition::Al, static_cast<std::uint32_t>(target));
                else if (x->rd == 1) p.call_vm(static_cast<std::uint32_t>(target));
                else throw std::runtime_error("RV64 internal JAL link register must be x0 or x1");
            } else { p.call_native(context.region_va + i * 4u + x->immediate, x->rd == 0 ? 0 : context.region_va + (i + 1) * 4u); if (x->rd == 0) p.return_reg(reg(10)); }
            break;
        }
        case Mnemonic::Jalr:
            if (x->immediate != 0) p.add_imm(rd, a, uimm);
            p.call_native_reg(x->immediate == 0 ? a : rd, x->rd == 0 ? 0 : context.region_va + (i + 1) * 4u);
            if (x->rd == 0) p.return_reg(reg(10));
            break;
        case Mnemonic::SetLessImm: case Mnemonic::SetLessUnsignedImm: {
            p.load_imm(ir::kStackPointerRegister, uimm);
            p.compare_reg(a, ir::kStackPointerRegister);
            p.load_imm(ir::kStackPointerRegister, 1);
            p.select(rd, ir::kStackPointerRegister, ir::kZeroRegister,
                     x->mnemonic == Mnemonic::SetLessImm ? ir::Condition::Lt : ir::Condition::Lo,
                     false, true);
            break;
        }
        case Mnemonic::SetLess: case Mnemonic::SetLessUnsigned:
            p.compare_reg(a, b);
            p.load_imm(ir::kStackPointerRegister, 1);
            p.select(rd, ir::kStackPointerRegister, ir::kZeroRegister,
                     x->mnemonic == Mnemonic::SetLess ? ir::Condition::Lt : ir::Condition::Lo,
                     false, true);
            break;
        case Mnemonic::MulHigh: case Mnemonic::MulHighSignedUnsigned:
            p.unsigned_mul_high(rd, a, b);
            p.compare_imm(a, 0);
            p.select(ir::kStackPointerRegister, b, ir::kZeroRegister,
                     ir::Condition::Lt, false, true);
            p.sub_reg_shifted(rd, rd, ir::kStackPointerRegister, ir::ShiftKind::Lsl, 0);
            if (x->mnemonic == Mnemonic::MulHigh) {
                p.compare_imm(b, 0);
                p.select(ir::kStackPointerRegister, a, ir::kZeroRegister,
                         ir::Condition::Lt, false, true);
                p.sub_reg_shifted(rd, rd, ir::kStackPointerRegister, ir::ShiftKind::Lsl, 0);
            }
            break;
        case Mnemonic::Fence: p.no_op(); break;
        case Mnemonic::Return: p.return_reg(reg(10)); break;
        }
    }
    p.remap_control_flow_targets(source_to_ir);
    p.validate(); return p;
}

ir::Program Lifter::lift_bytes(const std::vector<std::uint8_t>& bytes, const LiftContext& context) const {
    struct Item { std::uint32_t pc; std::uint32_t word; };
    std::vector<Item> items;
    for (std::size_t pc = 0; pc < bytes.size();) {
        if (bytes.size() - pc < 2) throw std::runtime_error("truncated RV64 instruction");
        const auto half = static_cast<std::uint16_t>(bytes[pc] | bytes[pc + 1] << 8u);
        if ((half & 3u) != 3u) {
            const auto word = decompress(half);
            if (!word) throw std::runtime_error("unsupported RVC instruction at byte offset " + std::to_string(pc));
            items.push_back({static_cast<std::uint32_t>(pc), *word}); pc += 2;
        } else {
            if (bytes.size() - pc < 4) throw std::runtime_error("truncated 32-bit RV64 instruction");
            items.push_back({static_cast<std::uint32_t>(pc), static_cast<std::uint32_t>(bytes[pc]) |
                static_cast<std::uint32_t>(bytes[pc + 1]) << 8u | static_cast<std::uint32_t>(bytes[pc + 2]) << 16u |
                static_cast<std::uint32_t>(bytes[pc + 3]) << 24u}); pc += 4;
        }
    }
    std::unordered_map<std::uint32_t, std::size_t> index_for_pc;
    for (std::size_t i = 0; i < items.size(); ++i) index_for_pc.emplace(items[i].pc, i);
    std::vector<std::uint32_t> words;
    words.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto word = items[i].word;
        const auto opcode = word & 0x7fu;
        if (opcode == 0x63u || opcode == 0x6fu) {
            const auto native_offset = opcode == 0x63u ? branch_imm(word) : jump_imm(word);
            const auto target_pc = static_cast<std::int64_t>(items[i].pc) + native_offset;
            const auto found = target_pc < 0 ? index_for_pc.end() : index_for_pc.find(static_cast<std::uint32_t>(target_pc));
            if (found != index_for_pc.end()) {
                const auto canonical_offset = static_cast<std::int32_t>(found->second - i) * 4;
                word = opcode == 0x63u ? reencode_branch(word, canonical_offset)
                                       : reencode_jump(word, canonical_offset);
            }
        }
        words.push_back(word);
    }
    auto mixed_context = context;
    mixed_context.instruction_offsets.reserve(items.size());
    mixed_context.instruction_offsets.clear();
    for (const auto& item : items) mixed_context.instruction_offsets.push_back(item.pc);
    return lift(words, mixed_context);
}

const char* to_string(Mnemonic m) noexcept {
    switch (m) {
    case Mnemonic::Lui: return "LUI"; case Mnemonic::Auipc: return "AUIPC"; case Mnemonic::Jal: return "JAL"; case Mnemonic::Jalr: return "JALR"; case Mnemonic::Branch: return "BRANCH";
    case Mnemonic::AddImm: return "ADDI"; case Mnemonic::SetLessImm: return "SLTI"; case Mnemonic::SetLessUnsignedImm: return "SLTIU"; case Mnemonic::XorImm: return "XORI"; case Mnemonic::OrImm: return "ORI"; case Mnemonic::AndImm: return "ANDI";
    case Mnemonic::ShiftLeftImm: return "SLLI"; case Mnemonic::ShiftRightImm: return "SRLI/SRAI"; case Mnemonic::Add: return "ADD"; case Mnemonic::Sub: return "SUB"; case Mnemonic::ShiftLeft: return "SLL"; case Mnemonic::SetLess: return "SLT"; case Mnemonic::SetLessUnsigned: return "SLTU"; case Mnemonic::Xor: return "XOR"; case Mnemonic::ShiftRight: return "SRL/SRA"; case Mnemonic::Or: return "OR"; case Mnemonic::And: return "AND";
    case Mnemonic::Load: return "LOAD"; case Mnemonic::Store: return "STORE"; case Mnemonic::AddWordImm: return "ADDIW"; case Mnemonic::ShiftLeftWordImm: return "SLLIW"; case Mnemonic::ShiftRightWordImm: return "SRLIW/SRAIW"; case Mnemonic::AddWord: return "ADDW"; case Mnemonic::SubWord: return "SUBW"; case Mnemonic::ShiftLeftWord: return "SLLW"; case Mnemonic::ShiftRightWord: return "SRLW/SRAW";
    case Mnemonic::Mul: return "MUL"; case Mnemonic::MulHigh: return "MULH"; case Mnemonic::MulHighSignedUnsigned: return "MULHSU"; case Mnemonic::MulHighUnsigned: return "MULHU"; case Mnemonic::Div: return "DIV"; case Mnemonic::DivUnsigned: return "DIVU"; case Mnemonic::Rem: return "REM"; case Mnemonic::RemUnsigned: return "REMU"; case Mnemonic::MulWord: return "MULW"; case Mnemonic::DivWord: return "DIVW"; case Mnemonic::DivUnsignedWord: return "DIVUW"; case Mnemonic::RemWord: return "REMW"; case Mnemonic::RemUnsignedWord: return "REMUW"; case Mnemonic::Fence: return "FENCE"; case Mnemonic::Return: return "RET";
    }
    return "unknown";
}

} // namespace myvmp::riscv64
